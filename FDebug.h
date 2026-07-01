//===------------------------------------------------------------*- C++ -*-===//
//
//                     Created by F8LEFT on 2018/7/4.
//                   Copyright (c) 2018. All rights reserved.
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef ANDDBG_ALOG_H
#define ANDDBG_ALOG_H

// ===== 调试日志宏 FLOG* =====
//
// 这些宏把调试信息直接 printf 到 stdout，格式为 [函数名:行号] 消息。
//   FLOGE = Error   FLOGW = Warn   FLOGI = Info
//   FLOGD = Debug   FLOGV = Verbose
// （当前实现下各级别行为一致，只是语义区分，便于将来分级过滤。）
//
// 为什么用编译期宏控制开关，而不是运行时变量？
//   核心代码里 FLOG 调用散布在 ElfReader/ElfRebuilder 等关键路径上，
//   每次调用都要判断运行时开关会引入分支开销；而调试日志在发布构建里
//   应当零开销。用编译期宏（#if）能让编译器在关闭时完全消除这些调用。
//
// SOFIXER_QUIET 宏：
//   当定义了 SOFIXER_QUIET（目前仅 SoFixer-skills-CLI 目标定义），
//   所有 FLOG* 展开为空，从而保证 CLI 的 stdout 只输出干净的结构化 JSON，
//   不被调试信息污染。旧入口 SoFixer32/SoFixer64 不定义该宏，行为不变。
//#if !defined(NDEBUG)
#if !defined(SOFIXER_QUIET)

#define TOSTR(fmt) #fmt
#define FLFMT TOSTR([%s:%d])        // [函数名:行号] 前缀
#define FNLINE TOSTR(\n)            // 换行

#define FLOGE(fmt, ...) printf(FLFMT fmt FNLINE, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FLOGD(fmt, ...) printf(FLFMT fmt FNLINE, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FLOGW(fmt, ...) printf(FLFMT fmt FNLINE, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FLOGI(fmt, ...) printf(FLFMT fmt FNLINE, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define FLOGV(fmt, ...) printf(FLFMT fmt FNLINE, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
// 关闭态：宏展开为空，编译器完全消除调用，零开销。
#define FLOGE(fmt, ...)
#define FLOGD(fmt, ...)
#define FLOGW(fmt, ...)
#define FLOGI(fmt, ...)
#define FLOGV(fmt, ...)
#endif

#endif //ANDDBG_ALOG_H
