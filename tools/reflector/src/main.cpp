// tools/reflector/src/main.cpp
//
// forge-reflect — build-time header parser and .gen.cpp emitter.
//
// Usage:
//   forge-reflect
//       --compile-db  <path/to/compile_commands.json>
//       --headers     <h1.h> [h2.h ...]
//       --output-dir  <path/to/build/generated>
//       [--strict]
//
// Annotation detection works via a prefix header injected as -include
// that redefines the empty FORGE_REFLECT_* macros into libclang-visible
// markers (sentinel static member + clang::annotate attributes).

#include <clang-c/Index.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using json   = nlohmann::json;


// ================================================================
//  §1  CXStr — RAII wrapper for CXString
// ================================================================

struct CXStr {
    CXString raw;

    explicit CXStr(CXString s) : raw(s) {}
    ~CXStr() { clang_disposeString(raw); }

    CXStr(const CXStr&)            = delete;
    CXStr& operator=(const CXStr&) = delete;

    const char* c_str() const {
        const char* p = clang_getCString(raw);
        return p ? p : "";
    }
    std::string str() const { return c_str(); }
};


// ================================================================
//  §2  CLI
// ================================================================

struct Config {
    fs::path              compileDb;
    std::vector<fs::path> headers;
    fs::path              outputDir;
    bool                  strictMode = false;
};

static void printUsage(const char* prog) {
    std::cerr
        << "Usage: " << prog << "\n"
        << "  --compile-db <path>      compile_commands.json from CMake\n"
        << "  --headers <h> [h2...]    headers to process\n"
        << "  --output-dir <dir>       destination for .gen.cpp files\n"
        << "  --strict                 any error is fatal (default: lax)\n";
}

static std::optional<Config> parseArgs(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if      (a == "--compile-db" && i+1 < argc)  cfg.compileDb  = argv[++i];
        else if (a == "--output-dir" && i+1 < argc)  cfg.outputDir  = argv[++i];
        else if (a == "--strict")                     cfg.strictMode = true;
        else if (a == "--headers") {
            while (i+1 < argc && argv[i+1][0] != '-')
                cfg.headers.emplace_back(argv[++i]);
        } else {
            std::cerr << "[forge-reflect] Unknown argument: " << a << "\n";
            return std::nullopt;
        }
    }
    if (cfg.compileDb.empty() || cfg.headers.empty() || cfg.outputDir.empty()) {
        std::cerr << "[forge-reflect] Missing required arguments.\n";
        return std::nullopt;
    }
    return cfg;
}


// ================================================================
//  §3  Compile database — extract -I and -D flags
// ================================================================

static std::vector<std::string> extractCompileFlags(const fs::path& dbPath) {
    std::vector<std::string> flags;

    std::ifstream f(dbPath);
    if (!f) {
        std::cerr << "[forge-reflect] Warning: could not open compile DB: " << dbPath << "\n";
        return flags;
    }
    json db;
    try { db = json::parse(f); }
    catch (const json::exception& e) {
        std::cerr << "[forge-reflect] Warning: compile DB parse failed: " << e.what() << "\n";
        return flags;
    }
    if (!db.is_array() || db.empty()) return flags;

    auto addOnce = [&](const std::string& s) {
        if (std::find(flags.begin(), flags.end(), s) == flags.end())
            flags.push_back(s);
    };

    for (const auto& entry : db) {
        std::vector<std::string> args;

        if (entry.contains("arguments") && entry["arguments"].is_array()) {
            args = entry["arguments"].get<std::vector<std::string>>();
        } else if (entry.contains("command") && entry["command"].is_string()) {
            std::istringstream iss(entry["command"].get<std::string>());
            for (std::string tok; iss >> tok;) args.push_back(std::move(tok));
        }

        for (size_t j = 0; j < args.size(); ++j) {
            const std::string& a = args[j];
            if (a.size() > 2 && a.substr(0,2) == "-I")
                { addOnce(a); }
            else if ((a == "-I" || a == "-isystem") && j+1 < args.size())
                { addOnce(a); addOnce(args[++j]); }
            else if (a.size() > 2 && a.substr(0,2) == "-D")
                { addOnce(a); }
            else if (a == "-D" && j+1 < args.size())
                { addOnce(a); addOnce(args[++j]); }
        }
    }
    return flags;
}


// ================================================================
//  §4  Reflection data structures
// ================================================================

enum class CKind : uint8_t { None, Vector, Optional, Map, Set };

