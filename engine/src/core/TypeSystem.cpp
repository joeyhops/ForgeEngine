#include <forge/TypeSystem.h>
#include <cassert>

namespace forge {

// ----------------------------------------------------------------
//  TypeRegistry
// ----------------------------------------------------------------

TypeRegistry& TypeRegistry::Get() {
    static TypeRegistry instance;
    return instance;
}

void TypeRegistry::RegisterType(TypeInfo info) {
    const TypeID id       = info.typeID;
    const TypeID parentID = info.parentTypeID;

    assert(m_types.find(id) == m_types.end()
        && "TypeID collision: two reflected types produce the same FNV-1a hash. "
           "Rename one type or increase TypeID width.");

    if (parentID != kInvalidTypeID) {
        m_children[parentID].push_back(id);
    }

    m_nameIndex[info.name] = id;
    m_types.emplace(id, std::move(info));
}

TypeInfo const* TypeRegistry::GetTypeInfo(TypeID id) const {
    auto it = m_types.find(id);
    return it != m_types.end() ? &it->second : nullptr;
}

TypeInfo const* TypeRegistry::GetTypeInfoByName(const char* name) const {
    auto it = m_nameIndex.find(name);
    if (it == m_nameIndex.end()) return nullptr;
    return GetTypeInfo(it->second);
}

IReflectedType* TypeRegistry::CreateInstance(TypeID id) const {
    const TypeInfo* info = GetTypeInfo(id);
    if (!info || !info->pfnCreate) return nullptr;
    return info->pfnCreate();
}

std::vector<TypeID> TypeRegistry::GetAllDerivedTypes(TypeID baseID) const {
    std::vector<TypeID> result;
    std::vector<TypeID> stack;

    auto it = m_children.find(baseID);
    if (it != m_children.end()) {
        for (TypeID child : it->second)
            stack.push_back(child);
    }

    while (!stack.empty()) {
        TypeID current = stack.back();
        stack.pop_back();
        result.push_back(current);

        auto childIt = m_children.find(current);
        if (childIt != m_children.end()) {
            for (TypeID child : childIt->second)
                stack.push_back(child);
        }
    }

    return result;
}


// ----------------------------------------------------------------
//  EnumRegistry
// ----------------------------------------------------------------

EnumRegistry& EnumRegistry::Get() {
    static EnumRegistry instance;
    return instance;
}

void EnumRegistry::RegisterEnum(EnumInfo info) {
    m_nameIndex[info.name] = info.typeID;
    m_enums.emplace(info.typeID, std::move(info));
}

EnumInfo const* EnumRegistry::GetEnumInfo(TypeID id) const {
    auto it = m_enums.find(id);
    return it != m_enums.end() ? &it->second : nullptr;
}

EnumInfo const* EnumRegistry::GetEnumInfoByName(const char* name) const {
    auto it = m_nameIndex.find(name);
    if (it == m_nameIndex.end()) return nullptr;
    return GetEnumInfo(it->second);
}

const char* EnumRegistry::GetValueName(TypeID enumID, int64_t value) const {
    const EnumInfo* info = GetEnumInfo(enumID);
    if (!info) return nullptr;
    for (const auto& v : info->values) {
        if (v.value == value) return v.name;
    }
    return nullptr;
}

bool EnumRegistry::TryGetValue(TypeID enumID, const char* name, int64_t& outValue) const {
    const EnumInfo* info = GetEnumInfo(enumID);
    if (!info) return false;
    for (const auto& v : info->values) {
        if (std::string_view(v.name) == name) {
            outValue = v.value;
            return true;
        }
    }
    return false;
}

} // namespace forge
