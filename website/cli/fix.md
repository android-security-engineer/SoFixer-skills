# fix — 修复 SO

把从内存 dump 的 SO 文件修复成可被 IDA 分析的合法 ELF。

## 用法

```bash
SoFixer-skills-CLI64 fix -s <源文件> -o <输出文件> -m <dump基地址>
```

## 选项

| 选项 | 必填 | 说明 |
|---|---|---|
| `-s`, `--source <path>` | ✅ | 待修复的 dump SO 文件 |
| `-o`, `--output <path>` | ✅ | 修复产物输出路径 |
| `-m`, `--membase <hex>` | ✅ | dump 时的内存基地址（十六进制，如 `0x7db078b000`） |

::: tip 为什么 fix 必须给 -m
dump 出来的 SO，其重定位项里是**进程内的绝对地址**（已被 linker 修正）。只有知道 dump 时的基地址，才能把这些绝对地址减回 SO 内的相对地址。若 `--membase` 缺失或为 0，`RebuildRelocs` 阶段会被跳过——结构能修，但重定位项保持绝对地址。详见 [工作原理 · RebuildRelocs](/guide/how-it-works#阶段四-rebuildrelocs-修复重定位)。
:::

## 输出示例（成功）

```json
{
  "ok": true,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "fix",
  "result": {
    "source": "dump.so",
    "output": "fixed.so",
    "membase": "0x7db078b000",
    "sections_rebuilt": 14,
    "relocations_fixed": 1234,
    "load_bias": "0x7db078b000",
    "load_size": 524288
  }
}
```

## 输出示例（失败）

```json
{
  "ok": false,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "fix",
  "error": {
    "code": "rebuild_failed",
    "message": "rebuild failed",
    "stage": "ElfRebuilder::Rebuild"
  }
}
```

## 常见错误码

| code | 触发场景 | 排查 |
|---|---|---|
| `load_failed` | 源文件不是合法 ELF，或位宽不符 | 先 `info -s` 确认；若 `class_mismatch` 换 CLI32/64 |
| `open_output_failed` | 输出路径不可写 | 检查目录权限/路径 |
| `rebuild_failed` | RebuildPhdr/Shdr/Relocs/Fin 任一阶段失败 | 看 `error.stage` 定位；常见是 dynamic 段损坏 |
| `membase_invalid` | `-m` 不是合法十六进制 | 加 `0x` 前缀，纯数字 |

## 退出码

- `0` — 修复成功
- `1` — 修复失败（见 `error.code`）
- `2` — 参数错误（缺 `-s` / `-o` / `-m`）
- `3` — 内部错误

## AI 调用建议

1. 修复前先 `info -s dump.so` 探测位宽与架构，确认用 CLI32 还是 CLI64。
2. `-m` 的值要和 dump SO 时所用脚本里的基地址一致（IDA dump 脚本通常会打印）。
3. 修复后用 `verify -s fixed.so` 确认结构有效，再用 IDA 打开。