struct ReflectedField {
    std::string name;
    std::string typeName;
    std::string elementType;
    std::string keyType;
    CKind       containerKind = CKind::None;
    std::string metadata;
};

struct ReflectedType {
    std::string                 qualifiedName;
    std::string                 parentName;
    std::vector<ReflectedField> fields;
};

struct ReflectedEnumValue {
    std::string name;
    int64_t     value = 0;
};

struct ReflectedEnum {
    std::string                     qualifiedName;
    bool                            isFlags = false;
    std::vector<ReflectedEnumValue> values;
};

struct ParseResult {
    std::vector<ReflectedType> types;
    std::vector<ReflectedEnum> enums;
    bool                       hadErrors = false;
};


// ================================================================
//  §5  Prefix header
// ================================================================

static fs::path writePrefixHeader(const fs::path& outputDir) {
    fs::path p = outputDir / "forge_reflect_prefix.h";
    std::ofstream f(p);
    f << R"(// Auto-generated by forge-reflect. DO NOT EDIT.
#pragma once

#undef FORGE_REFLECT_TYPE
#undef FORGE_REFLECT
#undef FORGE_REFLECT_ENUM

// Sentinel static member — isReflectedType() detects CXCursor_VarDecl "_forge_type_marker".
#define FORGE_REFLECT_TYPE(T) \
    static const int _forge_type_marker = 0;

// Attaches a clang annotation to the next field declaration.
#define FORGE_REFLECT() \
    [[clang::annotate("forge.reflect.field")]]

// Injects a using-alias sentinel.  [[clang::annotate]] is rejected before
// 'enum class' in Clang 18, so we use a type-alias token that the reflector
// can detect when it processes the immediately-following EnumDecl:
//   using _forge_reflect_enum_E_marker_ = int;
// isReflectedEnum() scans the parent scope for this alias.
#define FORGE_REFLECT_ENUM(E) \
    using _forge_reflect_enum_##E##_marker_ [[maybe_unused]] = int;
)";
    return p;
}


// ================================================================
//  §6  libclang helpers
// ================================================================

static std::string getQualifiedName(CXCursor cursor) {
    std::vector<std::string> parts;
    for (CXCursor c = cursor;;) {
        CXCursorKind k = clang_getCursorKind(c);
        if (k == CXCursor_TranslationUnit || k == CXCursor_InvalidFile) break;

        if (k == CXCursor_Namespace    || k == CXCursor_ClassDecl   ||
            k == CXCursor_StructDecl   || k == CXCursor_ClassTemplate||
            k == CXCursor_EnumDecl) {
            std::string part = CXStr(clang_getCursorSpelling(c)).str();
            if (!part.empty()) parts.push_back(part);
        }
        c = clang_getCursorSemanticParent(c);
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty()) out += "::";
        out += *it;
    }
    return out;
}

