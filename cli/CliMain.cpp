//===------------------------------------------------------------*- C++ -*-===//
// SoFixer-skills-CLI — 入口与子命令调度。
//
// 这是一个面向 AI 的统一 CLI 入口。所有输出默认为结构化 JSON（单行），
// 便于 AI agent 逐行解析。子命令风格类似 git/gh：
//
//   SoFixer-skills-CLI [全局选项] <子命令> [子命令选项] [参数]
//
// 子命令：
//   fix      修复从内存 dump 的 SO
//   info     查看 ELF/SO 结构
//   verify   校验修复后 SO 的结构有效性
//   version  工具/构建信息
//   help     列出命令与选项 schema
//
// 调度策略：先识别子命令以选定 option set，再交给 parseArgs 解析。
// 这与 CliArgs.cpp 的两阶段解析呼应——必须先知道子命令，才能知道哪些
// 选项合法。
//===----------------------------------------------------------------------===//
#include "CliArgs.h"
#include "Commands.h"
#include "CliConfig.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace sofixer_cli {

namespace {

// 根据子命令名返回它接受的选项集。
// 返回 nullptr 表示未识别的子命令（调用方据此报 unknown_command）。
const std::vector<CliOption>* optionsFor(const std::string& cmd) {
    if (cmd == "fix")    return &fixOptions();
    if (cmd == "info")   return &infoOptions();
    if (cmd == "verify") return &verifyOptions();
    if (cmd == "version" || cmd == "help") return &emptyOptions();
    return nullptr;
}

}  // namespace
}  // namespace sofixer_cli

int main(int argc, char* argv[]) {
    using namespace sofixer_cli;

    // 无参数 → 直接输出 help（最常见的"我该怎么用"入口）。
    if (argc < 2) {
        ParseResult ph;
        ph.format = OutputFormat::Json;
        return cmdHelp(ph);
    }

    // 顶层 --help / -h / --version：在任何子命令之前识别。
    std::string first = argv[1];
    if (first == "--help" || first == "-h") {
        ParseResult ph;
        ph.format = OutputFormat::Json;
        return cmdHelp(ph);
    }
    if (first == "--version") {
        ParseResult ph;
        ph.format = OutputFormat::Json;
        return cmdVersion(ph);
    }

    // 找到第一个非选项 token 作为子命令（跳过可能的全局选项）。
    std::string cmd;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') { cmd = argv[i]; break; }
    }

    // 选定 option set；未识别则报错。
    const std::vector<CliOption>* opts = optionsFor(cmd);
    if (!opts) {
        ParseResult pe;
        pe.format = OutputFormat::Json;
        if (cmd.empty()) {
            return emitError(pe, "", err::kUnknownCommand,
                             "no subcommand given", "dispatch", kExitArgError);
        }
        return emitError(pe, cmd, err::kUnknownCommand,
                         "unknown subcommand: " + cmd, "dispatch", kExitArgError);
    }

    // 完整解析 argv（含全局选项与子命令选项）。
    ParseResult r = parseArgs(argc, argv, *opts);
    if (!r.ok) {
        ParseResult pe;
        pe.format = r.format;  // 即使解析失败也尊重用户指定的 --format
        return emitError(pe, cmd, r.error.code, r.error.message + ": " + r.error.arg,
                         "parse", kExitArgError);
    }

    // 分派到对应 handler。
    if (cmd == "fix")          return cmdFix(r);
    else if (cmd == "info")    return cmdInfo(r);
    else if (cmd == "verify")  return cmdVerify(r);
    else if (cmd == "version") return cmdVersion(r);
    else if (cmd == "help")    return cmdHelp(r);

    // 理论不可达（optionsFor 已过滤），兜底返回内部错误。
    return kExitInternalError;
}
