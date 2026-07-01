//===------------------------------------------------------------*- C++ -*-===//
// 子命令 handler 实现。见 Commands.h 的总体设计。
//
// 本文件是 CLI 与核心 ELF 引擎之间的"编排层"：它不重新实现任何 ELF
// 解析/修复逻辑，只调用 ElfReader / ObElfReader / ElfRebuilder 的公共
// API，把结果包装成结构化 JSON。
//
// 复用关系：
//   fix     → ObElfReader + ElfRebuilder（修复内存 dump 的 SO）
//   info    → ElfReader（基类即可，普通 SO 不需要 dump 修复）
//   verify  → 直接读 ELF header + ElfReader::Load 做一致性检查
//   version → 仅常量，不碰引擎
//   help    → 反射各子命令的 option set，输出 schema
//===----------------------------------------------------------------------===//
#include "Commands.h"
#include "Json.h"
#include "CliConfig.h"
#include "ElfReader.h"
#include "ObElfReader.h"
#include "ElfRebuilder.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sofixer_cli {

// ---------- 选项集定义 ----------
// 每个子命令声明自己接受的选项。字段顺序对应 CliOption 的构造参数：
// {long_name, short_name, takes_value, required, help}
// 用 static 局部常量避免全局初始化顺序问题。

const std::vector<CliOption>& fixOptions() {
    // fix 的选项与旧入口 main.cpp 的 -s/-o/-m/-b/-d 一一对应，
    // 便于已有脚本迁移。--membase 对应旧 -m，是修复重定位的关键参数。
    static const std::vector<CliOption> opts = {
        {"source", 's', true,  true,  "source SO file (dumped from memory)"},
        {"output", 'o', true,  false, "output fixed SO path"},
        {"membase",'m', true,  false, "memory base address the SO was dumped from (hex, e.g. 0x7db078b000)"},
        {"baseso", 'b', true,  false, "original SO file path (experimental, used to get base information)"},
        {"debug",  'd', false, false, "reserved for compatibility (FLOG output is suppressed in CLI)"},
    };
    return opts;
}

const std::vector<CliOption>& infoOptions() {
    static const std::vector<CliOption> opts = {
        {"source", 's', true, true, "SO file to inspect"},
    };
    return opts;
}

const std::vector<CliOption>& verifyOptions() {
    static const std::vector<CliOption> opts = {
        {"source", 's', true, true, "SO file to verify"},
    };
    return opts;
}

const std::vector<CliOption>& emptyOptions() {
    // version / help 不接受业务选项。
    static const std::vector<CliOption> opts;
    return opts;
}

// ---------- 输出工具 ----------

// 把一行 JSON 打到 stdout。无论 text 还是 json 模式，当前都输出单行 JSON
// ——这是面向 AI 的稳定契约。text 模式留作未来扩展更易读的渲染。
static void printOut(const ParseResult& args, const std::string& json) {
    (void)args;  // 当前未区分使用，保留参数以备 text 模式扩展
    std::printf("%s\n", json.c_str());
}

// 统一错误响应。所有 handler 的失败路径都走这里，保证错误 JSON 格式一致：
//   {"ok":false,"tool":...,"arch":...,"command":...,
//    "error":{"code":...,"message":...,"stage":...}}
// stage 标注失败发生在哪个阶段（如 "ElfReader::Load"），便于 AI 定位。
int emitError(const ParseResult& args, const std::string& command,
              const std::string& code, const std::string& message,
              const std::string& stage, int exitCode) {
    JsonObj o;
    o.addBool("ok", false);
    o.addStr("tool", kToolName);
    o.addNum("arch", kArch);
    o.addStr("command", command);
    JsonObj e;
    e.addStr("code", code);
    e.addStr("message", message);
    if (!stage.empty()) e.addStr("stage", stage);
    o.add("error", e.str());
    printOut(args, o.str());
    return exitCode;
}

