# verify — 校验 SO

修复后用它校验产物 SO 的结构是否有效。

## 用法

```bash
SoFixer-skills-CLI64 verify -s <文件>
```

## 选项

| 选项 | 必填 | 说明 |
|---|---|---|
| `-s`, `--source <path>` | ✅ | 待校验的 SO 文件 |

## 输出示例（成功 · 全通过）

```json
{
  "ok": true,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "verify",
  "result": {
    "file": "fixed.so",
    "summary": {
      "passed": 8,
      "failed": 0
    },
    "checks": [
      { "name": "elf_header", "passed": true },
      { "name": "phdr_table", "passed": true },
      { "name": "shdr_table", "passed": true },
      { "name": "shstrtab", "passed": true }
    ]
  }
}
```

## 输出示例（有失败项）

```json
{
  "ok": false,
  "tool": "SoFixer-skills-CLI",
  "arch": 64,
  "command": "verify",
  "error": {
    "code": "verify_failed",
    "message": "2 checks failed",
    "stage": "Verify"
  },
  "summary": {
    "passed": 6,
    "failed": 2
  }
}
```

## 判定逻辑

- `summary.failed == 0` → 结构有效，`ok: true`，退出码 `0`。
- `summary.failed > 0` → 有问题项，`ok: false`，退出码 `1`。

::: tip 何时用 verify
- `fix` 之后，确认修复产物能否被 IDA 正常打开。
- 拿到一个不明来源的 SO，先 verify 看结构是否健康，再决定要不要修。
:::

## 校验项

| check | 检查内容 |
|---|---|
| `elf_header` | ELF 魔数、`e_type`、`e_machine`、`e_shnum`/`e_shoff`/`e_shstrndx` 是否自洽 |
| `phdr_table` | program header 是否合法、PT_LOAD 段是否覆盖内容 |
| `shdr_table` | section header table 是否可解析、`sh_name` 是否落在 `.shstrtab` 范围内 |
| `shstrtab` | `.shstrtab` section 是否存在且可读 |

## 退出码

- `0` — 全部校验通过
- `1` — 有校验项失败
- `2` — 参数错误（缺 `-s`）

## AI 调用建议

`verify` 是 `fix` 的标准收尾。AI 可这样编排：`fix` 成功后自动 `verify`，若 `failed > 0` 则在回复里列出失败项供用户排查；若全通过则提示可用 IDA 打开。
