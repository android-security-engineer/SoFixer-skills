//===------------------------------------------------------------*- C++ -*-===//
// SoFixer-skills-CLI — 配置 / 版本 / 错误码 常量集中定义。
//
// 教学要点：
//   1. 把所有"魔法字符串"集中到一个头文件，是为了让 Commands 和 CliMain
//      引用同一份真理（single source of truth），避免字符串散落各处导致
//      改一处漏一处。这对一个面向 AI 的 CLI 尤其重要——错误码是稳定契约，
//      AI agent 会依赖它的字面值做分支判断。
//   2. 全部用 constexpr 而非宏：constexpr 是类型安全的编译期常量，会进入
//      调试符号，且没有宏的副作用（如末尾多分号、作用域泄漏）。
//===----------------------------------------------------------------------===//
#ifndef SOFIXER_CLI_CONFIG_H
#define SOFIXER_CLI_CONFIG_H

#include <string>

namespace sofixer_cli {

// ---------- 工具身份 ----------
// 这些字符串会出现在每条 JSON 响应里，便于 AI 在多工具混用时识别来源。
constexpr const char* kToolName    = "SoFixer-skills-CLI";
constexpr const char* kToolVersion = "1.0.0";
// 核心引擎版本，与旧入口 main.cpp 里的 "SoFixer v2.1" 对齐，便于关联。
constexpr const char* kCoreVersion = "SoFixer v2.1";

// ---------- 位宽变体 ----------
// 为什么需要 32/64 双变体？
//   核心 ElfReader 用 __SO64__ 宏把 Elf_Ehdr/Elf_Phdr/Elf_Addr 等 typedef
//   成 32 位或 64 位结构体（见 macros.h），且 VerifyElfHeader() 会按位宽
//   拒绝错误 class 的 ELF。所以一个二进制只能处理一种位宽的 SO。CLI 也
//   必须编出两个变体：CLI32 处理 32 位 SO，CLI64 处理 64 位 SO。
#ifdef __SO64__
constexpr int         kArch       = 64;
constexpr const char* kTargetName = "SoFixer-skills-CLI64";
constexpr const char* kElfClass   = "ELF64";   // 与 ELFCLASS64 对应
#else
constexpr int         kArch       = 32;
constexpr const char* kTargetName = "SoFixer-skills-CLI32";
constexpr const char* kElfClass   = "ELF32";   // 与 ELFCLASS32 对应
#endif

// ---------- 输出格式 ----------
// 默认 JSON：CLI 主要面向 AI agent，JSON 是机器可解析的稳定格式。
// text 模式留给人类调试，当前实现与 json 一致，后续可扩展为更易读的渲染。
enum class OutputFormat {
    Json,  // 默认，机器友好
    Text,  // 人类友好
};

// ---------- 退出码 ----------
// 规范化退出码，便于 shell/CI/AI 用一个整数判断结果类别。
// 注意：旧入口 main.cpp 失败时 return -1（实际退出码 255），新 CLI 不沿用。
//   0 = 成功
//   1 = 业务失败（加载/重建/校验未通过等，属于"输入或数据问题"）
//   2 = 参数错误（未知命令/选项/缺必填，属于"调用方式问题"）
//   3 = 内部错误（未预期异常，属于"程序 bug"）
// AI 可以据此区分"换一份输入再试"还是"修正调用方式"。
constexpr int kExitOk            = 0;
constexpr int kExitBusinessError = 1;
constexpr int kExitArgError      = 2;
constexpr int kExitInternalError = 3;

// ---------- 稳定错误码 ----------
// 这是 AI 可依赖的机器可读错误码契约：字面值一旦发布就不再更改（只能新增）。
// AI agent 用 error.code 做条件分支，比解析人类可读的 message 可靠得多。
// 命名采用 snake_case 小写，与常见的 REST/CLI 错误码风格一致。
namespace err {
constexpr const char* kUnknownCommand = "unknown_command";   // 未识别的子命令
constexpr const char* kUnknownOption  = "unknown_option";    // 未识别的选项
constexpr const char* kMissingRequired= "missing_required";  // 缺少必填参数
constexpr const char* kInvalidValue   = "invalid_value";     // 参数值非法（如 membase 格式）
constexpr const char* kSourceNotFound = "source_not_found";  // 源文件不存在/不可读
constexpr const char* kClassMismatch  = "class_mismatch";    // SO 位宽与 CLI 变体不符
constexpr const char* kLoadFailed     = "load_failed";       // ElfReader::Load 返回 false
constexpr const char* kRebuildFailed  = "rebuild_failed";    // ElfRebuilder::Rebuild 返回 false
constexpr const char* kWriteFailed    = "write_failed";      // 输出文件不可写
constexpr const char* kNoDynamic      = "no_dynamic";        // 缺少 dynamic 段
constexpr const char* kInternalError  = "internal_error";    // 未预期异常
}  // namespace err

// ---------- 平台检测 ----------
// 用标准预定义宏检测平台：_WIN32 在 MSVC 和 MinGW 下都定义；
// __APPLE__ 在 macOS 下定义；__linux__ 在 Linux 下定义。
// 这三个宏是编译器内建的，零依赖，三平台一致。
inline std::string platformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

}  // namespace sofixer_cli

#endif  // SOFIXER_CLI_CONFIG_H