static bool hasAnnotation(CXCursor cursor, std::string_view annotation) {
    struct Ctx { std::string_view target; bool found = false; };
    Ctx ctx{annotation};
    clang_visitChildren(cursor,
        [](CXCursor c, CXCursor, CXClientData d) -> CXChildVisitResult {
            auto* ctx = static_cast<Ctx*>(d);
            if (clang_getCursorKind(c) == CXCursor_AnnotateAttr &&
                CXStr(clang_getCursorSpelling(c)).str() == ctx->target) {
                ctx->found = true;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        }, &ctx);
    return ctx.found;
}

// Check for _forge_type_marker static member (VarDecl) or field (FieldDecl).
static bool isReflectedType(CXCursor cursor) {
    bool found = false;
    clang_visitChildren(cursor,
        [](CXCursor c, CXCursor, CXClientData d) -> CXChildVisitResult {
            CXCursorKind k = clang_getCursorKind(c);
            if ((k == CXCursor_VarDecl || k == CXCursor_FieldDecl) &&
                CXStr(clang_getCursorSpelling(c)).str() == "_forge_type_marker") {
                *static_cast<bool*>(d) = true;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        }, &found);
    return found;
}

static std::string normalizeTypeName(std::string s) {
    // GCC inline namespace
    for (auto p = s.find("std::__cxx11::"); p != std::string::npos;
              p = s.find("std::__cxx11::"))
        s.replace(p, 14, "std::");

    // basic_string expansions
    static const struct { const char* from; const char* to; } kNorm[] = {
        { "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string" },
        { "std::basic_string<char>",  "std::string" },
    };
    for (auto& n : kNorm)
        for (auto p = s.find(n.from); p != std::string::npos; p = s.find(n.from))
            s.replace(p, std::strlen(n.from), n.to);

    return s;
}

// Parse Nth top-level template argument. Handles nested templates.
static std::string nthTemplateArg(const std::string& s, int n) {
    auto open = s.find('<');
    if (open == std::string::npos) return {};

    int  depth   = 0;
    int  current = 0;
    auto start   = open + 1;

    for (size_t i = open; i < s.size(); ++i) {
        if      (s[i] == '<') { ++depth; }
        else if (s[i] == '>') {
            if (--depth == 0) {
                if (current == n) {
                    std::string r = s.substr(start, i - start);
                    while (!r.empty() && r.front() == ' ') r.erase(r.begin());
                    while (!r.empty() && r.back()  == ' ') r.pop_back();
                    return r;
                }
                break;
            }
        } else if (s[i] == ',' && depth == 1) {
            if (current == n) {
                std::string r = s.substr(start, i - start);
                while (!r.empty() && r.front() == ' ') r.erase(r.begin());
                while (!r.empty() && r.back()  == ' ') r.pop_back();
                return r;
            }
            ++current;
            start = i + 1;
        }
    }
    return {};
}

struct ContainerInfo {
    CKind       kind = CKind::None;
    std::string elementType;
    std::string keyType;
};

static ContainerInfo detectContainer(const std::string& spelling) {
    auto sw = [&](const char* prefix) {
        return spelling.compare(0, std::strlen(prefix), prefix) == 0;
    };

    if (sw("std::vector<"))
        return { CKind::Vector,   nthTemplateArg(spelling, 0), {} };
    if (sw("std::optional<"))
        return { CKind::Optional, nthTemplateArg(spelling, 0), {} };
    if (sw("std::unordered_map<") || sw("std::map<"))
        return { CKind::Map,
                 nthTemplateArg(spelling, 1),
                 nthTemplateArg(spelling, 0) };
    if (sw("std::unordered_set<") || sw("std::set<"))
        return { CKind::Set,      nthTemplateArg(spelling, 0), {} };

    return {};
}


// ================================================================
//  §7  AST collection
// ================================================================

static std::optional<ReflectedField> collectField(CXCursor c, bool /*strict*/) {
    if (!hasAnnotation(c, "forge.reflect.field")) return std::nullopt;

    ReflectedField field;
    field.name = CXStr(clang_getCursorSpelling(c)).str();
    if (field.name.empty()) {
        std::cerr << "[forge-reflect] Warning: anonymous reflected field — skipped.\n";
        return std::nullopt;
    }

    CXType      type     = clang_getCursorType(c);
    std::string spelling = normalizeTypeName(CXStr(clang_getTypeSpelling(type)).str());

    auto ci = detectContainer(spelling);
    field.containerKind = ci.kind;
    field.elementType   = ci.elementType;
    field.keyType       = ci.keyType;
    field.typeName      = spelling;

    return field;
}

static std::string collectParentName(CXCursor classCursor) {
    struct Ctx { std::string name; };
    Ctx ctx;
    clang_visitChildren(classCursor,
        [](CXCursor c, CXCursor, CXClientData d) -> CXChildVisitResult {
            if (clang_getCursorKind(c) != CXCursor_CXXBaseSpecifier)
                return CXChildVisit_Continue;

            CXCursor baseDecl = clang_getCursorDefinition(clang_getCursorReferenced(c));
            if (clang_Cursor_isNull(baseDecl)) return CXChildVisit_Continue;

            std::string baseName = getQualifiedName(baseDecl);

            if (baseName == "forge::IReflectedType" || isReflectedType(baseDecl)) {
                static_cast<Ctx*>(d)->name = baseName;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        }, &ctx);
    return ctx.name;
}

static std::optional<ReflectedType> collectType(CXCursor cursor, bool strict) {
    ReflectedType type;
    type.qualifiedName = getQualifiedName(cursor);
    if (type.qualifiedName.empty()) {
        std::cerr << "[forge-reflect] Warning: reflected type with no qualified name — skipped.\n";
        return std::nullopt;
    }

    type.parentName = collectParentName(cursor);

    struct FieldCtx { std::vector<ReflectedField> fields; bool strict; };
    FieldCtx fctx { {}, strict };
    clang_visitChildren(cursor,
        [](CXCursor c, CXCursor, CXClientData d) -> CXChildVisitResult {
            if (clang_getCursorKind(c) == CXCursor_FieldDecl) {
                auto* ctx = static_cast<FieldCtx*>(d);
                if (auto f = collectField(c, ctx->strict))
                    ctx->fields.push_back(std::move(*f));
            }
            return CXChildVisit_Continue;
        }, &fctx);

    type.fields = std::move(fctx.fields);
    return type;
}

static std::optional<ReflectedEnum> collectEnum(CXCursor cursor, bool /*strict*/) {
    ReflectedEnum e;
    e.qualifiedName = getQualifiedName(cursor);
    if (e.qualifiedName.empty()) {
        std::cerr << "[forge-reflect] Warning: reflected enum with no qualified name — skipped.\n";
        return std::nullopt;
    }

    clang_visitChildren(cursor,
        [](CXCursor c, CXCursor, CXClientData d) -> CXChildVisitResult {
            if (clang_getCursorKind(c) == CXCursor_EnumConstantDecl) {
                auto* e = static_cast<ReflectedEnum*>(d);
                ReflectedEnumValue val;
                val.name  = CXStr(clang_getCursorSpelling(c)).str();
                val.value = clang_getEnumConstantDeclValue(c);
                e->values.push_back(val);
            }
            return CXChildVisit_Continue;
        }, &e);

    return e;
}


// ================================================================
//  §8  Enum sentinel detection + top-level AST visitor
//
//  FORGE_REFLECT_ENUM(E) emits:
//    using _forge_reflect_enum_E_marker_ = int;
//  isReflectedEnum() checks whether that alias exists in the same
//  semantic scope as the EnumDecl being examined.
// ================================================================

static bool isReflectedEnum(CXCursor enumCursor) {
    std::string enumName  = CXStr(clang_getCursorSpelling(enumCursor)).str();
    std::string sentinel  = "_forge_reflect_enum_" + enumName + "_marker_";

    CXCursor parent = clang_getCursorSemanticParent(enumCursor);

    struct Ctx { std::string_view target; bool found = false; };
    Ctx ctx { sentinel };
    clang_visitChildren(parent,
        [](CXCursor c, CXCursor, CXClientData d) -> CXChildVisitResult {
            auto* ctx = static_cast<Ctx*>(d);
            CXCursorKind k = clang_getCursorKind(c);
            if ((k == CXCursor_TypeAliasDecl || k == CXCursor_TypedefDecl) &&
                CXStr(clang_getCursorSpelling(c)).str() == ctx->target) {
                ctx->found = true;
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        }, &ctx);
    return ctx.found;
}

struct VisitorState {
    ParseResult& result;
    bool         strict;
};

static CXChildVisitResult topLevelVisitor(CXCursor cursor, CXCursor /*parent*/, CXClientData data) {
    auto* state = static_cast<VisitorState*>(data);
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_Namespace)
        return CXChildVisit_Recurse;

    if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl) {
        if (isReflectedType(cursor)) {
            if (auto type = collectType(cursor, state->strict))
                state->result.types.push_back(std::move(*type));
            else
                state->result.hadErrors = true;
            return CXChildVisit_Continue;
        }
        return CXChildVisit_Recurse;
    }

    if (kind == CXCursor_EnumDecl) {
        // Skip the anonymous sentinel enums we emit for detection
        std::string name = CXStr(clang_getCursorSpelling(cursor)).str();
        if (name.find("_forge_reflect_enum_") == 0) return CXChildVisit_Continue;

        if (isReflectedEnum(cursor)) {
            if (auto e = collectEnum(cursor, state->strict))
                state->result.enums.push_back(std::move(*e));
            else
                state->result.hadErrors = true;
        }
        return CXChildVisit_Continue;
    }

    // Skip sentinel type-alias declarations produced by FORGE_REFLECT_ENUM
    if (kind == CXCursor_TypeAliasDecl || kind == CXCursor_TypedefDecl) {
        return CXChildVisit_Continue;
    }

    return CXChildVisit_Continue;
}


// ================================================================
//  §9  Code emitter
// ================================================================

static std::string nameToIdent(const std::string& name) {
    std::string r;
    r.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ':' && i+1 < name.size() && name[i+1] == ':') {
            r += '_'; ++i;
        } else {
            r += name[i];
        }
    }
    return r;
}