// ---------- ELF 常量 → 名称映射 ----------
// 这些映射让 info/verify 的输出更可读：除了数值，还给出 ET_DYN / PT_LOAD
// 这样的符号名。返回 nullptr 表示未知，调用方据此决定是否输出 name 字段。
// 参数类型用 Elf32_Half/Elf32_Word 而非 64 位类型，因为这些字段的位宽
// 在 32/64 位 ELF 里是一致的（Half 永远 16 位，Word 永远 32 位）。

// e_type：文件类型。SO 文件通常是 ET_DYN（共享对象）。
static const char* eTypeName(Elf32_Half e_type) {
    switch (e_type) {
        case ET_NONE: return "ET_NONE";
        case ET_REL:  return "ET_REL";   // 可重定位文件（.o）
        case ET_EXEC: return "ET_EXEC";  // 可执行文件
        case ET_DYN:  return "ET_DYN";   // 共享对象（SO）—— SoFixer 的处理对象
        case ET_CORE:return "ET_CORE";   // core dump
        default: return nullptr;
    }
}

// e_machine：目标架构。Android SO 常见 EM_ARM(32) / EM_AARCH64(64)。
// 注意：本仓库自带的 elf.h 没有定义 EM_AARCH64，所以这里硬编码 183。
static const char* machineName(Elf32_Half machine) {
    switch (machine) {
        case EM_386:   return "EM_386";
        case EM_ARM:   return "EM_ARM";    // 40，32 位 ARM
        case EM_X86_64:return "EM_X86_64"; // 62
        case EM_MIPS:  return "EM_MIPS";
        case 183:      return "EM_AARCH64";  // 64 位 ARM，elf.h 未定义
        default: return nullptr;
    }
}

// p_type：program header 类型。
static const char* ptypeName(Elf32_Word p_type) {
    switch (p_type) {
        case PT_NULL:    return "PT_NULL";
        case PT_LOAD:    return "PT_LOAD";     // 需加载到内存的段
        case PT_DYNAMIC: return "PT_DYNAMIC";  // 指向 .dynamic 段
        case PT_INTERP:  return "PT_INTERP";
        case PT_NOTE:    return "PT_NOTE";
        case PT_SHLIB:   return "PT_SHLIB";
        case PT_PHDR:    return "PT_PHDR";     // 指向 phdr 自身
        case PT_TLS:     return "PT_TLS";
        default:
            // PT_ARM_EXIDX 是平台相关段（0x70000001），用单独判断。
            if (p_type == PT_ARM_EXIDX) return "PT_ARM_EXIDX";
            return nullptr;
    }
}

// 把段权限标志格式化成 "R"/"RW"/"RWX" 等可读字符串。
static std::string pflagsStr(Elf32_Word flags) {
    std::string s;
    if (flags & PF_R) s += 'R';
    if (flags & PF_W) s += 'W';
    if (flags & PF_X) s += 'X';
    if (s.empty()) s = "---";
    return s;
}

// ---------- fix ----------

// 解析 --membase 字符串为 Elf_Addr。
// 接受 "0x..." 十六进制、纯十六进制（含 a-f 字母）、或纯十进制。
// 这个启发式与旧 main.cpp 的 is16Bit lambda 保持一致，便于兼容旧脚本。
static Elf_Addr parseMemBase(const std::string& s, bool& ok) {
    ok = true;
    if (s.empty()) return 0;
    int base = 10;
    const char* str = s.c_str();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;  // 显式 0x 前缀
    } else {
        // 启发式：出现任何 a-f/A-F 字母就当作十六进制
        bool hasHex = false;
        for (char c : s) {
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                hasHex = true;
                break;
            }
        }
        if (hasHex) base = 16;
    }
#ifdef __SO64__
    return strtoull(str, nullptr, base);
#else
    return strtoul(str, nullptr, base);
#endif
}

