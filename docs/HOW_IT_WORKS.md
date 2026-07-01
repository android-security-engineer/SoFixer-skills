# SoFixer 工作原理与代码导览

> 面向想理解"内存 dump 的 SO 为什么不能直接分析，SoFixer 又如何修复它"的读者。
> 阅读本文前建议对 ELF 文件格式有基本了解（ELF header / program header / section header / dynamic 段）。

## 1. 问题：内存 dump 的 SO 长什么样

一个 SO 在磁盘上时，ELF 结构是完整的：有 ELF header、program header table（描述段）、section header table（描述节，给链接器/分析工具用）、dynamic 段（给动态链接器用）。

但当它被 Android 的 linker 加载进进程内存后，会发生几件事：

1. **section header table 不再被需要**。linker 只用 program header 和 dynamic 段来加载和重定位，section header 对运行时无意义，所以内存镜像里它可能已经不在了（或者从未被 mmap）。
2. **program header 的偏移被"摊平"**。磁盘上 `p_offset` 是文件偏移；加载后段被放到 `p_vaddr` 起的内存里，文件偏移和虚拟地址的对应关系变了。
3. **重定位项被改写成绝对地址**。原本重定位表里写的是"把这个符号的地址填到这里"，加载后 linker 真的把绝对地址填进去了，于是表里的值变成了进程内的绝对地址（含基地址）。
4. **`p_filesz` 不再等于 `p_memsz`**。加载后内存里 `.bss` 这类零初始化段也占空间，内存大小大于文件大小。

如果你直接把这段内存 dump 成文件，用 IDA 打开：section 没了、偏移错了、重定位表指向莫名其妙的绝对地址——分析无从下手。

**SoFixer 的工作就是把这四样东西"逆"回磁盘形态。**

## 2. 三层修复

核心流程在 `ElfRebuilder::Rebuild()`（`ElfRebuilder.cpp`）：

```
Rebuild() = RebuildPhdr() && ReadSoInfo() && RebuildShdr() && RebuildRelocs() && RebuildFin()
```

### 2.1 RebuildPhdr — 修正 program header

```cpp
phdr->p_filesz = phdr->p_memsz;     // 内存大小当成文件大小
phdr->p_paddr  = phdr->p_vaddr;     // paddr 在加载中无意义，对齐到 vaddr
phdr->p_offset = phdr->p_vaddr;     // 已加载的 SO：偏移就是虚拟地址
```

dump 出来的 SO，文件内容就是内存布局本身，所以"文件偏移 = 虚拟地址"是关键修正。

### 2.2 ReadSoInfo — 从 dynamic 段提取信息

`ElfRebuilder.cpp` 的 `ReadSoInfo()` 遍历 dynamic 段的 `DT_*` 条目，把散落在 dynamic 里的元信息收集到一个 `soinfo` 结构：

- `DT_STRTAB` / `DT_SYMTAB` → 字符串表、符号表地址
- `DT_HASH` → 哈希表（含 nbucket/nchain）
- `DT_REL` / `DT_RELA` / `DT_JMPREL` → 重定位表
- `DT_INIT_ARRAY` / `DT_FINI_ARRAY` → 构造/析构函数表
- `DT_SONAME` → SO 的名字

这一步是为 RebuildShdr 准备原料——因为 section header table 已经丢了，我们要从 dynamic 段"反推"出各个 section 的位置和大小。

### 2.3 RebuildShdr — 重建 section header table

这是最核心的修复。`ElfRebuilder.cpp` 的 `RebuildShdr()` 逐个构造 section header：

- 用 `soinfo` 里记录的地址，反推每个 section 在内存（= 文件）里的位置：`sh_addr = 段地址 - load_bias`
- 按依赖关系设置 `sh_link`（例如 `.hash` 链到 `.dynsym`，`.dynsym` 链到 `.dynstr`）
- 排序所有 section（按 `sh_addr`），并据此推算 `sh_size`（下一个 section 的地址减当前地址）
- 最后追加 `.shstrtab`（section 名字字符串表）

重建出的 section 包括：`.dynsym`、`.dynstr`、`.hash`、`.rel.dyn`、`.rela.dyn`、`.rel.plt`、`.plt`、`.text`、`.ARM.exidx`、`.fini_array`、`.init_array`、`.dynamic`、`.data`、`.shstrtab`。

