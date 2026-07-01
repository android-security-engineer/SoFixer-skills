# 命令总览

SoFixer-skills-CLI 采用**子命令 + 全局选项**风格（类似 git / gh）：

```
SoFixer-skills-CLI [全局选项] <子命令> [子命令选项] [参数]
```

## 子命令

| 子命令 | 作用 | 必填选项 | 详见 |
|---|---|---|---|
| [`fix`](./fix) | 修复从内存 dump 的 SO | `-s` | [fix](./fix) |
| [`info`](./info) | 查看 ELF / SO 结构信息 | `-s` | [info](./info) |
| [`verify`](./verify) | 校验 SO 结构有效性 | `-s` | [verify](./verify) |
| `version` | 工具与构建信息 | — | — |
| `help` | 列出命令与选项 schema | — | — |

## 全局选项

| 选项 | 说明 |
|---|---|
| `--format json\|text` | 输出格式，默认 `json`（机器友好） |
| `--pretty` | 美化输出（当前与默认一致，预留） |
| `--help` / `-h` | 等价于 `help` 子命令 |
| `--version` | 等价于 `version` 子命令 |

## 输出契约（面向 AI）

所有输出都是**单行 JSON**，统一信封：

```json
{
  "ok": true,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "fix",
  "result": { ... }
}
```

失败时：

```json
{
  "ok": false,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "fix",
  "error": {
    "code": "load_failed",
    "message": "source so file is invalid",
    "stage": "ElfReader::Load"
  }
}
```

- `ok`：唯一权威成败标志。
- `error.code`：机器可读的稳定错误码（见 [输出格式与错误码](./output)）。
- `error.stage`：失败发生在哪个阶段。

## 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 成功 |
| `1` | 业务失败（加载/重建/校验未通过） |
| `2` | 参数错误（未知命令/选项/缺必填） |
| `3` | 内部错误 |

AI 可据此区分"换输入再试"还是"修正调用方式"。

## 自省：help 子命令

AI agent 可先调 `help` 拿到全部命令的选项 schema，无需查阅文档即可正确调用：

```bash
SoFixer-skills-CLI64 help
```

返回每个命令的 `name`、`description`、`options`（含 `long`/`short`/`takes_value`/`required`/`help`）。

## 位宽变体

一个二进制只处理对应位宽的 SO（核心代码用 `__SO64__` 宏硬编码位宽）：

- `SoFixer-skills-CLI32` → 处理 32 位 SO
- `SoFixer-skills-CLI64` → 处理 64 位 SO

用 `info` 探测时若返回 `class_mismatch`，换另一个变体重试。
