---
layout: home

hero:
  name: SoFixer
  text: 修复从内存 dump 的 Android SO
  tagline: 重建 ELF 的 section / program header 与重定位，让 dump 下来的 SO 重新可被 IDA 分析。提供面向 AI Agent 的结构化 CLI。
  actions:
    - theme: brand
      text: AI Agent 一键接入
      link: /ai-agent/
    - theme: alt
      text: 它解决什么问题
      link: /guide/problem
    - theme: alt
      text: GitHub
      link: https://github.com/android-security-engineer/SoFixer-skills

features:
  - title: 重建 ELF 结构
    details: 从 dynamic 段反推并重建整张 section header table，修正 program header 偏移，修复重定位绝对地址。
  - title: 面向 AI 的 CLI
    details: SoFixer-skills-CLI 默认输出结构化 JSON，子命令可被 Claude Code / Codex 等 AI agent 直接调用与解析。
  - title: 跨平台 · 零依赖
    details: 纯 C++ 实现，Windows / Linux / macOS 同一份代码构建，无第三方依赖。
  - title: 自省式帮助
    details: help 子命令输出每个命令的选项 schema，AI 无需文档即可自省 CLI 能力并正确调用。
---

<div class="ai-prompt-banner">

## ⚡ 给 AI Agent 用的接入提示词（一键复制）

把下面这段复制到 **Claude Code** 或 **Codex** 的对话框，然后发送——AI 会自动下载工具、按引导配置，并准备好调用 SoFixer 修复 SO。

</div>

::: code-group

```text {title="Claude Code / Codex 通用提示词"}
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

<div class="ai-prompt-banner">

### 为什么这样设计？

- **默认 JSON + 稳定错误码**：AI 解析 `ok` / `error.code` 比读人类日志可靠，退出码让 AI 能一句话判断结果。
- **help 自省**：AI 可先调 `help` 拿到全部命令的选项 schema，无需翻文档。
- **info 探测位宽**：一份提示词覆盖 32/64 两种情况，AI 自己用 info 试错即可选对变体。

</div>

<style>
.ai-prompt-banner {
  margin-top: 2rem;
  padding: 1.25rem 1.5rem;
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-divider);
  border-left: 2px solid var(--vp-c-brand-1);
}
.ai-prompt-banner h2,
.ai-prompt-banner h3 {
  margin-top: 0;
  border-top: none;
}
</style>