// fix 子命令：修复一个从内存 dump 下来的 SO。
// 流程对应旧 main.cpp 的 main_loop，但用结构化 JSON 输出结果与错误。
int cmdFix(const ParseResult& args) {
    std::string source    = args.get("source");
    std::string output    = args.get("output");
    std::string membaseStr= args.get("membase");
    std::string baseso    = args.get("baseso");

    // 1. 先探测源文件可读，给出比 ElfReader 更明确的错误码。
    FILE* probe = std::fopen(source.c_str(), "rb");
    if (!probe) {
        return emitError(args, "fix", err::kSourceNotFound,
                         "source so file cannot be opened", "open_source",
                         kExitBusinessError);
    }
    std::fclose(probe);

    // 2. 解析 membase（可选）。未提供时为 0，RebuildRelocs 会跳过重定位修复。
    bool memOk = true;
    Elf_Addr membase = 0;
    if (!membaseStr.empty()) {
        membase = parseMemBase(membaseStr, memOk);
        if (!memOk) {
            return emitError(args, "fix", err::kInvalidValue,
                             "invalid --membase value", "parse_membase",
                             kExitArgError);
        }
    }

    // 3. 装载 SO。ObElfReader 继承 ElfReader，额外处理 dump SO 的 phdr 修正
    //    与从 base so 加载 dynamic 段的实验性功能。
    ObElfReader elf_reader;
    if (!elf_reader.setSource(source.c_str())) {
        return emitError(args, "fix", err::kSourceNotFound,
                         "unable to open source file", "open_source");
    }
    if (!baseso.empty()) {
        elf_reader.setBaseSoName(baseso.c_str());
    }
    elf_reader.setDumpSoBaseAddr(membase);

    // 4. Load() 内部完成：读 ehdr → 校验 → 读 phdr → 预留地址空间 →
    //    加载段到内存 → 定位 loaded_phdr。
    if (!elf_reader.Load()) {
        return emitError(args, "fix", err::kLoadFailed,
                         "source so file is invalid", "ElfReader::Load");
    }

    // 5. 重建 ELF 结构：RebuildPhdr → ReadSoInfo → RebuildShdr →
    //    RebuildRelocs → RebuildFin。产物在 rebuilder 的内存里。
    //    注意：getRebuildData() 返回的内存由 rebuilder 析构释放，
    //    所以写文件必须在 rebuilder 生命周期内完成。
    ElfRebuilder rebuilder(&elf_reader);
    if (!rebuilder.Rebuild()) {
        return emitError(args, "fix", err::kRebuildFailed,
                         "error occurred in rebuilding elf file", "ElfRebuilder::Rebuild");
    }

    size_t outSize = rebuilder.getRebuildSize();

    // 6. 取输入文件大小（用于结果汇报）。ElfReader 的 file_size 不直接
    //    暴露，这里用 fseek/ftell 自行获取。
    long inSize = 0;
    {
        FILE* f = std::fopen(source.c_str(), "rb");
        if (f) {
            std::fseek(f, 0, SEEK_END);
            inSize = std::ftell(f);
            std::fclose(f);
        }
    }

    // 7. 写出修复后的 SO（若指定了 --output）。
    if (!output.empty()) {
        FILE* file = std::fopen(output.c_str(), "wb+");
        if (!file) {
            return emitError(args, "fix", err::kWriteFailed,
                             "output so file cannot be written", "write_output");
        }
        std::fwrite(rebuilder.getRebuildData(), 1, outSize, file);
        std::fclose(file);
    }

    // 8. 输出结果 JSON。
    JsonObj r;
    r.addStr("source", source);
    if (output.empty()) r.addNull("output");
    else r.addStr("output", output);
    if (membaseStr.empty()) r.addNull("membase");
    else r.addStr("membase", membaseStr);  // 原样回显，便于核对
    if (baseso.empty()) r.addNull("baseso");
    else r.addStr("baseso", baseso);
    r.addNum("input_size", static_cast<uint64_t>(inSize > 0 ? inSize : 0));
    r.addNum("output_size", static_cast<uint64_t>(outSize));
    {
        // rebuild_steps 反映 Rebuild() 内部的 5 个阶段，便于 AI 理解流程。
        std::vector<std::string> steps = {
            jsonString("phdr"), jsonString("soinfo"),
            jsonString("shdr"), jsonString("relocs"), jsonString("fin")
        };
        r.add("rebuild_steps", jsonArray(steps));
    }
    r.add("warnings", jsonArray({}));  // 预留：未来可填充非致命告警

    JsonObj o;
    o.addBool("ok", true);
    o.addStr("tool", kToolName);
    o.addNum("arch", kArch);
    o.addStr("command", "fix");
    o.add("result", r.str());
    printOut(args, o.str());
    return kExitOk;
}

