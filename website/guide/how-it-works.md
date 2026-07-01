# 工作原理

> 这一页讲 SoFixer **怎么修**一个 dump 出来的 SO。建议先读 [前置背景知识](./background) 和 [它解决什么问题](./problem)。
>
> 阅读前需要对 ELF 格式有基本概念：ELF header、program header、section header、dynamic 段。下面会逐步推导，但不重复背景页的内容。

SoFixer 的核心修复逻辑在 `ElfRebuilder::Rebuild()`，由五个阶段顺序执行，任一失败则整体失败：

```
Rebuild() = RebuildPhdr()    // 阶段一：修正 program header
          && ReadSoInfo()    // 阶段二：从 dynamic 段收集信息
          && RebuildShdr()   // 阶段三：重建 section header table（核心）
          && RebuildRelocs() // 阶段四：修复重定位（需 dump 基地址）
          && RebuildFin()    // 阶段五：拼装最终文件
```

整体数据流：

```
 dump.so（内存镜像）
    │
    ▼
┌──────────────────────────────────────────────┐
│ ① RebuildPhdr   修正 p_offset/p_filesz       │
│ ② ReadSoInfo    遍历 dynamic 段 → soinfo     │
│ ③ RebuildShdr   soinfo + 反推 → 新 section 表 │
│ ④ RebuildRelocs 绝对地址 - 基地址 → 相对地址  │
│ ⑤ RebuildFin    内容+shstrtab+section表 → 输出│
└──────────────────────────────────────────────┘
    │
    ▼
 fixed.so（合法 ELF，IDA 可分析）
```

## 阶段一：RebuildPhdr — 修正 program header

### 要解决的问题

dump 出来的 SO，**文件内容就是内存布局本身**——你 dump 的是内存那块连续字节。但 program header 里记录的还是"磁盘坐标系"：`p_offset` 是文件偏移。

这就矛盾了：文件第 0 字节对应内存基地址，但 header 说"这段在文件偏移 X"。IDA 按 `p_offset` 去读，读到的位置是错的。

### 怎么修

关键洞察：**既然文件内容 = 内存内容，那么文件偏移就等于虚拟地址**（减去基地址后）。所以把坐标系从"磁盘"切到"内存"：

```cpp
phdr->p_filesz = phdr->p_memsz;     // 内存大小当作文件大小
phdr->p_paddr  = phdr->p_vaddr;     // paddr 加载中无意义，对齐到 vaddr
phdr->p_offset = phdr->p_vaddr;     // 已加载的 SO：偏移就是虚拟地址
```

逐行解释：

- `p_filesz = p_memsz`：磁盘文件里 `.bss` 这类零初始化段不占空间（`p_filesz < p_memsz`），但内存里它们占空间。dump 出来的文件包含了这部分，所以文件大小该等于内存大小。
- `p_paddr = p_vaddr`：`p_paddr` 在加载过程中不被使用，设成 `p_vaddr` 只是保持自洽。
- `p_offset = p_vaddr`：**最关键的一行**。把"文件偏移"重新定义为"虚拟地址"，让 IDA 按 `p_vaddr` 读内容时能对上。

::: details 为什么要减基地址？
严格说 `p_offset` 应该等于 `p_vaddr - load_bias`（相对地址）。但代码里直接用 `p_vaddr`，是因为后续处理统一在 `load_bias` 基址上操作，`RebuildFin` 输出时会从 `load_bias` 起拷 `load_size` 字节，相当于整体平移到文件开头。两种等价，实现选了后者。
:::

## 阶段二：ReadSoInfo — 从 dynamic 段提取信息

### 要解决的问题

阶段三要重建 section header table，但 section 信息全丢了。**唯一的线索是 dynamic 段**——它记录了各关键结构的地址。这一步就是把 dynamic 段里散落的线索收集进一个 `soinfo` 结构。

### 怎么做

`ReadSoInfo()` 遍历 dynamic 段的 `DT_*` 条目，把地址/计数填进 `soinfo`：

