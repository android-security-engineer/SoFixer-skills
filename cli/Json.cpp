//===------------------------------------------------------------*- C++ -*-===//
// 极简 JSON 写出器实现。见 Json.h 的设计说明。
//===----------------------------------------------------------------------===//
#include "Json.h"
#include <cstdio>
#include <cstring>

namespace sofixer_cli {

// JSON 字符串转义。对照 RFC 8259：必须转义 " 和 \，以及控制字符
// (U+0000~U+001F)。其它可选项（如 '/'）我们不复述，保持输出紧凑。
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);  // 预留一点余量，避免多次 realloc
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);  // 先转 unsigned，避免符号扩展
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;   // backspace
            case '\f': out += "\\f"; break;   // form feed
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    // 控制字符用 \uXXXX 转义
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // 可打印 ASCII / UTF-8 字节原样输出（我们假设输入是 UTF-8）
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

// 加引号 + 转义，得到一个完整的 JSON 字符串字面量。
std::string jsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    out += jsonEscape(s);
    out += '"';
    return out;
}

// 把地址格式化成 "0x..." 字符串字面量（含引号）。
// 32/64 位用不同的 printf 宽数说明符，避免平台警告。
std::string jsonHex(Elf_Addr v) {
    char buf[32];
#ifdef __SO64__
    std::snprintf(buf, sizeof(buf), "\"0x%llx\"", static_cast<unsigned long long>(v));
#else
    std::snprintf(buf, sizeof(buf), "\"0x%lx\"", static_cast<unsigned long>(v));
#endif
    return std::string(buf);
}

// 无符号十进制数字 token（无引号）。
std::string jsonNum(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
    return std::string(buf);
}

// 有符号十进制数字 token。
std::string jsonNum(int64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    return std::string(buf);
}

std::string jsonBool(bool b) {
    return b ? std::string("true") : std::string("false");
}

// ---- JsonObj ----
// add() 是底层入口：key 做转义加引号，value 直接存原始片段。
// 所有 addXxx 便捷方法最终都委托到这里。
JsonObj& JsonObj::add(const std::string& k, const std::string& rawValue) {
    entries_.push_back(std::make_pair(jsonString(k), rawValue));
    return *this;  // 返回 *this 支持链式调用
}

JsonObj& JsonObj::addStr(const std::string& k, const std::string& v) {
    return add(k, jsonString(v));  // value 也要转义加引号
}

JsonObj& JsonObj::addStr(const std::string& k, const char* v) {
    // 防御 nullptr
    return add(k, jsonString(v ? std::string(v) : std::string()));
}

JsonObj& JsonObj::addNum(const std::string& k, uint64_t v) {
    return add(k, jsonNum(v));
}

JsonObj& JsonObj::addNum(const std::string& k, int64_t v) {
    return add(k, jsonNum(v));
}

// plain int 重载：避免调用方传 int 时在 uint64/int64 间产生重载歧义。
JsonObj& JsonObj::addNum(const std::string& k, int v) {
    return add(k, jsonNum(static_cast<int64_t>(v)));
}

JsonObj& JsonObj::addBool(const std::string& k, bool v) {
    return add(k, jsonBool(v));
}

JsonObj& JsonObj::addHex(const std::string& k, Elf_Addr v) {
    return add(k, jsonHex(v));
}

JsonObj& JsonObj::addNull(const std::string& k) {
    return add(k, std::string("null"));
}

// 序列化为紧凑对象。键值保持插入顺序——JSON 对象虽然语义上无序，
// 但保持顺序对人类阅读和 AI 解析都更友好。
std::string JsonObj::str() const {
    std::string out;
    out += '{';
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (i) out += ',';  // 首元素前不加逗号
        out += entries_[i].first;
        out += ':';
        out += entries_[i].second;
    }
    out += '}';
    return out;
}

// 拼装数组。元素必须是已序列化的合法 JSON 片段。
std::string jsonArray(const std::vector<std::string>& elements) {
    std::string out;
    out += '[';
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i) out += ',';
        out += elements[i];
    }
    out += ']';
    return out;
}

}  // namespace sofixer_cli