// ---------- info ----------

// 把一个 program header 序列化成 JSON 对象字符串。
static std::string segmentJson(const Elf_Phdr& ph, int idx) {
    JsonObj s;
    s.addNum("index", static_cast<int64_t>(idx));
    // type 同时给数值和符号名（如 PT_LOAD），未知则只给数值。
    const char* tn = ptypeName(ph.p_type);
    if (tn) {
        JsonObj t; t.addNum("value", static_cast<uint64_t>(ph.p_type)); t.addStr("name", tn);
        s.add("type", t.str());
    } else {
        JsonObj t; t.addNum("value", static_cast<uint64_t>(ph.p_type)); s.add("type", t.str());
    }
    s.addStr("flags", pflagsStr(ph.p_flags));
    s.addHex("offset", ph.p_offset);
    s.addHex("vaddr",  ph.p_vaddr);
    s.addHex("paddr",  ph.p_paddr);
    s.addHex("filesz", ph.p_filesz);
    s.addHex("memsz",  ph.p_memsz);
    s.addHex("align",  ph.p_align);
    return s.str();
}

// info 子命令：读取并输出 ELF/SO 的结构信息。
// 用基类 ElfReader（普通 SO 不需要 dump 修复），Load 后从公共访问器取数据。
int cmdInfo(const ParseResult& args) {
    std::string source = args.get("source");

    FILE* probe = std::fopen(source.c_str(), "rb");
    if (!probe) {
        return emitError(args, "info", err::kSourceNotFound,
                         "source file cannot be opened", "open_source");
    }
    std::fclose(probe);

    ElfReader elf_reader;
    if (!elf_reader.setSource(source.c_str())) {
        return emitError(args, "info", err::kSourceNotFound,
                         "unable to open source file", "open_source");
    }
    // Load 失败最常见的原因是位宽不符（32 位 CLI 读 64 位 SO）。
    // VerifyElfHeader 会按 __SO64__ 拒绝错误 class，这里在 message 里提示。
    if (!elf_reader.Load()) {
        return emitError(args, "info", err::kLoadFailed,
                         "failed to load ELF (check class matches CLI arch: " +
                         std::string(kElfClass) + ")", "ElfReader::Load");
    }

    const Elf_Ehdr* ehdr = elf_reader.record_ehdr();

    // 取文件大小用于汇报。
    long file_size = 0;
    {
        FILE* f = std::fopen(source.c_str(), "rb");
        if (f) {
            std::fseek(f, 0, SEEK_END);
            file_size = std::ftell(f);
            std::fclose(f);
        }
    }

    JsonObj r;
    r.addStr("path", source);
    r.addNum("file_size", static_cast<uint64_t>(file_size > 0 ? file_size : 0));
    r.addStr("elf_class", kElfClass);   // 由 __SO64__ 决定，即 CLI 自身位宽
    r.addStr("endian", "little");       // SoFixer 只支持小端（VerifyElfHeader 校验）

    // e_type / machine 同时给数值与符号名。
    {
        JsonObj t; t.addNum("value", static_cast<uint64_t>(ehdr->e_type));
        const char* tn = eTypeName(ehdr->e_type);
        if (tn) t.addStr("name", tn);
        r.add("e_type", t.str());
    }
    {
        JsonObj m; m.addNum("value", static_cast<uint64_t>(ehdr->e_machine));
        const char* mn = machineName(ehdr->e_machine);
        if (mn) m.addStr("name", mn);
        r.add("machine", m.str());
    }
    r.addHex("entry", ehdr->e_entry);
    r.addNum("e_phnum",    static_cast<uint64_t>(ehdr->e_phnum));
    r.addNum("e_shnum",    static_cast<uint64_t>(ehdr->e_shnum));
    r.addNum("e_shstrndx", static_cast<uint64_t>(ehdr->e_shstrndx));

    // 段表（program headers）。loaded_phdr() 返回加载到内存后的 phdr。
    {
        std::vector<std::string> segs;
        const Elf_Phdr* ph = elf_reader.loaded_phdr();
        for (size_t i = 0; i < elf_reader.phdr_count(); ++i) {
            segs.push_back(segmentJson(ph[i], static_cast<int>(i)));
        }
        r.add("segments", jsonArray(segs));
    }

    // 动态段。ElfReader::GetDynamicSection 是 protected，所以用自由函数
    // phdr_table_get_dynamic_section（声明在 ElfReader.h，是 public 的）。
    Elf_Dyn* dyn = nullptr;
    size_t dynCount = 0;
    Elf_Word dynFlags = 0;
    phdr_table_get_dynamic_section(elf_reader.loaded_phdr(),
                                   static_cast<int>(elf_reader.phdr_count()),
                                   elf_reader.load_bias(),
                                   &dyn, &dynCount, &dynFlags);

    JsonObj d;
    d.addBool("present", dyn != nullptr);
    d.addNum("count", static_cast<uint64_t>(dynCount));
    if (dyn) {
        // 遍历到 DT_NULL 终止项（dynamic 段约定以 d_tag==DT_NULL 结尾）。
        // 注意：DT_NEEDED/DT_SONAME 的 d_val 是字符串表偏移，不是库名本身。
        // 解析成可读库名需要读 strtab，当前 info 未加载 strtab 内容，
        // 因此这里输出的 needed 是偏移值。这是已知的简化，便于 AI 至少
        // 能看到依赖项的数量与位置。
        std::vector<std::string> needed;
        std::string soname;
        size_t counted = 0;
        for (Elf_Dyn* p = dyn; p->d_tag != DT_NULL; ++p) {
            ++counted;
            if (p->d_tag == DT_NEEDED) {
                needed.push_back(jsonNum(static_cast<uint64_t>(p->d_un.d_val)));
            }
            if (p->d_tag == DT_SONAME) {
                soname = std::to_string(p->d_un.d_val);
            }
        }
        d.addNum("entries_scanned", static_cast<uint64_t>(counted));
        d.add("needed", jsonArray(needed));
        if (soname.empty()) d.addNull("soname");
        else d.addStr("soname", soname);  // 同理：偏移值，非名字
    }
    r.add("dynamic", d.str());

    JsonObj o;
    o.addBool("ok", true);
    o.addStr("tool", kToolName);
    o.addNum("arch", kArch);
    o.addStr("command", "info");
    o.add("result", r.str());
    printOut(args, o.str());
    return kExitOk;
}

