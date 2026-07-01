# 磁盘 ELF vs 内存 ELF

> 这一页用**字段级对照**展示：同一个 SO，在磁盘上和加载到内存后，到底哪里不一样了。理解了这个，就理解了 SoFixer 要修什么。
>
> 建议先读 [前置背景知识](./background)。

## 一图总览

```
磁盘上的 SO（合法 ELF）              内存中的 SO（被 linker 改写）
┌─────────────────────┐             ┌─────────────────────┐
│ ELF Header           │             │ ELF Header           │  e_shoff=0, e_shnum=0
│  e_shoff = 0x1234    │             │  e_shoff = 0  ←丢了  │  section 表没了
│  e_shnum = 14        │             │  e_shnum = 0         │
├─────────────────────┤             ├─────────────────────┤
│ Program Header Table │             │ Program Header Table │  p_offset 还是旧值
│  p_offset = 0x100    │             │  p_offset = 0x100←对不上│  与内存布局不符
│  p_filesz = 0x500    │             │  p_filesz = 0x500    │
│  p_memsz  = 0x800    │             │  p_memsz  = 0x800    │
├─────────────────────┤             ├─────────────────────┤
│ 各段内容（代码/数据） │ ──加载────▶ │ 各段内容（被改写）    │  重定位项变了
│  重定位项 = 相对偏移  │             │  重定位项 = 绝对地址  │  ←加了基地址
│  GOT = 未解析        │             │  GOT = 已填真实地址   │  ←linker 填了
├─────────────────────┤             ├─────────────────────┤
│ Section Header Table │             │ （不存在）            │  ←linker 根本不加载
│  14 个 section       │             │                      │
└─────────────────────┘             └─────────────────────┘
        ↑                                    ↑
   IDA 能正常分析                     IDA 打开一片混乱
```

## 字段级对照表

### ELF Header

| 字段 | 磁盘形态 | 内存形态（dump 出来） | SoFixer 处理 |
|---|---|---|---|
| `e_ident` | 完整 | 完整（不变） | 不动 |
| `e_type` | `ET_DYN` | `ET_DYN`（不变） | RebuildFin 固定为 `ET_DYN` |
| `e_machine` | 40/183 等 | 不变 | RebuildFin 固定 |
| `e_shoff` | 指向 section 表 | **通常 0**（表没加载） | RebuildFin 重写为新表偏移 |
| `e_shnum` | section 数量 | **通常 0** | RebuildFin 重写为重建数量 |
| `e_shstrndx` | `.shstrtab` 索引 | **通常 0** | RebuildFin 重写 |

### Program Header（每个段）

| 字段 | 磁盘形态 | 内存形态 | SoFixer 处理 |
|---|---|---|---|
| `p_type` | `PT_LOAD` 等 | 不变 | 不动 |
| `p_offset` | 文件偏移（如 0x100） | **仍是旧值**，但与内存布局不符 | **RebuildPhdr：设为 `p_vaddr`** |
| `p_vaddr` | 虚拟地址 | 不变 | 不动（基准坐标） |
| `p_paddr` | 物理地址 | 不变 | RebuildPhdr 设为 `p_vaddr` |
| `p_filesz` | 文件中大小（< memsz） | **仍是旧值**，但 dump 含 bss 区 | **RebuildPhdr：设为 `p_memsz`** |
| `p_memsz` | 内存中大小 | 不变 | 不动 |

### Section Header Table

| | 磁盘形态 | 内存形态 | SoFixer 处理 |
|---|---|---|---|
| 整张表 | 存在，14 个 section | **完全不存在**（linker 不加载） | **RebuildShdr：从 dynamic 段反推，整张重建** |
| `.shstrtab` | 存在 | 不存在 | RebuildShdr + RebuildFin 重建 |

### 重定位项

| | 磁盘形态 | 内存形态 | SoFixer 处理 |
|---|---|---|---|
| 相对重定位（`R_*_RELATIVE`）的值 | 相对偏移 | **绝对地址**（加了基地址） | **RebuildRelocs：减回基地址** |
| 跨 SO 重定位（`R_ARM_JUMP_SLOT` 等） | 符号索引 | 外部符号的真实地址 | **跳过**（无法还原） |
| GOT 表项 | 未解析/相对 | 外部符号真实地址 | 跳过（依赖别的 SO） |

## 为什么会变成这样

回顾 linker 加载流程（[背景知识](./background#android-linker加载一个-so-时发生了什么)）：

1. 读 ELF header、program header。→ header 本身没大改（除了 section 相关字段本来就没用到）。
2. 按 `PT_LOAD` 把内容映射到内存。→ 内容搬进去了，但 header 里的 `p_offset` 还指着旧的文件偏移，对不上了。
3. 处理重定位，把绝对地址写回。→ 重定位项从相对变绝对。
4. 调构造函数。
5. **section header table 不读**。→ 内存里根本没有它。

dump 内存得到的就是步骤 1-4 之后的产物，带着所有"被改写"的痕迹。

## SoFixer 的逆向操作

把上表"内存形态"一列，逆回"磁盘形态"：

| 问题 | 逆操作 | 阶段 |
|---|---|---|
| `p_offset`/`p_filesz` 对不上 | 改写为 `p_vaddr`/`p_memsz` | RebuildPhdr |
| section 表不存在 | 从 dynamic 段反推重建 | RebuildShdr |
| 重定位是绝对地址 | 减去 dump 基地址 | RebuildRelocs |
| ELF header 的 section 字段为 0 | 重写 `e_shoff`/`e_shnum`/`e_shstrndx` | RebuildFin |

四步对应四个"被改写"的痕迹，一一对应。这就是 SoFixer 全部工作的本质——**把 linker 做过的改动逆回去**。

## 相关页面

- [工作原理](./how-it-works) — 每个逆操作的具体实现
- [ELF 字段速查](./elf-reference) — 字段含义详解
- [重定位原理](./relocation) — 重定位项变化的细节
