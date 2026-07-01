# SoFixer

> 修复从内存中 dump 下来的 Android SO 文件。

[![build](https://img.shields.io/github/actions/workflow/status/android-security-engineer/SoFixer-skills/build_check.yml?branch=master&label=build)](https://github.com/android-security-engineer/SoFixer-skills/actions)
[![release](https://img.shields.io/github/v/tag/android-security-engineer/SoFixer-skills?label=release)](https://github.com/android-security-engineer/SoFixer-skills/releases)
[![license](https://img.shields.io/badge/license-MIT-39d0d8)](LICENSE)

🌐 **[官网 / 文档](https://android-security-engineer.github.io/SoFixer-skills/)** · 一个二进制修复工具

📚 **[工作原理与代码导览](https://android-security-engineer.github.io/SoFixer-skills/guide/how-it-works)** — 想理解修复机制与代码结构，从这里开始。

---

运行中的 SO 被 loader 修改过后，ELF 结构已不再是磁盘上的样子：section header table 丢失、program header 的文件偏移与虚拟地址错位、重定位项指向绝对地址。直接 dump 内存得到的文件，IDA 打开一片混乱。

**SoFixer 还原这三层结构：**

- **RebuildPhdr** — 修正 program header（`p_filesz` 对齐 `p_memsz`，`p_offset` 还原为 `p_vaddr`）
- **RebuildShdr** — 从 dynamic 段推断并重建整张 section header table
- **RebuildRelocs** — 依据 dump 基地址，把重定位项里的绝对地址减回相对地址

---

## SoFixer-skills-CLI（面向 AI）

新增的统一入口，默认输出 **结构化 JSON**，便于 AI agent 与脚本调用。`C++` / 跨平台（Windows · Linux · macOS）/ 32·64 双变体。

```bash
# 修复从内存 dump 的 SO（带基地址）
SoFixer-skills-CLI64 fix -s dump.so -o fixed.so -m 0x7db078b000

# 查看任意 SO 的结构信息
SoFixer-skills-CLI64 info -s libtest.so

# 校验修复后的 SO 是否结构有效
SoFixer-skills-CLI64 verify -s fixed.so

# 工具与构建信息
SoFixer-skills-CLI64 version
```

成功响应（单行 JSON）：

```json
{"ok":true,"tool":"SoFixer-skills-CLI","arch":64,"command":"fix",
 "result":{"source":"dump.so","output":"fixed.so",
           "input_size":1392640,"output_size":1396736,
           "rebuild_steps":["phdr","soinfo","shdr","relocs","fin"]}}
```

| 子命令 | 作用 | 必填 |
|---|---|---|
| `fix` | 修复内存 dump 的 SO | `-s` |
| `info` | 读取 ELF / SO 结构信息 | `-s` |
| `verify` | 校验 SO 结构有效性 | `-s` |
| `version` | 工具与构建信息 | — |
| `help` | 列出命令与选项 schema | — |

**退出码：** `0` 成功 · `1` 业务失败 · `2` 参数错误 · `3` 内部错误。所有错误都以结构化 JSON 返回，含稳定的 `error.code`。详见 `cli/` 目录或运行 `help` 子命令。

> 位宽说明：核心代码用 `__SO64__` 宏区分 32/64 位，一个二进制只能处理对应位宽的 SO。请用 `CLI32` 处理 32 位 SO、`CLI64` 处理 64 位 SO。

---

## 构建

```shell
mkdir build && cd build
# 64 位（处理 64 位 SO，生成 SoFixer64 + SoFixer-skills-CLI64）
cmake -DSO_64=ON ..
# 或 32 位
cmake -DSO_64=OFF ..
cmake --build . -j
```

依赖：CMake ≥ 3.3、C++11 编译器。无第三方库。

---

## 从内存 dump SO（IDA 脚本）

```python
import idaapi
start_address = 0x0000007DB078B000
end_address   = 0x0000007DB08DE000
data_length   = end_address - start_address
fp = open('E:\\path.so', 'wb')
cur = 0
while cur < data_length:
    towrite = min(0x100000, data_length - cur)
    fp.write(idaapi.dbg_read_memory(start_address + cur, towrite))
    cur += towrite
fp.close()
```

拿到 dump 后，用 `fix` 子命令，`-m` 传入上面同一个基地址即可：

```shell
sofixer -s source.so -o fix.so -m 0x7DB078B000 -d
```

旧入口（`SoFixer32` / `SoFixer64`）参数：`-s` 源文件 · `-o` 输出 · `-m` dump 基地址（16 位）· `-b` 原 SO（实验性）· `-d` 调试信息。

---

## 原理

参考 TK 的 SO 修复文章：<http://bbs.pediy.com/thread-191649.htm>

## 已知问题

重定位表解析有几处已知错误，暂未修复；`.got` 与 `.bss` section 的生成代码尚未启用。
