# 它解决什么问题

## 一句话

SoFixer 修复**从进程内存中 dump 下来的 Android SO 文件**，让它能重新被 IDA 等逆向工具正常分析。

## 问题背景

Android 逆向中，经常需要拿到一个 SO 在运行时的真实代码（例如经过了加壳、脱壳、动态解密后的形态）。常见做法是：用 IDA 调试目标进程，把 SO 所在内存区域整块 dump 成文件。

但这份 dump 文件**不是磁盘上的 SO**——它是被 Android linker 加载、修改过后的内存镜像，直接用 IDA 打开会一片混乱：

| 问题 | 原因 |
|---|---|
| section 全没了 | linker 运行时只用 program header 和 dynamic 段，section header table 不加载进内存 |
| 偏移对不上 | 磁盘上 `p_offset` 是文件偏移；加载后段在 `p_vaddr`，文件偏移与虚拟地址关系变了 |
| 重定位表指向绝对地址 | linker 已把符号的绝对地址填进重定位项，表里满是进程内地址 |
| `p_filesz` ≠ `p_memsz` | `.bss` 等零初始化段在内存里占空间，但磁盘文件没有 |

结果：IDA 找不到函数、看不到符号、交叉引用断裂，分析无从下手。

## SoFixer 的解法

把上面四样东西逆回磁盘形态——这就是 SoFixer 做的全部事情：

1. **修复 program header**：把 `p_offset` 还原成 `p_vaddr`，`p_filesz` 对齐到 `p_memsz`。
2. **重建 section header table**：从 dynamic 段反推 `.dynsym` / `.dynstr` / `.hash` / `.rel.dyn` / `.plt` 等所有 section 的位置和大小，重新生成一张完整的 section 表。
3. **修复重定位**：依据 dump 时的内存基地址，把重定位项里的绝对地址减回相对地址。
4. **拼装最终文件**：把内容 + `.shstrtab` + section 表组合成合法 ELF，修正 ELF header。

修复后的 SO 可以直接拖进 IDA 分析，符号、函数、交叉引用基本恢复。

## 什么时候用 SoFixer

- 你从内存 dump 出了一个 SO，IDA 打开后符号全无、偏移错乱。
- 你做壳的脱壳分析，需要还原被加载后的 SO 的可分析形态。
- 你想对一个运行时已被修改的 SO 做静态逆向。

## 不适合的场景

- SO 本身是磁盘原文件、未经加载——它已经是合法 ELF，不需要修复。
- 你需要的是运行时 hook 而非静态分析——那该用 frida 等工具，不是 SoFixer。

继续阅读 [工作原理](./how-it-works) 了解修复的底层细节。
