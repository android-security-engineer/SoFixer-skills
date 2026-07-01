# AI Agent 接入

SoFixer-skills-CLI 专门为 AI agent（Claude Code、Codex 等）设计：默认输出结构化 JSON、稳定错误码、规范化退出码，并提供 `help` 自省。让 AI 不读文档也能正确调用。

## 三种接入方式

| 方式 | 适合 | 链接 |
|---|---|---|
| **一键复制提示词** | 把现成提示词粘进对话框，AI 自动配置 | 见下方 |
| [Claude Code](./claude-code) | 用 Claude Code 的 Skill / MCP 机制 | [接入指南](./claude-code) |
| [Codex](./codex) | 用 OpenAI Codex 的 CLI 调用方式 | [接入指南](./codex) |

## 一键复制提示词

把下面这段复制到 AI 对话框，发送即可。AI 会自动获取工具、按引导配置，并准备好调用 SoFixer 修复 SO。

::: code-group

```text {title="通用提示词（Claude Code / Codex）"}
你现在是 SoFixer 工具的操作助手。SoFixer 是一个修复「从内存 dump 下来的 Android SO 文件」的命令行工具，提供面向 AI 的结构化 JSON CLI（SoFixer-skills-CLI）。

请按以下步骤接入并准备好工具：

1. 先访问官网了解能力：https://android-security-engineer.github.io/SoFixer-skills/ai-agent/
   重点阅读 CLI 子命令 fix / info / verify，以及输出格式（默认 JSON，含 ok/error.code/退出码 0-3）。

2. 获取可执行文件：
   - 优先到 https://github.com/android-security-engineer/SoFixer-skills/releases 下载对应平台的 SoFixer-skills-CLI（Windows/Linux/macOS，分 32/64 位变体）。
   - 若没有对应平台的预编译包，则从源码构建：git clone 后 cmake -DSO_64=ON（或 OFF）+ cmake --build。
   - 把可执行文件放到 PATH 可访问处，或记录其绝对路径。

3. 位宽选择：处理 64 位 SO 用 CLI64，处理 32 位 SO 用 CLI32。判断方法——
   先用 info 子命令探测，若返回 error.code=load_failed 且提示 class 不符，换另一个位宽变体重试。

4. 验证安装：运行 `<CLI> version`，应输出一行 JSON，含 "ok":true、"arch":32 或 64、"platform" 字段。

5. 标准工作流（针对用户提供的 dump.so）：
   a. <CLI> info -s dump.so            # 查看结构，确认位宽与架构
   b. <CLI> fix -s dump.so -o fixed.so -m <dump基地址>   # 修复，需提供内存 dump 时的基地址
   c. <CLI> verify -s fixed.so         # 校验修复结果，看 summary.passed/failed

6. 所有输出都是单行 JSON，请用 JSON 解析后提取字段，不要用正则猜测。
   成功看 ok=true；失败看 error.code 与 error.stage；退出码 0=成功 / 1=业务失败 / 2=参数错误 / 3=内部错误。

现在请告诉我：你准备在哪个平台使用？是否有现成的 dump.so 需要修复？我会据此帮你选对变体并跑通流程。
```

:::

## 为什么这样设计

- **默认 JSON + 稳定错误码**：AI 解析 `ok` / `error.code` 比读人类日志可靠得多，退出码让 AI 一句话判断结果。
- **help 自省**：AI 可先调 `help` 拿到全部命令的选项 schema（含必填/可选/取值），无需翻文档。
- **info 探测位宽**：一份提示词覆盖 32/64 两种情况，AI 用 `info` 试错即可选对变体，不必让用户先判断。
- **64 位地址用字符串**：JSON Number 只能精确到 2^53，地址类字段一律十六进制字符串，避免精度丢失。

## AI 标准工作流

```
info  ──位宽不符──▶ 换变体重试
  │
  └─ok─▶ fix -m <基地址> ──▶ verify ──▶ 全通过则可用 IDA 打开
```

1. `info -s dump.so` — 探测位宽与架构。`class_mismatch` 就换变体。
2. `fix -s dump.so -o fixed.so -m 0x...` — 修复，`-m` 是 dump 时的内存基地址。
3. `verify -s fixed.so` — 校验，看 `summary.passed/failed`，全通过即结构有效。

详见 [CLI 参考](/cli/) 与 [输出格式与错误码](/cli/output)。
