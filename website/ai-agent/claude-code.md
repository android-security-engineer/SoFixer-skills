# Claude Code 接入

Claude Code 是 Anthropic 的官方 CLI agent。本文给出一键接入 SoFixer 的方式。

## 方式一：直接粘贴提示词（最快）

把下面这段复制进 Claude Code 对话框发送，Claude 会自动获取工具并配置好。

::: code-group

```text {title="Claude Code 接入提示词"}
你现在是 SoFixer 工具的操作助手。SoFixer 修复「从内存 dump 下来的 Android SO 文件」，提供面向 AI 的结构化 JSON CLI（SoFixer-skills-CLI）。

请按以下步骤接入：

1. 访问官网了解能力：https://android-security-engineer.github.io/SoFixer-skills/ai-agent/
   重点阅读 CLI 子命令 fix / info / verify，以及输出格式（默认 JSON，含 ok/error.code/退出码 0-3）。

2. 获取可执行文件：
   - 优先到 https://github.com/android-security-engineer/SoFixer-skills/releases 下载对应平台的 SoFixer-skills-CLI（Windows/Linux/macOS，分 32/64 位变体）。
   - 没有对应预编译包则从源码构建：git clone 后 cmake -DSO_64=ON（或 OFF）+ cmake --build。
   - 放到 PATH 或记录绝对路径。可以用 ! 前缀在 Claude Code 里直接跑构建命令，输出会回到对话。

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

## 方式二：用 Claude Code 的 Skill 机制（持久化）

如果你经常用，可把提示词沉淀成一个 Skill，避免每次复制：

1. 在项目或全局的 `.claude/skills/` 下建一个目录，如 `sofixer/`。
2. 写一个 `SKILL.md`，frontmatter 标注 `name` 与 `description`，正文放上面的接入流程。
3. 之后在 Claude Code 里输入 `/sofixer` 即可唤起。

::: tip 何时用 Skill
- 你或团队反复修 SO，想让 AI 默认带上正确的调用约定。
- 想把"先 info 探测→fix→verify"的编排固化下来。
:::

## Claude Code 调用 CLI 的技巧

- **`!` 前缀**：直接在对话框输入 `!SoFixer-skills-CLI64 info -s dump.so`，命令在本 session 跑，输出回到对话，Claude 直接看到结果。
- **让 Claude 自建命令**：描述清楚后，Claude 会自己用 Bash 工具调 CLI 并解析 JSON。
- **位宽试错自动化**：告诉 Claude "如果 class_mismatch 就换变体重试"，它会自己编排。

## 示例对话

```
你：帮我修一下 dump.so，dump 时的基地址是 0x7db078b000。

Claude：
  → !SoFixer-skills-CLI64 info -s dump.so       # 探测
  → 看到 elf_class=ELFCLASS64，确认用 CLI64
  → !SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000
  → 解析 JSON：ok=true，relocations_fixed=1234
  → !SoFixer-skills-CLI64 verify -s fixed.so
  → summary.passed=8, failed=0，全通过
  → "已修复完成，fixed.so 可直接拖进 IDA 分析。"
```