static std::string ckindStr(CKind k) {
    switch (k) {
        case CKind::None:     return "forge::ContainerKind::None";
        case CKind::Vector:   return "forge::ContainerKind::Vector";
        case CKind::Optional: return "forge::ContainerKind::Optional";
        case CKind::Map:      return "forge::ContainerKind::Map";
        case CKind::Set:      return "forge::ContainerKind::Set";
    }
    return "forge::ContainerKind::None";
}

static void emitTypeRegistration(std::ostream& out, const ReflectedType& type) {
    const std::string id = nameToIdent(type.qualifiedName);
    const std::string parentID = type.parentName.empty()
        ? "forge::kInvalidTypeID"
        : "forge::fnv1a(\"" + type.parentName + "\")";

    out << "// ── " << type.qualifiedName << "\n"
        << "namespace {\n\n"
        << "struct " << id << "_Registrar {\n"
        << "    " << id << "_Registrar() {\n"
        << "        forge::TypeInfo info;\n"
        << "        info.typeID       = forge::fnv1a(\"" << type.qualifiedName << "\");\n"
        << "        info.parentTypeID = " << parentID << ";\n"
        << "        info.name         = \"" << type.qualifiedName << "\";\n"
        << "        info.size         = sizeof(" << type.qualifiedName << ");\n"
        << "        info.alignment    = alignof(" << type.qualifiedName << ");\n";

    // ReflectCreateHelper sets pfnCreate/pfnDestroy only when T IS-A IReflectedType.
    // Plain data structs (e.g. AnimEvent) leave both null — no-op specialisation.
    out << "        forge::detail::ReflectCreateHelper<" << type.qualifiedName << ">::apply(info);\n";

    if (!type.fields.empty()) {
        out << "        info.properties = {\n";
        for (const auto& f : type.fields) {
            const std::string typeID = "forge::fnv1a(\"" + f.typeName + "\")";
            const std::string elemID = f.elementType.empty()
                ? "forge::kInvalidTypeID"
                : "forge::fnv1a(\"" + f.elementType + "\")";
            const std::string keyID  = f.keyType.empty()
                ? "forge::kInvalidTypeID"
                : "forge::fnv1a(\"" + f.keyType + "\")";

            out << "            {\n"
                << "                " << typeID << ",\n"
                << "                " << elemID << ",\n"
                << "                " << keyID  << ",\n"
                << "                " << ckindStr(f.containerKind) << ",\n"
                << "                static_cast<uint32_t>(offsetof("
                    << type.qualifiedName << ", " << f.name << ")),\n"
                << "                static_cast<uint32_t>(sizeof("
                    << f.typeName << ")),\n"
                << "                \"" << f.name << "\",\n"
                << "                \"" << f.metadata << "\"\n"
                << "            },\n";
        }
        out << "        };\n";
    }

    out << "        forge::TypeRegistry::Get().RegisterType(std::move(info));\n"
        << "    }\n"
        << "};\n\n"
        << "static " << id << "_Registrar s_" << id << "_Registrar;\n\n"
        << "} // anonymous namespace\n\n";
}

