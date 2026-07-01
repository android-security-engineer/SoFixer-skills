//===------------------------------------------------------------*- C++ -*-===//
// argv 解析器实现。见 CliArgs.h 的设计说明。
//
// 解析分两个阶段：
//   阶段一：扫描找到第一个非选项 token，作为子命令。在此之前只接受
//           全局选项（--format/--pretty），其它选项视为"子命令前误用"。
//   阶段二：子命令确定后，用对应的 option set 解析剩余 token。
// 为什么要两阶段？因为不同子命令接受不同选项（fix 有 -m，info 没有），
// 必须先知道子命令才能知道哪些选项合法。
//===----------------------------------------------------------------------===//
#include "CliArgs.h"
#include <cstring>

namespace sofixer_cli {

namespace {

// 按长名查找选项定义。线性扫描即可——选项数量很少（≤5），无需哈希。
const CliOption* findLong(const std::vector<CliOption>& opts,
                          const std::string& name) {
    for (const auto& o : opts) {
        if (o.long_name == name) return &o;
    }
    return nullptr;
}

// 按短名查找选项定义。
const CliOption* findShort(const std::vector<CliOption>& opts, char c) {
    for (const auto& o : opts) {
        if (o.short_name == c) return &o;
    }
    return nullptr;
}

// 处理 --format 的值并写回 r.format。失败时填充 r.error 并返回 false。
// 抽成函数是因为 --format 在"子命令前"和"子命令后"两处都要识别，
// 不抽出来会重复三遍同样的校验逻辑。
//   v        : 已取出的格式值（"json" 或 "text" 或非法值）
//   r        : 解析结果，成功时更新 format，失败时更新 error
//   argForErr: 出错时回填到 error.arg 的原始 token，便于定位
bool applyFormat(const std::string& v, ParseResult& r,
                 const std::string& argForErr) {
    if (v == "json") { r.format = OutputFormat::Json; return true; }
    if (v == "text") { r.format = OutputFormat::Text; return true; }
    r.ok = false;
    r.error.code = err::kInvalidValue;
    r.error.message = "invalid --format value (expected json|text)";
    r.error.arg = argForErr.empty() ? v : argForErr;
    return false;
}

}  // namespace

ParseResult parseArgs(int argc, char* argv[],
                      const std::vector<CliOption>& opts) {
    ParseResult r;
    r.ok = true;

    // argv[0] 是程序名，从 1 开始。
    int i = 1;

    // ---- 阶段一：寻找子命令 ----
    // 在遇到第一个非选项 token 之前，只接受全局选项。
    while (i < argc) {
        const char* a = argv[i];

        // --format json|text  或  --format=json|text
        if (std::strcmp(a, "--format") == 0) {
            if (i + 1 < argc) {
                if (!applyFormat(argv[++i], r, a)) return r;
            }
            ++i;
            continue;
        }
        if (std::strncmp(a, "--format=", 9) == 0) {
            if (!applyFormat(a + 9, r, a)) return r;
            ++i;
            continue;
        }
        // --pretty（布尔 flag，无需值）
        if (std::strcmp(a, "--pretty") == 0) {
            r.pretty = true;
            ++i;
            continue;
        }
        if (a[0] == '-') {
            // 子命令之前出现了非全局选项 —— 视为错误，因为此时还不知道
            // 用哪个子命令的 option set，无法判断它是否合法。
            r.ok = false;
            r.error.code = err::kUnknownOption;
            r.error.message = "unexpected option before subcommand";
            r.error.arg = a;
            return r;
        }
        // 第一个非选项 token 即子命令。
        r.command = a;
        ++i;
        break;
    }

    // ---- 阶段二：按子命令的 option set 解析剩余 token ----
    while (i < argc) {
        const char* a = argv[i];

        // 全局选项在任何位置都识别（与阶段一保持一致）。
        if (std::strcmp(a, "--format") == 0) {
            if (i + 1 < argc) {
                if (!applyFormat(argv[++i], r, a)) return r;
            }
            ++i;
            continue;
        }
        if (std::strncmp(a, "--format=", 9) == 0) {
            if (!applyFormat(a + 9, r, a)) return r;
            ++i;
            continue;
        }
        if (std::strcmp(a, "--pretty") == 0) {
            r.pretty = true;
            ++i;
            continue;
        }

        // 长选项：--long 或 --long=value
        if (a[0] == '-' && a[1] == '-') {
            const char* eq = std::strchr(a + 2, '=');
            std::string name;
            std::string inlineVal;
            bool hasInline = false;
            if (eq) {
                // --long=value 形式：等号前是名，等号后是值
                name.assign(a + 2, eq - (a + 2));
                inlineVal = eq + 1;
                hasInline = true;
            } else {
                name = a + 2;
            }
            const CliOption* o = findLong(opts, name);
            if (!o) {
                r.ok = false;
                r.error.code = err::kUnknownOption;
                r.error.message = "unknown option";
                r.error.arg = std::string("--") + name;
                return r;
            }
            if (o->takes_value) {
                if (hasInline) {
                    r.values[o->long_name] = inlineVal;
                } else if (i + 1 < argc) {
                    // --long value 形式：取下一个 token 作为值
                    r.values[o->long_name] = argv[++i];
                } else {
                    r.ok = false;
                    r.error.code = err::kMissingRequired;
                    r.error.message = "option requires a value";
                    r.error.arg = std::string("--") + name;
                    return r;
                }
            } else {
                // 布尔 flag
                r.flags[o->long_name] = true;
            }
            ++i;
            continue;
        }

        // 短选项：-s value | -svalue | -s(flag)
        if (a[0] == '-' && a[1] != '\0') {
            char c = a[1];
            const CliOption* o = findShort(opts, c);
            if (!o) {
                r.ok = false;
                r.error.code = err::kUnknownOption;
                r.error.message = "unknown option";
                r.error.arg = std::string("-") + c;
                return r;
            }
            if (o->takes_value) {
                if (a[2] != '\0') {
                    // -svalue 形式：字母后面紧跟值
                    r.values[o->long_name] = a + 2;
                } else if (i + 1 < argc) {
                    // -s value 形式：取下一个 token
                    r.values[o->long_name] = argv[++i];
                } else {
                    r.ok = false;
                    r.error.code = err::kMissingRequired;
                    r.error.message = "option requires a value";
                    r.error.arg = std::string("-") + c;
                    return r;
                }
            } else {
                r.flags[o->long_name] = true;
            }
            ++i;
            continue;
        }

        // 既非选项、也非全局选项的 token → 位置参数。
        r.positional.push_back(a);
        ++i;
    }

    // ---- 阶段三：校验必填项 ----
    for (const auto& o : opts) {
        if (o.required && !r.has(o.long_name)) {
            r.ok = false;
            r.error.code = err::kMissingRequired;
            r.error.message = "missing required option";
            r.error.arg = "--" + o.long_name;
            return r;
        }
    }

    return r;
}

}  // namespace sofixer_cli
