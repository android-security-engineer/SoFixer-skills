# 快速开始

> 完整理解工具请先读 [前置背景知识](./background) 和 [它解决什么问题](./problem)。本页教你**动手跑通**一次修复。

## 整体流程

```
① 拿到工具          ② 从 IDA dump 出 SO（记下基地址）
   SoFixer-skills-CLI        ↓
        ↓                dump.so + 基址 0x7db078b000
   version 验证              ↓
                       ③ fix 修复（传入同一个基地址）
                              ↓
                         fixed.so
                              ↓
                       ④ verify 校验
                              ↓
                       ⑤ IDA 打开分析
```

## 第一步：获取工具

### 方式一：下载预编译包（推荐）

到 [GitHub Releases](https://github.com/android-security-engineer/SoFixer-skills/releases) 下载对应平台的可执行文件：

| 文件名 | 平台 | 处理哪种 SO |
|---|---|---|
| `SoFixer-skills-CLI-Linux-64` | Linux | 64 位 SO |
| `SoFixer-skills-CLI-Linux-32` | Linux | 32 位 SO |
| `SoFixer-skills-CLI-macOS-64` | macOS | 64 位 SO |
| `SoFixer-skills-CLI-Windows-64.exe` | Windows | 64 位 SO |
| `SoFixer-skills-CLI-Windows-32.exe` | Windows | 32 位 SO |

::: tip 位宽怎么选
处理 64 位 SO 用 CLI64，处理 32 位 SO 用 CLI32。不确定的话，先用 `info` 子命令探测，若报 `class_mismatch` 就换另一个变体。Android 上 64 位设备的应用 SO 多为 64 位，32 位老应用或特定 SO 才是 32 位。
:::

### 方式二：从源码构建

```bash
git clone https://github.com/android-security-engineer/SoFixer-skills.git
cd SoFixer-skills
mkdir build && cd build

# 64 位（处理 64 位 SO）
cmake -DSO_64=ON ..
cmake --build . -j

# 或 32 位
cmake -DSO_64=OFF ..
cmake --build . -j
```

依赖：CMake ≥ 3.5、C++11 编译器。无第三方库。构建会同时产出旧的 `SoFixer32/64` 和新的 `SoFixer-skills-CLI32/64`。

## 第二步：验证安装

```bash
SoFixer-skills-CLI64 version
```

应输出一行 JSON：

```json
{"ok":true,"tool":"SoFixer-skills-CLI","version":"1.0.0","arch":64,
 "target":"SoFixer-skills-CLI64","core":"SoFixer v2.1",
 "platform":"linux","compiler":"13.3.0"}
```

看到 `"ok":true` 即安装成功。

## 第三步：从 IDA dump SO

修复的前提是先有一个从内存 dump 出来的 SO。这一步用 IDA Pro 完成。

### 3.1 准备：让 IDA 附加到目标进程

1. 用 USB 连接 Android 设备，启动 `frida-server` 或 `gdbserver`（取决于你的调试方式）。
2. IDA 选择 `Debugger → Attach to process`，连上目标进程。
3. 让程序运行到 SO 已被加载、解密的状态（如果是加壳应用，要等脱壳完成）。

### 3.2 找到 SO 的基地址

在 IDA 的 **Modules 面板**（`View → Open subviews → Modules`）找到目标 SO，能看到它的加载基址，例如 `0x7DB078B000`。

**这个地址就是 dump 基地址，务必记下**——修复时 `fix -m` 要传入同一个值。

### 3.3 执行 dump

在 IDA 的 Python 控制台运行（替换 `start_address` / `end_address` 为你的 SO 基址和结束地址）：

```python
import idaapi
start_address = 0x0000007DB078B000   # SO 基地址（与 Modules 面板一致）
end_address   = 0x0000007DB08DE000   # 基地址 + SO 大小
data_length   = end_address - start_address
fp = open('E:\\dump.so', 'wb')
cur = 0
while cur < data_length:
    towrite = min(0x100000, data_length - cur)  # 每次最多 1MB
    fp.write(idaapi.dbg_read_memory(start_address + cur, towrite))
    cur += towrite
fp.close()
```

::: tip 结束地址怎么定
SO 大小可从 Modules 面板看，或用 `cat /proc/<pid>/maps`（需 root/adb shell）找该 SO 的内存映射行，`结束地址 - 起始地址` 即大小。宁可多 dump 一点，不要少——多出的尾部不影响修复，少了会导致数据不完整。
:::

::: warning 基地址必须和 dump 用的一致
`fix -m` 传的基地址，必须等于上面脚本里的 `start_address`。两者不一致，重定位会修复错误。如果你换次设备/重启进程，基地址会变（ASLR），要重新记录。
:::

现在你有了 `dump.so` 和基地址 `0x7db078b000`。

## 第四步：修复

```bash
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000
```

参数：

- `-s dump.so`：第三步 dump 出来的文件。
- `-o fixed.so`：修复产物输出路径。
- `-m 0x7db078b000`：第三步记下的基地址（十六进制）。

成功后 `fixed.so` 即为修复产物。看输出 JSON 的 `"ok":true` 确认。

## 第五步：校验

```bash
SoFixer-skills-CLI64 verify -s fixed.so
```

看 `summary.passed` / `summary.failed`，全 `passed` 即结构有效，退出码 `0`。

## 第六步：用 IDA 打开

把 `fixed.so` 拖进 IDA Pro。正常情况下：

- 能看到 `.text` / `.dynsym` / `.dynstr` 等 section。
- 函数列表有内容，符号名正常显示。
- 交叉引用基本恢复。

如果仍有问题，检查：dump 是否完整、基地址是否正确、位宽是否选对。

## 三步修复流程（速查）

如果你已经熟悉 dump 流程，核心就三步：

```bash
# 1. 查看结构（确认位宽与架构）
SoFixer-skills-CLI64 info -s dump.so

# 2. 修复（传入 dump 时的内存基地址）
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000

# 3. 校验
SoFixer-skills-CLI64 verify -s fixed.so
```

## 下一步

- [CLI 命令参考](/cli/) — 每个子命令的完整选项与输出字段
- [AI Agent 接入](/ai-agent/) — 让 Claude Code / Codex 自动调用 SoFixer
- [工作原理](./how-it-works) — 深入了解修复的底层机制