> 注：`.got` 和 `.bss` 的生成代码目前被注释掉了（见 `ElfRebuilder.cpp` 的注释块），属于已知未完成项。

### 2.4 RebuildRelocs — 修复重定位

`ElfRebuilder.cpp` 的 `relocate<isRela>()` 模板处理重定位项。关键逻辑：

```cpp
case R_ARM_RELATIVE:
case R_386_RELATIVE:
    *prel = *prel - dump_base;   // 绝对地址减去 dump 基地址，还原成相对地址
```

用户通过 `-m` / `--membase` 传入 dump 时的内存基地址。重定位表里的值是 `符号地址 + 基地址`，减去基地址就还原成 SO 内的相对地址。这就是为什么 fix 子命令需要 `--membase`——没有它，重定位无法修复（`RebuildRelocs` 在 `dump_so_base_ == 0` 时直接跳过）。

### 2.5 RebuildFin — 拼装最终文件

`RebuildFin()` 把修复后的数据拼成完整 ELF：

1. 复制 `load_bias` 起的 `load_size` 字节（SO 的全部内容）
2. 追加 `.shstrtab`
3. 追加 section header table
4. 修正 ELF header：`e_shnum`、`e_shoff`、`e_shstrndx`，并固定 `e_type = ET_DYN`、`e_machine`（40=ARM / 183=AARCH64）

## 3. 代码导览

| 文件 | 职责 |
|---|---|
| `ElfReader.h/.cpp` | 基础 ELF 读取器：解析 ehdr/phdr，把段加载到内存（模拟 linker）。开头有大段加载原理注释，推荐先读。 |
| `ObElfReader.h/.cpp` | 继承 ElfReader，针对"内存 dump 的 SO"：修复 dump SO 的 phdr、可从 base so 加载 dynamic 段（实验性）。 |
| `ElfRebuilder.h/.cpp` | 核心修复逻辑（上文三层）。`soinfo` 结构是 dynamic 段信息的容器。 |
| `main.cpp` | 旧入口（SoFixer32/64），getopt 风格，输出人类可读日志。 |
| `cli/` | 新的面向 AI 的 CLI（见下）。 |

### 3.1 cli/ 目录导览

```
cli/CliConfig.h    常量集中地：版本、错误码、退出码、平台检测
cli/Json.h/.cpp    极简 JSON 写出器（无第三方依赖）
cli/CliArgs.h/.cpp 自写 argv 解析器（不用 getopt，三平台一致）
cli/Commands.h/.cpp 五个子命令的实现（fix/info/verify/version/help）
cli/CliMain.cpp    入口与子命令调度
```

**为什么不用 getopt？** MSVC 没有 `getopt.h`。为了三平台零依赖一致，手写了约 150 行的解析器（见 `CliArgs.cpp` 文件头注释）。

**为什么 JSON 输出里地址是字符串？** JSON Number 是 IEEE 754 double，只能精确到 2^53，而 64 位地址可达 2^64。所以 `Elf_Addr` 一律输出成 `"0x7db078b000"`（见 `Json.h` 文件头注释）。这是面向 AI 输出的常见坑。

**FLOG 调试输出如何不污染 JSON？** `FDebug.h` 用 `#if !defined(SOFIXER_QUIET)` 控制。CLI 目标在 CMakeLists.txt 里定义 `SOFIXER_QUIET`，使 FLOG 展开为空；旧入口不定义它，行为不变。因为是编译期宏，核心源文件被两个目标各编译一次（详见 CMakeLists.txt 注释）。

## 4. 一次完整的修复流程

```bash
# 1. 在 IDA 里从内存 dump SO（带基地址 0x7DB078B000）
#    用 README 里的 idaapi 脚本

# 2. 用 CLI 修复（传入同一个基地址）
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000

# 3. 校验修复结果
SoFixer-skills-CLI64 verify -s fixed.so

# 4. 用 IDA 打开 fixed.so 分析
```

## 5. 已知限制

- 重定位表解析有几处已知错误（原作者注明），暂未修复。
- `.got` / `.bss` section 生成未启用。
- `info` 子命令输出的 `DT_NEEDED` / `DT_SONAME` 是字符串表偏移，而非库名本身（解析库名需读 strtab，当前未实现）。
- 一个二进制只处理一种位宽的 SO：用 CLI32 处理 32 位、CLI64 处理 64 位。
