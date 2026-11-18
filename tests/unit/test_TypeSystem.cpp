#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <forge/AnimGraph.h>
#include <forge/AnimationClip.h>
#include <forge/HitboxTypes.h>
#include <forge/TypeSystem.h>

#include "support/AnimTestUtils.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <string_view>

using namespace forge;

static_assert(fnv1a("") == 0x811c9dc5u, "FNV-1a offset basis");
static_assert(fnv1a("a") == 0xe40c292cu, "FNV-1a(\"a\")");
static_assert(fnv1a("foobar") == 0xbf9cf968u, "FNV-1a(\"foobar\")");

TEST_CASE("TypeName extracts fully qualified names from the compiler signature",
          "[unit][reflection]") {
  CHECK(TypeName<CapsuleSegment>::value == "forge::CapsuleSegment");
  CHECK(TypeName<AnimEvent>::value == "forge::AnimEvent");
  CHECK(TypeName<ClipNode::Definition>::value == "forge::ClipNode::Definition");
  CHECK(TypeName<float>::value == "float");
  CHECK(TypeName<glm::vec3>::value == "glm::vec3");
}

TEST_CASE("TypeID_of is the FNV-1a hash of the type name",
          "[unit][reflection]") {
  STATIC_REQUIRE(TypeID_of<float>() == fnv1a("float"));
  CHECK(TypeID_of<glm::vec3>() == fnv1a("glm::vec3"));
  CHECK(TypeID_of<AnimEvent>() == fnv1a("forge::AnimEvent"));
}

TEST_CASE("Every reflected header's registrations survive the link",
          "[unit][reflection][link]") {
  const auto& types = TypeRegistry::Get();
  CHECK(types.GetTypeInfoByName("forge::CapsuleSegment") != nullptr);
  CHECK(types.GetTypeInfoByName("forge::AnimEvent") != nullptr);
  CHECK(types.GetTypeInfoByName("forge::MaterialDefinition") != nullptr);
  CHECK(types.GetTypeInfoByName("forge::ClipNode::Definition") != nullptr);
  CHECK(EnumRegistry::Get().GetEnumInfoByName("forge::AnimEventType") != nullptr);
  CHECK(EnumRegistry::Get().GetEnumInfoByName("forge::ConditionType") != nullptr);
}

TEST_CASE("AnimEvent reflection data matches the real struct layout",
          "[unit][reflection]") {
  const TypeInfo* info = TypeRegistry::Get().GetTypeInfoByName("forge::AnimEvent");
  REQUIRE(info != nullptr);
  CHECK(info->size == sizeof(AnimEvent));
  CHECK(info->alignment == alignof(AnimEvent));

  AnimEvent probe{};
  const auto offsetOf = [&](const void* member) {
    return static_cast<uint32_t>(reinterpret_cast<const char*>(member) -
                                 reinterpret_cast<const char*>(&probe));
  };

  struct Expected { std::string_view name; uint32_t offset; TypeID type; };
  const Expected expected[] = {
    { "startTime", offsetOf(&probe.startTime), fnv1a("float") },
    { "endTime",   offsetOf(&probe.endTime),   fnv1a("float") },
    { "type",      offsetOf(&probe.type),      fnv1a("forge::AnimEventType") },
    { "payload",   offsetOf(&probe.payload),   fnv1a("std::string") },
    { "capsules",  offsetOf(&probe.capsules),  fnv1a("std::vector<forge::CapsuleSegment>") },
  };

  REQUIRE(info->properties.size() == std::size(expected));
  for (size_t i = 0; i < std::size(expected); ++i) {
    const PropertyInfo& p = info->properties[i];
    INFO("property " << i << " (" << expected[i].name << ")");
    CHECK(std::string_view(p.name) == expected[i].name);
    CHECK(p.offset == expected[i].offset);
    CHECK(p.typeID == expected[i].type);
  }

  const PropertyInfo& caps = info->properties[4];
  CHECK(caps.containerKind == ContainerKind::Vector);
  CHECK(caps.elementTypeID == fnv1a("forge::CapsuleSegment"));
}

TEST_CASE("TypeRegistry finds animation node definitions through inheritance",
          "[unit][reflection]") {
  const auto derived = TypeRegistry::Get().GetAllDerivedTypes(fnv1a("forge::IAnimNodeDefinition"));
  for (const char* name : { "forge::ClipNode::Definition",
                            "forge::Blend1DNode::Definition",
                            "forge::Blend2DNode::Definition",
                            "forge::ClipSelectorNode::Definition",
                            "forge::StateMachineNode::Definition" }) {
    INFO(name);
    CHECK(std::find(derived.begin(), derived.end(), fnv1a(name)) != derived.end());
  }
}

TEST_CASE("TypeRegistry creates reflected definitions that identify themselves",
          "[unit][reflection]") {
  const TypeID id = fnv1a("forge::ClipNode::Definition");
  const TypeInfo* info = TypeRegistry::Get().GetTypeInfo(id);
  REQUIRE(info != nullptr);
  REQUIRE(info->pfnCreate != nullptr);
  REQUIRE(info->pfnDestroy != nullptr);

  IReflectedType* obj = TypeRegistry::Get().CreateInstance(id);
  REQUIRE(obj != nullptr);
  CHECK(dynamic_cast<ClipNode::Definition*>(obj) != nullptr);
  CHECK(obj->GetTypeInfo() == info);
  info->pfnDestroy(obj);
}

TEST_CASE("EnumRegistry knows every AnimEventType value, in declaration order",
          "[unit][reflection]") {
  const TypeID id = fnv1a("forge::AnimEventType");
  const EnumInfo* info = EnumRegistry::Get().GetEnumInfo(id);
  REQUIRE(info != nullptr);

  CHECK(info->values.size() == static_cast<size_t>(forge::test::kAnimEventTypeCount));
  for (size_t i = 0; i < info->values.size(); ++i) {
    INFO(info->values[i].name);
    CHECK(info->values[i].value == static_cast<int64_t>(i));
  }

  const char* recovery = EnumRegistry::Get().GetValueName(id, static_cast<int64_t>(AnimEventType::RecoveryBegin));
  REQUIRE(recovery != nullptr);
  CHECK(std::string_view(recovery) == "RecoveryBegin");

  int64_t v = -1;
  CHECK(EnumRegistry::Get().TryGetValue(id, "DeflectWindow", v));
  CHECK(v == static_cast<int64_t>(AnimEventType::DeflectWindow));
  CHECK_FALSE(EnumRegistry::Get().TryGetValue(id, "NotAnEvent", v));
}