| dynamic 标签 | 提取的信息 | 喂给阶段三用来重建哪个 section |
|---|---|---|
| `DT_STRTAB` / `DT_SYMTAB` | 字符串表、符号表地址 | `.dynstr` / `.dynsym` |
| `DT_HASH` | 哈希表（含 nbucket/nchain） | `.hash` |
| `DT_REL` / `DT_RELA` / `DT_JMPREL` | 重定位表地址 | `.rel.dyn` / `.rela.dyn` / `.rel.plt` |
| `DT_INIT_ARRAY` / `DT_FINI_ARRAY` | 构造/析构函数表 | `.init_array` / `.fini_array` |
| `DT_PLTGOT` / `DT_JMPREL` | PLT 相关 | `.plt` |
| `DT_SONAME` | SO 的名字 | （元信息） |

这一步本身不修复任何东西，只是为阶段三**准备原料**：把"section header table 已经丢了，要从 dynamic 段反推"所需的所有地址备齐。

::: details soinfo 是什么
`soinfo` 是 Android linker 内部用来描述一个已加载 SO 的结构体，SoFixer 借用了这个概念。它是个"中转站"：阶段二往里填数据，阶段三从里读数据。字段包括各表的指针、元素个数、`load_bias`、`min_load`/`max_load`（加载范围）等。
:::

## 阶段三：RebuildShdr — 重建 section header table

> 这是**最核心**的修复，也是 SoFixer 名字的由来（SO Fixer）。

### 要解决的问题

IDA 等 ELF 分析工具靠 section header table 理解文件结构。但内存里根本没有这张表（linker 不加载它）。我们需要**凭空重建一张**。

### 怎么做：从地址反推 section

思路：阶段二已经从 dynamic 段拿到各结构的**虚拟地址**。每个地址对应一个 section 的起始位置。逐个构造 section header：

1. **算地址**：用 `soinfo` 里记录的地址，反推每个 section 在文件里的位置：`sh_addr = 段地址 - load_bias`（转成相对地址）。
2. **设链接关系**：按依赖关系设置 `sh_link`。比如 `.hash` 的 `sh_link` 指向 `.dynsym`（哈希表索引的是符号表），`.dynsym` 的 `sh_link` 指向 `.dynstr`（符号名在字符串表里）。
3. **排序**：把所有 section 按 `sh_addr` 排序（冒泡排序）。
4. **推算大小**：排序后，每个 section 的 `sh_size = 下一个 section 的 sh_addr - 当前 sh_addr`。最后一个用 `max_load` 兜底。
5. **追加 `.shstrtab`**：section 的名字（`.text`、`.dynsym` 等）要存在一个专门的字符串表里，叫 `.shstrtab`。

重建出的 section 包括：`.dynsym`、`.dynstr`、`.hash`、`.rel.dyn`、`.rela.dyn`、`.rel.plt`、`.plt`、`.text`、`.ARM.exidx`、`.fini_array`、`.init_array`、`.dynamic`、`.data`、`.shstrtab`。

::: details 为什么要排序再推算大小
section header table 里每个 section 必须有准确的 `sh_size`，但 dynamic 段只告诉我们"表从哪开始"，不直接告诉我们"表多大"。巧妙之处在于：这些 section 在内存里是**顺序排列、不重叠**的。所以排好序后，"我到下一个 section 之间的距离"就是我的大小。这是一种典型的"用边界推断区间"手法。
:::

::: details sh_link 是什么
section header 里有个 `sh_link` 字段，指向"与我相关的另一个 section 的索引"。比如 `.rel.dyn`（重定位表）的 `sh_link` 指向 `.dynsym`——因为重定位项引用的符号在符号表里。重建时必须正确设置这些链接，IDA 才能顺着链接把"重定位→符号→名字"串起来。
:::

::: warning 已知未完成
`.got` 和 `.bss` 的生成代码目前**被注释掉**（`ElfRebuilder.cpp` 里可见大段注释）。属于已知未完成项——这两个 section 在修复产物里缺失，但不影响 IDA 基本分析。
:::

## 阶段四：RebuildRelocs — 修复重定位

### 要解决的问题

