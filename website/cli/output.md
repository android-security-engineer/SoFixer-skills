# 输出格式与错误码

SoFixer-skills-CLI 全部输出都是**单行 JSON**，便于 AI agent 用标准 JSON 解析器读取，而非正则猜人类日志。

## 统一信封

无论哪个子命令，输出都遵循同一结构：

```json
{
  "ok": true | false,
  "tool": "SoFixer-skills-CLI",
  "arch": 32 | 64,
  "command": "fix | info | verify | version | help",
  "result": { ... }     // ok=true 时
  "error": { ... }      // ok=false 时
}
```

字段：

| 字段 | 说明 |
|---|---|
| `ok` | **唯一权威成败标志**。AI 应只看它判断成功与否。 |
| `arch` | 本次二进制的位宽（32 或 64），告诉 AI 当前是哪个变体 |
| `command` | 触发的子命令名 |
| `result` | 成功时的结果对象，各命令不同（见 [fix](./fix)/[info](./info)/[verify](./verify)） |
| `error` | 失败时的错误对象 |

## 错误对象

```json
"error": {
  "code": "load_failed",
  "message": "source so file is invalid",
  "stage": "ElfReader::Load"
}
```

| 字段 | 说明 |
|---|---|
| `code` | **机器可读的稳定错误码**，API 跨版本保持兼容，AI 据此分支 |
| `message` | 人类可读描述（措辞可能随版本调整，**不要**据此判断） |
| `stage` | 失败发生在哪个阶段（如 `ElfReader::Load`、`ElfRebuilder::Rebuild`） |

## 错误码表

| code | 含义 | 处理建议 |
|---|---|---|
| `load_failed` | 源文件非合法 ELF | 先 `info` 探测；检查文件是否完整 |
| `class_mismatch` | ELF 位宽与当前变体不符 | 换 CLI32 ↔ CLI64 重试 |
| `open_output_failed` | 输出文件无法写 | 检查目录权限/路径 |
| `membase_invalid` | `-m` 不是合法十六进制 | 加 `0x` 前缀 |
| `rebuild_failed` | 修复阶段失败 | 看 `error.stage`，常见是 dynamic 段损坏 |
| `verify_failed` | 校验有失败项 | 看 `summary` 的失败项 |
| `unknown_command` | 未知子命令 | 调 `help` 查可用命令 |
| `unknown_option` | 未知选项 | 调 `help` 查选项 schema |
| `missing_required` | 缺必填选项 | 看 `error.message` 指出的选项 |
| `internal_error` | 未预期内部错误 | 提 issue，附完整命令与输出 |

## 64 位地址的处理

JSON Number 本质是 IEEE 754 double，**只能精确到 2^53**。SO 的 64 位虚拟地址会超出。因此 CLI 中所有 64 位地址、`entry`、`vaddr` 等字段都用**十六进制字符串**输出（如 `"0x7db078b000"`），AI 解析后按需转成数值即可。

## 退出码

CLI 同时用退出码表达结果，方便 shell 脚本和 AI 判断：

| 退出码 | 含义 | 对应情况 |
|---|---|---|
| `0` | 成功 | `ok: true` |
| `1` | 业务失败 | `ok: false`，输入/修复/校验层面的问题 |
| `2` | 参数错误 | `unknown_command` / `unknown_option` / `missing_required` |
| `3` | 内部错误 | `internal_error` |

AI 可据此一句话分流：退出码 `2` → 修正调用方式（换命令/补选项）；`1` → 换输入或排查数据；`0` → 读 `result`。

## 自省：help

不确定某个命令的选项时，调 `help`（或 `--help`）拿结构化 schema：

```bash
SoFixer-skills-CLI64 help
```

返回每个命令的 `name`、`description`、`options`（含 `long`/`short`/`takes_value`/`required`/`help`）。AI 无需查阅文档即可自省 CLI 能力。
