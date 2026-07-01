# info — 查看 SO 结构

查看一个 SO 的 ELF 结构信息，主要用于修复前**探测位宽与架构**、确认源文件是否合法。

## 用法

```bash
SoFixer-skills-CLI64 info -s <源文件>
```

## 选项

| 选项 | 必填 | 说明 |
|---|---|---|
| `-s`, `--source <path>` | ✅ | 待查看的 SO 文件 |

## 输出示例（成功）

```json
{
  "ok": true,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "info",
  "result": {
    "file": "dump.so",
    "elf_class": "ELFCLASS64",
    "data_encoding": "ELFDATA2LSB",
    "machine": "AARCH64",
    "entry": "0x7db078b010",
    "segments": [
      { "type": "PT_LOAD", "vaddr": "0x7db078b000", "filesz": 524288, "memsz": 524288 }
    ],
    "dynamic": {
      "DT_SONAME": 15,
      "DT_NEEDED": [3],
      "DT_STRTAB": "0x7db0831000"
    }
  }
}
```

## 输出示例（失败 · 位宽不符）

```json
{
  "ok": false,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "info",
  "error": {
    "code": "class_mismatch",
    "message": "elf class mismatch, use the other variant",
    "stage": "ElfReader::Load"
  }
}
```

::: warning 位宽不符是最常见情况
`info` 报 `class_mismatch` 时，**换另一个位宽变体重试**即可（CLI64 ↔ CLI32）。这是预期内的探测流程，不是真正的错误。
:::

## 字段说明

| 字段 | 含义 |
|---|---|
| `elf_class` | ELFCLASS32 / ELFCLASS64，对应 CLI32 / CLI64 |
| `machine` | ARM（40）/ AARCH64（183）等 |
| `entry` | 入口虚拟地址（十六进制字符串，避免精度丢失） |
| `segments` | program header 列表 |
| `dynamic` | dynamic 段里的关键 `DT_*` 条目 |

## 已知限制

- `DT_NEEDED` / `DT_SONAME` 输出的是**字符串表偏移**，而非库名本身（要拿库名需自己按偏移读 strtab）。
- 64 位地址一律用十六进制字符串输出（JSON Number 是 double，只能精确到 2^53）。

## 退出码

- `0` — 读取成功
- `1` — 读取失败（`load_failed` / `class_mismatch`）
- `2` — 参数错误（缺 `-s`）

## AI 调用建议

把 `info` 当作修复前的**第一步探测**：先拿它确认位宽与架构，再决定用哪个变体跑 `fix`。`class_mismatch` 不是错，是探测信号。