// ---------- verify ----------

// verify 子命令：对修复后的 SO 做静态结构校验。
// 这是"尽力而为"的静态检查——不调用真正的 linker 加载，而是核对
// ELF 结构的关键不变式。每项检查产出一个 {name, pass, detail?} 条目。
int cmdVerify(const ParseResult& args) {
    std::string source = args.get("source");

    // 直接读 ELF header 做前几项检查（不需要完整 Load）。
    FILE* probe = std::fopen(source.c_str(), "rb");
    if (!probe) {
        return emitError(args, "verify", err::kSourceNotFound,
                         "source file cannot be opened", "open_source");
    }
    Elf_Ehdr ehdr;
    size_t got = std::fread(&ehdr, 1, sizeof(ehdr), probe);
    std::fclose(probe);
    if (got != sizeof(ehdr)) {
        return emitError(args, "verify", err::kLoadFailed,
                         "file too small to contain an ELF header", "read_ehdr");
    }

    // checks 收集每项检查的 JSON；passed/failed 实时累计。
    std::vector<std::pair<std::string, std::string>> checks;  // name -> raw obj
    int passed = 0, failed = 0;
    auto addCheck = [&](const std::string& name, bool pass, const std::string& detail = "") {
        JsonObj c;
        c.addStr("name", name);
        c.addBool("pass", pass);
        if (!detail.empty()) c.addStr("detail", detail);
        checks.push_back(std::make_pair(name, c.str()));
        if (pass) ++passed; else ++failed;
    };

    // 1. ELF 魔数：必须是 0x7f 'E' 'L' 'F'。
    bool magicOk = ehdr.e_ident[EI_MAG0] == ELFMAG0 &&
                   ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
                   ehdr.e_ident[EI_MAG2] == ELFMAG2 &&
                   ehdr.e_ident[EI_MAG3] == ELFMAG3;
    addCheck("elf_magic", magicOk);

    // 2. 位宽与 CLI 变体一致（32 位 CLI 只能校验 32 位 SO）。
    bool classOk = false;
#ifdef __SO64__
    classOk = ehdr.e_ident[EI_CLASS] == ELFCLASS64;
#else
    classOk = ehdr.e_ident[EI_CLASS] == ELFCLASS32;
#endif
    addCheck("elf_class", classOk, std::string("cli=") + kElfClass);

    // 3. e_version == EV_CURRENT。
    bool verOk = ehdr.e_version == EV_CURRENT;
    addCheck("elf_version", verOk);

    // 4-7 需要完整 Load 才能检查 phdr/dynamic。若前三项已失败则跳过，
    //    避免 ElfReader::Load 报出误导性错误。
    bool loaded = false;
    ElfReader elf_reader;
    if (classOk && magicOk && verOk) {
        if (elf_reader.setSource(source.c_str()) && elf_reader.Load()) {
            loaded = true;
        }
    }
    if (!loaded) {
        addCheck("has_pt_load", false, "load failed");
        addCheck("has_dynamic", false, "load failed");
        addCheck("dynamic_terminated", false, "load failed");
        addCheck("shdr_table", false, "load failed");
    } else {
        // 4. 至少一个 PT_LOAD 段（可加载段）。
        const Elf_Phdr* ph = elf_reader.loaded_phdr();
        size_t phnum = elf_reader.phdr_count();
        bool hasLoad = false;
        for (size_t i = 0; i < phnum; ++i) {
            if (ph[i].p_type == PT_LOAD) { hasLoad = true; break; }
        }
        addCheck("has_pt_load", hasLoad);

        // 5. 存在 PT_DYNAMIC 段且非空。
        Elf_Dyn* dyn = nullptr;
        size_t dynCount = 0;
        Elf_Word dynFlags = 0;
        phdr_table_get_dynamic_section(elf_reader.loaded_phdr(),
                                       static_cast<int>(elf_reader.phdr_count()),
                                       elf_reader.load_bias(),
                                       &dyn, &dynCount, &dynFlags);
        bool hasDyn = dyn != nullptr && dynCount > 0;
        addCheck("has_dynamic", hasDyn,
                 hasDyn ? ("count=" + std::to_string(dynCount)) : "no PT_DYNAMIC");

        // 6. dynamic 段以 DT_NULL 终止（ReadSoInfo 的循环依赖此约定）。
        bool termOk = false;
        if (hasDyn) {
            for (size_t i = 0; i < dynCount; ++i) {
                if (dyn[i].d_tag == DT_NULL) { termOk = true; break; }
            }
        }
        addCheck("dynamic_terminated", termOk);

        // 7. section header table 基本健全。修复后的 SO 应该有 section。
        bool shdrOk = true;
        std::string shdrDetail;
        if (ehdr.e_shnum == 0) {
            shdrOk = false;
            shdrDetail = "e_shnum=0 (no section headers)";
        } else if (ehdr.e_shoff == 0) {
            shdrOk = false;
            shdrDetail = "e_shoff=0 but e_shnum>0";
        } else if (ehdr.e_shstrndx != 0 && ehdr.e_shstrndx >= ehdr.e_shnum) {
            shdrOk = false;
            shdrDetail = "e_shstrndx out of range";
        }
        if (shdrDetail.empty()) shdrDetail = "shnum=" + std::to_string(ehdr.e_shnum);
        addCheck("shdr_table", shdrOk, shdrDetail);
    }

    // 汇总。把 checks 数组与 summary 一起输出。
    std::vector<std::string> checkArr;
    for (const auto& kv : checks) checkArr.push_back(kv.second);

    JsonObj r;
    r.addStr("source", source);
    JsonObj summary;
    summary.addNum("total",  static_cast<int64_t>(checks.size()));
    summary.addNum("passed", static_cast<int64_t>(passed));
    summary.addNum("failed", static_cast<int64_t>(failed));
    r.add("summary", summary.str());
    r.add("checks", jsonArray(checkArr));

    JsonObj o;
    o.addBool("ok", true);
    o.addStr("tool", kToolName);
    o.addNum("arch", kArch);
    o.addStr("command", "verify");
    o.add("result", r.str());
    printOut(args, o.str());

    // verify 的 ok=true 表示"校验过程本身成功完成"；是否有检查失败看
    // summary.failed。退出码则反映是否全通过，便于 AI/脚本一句话判断。
    return failed == 0 ? kExitOk : kExitBusinessError;
}