static void emitEnumRegistration(std::ostream& out, const ReflectedEnum& e) {
    const std::string id = nameToIdent(e.qualifiedName);

    out << "// ── enum " << e.qualifiedName << "\n"
        << "namespace {\n\n"
        << "struct " << id << "_EnumRegistrar {\n"
        << "    " << id << "_EnumRegistrar() {\n"
        << "        forge::EnumInfo info;\n"
        << "        info.typeID  = forge::fnv1a(\"" << e.qualifiedName << "\");\n"
        << "        info.name    = \"" << e.qualifiedName << "\";\n"
        << "        info.isFlags = " << (e.isFlags ? "true" : "false") << ";\n"
        << "        info.values  = {\n";

    for (const auto& v : e.values) {
        out << "            { \"" << v.name << "\", "
            << "static_cast<int64_t>("
            << e.qualifiedName << "::" << v.name
            << ") },\n";
    }

    out << "        };\n"
        << "        forge::EnumRegistry::Get().RegisterEnum(std::move(info));\n"
        << "    }\n"
        << "};\n\n"
        << "static " << id << "_EnumRegistrar s_" << id << "_EnumRegistrar;\n\n"
        << "} // anonymous namespace\n\n";
}


// ================================================================
//  §10  Utilities
// ================================================================

static fs::path genFilePath(const fs::path& header, const fs::path& outputDir) {
    return outputDir / (header.stem().string() + ".gen.cpp");
}

static bool needsRegeneration(const fs::path& header, const fs::path& genFile) {
    if (!fs::exists(genFile)) return true;
    return fs::last_write_time(header) > fs::last_write_time(genFile);
}

