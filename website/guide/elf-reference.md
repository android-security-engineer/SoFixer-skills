# ELF 字段速查

> 这一页是查阅用的"字典"。读 [工作原理](./how-it-works) 或看 CLI 输出时遇到不认识的 ELF 字段，来这里查。
>
> 不需要一次读完——需要时回来翻即可。

## ELF Header（文件总目录）

ELF 文件最开头的 `Elf_Ehdr`，说明"我是什么文件、有几段、各种表在哪"。

| 字段 | 含义 | SoFixer 关注点 |
|---|---|---|
| `e_ident` | 魔数 + 类别 + 字节序等标识 | 判断 32/64 位（`ELFCLASS32/64`）、大小端 |
| `e_type` | 文件类型 | 固定为 `ET_DYN`（3）= 共享库 |
| `e_machine` | 目标架构 | 40 = ARM，183 = AARCH64，62 = x86_64 |
| `e_entry` | 程序入口虚拟地址 | `info` 输出里能看到 |
| `e_phoff` | program header table 在文件中的偏移 | |
| `e_shoff` | section header table 在文件中的偏移 | dump 出来的 SO 里通常为 0（表丢了），RebuildFin 重写 |
| `e_phnum` | program header 数量 | |
| `e_shnum` | section header 数量 | RebuildFin 重写为重建后的数量 |
| `e_shstrndx` | `.shstrtab` 在 section 表中的索引 | RebuildFin 重写 |

::: tip 32 位 vs 64 位
`Elf32_Ehdr` 和 `Elf64_Ehdr` 字段相同，但地址/偏移字段的位宽不同（32 位用 4 字节，64 位用 8 字节）。SoFixer 用 `__SO64__` 宏在编译期切换，所以一个二进制只处理一种位宽。
:::

## Program Header（段头，给加载器看）

`Elf_Phdr`，描述一个 segment 怎么装进内存。`info` 输出的 `segments` 数组就是这些。

| 字段 | 含义 | SoFixer 关注点 |
|---|---|---|
| `p_type` | 段类型（见下表） | 主要关心 `PT_LOAD` |
| `p_offset` | 段在**文件**中的偏移 | **RebuildPhdr 改写**：设为 `p_vaddr` |
| `p_vaddr` | 段加载到**内存**后的虚拟地址 | 基准坐标 |
| `p_paddr` | 物理地址（加载中不用） | RebuildPhdr 设为 `p_vaddr` |
| `p_filesz` | 段在文件中的大小 | **RebuildPhdr 改写**：设为 `p_memsz` |
| `p_memsz` | 段在内存中的大小 | 可能大于 `p_filesz`（`.bss` 零初始化段） |
| `p_flags` | 段权限 | `R`/`W`/`X` 组合 |
| `p_align` | 对齐要求 | 加载中用，RebuildPhdr 不动 |

### 常见 `p_type`

| 值 | 名称 | 含义 |
|---|---|---|
| `PT_LOAD` (1) | 可加载段 | 真正装进内存的段，SoFixer 核心处理对象 |
| `PT_DYNAMIC` (2) | dynamic 段 | 指向 dynamic 段，ReadSoInfo 的入口 |
| `PT_INTERP` (3) | 解释器 | SO 通常没有 |
| `PT_NOTE` (4) | 注释段 | 辅助信息 |

## Section Header（节头，给分析工具看）

`Elf_Shdr`，描述一个 section。RebuildShdr 重建的就是这些。

| 字段 | 含义 | SoFixer 关注点 |
|---|---|---|
| `sh_name` | 名字在 `.shstrtab` 中的偏移 | 重建时填入对应偏移 |
| `sh_type` | 节类型（见下表） | 决定如何解析这段数据 |
| `sh_flags` | 属性标志 | `SHF_ALLOC`（占内存）/ `SHF_WRITE`（可写）等 |
| `sh_addr` | 节加载后的虚拟地址 | RebuildShdr 反推：`段地址 - load_bias` |
| `sh_offset` | 节在文件中的偏移 | 设为 `sh_addr`（同 RebuildPhdr 思路） |
| `sh_size` | 节的大小 | **RebuildShdr 推算**：下一个节地址 - 当前地址 |
| `sh_link` | 关联的另一个 section 索引 | 重建依赖关系，见下 |
| `sh_info` | 额外信息 | 因 `sh_type` 而异 |
| `sh_addralign` | 对齐 | 4 或 8 |
| `sh_entsize` | 固定大小条目的单条大小 | 表类节用，如 `.dynsym` 每项大小 |

### 常见 `sh_type`

