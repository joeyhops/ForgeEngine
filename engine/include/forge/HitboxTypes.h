#pragma once
#include <glm/glm.hpp>
#include <forge/TypeSystem.h>

#include <vector>

namespace forge {

// One capsule segment defined in weapon mesh local space
// P0 and P1 are axis endpoints; radius is uniform
// Typical longsword setup: three segments covering guard, blade, tip
struct CapsuleSegment {
  FORGE_REFLECT_TYPE(CapsuleSegment)

  FORGE_REFLECT()
  glm::vec3 localP0 = { 0.0f, 0.0f, 0.0f };

  FORGE_REFLECT()
  glm::vec3 localP1 = { 0.0f, 1.0f, 0.0f };

  FORGE_REFLECT()
  float radius = 0.08f;
};

// Capsule segment after transformation to world space
// Computed each frame in ForgeGame and stored in CombatComponent
struct WorldCapsule {
  glm::vec3 worldP0 = { 0.0f, 0.0f, 0.0f };
  glm::vec3 worldP1 = { 0.0f, 1.0f, 0.0f };
  float radius = 0.08f;
};

}
