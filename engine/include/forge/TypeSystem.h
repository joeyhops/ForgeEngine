#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <string>

namespace forge {

// ================================================================
//  FNV-1a  —  constexpr, platform-stable TypeID hashing
// ================================================================

using TypeID = uint32_t;
static constexpr TypeID kInvalidTypeID = 0u;

constexpr TypeID fnv1a(std::string_view str) noexcept {
    uint32_t h = 2166136261u;
    for (unsigned char c : str) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// ================================================================
//  TypeName<T>  —  compile-time type name extraction
//
//  Primary template uses __PRETTY_FUNCTION__ / __FUNCSIG__ to pull
//  the type name out of the compiler's own function signature string.
//
//  stdlib and primitive types get explicit specializations because
//  their pretty-printed names are implementation-defined.
// ================================================================

namespace detail {

    template<typename T>
    constexpr std::string_view raw_sig() noexcept {
#if defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return __PRETTY_FUNCTION__;
#endif
    }

    template<typename T>
    constexpr std::string_view extract_name() noexcept {
        constexpr std::string_view sig = raw_sig<T>();
#if defined(_MSC_VER)
        constexpr std::string_view needle = "raw_sig<";
        constexpr auto prefix = sig.find(needle) + needle.size();
        constexpr auto suffix = sig.rfind(">(");
        return sig.substr(prefix, suffix - prefix);
#else
        constexpr auto prefix = sig.find("T = ") + 4;
        constexpr auto suffix = sig.rfind(']');
        return sig.substr(prefix, suffix - prefix);
#endif
    }

} // namespace detail

template<typename T>
struct TypeName {
    static constexpr std::string_view value = detail::extract_name<T>();
};

// ── Primitive specializations ────────────────────────────────────
template<> struct TypeName<void>     { static constexpr std::string_view value = "void";     };
template<> struct TypeName<bool>     { static constexpr std::string_view value = "bool";     };
template<> struct TypeName<char>     { static constexpr std::string_view value = "char";     };
template<> struct TypeName<int8_t>   { static constexpr std::string_view value = "int8_t";   };
template<> struct TypeName<uint8_t>  { static constexpr std::string_view value = "uint8_t";  };
template<> struct TypeName<int16_t>  { static constexpr std::string_view value = "int16_t";  };
template<> struct TypeName<uint16_t> { static constexpr std::string_view value = "uint16_t"; };
template<> struct TypeName<int32_t>  { static constexpr std::string_view value = "int32_t";  };
template<> struct TypeName<uint32_t> { static constexpr std::string_view value = "uint32_t"; };
template<> struct TypeName<int64_t>  { static constexpr std::string_view value = "int64_t";  };
template<> struct TypeName<uint64_t> { static constexpr std::string_view value = "uint64_t"; };
template<> struct TypeName<float>    { static constexpr std::string_view value = "float";    };
template<> struct TypeName<double>   { static constexpr std::string_view value = "double";   };

// ── stdlib specializations ───────────────────────────────────────
template<> struct TypeName<std::string>
    { static constexpr std::string_view value = "std::string"; };

template<typename T>
constexpr TypeID TypeID_of() noexcept {
    return fnv1a(TypeName<T>::value);
}


// ================================================================
//  ContainerKind + PropertyInfo
// ================================================================

enum class ContainerKind : uint8_t {
    None     = 0,
    Vector,
    Optional,
    Map,
    Set,
};

struct PropertyInfo {
    TypeID        typeID;
    TypeID        elementTypeID;
    TypeID        keyTypeID;
    ContainerKind containerKind;
    uint32_t      offset;
    uint32_t      size;
    const char*   name;
    const char*   metadata;
};


// ================================================================
//  IReflectedType
// ================================================================

struct TypeInfo;

class IReflectedType {
public:
    virtual ~IReflectedType() = default;
    virtual TypeInfo const* GetTypeInfo() const = 0;
};


// ================================================================
//  TypeInfo
// ================================================================

struct TypeInfo {
    TypeID                    typeID;
    TypeID                    parentTypeID;
    size_t                    size;
    size_t                    alignment;
    const char*               name;
    std::vector<PropertyInfo> properties;
    IReflectedType* (*pfnCreate)()                 = nullptr;
    void            (*pfnDestroy)(IReflectedType*) = nullptr;
};


// ================================================================
//  detail::ReflectCreateHelper
//
//  Used by generated .gen.cpp files to set pfnCreate/pfnDestroy
//  only when T actually inherits IReflectedType.  Plain data structs
//  (reflected for serialisation only) leave both pointers null.
//
//  Must be a template specialisation rather than if constexpr so that
//  the non-taken branch is never type-checked outside of a template
//  instantiation context.
// ================================================================

namespace detail {
    template<typename T, bool = std::is_base_of_v<IReflectedType, T>>
    struct ReflectCreateHelper {
        static void apply(TypeInfo&) {}
    };

    template<typename T>
    struct ReflectCreateHelper<T, true> {
        static void apply(TypeInfo& info) {
            info.pfnCreate  = []() -> IReflectedType* { return new T(); };
            info.pfnDestroy = [](IReflectedType* p) { delete static_cast<T*>(p); };
        }
    };
} // namespace detail


// ================================================================
//  TypeRegistry
// ================================================================

class TypeRegistry {
public:
    static TypeRegistry& Get();

    void RegisterType(TypeInfo info);

    TypeInfo const* GetTypeInfo(TypeID id) const;
    TypeInfo const* GetTypeInfoByName(const char* name) const;
    IReflectedType* CreateInstance(TypeID id) const;

    std::vector<TypeID> GetAllDerivedTypes(TypeID baseID) const;

private:
    TypeRegistry() = default;

    std::unordered_map<TypeID, TypeInfo>              m_types;
    std::unordered_map<std::string, TypeID>           m_nameIndex;
    std::unordered_map<TypeID, std::vector<TypeID>>   m_children;
};


// ================================================================
//  EnumValueInfo + EnumInfo + EnumRegistry
// ================================================================

struct EnumValueInfo {
    const char* name;
    int64_t     value;
};

struct EnumInfo {
    TypeID                      typeID;
    const char*                 name;
    bool                        isFlags;
    std::vector<EnumValueInfo>  values;
};

class EnumRegistry {
public:
    static EnumRegistry& Get();

    void             RegisterEnum(EnumInfo info);
    EnumInfo const*  GetEnumInfo(TypeID id) const;
    EnumInfo const*  GetEnumInfoByName(const char* name) const;

    const char* GetValueName(TypeID enumID, int64_t value) const;
    bool        TryGetValue(TypeID enumID, const char* name, int64_t& outValue) const;

private:
    EnumRegistry() = default;

    std::unordered_map<TypeID, EnumInfo>    m_enums;
    std::unordered_map<std::string, TypeID> m_nameIndex;
};


// ================================================================
//  Reflection macros
//
//  Empty at compile time — markers for forge-reflect header parsing.
//  Guard with #ifndef so the forge-reflect prefix header can redefine
//  them before this header is included without being overwritten.
// ================================================================

#ifndef FORGE_REFLECT_TYPE
#define FORGE_REFLECT_TYPE(TypeName)   /* forge-reflect marker */
#endif

#ifndef FORGE_REFLECT
#define FORGE_REFLECT()                /* forge-reflect marker */
#endif

#ifndef FORGE_REFLECT_ENUM
#define FORGE_REFLECT_ENUM(EnumName)   /* forge-reflect marker */
#endif

} // namespace forge
