#pragma once
#include <forge/TypeSystem.h>

#include <glm/glm.hpp>

#include <string>

namespace forge {

struct MaterialDefinition : public IReflectedType {
  FORGE_REFLECT_TYPE(MaterialDefinition)

  TypeInfo const* GetTypeInfo() const override;

  FORGE_REFLECT() std::string albedoPath;
  FORGE_REFLECT() std::string normalMapPath;
  FORGE_REFLECT() std::string metallicMapPath;
  FORGE_REFLECT() std::string roughnessMapPath;
  FORGE_REFLECT() std::string aoMapPath;

  FORGE_REFLECT() glm::vec3 albedoTint = { 1.0f, 1.0f, 1.0f };
  FORGE_REFLECT() float metallic = 0.0f;
  FORGE_REFLECT() float roughness = 0.5f;
};

}
