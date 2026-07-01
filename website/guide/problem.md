# 它解决什么问题

> 如果你不熟悉 ELF、SO、加载器这些概念，建议先读 [前置背景知识](./background)。

## 一句话

SoFixer 修复**从进程内存中 dump 下来的 Android SO 文件**，让它能重新被 IDA 等逆向工具正常分析。

## 场景：为什么要从内存 dump SO

逆向一个 Android 应用时，你常常拿不到"原始的、可读的 SO"：

- **加壳/混淆**：磁盘上的 `.so` 是被加密或变形过的，直接拖进 IDA 看到的是壳的解密 stub，不是真实逻辑。
- **运行时解密**：有些代码要等程序跑起来、解密完成后才在内存里现出真身。
- **动态下发**：SO 是从服务器下载后在内存里加载的，磁盘上根本没有文件。

这些情况下，唯一能看到真实代码的办法是：**让程序先正常跑起来（让 SO 被解密/加载到内存），再把那块内存原样拷出来**。这个过程叫 **内存 dump**。

```
  磁盘上的 SO            进程内存里的 SO              dump 出来的文件
 ┌──────────┐    加载     ┌──────────────┐    拷贝    ┌──────────────┐
 │ 加密/变形 │ ────────▶  │ 真实代码(解密) │ ────────▶  │  内存镜像     │
 │  看不懂   │  linker改写 │ 已被linker改写 │  一整块     │  ⚠️ 不是合法ELF│
 └──────────┘            └──────────────┘            └──────────────┘
                                                          ↑
                                                       IDA 打不开
```

这三种形态的流转：

```mermaid
flowchart LR
    A["磁盘 SO<br/>加密/变形<br/>(看不懂)"] -->|"linker 加载<br/>解密+改写"| B["内存 SO<br/>真实代码<br/>(被改坏)"]
    B -->|"IDA dump<br/>整块内存"| C["dump.so<br/>内存镜像<br/>(非合法 ELF)"]
    C -->|"SoFixer 修复"| D["fixed.so<br/>合法 ELF<br/>(IDA 可分析)"]

    classDef bad fill:#0d1117,stroke:#d8a839,color:#d8a839
    classDef good fill:#0d1117,stroke:#56d364,color:#56d364
    classDef base fill:#161b22,stroke:#39d0d8,color:#e6edf3
    class A,C bad
    class B,D good
```


## 问题：dump 出来的文件不是合法 ELF

dump 拿到的文件，内容就是那块内存的原始字节。但**内存里的 SO 已经不是磁盘上的 SO 了**——Android linker 加载时改写了它，导致直接用 IDA 打开一片混乱。

### 具体坏在哪四点

| 问题 | 原因 | IDA 里的表现 |
|---|---|---|
| **section 全没了** | linker 运行时只用 program header 和 dynamic 段，section header table **根本不加载进内存** | 看不到 `.text`/`.data` 等节，函数列表空 |
| **偏移对不上** | 磁盘上 `p_offset` 是文件偏移；加载后段在 `p_vaddr`，文件偏移与虚拟地址的对应关系变了 | IDA 按偏移读内容，读到错位置，反汇编是乱码 |
| **重定位指向绝对地址** | linker 已把符号的绝对地址填进重定位项，表里满是进程内地址（如 `0x7db078c000`） | 交叉引用指向运行时地址，无法还原代码逻辑 |
| **`p_filesz` ≠ `p_memsz`** | `.bss` 等零初始化段在内存里占空间，但磁盘文件没有这段数据 | 文件大小与内存布局对不上，解析报错 |

### 为什么 IDA 这么依赖这些

IDA 静态分析靠的是 section header table 来"理解"文件结构——`.text` 是代码、`.dynsym` 是符号表、`.plt` 是过程链接表。没有这些 section 划分，IDA 就像一个拿到一整坨没有目录的厚书的人，只能盲目翻，找不到函数边界、看不到符号名、交叉引用全部断裂。

## SoFixer 的解法

把上面四样东西**逆回磁盘形态**——这就是 SoFixer 做的全部事情：

1. **修复 program header**：把 `p_offset` 还原成 `p_vaddr`，`p_filesz` 对齐到 `p_memsz`。→ 解决偏移对不上。
2. **重建 section header table**：从 dynamic 段反推 `.dynsym` / `.dynstr` / `.hash` / `.rel.dyn` / `.plt` 等所有 section 的位置和大小，重新生成一张完整的 section 表。→ 解决 section 全没了。
3. **修复重定位**：依据 dump 时的内存基地址，把重定位项里的绝对地址减回相对地址。→ 解决重定位指向绝对地址。
4. **拼装最终文件**：把内容 + `.shstrtab`（section 名字表）+ section 表组合成合法 ELF，修正 ELF header。→ 收尾。

```
  dump 出来的内存镜像                SoFixer 修复后
 ┌──────────────────┐   RebuildPhdr    ┌──────────────────┐
 │ 无 section 表     │   RebuildShdr    │  完整 section 表  │
 │ 偏移错乱          │   RebuildRelocs  │  偏移已修正        │
 │ 重定位=绝对地址    │   RebuildFin     │  重定位=相对地址   │
 │ ELF header 不自洽 │ ──────────────▶  │  合法 ELF          │
 └──────────────────┘                  └──────────────────┘
       IDA 打不开                            IDA 能正常分析
```

修复后的 SO 可以直接拖进 IDA 分析，符号、函数、交叉引用基本恢复。

## 什么时候用 SoFixer

- 你从内存 dump 出了一个 SO，IDA 打开后符号全无、偏移错乱。
- 你做壳的脱壳分析，需要还原被加载后的 SO 的可分析形态。
- 你想对一个运行时已被修改的 SO 做静态逆向。

## 不适合的场景

- **SO 本身是磁盘原文件、未经加载**——它已经是合法 ELF，不需要修复。
- **你需要的是运行时 hook 而非静态分析**——那该用 frida 等工具，不是 SoFixer。
- **SO 严重损坏或只 dump 了部分内存**——SoFixer 假设你 dump 了完整的加载区域，缺数据它无法补。

继续阅读 [工作原理](./how-it-works) 了解修复的底层细节。