// ---------- version ----------

// version 子命令：输出工具与构建信息，便于 AI 识别运行环境。
int cmdVersion(const ParseResult& args) {
    JsonObj r;
    r.addStr("tool", kToolName);
    r.addStr("version", kToolVersion);
    r.addNum("arch", kArch);            // 32 或 64
    r.addStr("target", kTargetName);    // 完整可执行名
    r.addStr("core", kCoreVersion);     // 核心引擎版本
    r.addStr("platform", platformName());
#if defined(__VERSION__)
    // __VERSION__ 是 GCC/Clang 内建宏，给出编译器版本字符串。
    r.addStr("compiler", __VERSION__);
#else
    r.addStr("compiler", "unknown");
#endif

    JsonObj o;
    o.addBool("ok", true);
    o.addStr("tool", kToolName);
    o.addNum("arch", kArch);
    o.addStr("command", "version");
    o.add("result", r.str());
    printOut(args, o.str());
    return kExitOk;
}

// ---------- help ----------

// 把单个 CliOption 序列化成 JSON schema，便于 AI 自省 CLI 能力。
static std::string optionSchema(const CliOption& o) {
    JsonObj j;
    j.addStr("long", o.long_name);
    if (o.short_name) {
        JsonObj s; s.addStr("short", std::string(1, o.short_name));
        j.add("short", s.str());
    } else {
        j.addNull("short");
    }
    j.addBool("takes_value", o.takes_value);
    j.addBool("required", o.required);
    j.addStr("help", o.help);
    return j.str();
}