| 值 | 名称 | 含义 |
|---|---|---|
| `SHT_NULL` (0) | 空 | 占位，section 表第 0 项 |
| `SHT_PROGBITS` (1) | 程序数据 | `.text`/`.plt`/`.data` 等"真实内容" |
| `SHT_SYMTAB` (2) | 符号表（完整） | 静态链接用，运行时通常没有 |
| `SHT_STRTAB` (3) | 字符串表 | `.dynstr`/`.shstrtab` |
| `SHT_RELA` (4) | 显式 addend 重定位表 | 64 位常见 |
| `SHT_HASH` (5) | 符号哈希表 | `.hash` |
| `SHT_DYNAMIC` (6) | dynamic 段 | `.dynamic` |
| `SHT_NOTE` (7) | 注释 | |
| `SHT_NOBITS` (8) | 不占文件空间 | `.bss`（内存里有，文件里没有） |
| `SHT_REL` (9) | 隐式 addend 重定位表 | 32 位常见 |
| `SHT_DYNSYM` (11) | 动态符号表 | `.dynsym`，运行时符号 |

### `sh_link` 依赖关系

RebuildShdr 必须正确设置这些链接，IDA 才能顺藤摸瓜：

| section | `sh_link` 指向 | 原因 |
|---|---|---|
| `.dynsym` | `.dynstr` | 符号名存在字符串表里 |
| `.hash` | `.dynsym` | 哈希表索引的是动态符号 |
| `.rel.dyn` / `.rela.dyn` | `.dynsym` | 重定位项引用符号 |
| `.rel.plt` / `.rela.plt` | `.dynsym` | 同上 |
| `.dynamic` | `.dynstr` | `DT_SONAME` 等是字符串表偏移 |

## Dynamic 段条目（`Elf_Dyn`）

dynamic 段是一串 `{d_tag, d_val}` 条目。`d_tag` 是标签（`DT_*`），`d_val` 是值（通常是地址或计数）。ReadSoInfo 遍历的就是这些。

| `d_tag` | 含义 | 喂给谁 |
|---|---|---|
| `DT_NEEDED` (1) | 依赖的其他 SO 名（`.dynstr` 偏移） | 元信息 |
| `DT_PLTRELSZ` (2) | PLT 重定位表大小 | `.rel.plt` |
| `DT_PLTGOT` (3) | GOT 地址 | `.plt` |
| `DT_HASH` (4) | 哈希表地址 | `.hash` |
| `DT_STRTAB` (5) | 字符串表地址 | `.dynstr` |
| `DT_SYMTAB` (6) | 符号表地址 | `.dynsym` |
| `DT_RELA` (7) | RELA 重定位表地址 | `.rela.dyn` |
| `DT_RELASZ` (8) | RELA 表大小 | |
| `DT_REL` (17) | REL 重定位表地址 | `.rel.dyn` |
| `DT_RELSZ` (18) | REL 表大小 | |
| `DT_JMPREL` (23) | PLT 重定位表地址 | `.rel.plt` |
| `DT_INIT_ARRAY` (25) | 构造函数表 | `.init_array` |
| `DT_FINI_ARRAY` (26) | 析构函数表 | `.fini_array` |
| `DT_SONAME` (14) | 本 SO 名字（`.dynstr` 偏移） | 元信息 |

::: warning info 输出的是偏移不是名字
`DT_NEEDED` / `DT_SONAME` 的 `d_val` 是 `.dynstr` 里的**偏移**，不是字符串本身。`info` 子命令直接输出这个偏移值，要拿真实名字得自己按偏移读 `.dynstr`。
:::

## 重定位条目（`Elf_Rel` / `Elf_Rela`）

每条重定位记录说"'哪个位置（`r_offset`）要按什么方式（`r_info` 的类型部分）改写，引用哪个符号（`r_info` 的符号部分）"。

| 字段 | 含义 |
|---|---|
| `r_offset` | 要改写的位置（虚拟地址） |
| `r_info` | 编码了重定位类型 + 引用的符号索引 |
| `r_addend` | 仅 `Elf_Rela` 有：额外加到结果上的常数 |

`r_info` 拆分：低 8/32 位是类型（`ELF_R_TYPE`），高位是符号索引（`ELF_R_SYM`）。

重定位类型见 [重定位原理](./relocation)。

## 相关页面

- [前置背景知识](./background) — 这些字段为什么存在
- [工作原理](./how-it-works) — SoFixer 怎么改写这些字段
- [重定位原理](./relocation) — 重定位类型详解
- [磁盘 vs 内存](./disk-vs-memory) — 字段在两种形态下的对照