[背景知识里讲过](./background#重定位为什么运行时代码要被改写)：磁盘 SO 的重定位项是相对地址，加载后 linker 把绝对地址写回了内存。dump 出来的是绝对地址（如 `0x7db078c000`），IDA 看到这种地址无法理解代码逻辑——它指向进程运行时的某个位置，而不是 SO 内部。

### 怎么修

`relocate<isRela>()` 模板遍历重定位项，关键逻辑：

```cpp
case R_ARM_RELATIVE:
case R_386_RELATIVE:
    *prel = *prel - dump_base;   // 绝对地址减去 dump 基地址，还原成相对地址
```

用户通过 `-m` / `--membase` 传入 dump 时的内存基地址。重定位项里的值是 `符号地址 + 基地址`（绝对地址），减去基地址就还原成 SO 内的相对地址。

::: tip 为什么 fix 需要 --membase
没有基地址，重定位无法修复——你不知道该减去哪个数。`RebuildRelocs` 在 `dump_so_base_ == 0` 时**直接跳过这一步**：SO 仍能修出结构（前三个阶段不受影响），但重定位项保持绝对地址。所以 `--membase` 不是"让修复更好"，而是"重定位能不能修"的开关。
:::

::: details R_ARM_RELATIVE 是什么
重定位有多种类型，`R_ARM_RELATIVE`（ARM 32 位）/ `R_AARCH64_RELATIVE`（ARM 64 位）是最常见的一种：表示"这个位置要加上基地址"。linker 加载时就是把基地址加进去的，所以还原时减回去即可。其他类型（如 `R_ARM_JUMP_SLOT`）涉及外部符号解析，更复杂，SoFixer 对部分类型的处理有已知问题。
:::

## 阶段五：RebuildFin — 拼装最终文件

### 要解决的问题

前四步都是在内存数据上修改/收集信息，还没产出真正的输出文件。这一步把所有东西拼成合法 ELF。

### 怎么做

按顺序拼接三块，再修 ELF header：

1. **拷内容**：从 `load_bias` 起拷 `load_size` 字节（SO 的全部内容，已是修复后的）。
2. **追加 `.shstrtab`**：阶段三构造的 section 名字字符串表。
3. **追加 section header table**：阶段三构造的所有 section header。
4. **修正 ELF header**：
   - `e_shnum` = section 数量
   - `e_shoff` = section table 在文件中的偏移
   - `e_shstrndx` = `.shstrtab` 的索引
   - 固定 `e_type = ET_DYN`（表示这是共享库）
   - 固定 `e_machine`：40 = ARM（32 位）/ 183 = AARCH64（64 位）

```
最终文件布局：
┌─────────────────────────┐  offset 0
│  SO 内容（load_size）    │  ← 修复后的代码/数据/重定位
├─────────────────────────┤  offset = load_size
│  .shstrtab              │  ← section 名字表
├─────────────────────────┤  offset = load_size + shstrtab.len
│  Section Header Table   │  ← 重建的 section 表
└─────────────────────────┘
   + ELF header 指向上面的位置
```

## 代码导览

| 文件 | 职责 |
|---|---|
| `ElfReader.h/.cpp` | 基础 ELF 读取器：解析 ehdr/phdr，把段加载到内存（模拟 linker）。开头有大段加载原理注释。 |
| `ObElfReader.h/.cpp` | 继承 ElfReader，针对内存 dump 的 SO：修复 phdr、可从 base so 加载 dynamic 段（实验性）。 |
| `ElfRebuilder.h/.cpp` | 核心修复逻辑（上述五阶段）。`soinfo` 是 dynamic 段信息的容器。 |
| `main.cpp` | 旧入口（SoFixer32/64），getopt 风格，输出人类可读日志。 |
| `cli/` | 新的面向 AI 的 CLI（见 [CLI 参考](/cli/)）。 |
| `macros.h` | `__SO64__` 位宽抽象、`TEMP_FAILURE_RETRY` 跨平台处理。 |
| `elf.h` | ELF 格式常量与结构体定义（32/64 位）。 |

## 一次完整流程

```bash
# 1. 在 IDA 里从内存 dump SO（带基地址 0x7DB078B000）
#    用快速开始页里的 idaapi 脚本

# 2. 用 CLI 修复（传入同一个基地址）
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000

# 3. 校验修复结果
SoFixer-skills-CLI64 verify -s fixed.so

# 4. 用 IDA 打开 fixed.so 分析
```

## 已知限制

- 重定位表解析有几处已知错误（原作者注明），暂未修复——部分重定位类型可能还原不准。
- `.got` / `.bss` section 生成未启用（代码被注释）。
- `info` 子命令输出的 `DT_NEEDED` / `DT_SONAME` 是字符串表偏移，而非库名本身。
- 一个二进制只处理一种位宽的 SO：CLI32 处理 32 位、CLI64 处理 64 位（因 `__SO64__` 硬编码）。
- 假设你 dump 了**完整的加载区域**；若只 dump 了部分内存，SoFixer 无法补全缺失数据。