// 把一个子命令序列化成 JSON schema。
static std::string commandSchema(const std::string& name,
                                 const std::string& desc,
                                 const std::vector<CliOption>& opts) {
    JsonObj c;
    c.addStr("name", name);
    c.addStr("description", desc);
    std::vector<std::string> os;
    for (const auto& o : opts) os.push_back(optionSchema(o));
    c.add("options", jsonArray(os));
    return c.str();
}

// help 子命令：列出所有命令及其选项 schema。
// 设计意图：AI agent 可以先调 help 拿到完整 schema，再决定如何调用，
// 无需查阅外部文档。这是"面向 AI"的关键自省能力。
int cmdHelp(const ParseResult& args) {
    JsonObj r;
    r.addStr("tool", kToolName);
    r.addStr("version", kToolVersion);
    std::vector<std::string> cmds = {
        commandSchema("fix",    "fix a memory-dumped SO file",              fixOptions()),
        commandSchema("info",   "inspect ELF/SO structure",                 infoOptions()),
        commandSchema("verify", "verify structural validity of a fixed SO", verifyOptions()),
        commandSchema("version","print tool and build info",                emptyOptions()),
        commandSchema("help",   "print this help",                          emptyOptions()),
    };
    r.add("commands", jsonArray(cmds));

    JsonObj o;
    o.addBool("ok", true);
    o.addStr("tool", kToolName);
    o.addNum("arch", kArch);
    o.addStr("command", "help");
    o.add("result", r.str());
    printOut(args, o.str());
    return kExitOk;
}

}  // namespace sofixer_cli
