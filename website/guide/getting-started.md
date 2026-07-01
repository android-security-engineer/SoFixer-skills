# 快速开始

## 获取工具

### 方式一：下载预编译包（推荐）

到 [GitHub Releases](https://github.com/android-security-engineer/SoFixer-skills/releases) 下载对应平台的可执行文件：

| 文件名 | 平台 | 位宽 |
|---|---|---|
| `SoFixer-skills-CLI-Linux-64` | Linux | 64 位 SO |
| `SoFixer-skills-CLI-Linux-32` | Linux | 32 位 SO |
| `SoFixer-skills-CLI-macOS-64` | macOS | 64 位 SO |
| `SoFixer-skills-CLI-Windows-64.exe` | Windows | 64 位 SO |
| `SoFixer-skills-CLI-Windows-32.exe` | Windows | 32 位 SO |

::: tip 位宽怎么选
处理 64 位 SO 用 CLI64，处理 32 位 SO 用 CLI32。不确定的话，先用 `info` 子命令探测，若报 `class_mismatch` 就换另一个变体。
:::

### 方式二：从源码构建

```bash
git clone https://github.com/android-security-engineer/SoFixer-skills.git
cd SoFixer
mkdir build && cd build

# 64 位（处理 64 位 SO）
cmake -DSO_64=ON ..
cmake --build . -j

# 或 32 位
cmake -DSO_64=OFF ..
cmake --build . -j
```

依赖：CMake ≥ 3.3、C++11 编译器。无第三方库。构建会同时产出旧的 `SoFixer32/64` 和新的 `SoFixer-skills-CLI32/64`。

## 验证安装

```bash
SoFixer-skills-CLI64 version
```

应输出一行 JSON：

```json
{"ok":true,"tool":"SoFixer-skills-CLI","version":"1.0.0","arch":64,
 "target":"SoFixer-skills-CLI64","core":"SoFixer v2.1",
 "platform":"linux","compiler":"13.3.0"}
```

## 三步修复流程

假设你已从内存 dump 出 `dump.so`，并知道 dump 时的基地址是 `0x7db078b000`。

### 1. 查看结构（确认位宽与架构）

```bash
SoFixer-skills-CLI64 info -s dump.so
```

输出含 `elf_class`、`machine`、`segments`、`dynamic` 等字段。若报 `load_failed`，换 CLI32 重试。

### 2. 修复

```bash
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000
```

`-m` 是 dump 时的内存基地址，十六进制。成功后 `fixed.so` 即为修复产物。

### 3. 校验

```bash
SoFixer-skills-CLI64 verify -s fixed.so
```

看 `summary.passed` / `summary.failed`，全 passed 即结构有效。退出码 `0` 表示全通过。

## 下一步

- [CLI 命令参考](/cli/) — 每个子命令的完整选项与输出字段
- [AI Agent 接入](/ai-agent/) — 让 Claude Code / Codex 自动调用 SoFixer