static std::string absIncludePath(const fs::path& header) {
    std::string s = fs::absolute(header).string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}


// ================================================================
//  §11  main
// ================================================================

int main(int argc, char** argv) {
    auto cfgOpt = parseArgs(argc, argv);
    if (!cfgOpt) { printUsage(argv[0]); return 1; }
    const Config& cfg = *cfgOpt;

    fs::create_directories(cfg.outputDir);

    const fs::path prefixHeader = writePrefixHeader(cfg.outputDir);
    const std::vector<std::string> compileFlags = extractCompileFlags(cfg.compileDb);

    std::vector<std::string> clangArgStrs = {
        "-x", "c++-header",
        "-std=c++17",
        "-include", prefixHeader.string()
    };
    for (const auto& f : compileFlags)
        clangArgStrs.push_back(f);

    std::vector<const char*> clangArgs;
    clangArgs.reserve(clangArgStrs.size());
    for (const auto& s : clangArgStrs) clangArgs.push_back(s.c_str());

    CXIndex index = clang_createIndex(1, 0);
    int exitCode  = 0;

    for (const auto& header : cfg.headers) {
        const fs::path genPath = genFilePath(header, cfg.outputDir);

        if (!needsRegeneration(header, genPath)) {
            std::cout << "[forge-reflect] Up to date: " << header.filename().string() << "\n";
            continue;
        }

        std::cout << "[forge-reflect] Processing:  " << header.filename().string() << "\n";

        CXTranslationUnit tu = nullptr;
        CXErrorCode err = clang_parseTranslationUnit2(
            index,
            header.string().c_str(),
            clangArgs.data(), static_cast<int>(clangArgs.size()),
            nullptr, 0,
            CXTranslationUnit_SkipFunctionBodies,
            &tu
        );

        if (err != CXError_Success || !tu) {
            std::cerr << "[forge-reflect] Error: failed to parse "
                      << header << " (CXErrorCode=" << err << ")\n";
            if (cfg.strictMode) { exitCode = 1; break; }
            continue;
        }

        bool fatalDiag = false;
        const unsigned numDiags = clang_getNumDiagnostics(tu);
        for (unsigned d = 0; d < numDiags; ++d) {
            CXDiagnostic diag = clang_getDiagnostic(tu, d);
            CXDiagnosticSeverity sev = clang_getDiagnosticSeverity(diag);

            if (sev >= CXDiagnostic_Error) {
                std::cerr << "[forge-reflect] "
                          << CXStr(clang_formatDiagnostic(diag,
                                CXDiagnostic_DisplaySourceLocation |
                                CXDiagnostic_DisplayColumn)).str()
                          << "\n";
                if (cfg.strictMode) fatalDiag = true;
            }
            clang_disposeDiagnostic(diag);
        }

        if (fatalDiag) {
            clang_disposeTranslationUnit(tu);
            exitCode = 1;
            break;
        }

        ParseResult result;
        VisitorState state{ result, cfg.strictMode };
        clang_visitChildren(clang_getTranslationUnitCursor(tu), topLevelVisitor, &state);

        if (result.hadErrors && cfg.strictMode) {
            std::cerr << "[forge-reflect] Errors collecting types from "
                      << header << " (strict mode)\n";
            clang_disposeTranslationUnit(tu);
            exitCode = 1;
            break;
        }

        clang_disposeTranslationUnit(tu);

        if (result.types.empty() && result.enums.empty()) {
            std::cout << "[forge-reflect] No reflected types in "
                      << header.filename().string() << " — skipping emit\n";
            continue;
        }

        std::ofstream out(genPath);
        if (!out) {
            std::cerr << "[forge-reflect] Error: could not write " << genPath << "\n";
            if (cfg.strictMode) { exitCode = 1; break; }
            continue;
        }

        out << "// Auto-generated by forge-reflect. DO NOT EDIT.\n"
            << "// Source: " << header.string() << "\n\n"
            << "#include <forge/TypeSystem.h>\n"
            << "#include \"" << absIncludePath(header) << "\"\n\n";

        for (const auto& type : result.types)
            emitTypeRegistration(out, type);
        for (const auto& e : result.enums)
            emitEnumRegistration(out, e);

        std::cout << "[forge-reflect] Generated: " << genPath.filename().string()
                  << "  (" << result.types.size() << " type(s), "
                  << result.enums.size() << " enum(s))\n";
    }

    clang_disposeIndex(index);
    return exitCode;
}
