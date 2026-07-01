# Codex 接入

OpenAI Codex（codex CLI）可调用本地命令行工具。本文给出接入 SoFixer 的方式。

## 一键复制提示词

把下面这段复制进 Codex 对话框发送，它会自动获取工具并配置好。

::: code-group

```text {title="Codex 接入提示词"}
你现在是 SoFixer 工具的操作助手。SoFixer 修复「从内存 dump 下来的 Android SO 文件」，提供面向 AI 的结构化 JSON CLI（SoFixer-skills-CLI）。

请按以下步骤接入：

1. 访问官网了解能力：https://android-security-engineer.github.io/SoFixer-skills/ai-agent/
   重点阅读 CLI 子命令 fix / info / verify，以及输出格式（默认 JSON，含 ok/error.code/退出码 0-3）。

2. 获取可执行文件：
   - 优先到 https://github.com/android-security-engineer/SoFixer-skills/releases 下载对应平台的 SoFixer-skills-CLI（Windows/Linux/macOS，分 32/64 位变体）。
   - 没有对应预编译包则从源码构建：git clone 后 cmake -DSO_64=ON（或 OFF）+ cmake --build。
   - 放到 PATH 或记录绝对路径。

3. 位宽选择：64 位 SO 用 CLI64，32 位 SO 用 CLI32。判断方法——
   先 info 子命令探测，若 error.code=load_failed 且提示 class 不符，换另一个变体重试。

4. 验证安装：运行 `<CLI> version`，应输出一行 JSON，含 "ok":true、"arch":32 或 64、"platform"。

5. 标准工作流（针对用户提供的 dump.so）：
   a. <CLI> info -s dump.so            # 查看结构，确认位宽与架构
   b. <CLI> fix -s dump.so -o fixed.so -m <dump基地址>   # 修复，需 dump 时的内存基地址
   c. <CLI> verify -s fixed.so         # 校验，看 summary.passed/failed

6. 输出都是单行 JSON，用 JSON 解析提取字段，不要用正则猜测。
   成功看 ok=true；失败看 error.code 与 error.stage；退出码 0=成功 / 1=业务失败 / 2=参数错误 / 3=内部错误。

现在请告诉我：你在哪个平台使用？是否有现成的 dump.so 需要修复？
```

:::

## Codex 调用 CLI 的要点

- **执行权限**：确保 Codex 有权执行本地 shell 命令（Codex 的默认 sandbox 可能限制执行，按需放开对 SoFixer-skills-CLI 的调用）。
- **绝对路径更稳**：把可执行文件路径告诉 Codex（如 `/usr/local/bin/SoFixer-skills-CLI64`），避免 PATH 查找问题。
- **JSON 解析**：Codex 拿到单行 JSON 后，可直接解析，无需处理多行人类日志。

## 位宽试错自动化

把这段告诉 Codex，它就能自己选对变体：

> 对待修 SO，先用 `info` 探测位宽。若返回 `error.code=class_mismatch`，则切换到另一个位宽变体（CLI32 ↔ CLI64）重试 `info`，直到 `ok=true` 为止。

## 示例对话

```
你：帮我修一下 dump.so，dump 时的基地址是 0x7db078b000。

Codex：
  → SoFixer-skills-CLI64 info -s dump.so
  → elf_class=ELFCLASS64，确认用 CLI64
  → SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000
  → 解析 JSON：ok=true，relocations_fixed=1234
  → SoFixer-skills-CLI64 verify -s fixed.so
  → summary.passed=8, failed=0
  → "修复完成，fixed.so 可用 IDA 打开分析。"
```

## 跨工具通用性

提示词刻意写得**与具体 agent 无关**——只描述 CLI 调用约定与 JSON 契约。因此除了 Codex，Cursor、Cline、Aider 等能跑本地命令的 agent 都可直接套用同一提示词。
