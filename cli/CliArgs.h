//===------------------------------------------------------------*- C++ -*-===//
// 极简、零依赖的 argv 解析器（子命令 + --long / -short 风格）。
//
// 为什么不用 getopt/getopt_long？
//   1. MSVC 没有 getopt.h；MinGW 虽有但行为有平台差异。为了让 CLI 在
//      Windows/Linux/macOS 用同一份代码、同一套行为，干脆手写。
//   2. getopt 表达"子命令 + 长选项"风格比较别扭（要为每个子命令切换
//      optstring）。手写解析器能完全控制错误信息格式，对 AI 更友好。
//   3. 整个解析器约 150 行，远小于引入一个跨平台 getopt 替代库的复杂度。
//
// 支持的语法：
//   --source PATH        长选项 + 空格分隔的值
//   --source=PATH        长选项 + 等号内联值
//   -s PATH              短选项 + 空格分隔的值
//   -sPATH               短选项 + 紧贴的值
//   -d / --debug         不带值的布尔 flag
//   位置参数             非选项 token，收集到 positional
//
// 全局选项（任何位置都可出现）：--format json|text、--pretty
//===----------------------------------------------------------------------===//
#ifndef SOFIXER_CLI_ARGS_H
#define SOFIXER_CLI_ARGS_H

#include <string>
#include <vector>
#include <map>
#include "CliConfig.h"

namespace sofixer_cli {

// 描述一个子命令接受的选项。
struct CliOption {
    std::string long_name;   // 长选项名，如 "source"（不含前导 --）
    char        short_name;  // 短选项字母，如 's'；0 表示无短选项
    bool        takes_value; // true=需要值，false=布尔 flag
    bool        required;    // true=必填，缺失则报 missing_required
    std::string help;        // 人类可读说明，用于 help 子命令
};

// 解析结果。
struct ParseResult {
    bool        ok = false;                       // 解析是否成功
    OutputFormat format = OutputFormat::Json;     // --format 的结果
    bool        pretty = false;                   // --pretty
    std::string command;                          // 子命令；无则为 ""
    std::map<std::string, std::string> values;    // long_name -> 值
    std::map<std::string, bool> flags;            // long_name -> true（布尔 flag）
    std::vector<std::string> positional;          // 剩余位置参数
    struct Error {
        std::string code;     // 见 CliConfig.h 的 err:: 命名空间
        std::string message;
        std::string arg;      // 出错的 token，便于定位
    } error;

    // 便捷查询。
    bool has(const std::string& long_name) const {
        return values.count(long_name) > 0 || flags.count(long_name) > 0;
    }
    std::string get(const std::string& long_name) const {
        auto it = values.find(long_name);
        return it == values.end() ? std::string() : it->second;
    }
};

// 解析 argv。第一个非选项 token 视为子命令；其后按 opts 解析。
// 全局选项（--format/--pretty）在任何位置都识别。
ParseResult parseArgs(int argc, char* argv[],
                      const std::vector<CliOption>& opts);

}  // namespace sofixer_cli

#endif  // SOFIXER_CLI_ARGS_H
