//===------------------------------------------------------------*- C++ -*-===//
// 极简 JSON 写出器（只写不解析）。
//
// 为什么自己写而不引第三方库（如 nlohmann/json）？
//   SoFixer 核心是零依赖的 C++ 项目，CLI 保持同样风格——无第三方依赖，
//   三个平台用同一份代码构建。我们只需要"写出" JSON，不需要解析，所以
//   ~120 行手写就够，没必要引入一个完整 JSON 库。
//
// 设计：流式 builder（JsonObj）而不是完整 DOM。
//   Commands.cpp 需要嵌套输出（result 里有数组、对象），但我们不需要
//   反复修改已构建的树，只需要按顺序拼装再序列化。所以用 vector<pair>
//   存 (key, rawValue) 对，序列化时拼成 {"k":v,...}。比完整 DOM 轻得多。
//
// ⚠️ 关键教学点：64 位地址必须用字符串输出。
//   JSON 规范里的 Number 是 IEEE 754 double，只能精确表示到 2^53。
//   而 64 位虚拟地址可达 2^64，直接当数字输出会丢精度（例如
//   0x7db078b000 在 JS 里会被截断）。所以凡是 Elf_Addr，一律输出成
//   "0x7db078b000" 这样的十六进制字符串。这是面向 AI 输出时的常见坑。
//===----------------------------------------------------------------------===//
#ifndef SOFIXER_CLI_JSON_H
#define SOFIXER_CLI_JSON_H

#include <cstdint>
#include <string>
#include <vector>
#include "macros.h"  // 提供 Elf_Addr 的 typedef（32 或 64 位）

namespace sofixer_cli {

// 对字符串做 JSON 转义：处理 "、\、控制字符等。
std::string jsonEscape(const std::string& s);

// 给字符串加引号并转义：返回 "..." 形式。
std::string jsonString(const std::string& s);

// 把 Elf_Addr 格式化成十六进制字符串字面量，如 "0x7db078b000"。
// 见文件头说明：64 位地址必须用字符串，否则丢精度。
std::string jsonHex(Elf_Addr v);

// 把无符号整数格式化成十进制数字 token（无引号），如 1392640。
std::string jsonNum(uint64_t v);

// 把有符号整数格式化成数字 token。
std::string jsonNum(int64_t v);

// 把布尔值格式化成 true / false token。
std::string jsonBool(bool b);

// 流式 JSON 对象 builder。
// 用法：JsonObj o; o.addStr("k","v").addNum("n", 1); ... o.str() 得到 {"k":"v","n":1}
// 每个 add* 返回 *this，便于链式调用。
class JsonObj {
public:
    JsonObj() = default;

    // 添加一个已序列化的原始 JSON 值（字符串形式）作为某 key 的值。
    // 调用方负责保证 rawValue 是合法 JSON 片段。
    JsonObj& add(const std::string& k, const std::string& rawValue);
    // 标量便捷重载：自动做转义/格式化。
    JsonObj& addStr(const std::string& k, const std::string& v);
    JsonObj& addStr(const std::string& k, const char* v);
    JsonObj& addNum(const std::string& k, uint64_t v);
    JsonObj& addNum(const std::string& k, int64_t v);
    JsonObj& addNum(const std::string& k, int v);  // 消歧：plain int 否则在 uint64/int64 间歧义
    JsonObj& addBool(const std::string& k, bool v);
    JsonObj& addHex(const std::string& k, Elf_Addr v);  // 输出十六进制字符串
    JsonObj& addNull(const std::string& k);

    // 序列化为紧凑 JSON：{"k":v,"k2":v2}。无缩进，便于 AI 逐行解析。
    std::string str() const;

private:
    // 存 (已加引号的 key, 原始 value 片段) 对，保持插入顺序。
    std::vector<std::pair<std::string, std::string>> entries_;
};

// 把若干已序列化的元素字符串拼成一个 JSON 数组 [e1,e2,...]。
std::string jsonArray(const std::vector<std::string>& elements);

}  // namespace sofixer_cli

#endif  // SOFIXER_CLI_JSON_H
