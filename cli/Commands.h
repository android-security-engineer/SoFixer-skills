//===------------------------------------------------------------*- C++ -*-===//
// SoFixer-skills-CLI 各子命令的 handler 声明。
//
// 每个子命令的 handler 签名一致：
//   int cmdXxx(const ParseResult& args);
// 职责：执行业务 → 打印结构化响应 → 返回退出码（见 CliConfig.h）。
// 这样 CliMain.cpp 的 dispatch 只是一个简单的 if-else 链，易于扩展。
//
// 每个 handler 都保证向 stdout 输出恰好一行 JSON（成功或失败均同），
// 这是"面向 AI"的核心契约：AI 调用方只需读一行、解析 JSON、看 ok 字段。
//===----------------------------------------------------------------------===//
#ifndef SOFIXER_CLI_COMMANDS_H
#define SOFIXER_CLI_COMMANDS_H

#include "CliArgs.h"

namespace sofixer_cli {

// 各子命令接受的选项集。返回引用指向 static 常量，既用于解析，也用于
// help 子命令自省（输出每个命令的选项 schema）。
const std::vector<CliOption>& fixOptions();
const std::vector<CliOption>& infoOptions();
const std::vector<CliOption>& verifyOptions();
const std::vector<CliOption>& emptyOptions();  // version / help 无选项

// ---- 子命令 handler ----
int cmdFix(const ParseResult& args);     // 修复内存 dump 的 SO
int cmdInfo(const ParseResult& args);    // 查看 ELF/SO 结构
int cmdVerify(const ParseResult& args);  // 校验 SO 结构有效性
int cmdVersion(const ParseResult& args); // 工具/构建信息
int cmdHelp(const ParseResult& args);    // 列出命令与选项 schema

// 统一的错误输出工具：打印结构化错误 JSON 并返回退出码。
// 各 handler 在失败路径上调用它，保证错误响应格式一致。
int emitError(const ParseResult& args, const std::string& command,
              const std::string& code, const std::string& message,
              const std::string& stage = "", int exitCode = kExitBusinessError);

}  // namespace sofixer_cli

#endif  // SOFIXER_CLI_COMMANDS_H
