# 前置背景知识

> 这一页面向**没有逆向/ELF 背景的读者**。如果你已经熟悉 ELF 格式和 Android 加载流程，可直接跳到 [它解决什么问题](./problem)。
>
> 读完后你会理解：SO 是什么、ELF 长什么样、Android 怎么把 SO 装进内存、以及为什么"从内存 dump 出来的 SO"和"磁盘上的 SO"不是同一个东西。

## SO 是什么

Android 应用里那些用 C/C++ 写的关键代码（比如加解密、音视频编解码、反作弊逻辑），编译后是 `.so` 文件——**共享库**（Shared Object），Unix 世界的"动态链接库"，相当于 Windows 的 `.dll`。

一个 APK 装到手机上，里面的 `.so` 躺在 APK 里是"磁盘形态"；当应用运行起来，Android 系统会把这些 `.so` 加载进进程内存，变成"内存形态"——**SoFixer 处理的就是后者**。

## ELF：SO 的文件格式

`.so` 文件的格式叫 **ELF**（Executable and Linkable Format）。它本质上是一张"装运清单"：告诉系统"这段代码放在哪、依赖哪些符号、怎么重定位"。

一个 ELF 文件由这几部分组成：

```
┌─────────────────────────┐
│  ELF Header（总目录）    │  ← 文件开头，说明"我是什么、有几段、表在哪"
├─────────────────────────┤
│  Program Header Table   │  ← 给"加载器"看：怎么把文件切成段装进内存
├─────────────────────────┤
│  各段内容（代码/数据）   │  ← 真正的机器码、全局变量等
├─────────────────────────┤
│  Section Header Table   │  ← 给"链接器/分析工具"看：按语义切成 section
└─────────────────────────┘
```

### Segment（段）vs Section（节）

初学者最容易混淆这两个概念，但它们是**同一块数据的两种切法**：

| | Segment（段） | Section（节） |
|---|---|---|
| 给谁看 | 加载器（runtime） | 链接器、IDA 等分析工具 |
| 关心什么 | "这块要装进内存，权限是啥" | "这块是函数表？字符串表？代码？" |
| 来源 | Program Header Table | Section Header Table |
| 举例 | 一个 PT_LOAD 段 = 代码+只读数据打包 | `.text`（代码）/ `.data`（已初始化全局变量）/ `.dynsym`（动态符号表） |

**关键**：加载器只认 segment，**不认 section**。运行时 section header table 根本不会被加载进内存——这正是 dump 出来的 SO 缺 section 的根因（见下文 [为什么 dump 的 SO 坏了](./problem)）。

### 虚拟地址 vs 文件偏移

ELF 里每个段有两个"位置"描述：

- **`p_offset`（文件偏移）**：这块数据在**磁盘文件**里的第几个字节开始。
- **`p_vaddr`（虚拟地址）**：这块数据加载到**进程内存**后，应该在哪个内存地址。

磁盘上和内存里是两套坐标。系统加载时按 `p_vaddr` 把内容放到内存对应位置。

::: tip 为什么需要两套
一个文件可能被加载到不同进程的不同基址（ASLR 地址随机化）。磁盘偏移固定，但加载后的虚拟地址会随基址平移。
:::

## dynamic 段：SO 的"自我描述"

除了代码和数据，SO 还需要一个"自我描述"区——告诉系统"我叫什么名字、我依赖哪些别的 SO、我的符号表在哪、要重定位哪些地址"。这就是 **dynamic 段**，里面是一串 `DT_*` 标签：

| 标签 | 含义 |
|---|---|
| `DT_SONAME` | 我自己的名字 |
| `DT_NEEDED` | 我依赖的其他 SO |
| `DT_STRTAB` / `DT_SYMTAB` | 字符串表 / 符号表的地址 |
| `DT_REL` / `DT_RELA` / `DT_JMPREL` | 重定位表地址 |
| `DT_INIT_ARRAY` / `DT_FINI_ARRAY` | 构造/析构函数表 |

**dynamic 段是 SoFixer 的命根子**：section header table 丢了之后，所有 section 的位置只能从 dynamic 段里这些标签"反推"出来。完整的 `DT_*` 标签清单见 [ELF 字段速查](./elf-reference)。

## 重定位：为什么运行时代码要被改写

C 代码里写 `printf("hi")`，编译时编译器不知道 `printf` 最终在哪个地址（它来自 libc.so，要等运行时才知道）。于是编译器在代码里留个"占位符"，再登记一条**重定位记录**："'这条指令的第 4 字节，等运行时把 `printf` 的真实地址填进去。"

- **磁盘上的 SO**：重定位项里是**相对偏移**（"我离自己基地址多远"）。
- **加载进内存后**：linker 算出真实绝对地址（"基地址 + 偏移"），把绝对地址**直接写回**重定位项所在的内存。

所以同一份重定位表，磁盘形态是相对地址，内存形态是绝对地址。dump 内存得到的自然是绝对地址——**必须减回基地址才能还原成磁盘形态**，这正是 SoFixer `RebuildRelocs` 干的事。

::: tip 想深入
重定位还分"相对重定位"和"需要符号解析的重定位"两类，后者涉及跨 SO 调用和 PLT/GOT 机制，SoFixer 对两类处理不同。详见 [重定位原理](./relocation)。
:::

## Android linker：加载一个 SO 时发生了什么

Android 的动态链接器（linker）加载一个 SO 时，大致步骤：

1. **读 ELF header**，找到 program header table。
2. **按 PT_LOAD 段把内容映射到内存**（`mmap`），放到某个基址（`load_bias`）。
3. **处理重定位**：遍历重定位表，把符号的真实地址填进对应内存位置。
4. **调用构造函数**（`DT_INIT_ARRAY`）。
5. **section header table？根本不读**——运行时用不到。

::: warning 第 5 点是关键
linker 从不碰 section header table。所以内存里**根本没有 section 信息**。你从内存 dump，自然 dump 不到 section——只能靠 dynamic 段反推。
:::

## "dump 基地址"是什么

linker 把 SO 放进内存时，起始地址叫**基地址**（base address / load_bias）。比如 `0x7db078b000`。

- 内存里 SO 内部的所有地址 = 基地址 + 相对偏移。
- 你从 IDA 调试器里 dump 内存时，**记下这个起始地址**，就是 dump 基地址。
- 修复重定位时，SoFixer 用它把绝对地址减回相对地址：`相对地址 = 绝对地址 - dump基地址`。

::: tip 基地址从哪来
IDA 调试目标进程时，能在 Modules 面板看到每个 SO 的加载基址。dump 脚本里的 `start_address` 就是这个基址——见 [快速开始 · 从 IDA dump](./getting-started#从-ida-dump-so)。
:::

## 小结：为什么需要 SoFixer

把上面串起来：

1. SO 在磁盘上是合法 ELF（有 section 表、相对地址重定位）。
2. 加载进内存后，linker 改了它：section 表丢了、偏移坐标变了、重定位变成绝对地址。
3. 你从内存 dump，得到的是"被改坏的"内存镜像，IDA 不认识。
4. SoFixer 把它**逆回磁盘形态**：重建 section 表、修正偏移、还原重定位。

接下来读 [它解决什么问题](./problem) 看具体坏成什么样，或直接看 [工作原理](./how-it-works) 看怎么修。
