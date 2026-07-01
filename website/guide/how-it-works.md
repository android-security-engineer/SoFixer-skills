# 工作原理

> 阅读前建议对 ELF 格式有基本了解：ELF header、program header、section header、dynamic 段。

SoFixer 的核心修复流程在 `ElfRebuilder::Rebuild()`，由五个阶段组成：

```
Rebuild() = RebuildPhdr() && ReadSoInfo() && RebuildShdr() && RebuildRelocs() && RebuildFin()
```

## 阶段一：RebuildPhdr — 修正 program header

dump 出来的 SO，文件内容就是内存布局本身，所以**文件偏移 = 虚拟地址**是关键修正：

```cpp
phdr->p_filesz = phdr->p_memsz;     // 内存大小当作文件大小
phdr->p_paddr  = phdr->p_vaddr;     // paddr 在加载中无意义，对齐到 vaddr
phdr->p_offset = phdr->p_vaddr;     // 已加载的 SO：偏移就是虚拟地址
```

## 阶段二：ReadSoInfo — 从 dynamic 段提取信息

`ReadSoInfo()` 遍历 dynamic 段的 `DT_*` 条目，把散落的元信息收集进 `soinfo` 结构：

| dynamic 标签 | 提取的信息 |
|---|---|
| `DT_STRTAB` / `DT_SYMTAB` | 字符串表、符号表地址 |
| `DT_HASH` | 哈希表（含 nbucket/nchain） |
| `DT_REL` / `DT_RELA` / `DT_JMPREL` | 重定位表 |
| `DT_INIT_ARRAY` / `DT_FINI_ARRAY` | 构造/析构函数表 |
| `DT_SONAME` | SO 的名字 |

这一步是为 RebuildShdr 准备原料——section header table 已经丢了，要从 dynamic 段**反推**出各 section 的位置和大小。

## 阶段三：RebuildShdr — 重建 section header table

最核心的修复。逐个构造 section header：

1. 用 `soinfo` 里记录的地址，反推每个 section 在内存（= 文件）里的位置：`sh_addr = 段地址 - load_bias`。
2. 按依赖关系设置 `sh_link`（例如 `.hash` 链到 `.dynsym`，`.dynsym` 链到 `.dynstr`）。
3. 排序所有 section（按 `sh_addr`），据此推算 `sh_size`（下一个 section 地址减当前地址）。
4. 追加 `.shstrtab`（section 名字字符串表）。

重建出的 section 包括：`.dynsym`、`.dynstr`、`.hash`、`.rel.dyn`、`.rela.dyn`、`.rel.plt`、`.plt`、`.text`、`.ARM.exidx`、`.fini_array`、`.init_array`、`.dynamic`、`.data`、`.shstrtab`。

::: warning 已知未完成
`.got` 和 `.bss` 的生成代码目前被注释掉，属于已知未完成项。
:::

## 阶段四：RebuildRelocs — 修复重定位

`relocate<isRela>()` 模板处理重定位项，关键逻辑：

```cpp
case R_ARM_RELATIVE:
case R_386_RELATIVE:
    *prel = *prel - dump_base;   // 绝对地址减去 dump 基地址，还原成相对地址
```

用户通过 `-m` / `--membase` 传入 dump 时的内存基地址。重定位表里的值是 `符号地址 + 基地址`，减去基地址就还原成 SO 内的相对地址。

::: tip 为什么 fix 需要 --membase
没有基地址，重定位无法修复。`RebuildRelocs` 在 `dump_so_base_ == 0` 时直接跳过这一步——SO 仍可修复出结构，但重定位项保持绝对地址。
:::

## 阶段五：RebuildFin — 拼装最终文件

1. 复制 `load_bias` 起的 `load_size` 字节（SO 的全部内容）。
2. 追加 `.shstrtab`。
3. 追加 section header table。
4. 修正 ELF header：`e_shnum`、`e_shoff`、`e_shstrndx`，固定 `e_type = ET_DYN`、`e_machine`（40=ARM / 183=AARCH64）。

## 代码导览

| 文件 | 职责 |
|---|---|
| `ElfReader.h/.cpp` | 基础 ELF 读取器：解析 ehdr/phdr，把段加载到内存（模拟 linker）。开头有大段加载原理注释。 |
| `ObElfReader.h/.cpp` | 继承 ElfReader，针对内存 dump 的 SO：修复 phdr、可从 base so 加载 dynamic 段（实验性）。 |
| `ElfRebuilder.h/.cpp` | 核心修复逻辑（上述五阶段）。`soinfo` 是 dynamic 段信息的容器。 |
| `main.cpp` | 旧入口（SoFixer32/64），getopt 风格，输出人类可读日志。 |
| `cli/` | 新的面向 AI 的 CLI（见 [CLI 参考](/cli/)）。 |

## 一次完整流程

```bash
# 1. 在 IDA 里从内存 dump SO（带基地址 0x7DB078B000）
#    用 README 里的 idaapi 脚本

# 2. 用 CLI 修复（传入同一个基地址）
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000

# 3. 校验修复结果
SoFixer-skills-CLI64 verify -s fixed.so

# 4. 用 IDA 打开 fixed.so 分析
```

## 已知限制

- 重定位表解析有几处已知错误（原作者注明），暂未修复。
- `.got` / `.bss` section 生成未启用。
- `info` 子命令输出的 `DT_NEEDED` / `DT_SONAME` 是字符串表偏移，而非库名本身。
- 一个二进制只处理一种位宽的 SO：CLI32 处理 32 位、CLI64 处理 64 位。
