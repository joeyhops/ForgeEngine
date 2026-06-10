#pragma once
#include <forge/Animator.h>
#include <forge/AssetManager.h>
#include <forge/CharacterController.h>
#include <forge/CombatComponent.h>
#include <forge/EquipmentComponent.h>
#include <forge/Transform.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace forge {
  class PhysicsWorld;
  class LuaState;
  class CombatSystem;
  class DebugUI;
}

enum class DodgeDir {
  Forward = 0,
  ForwardRight = 1,
  Right = 2,
  BackwardRight = 3,
  Backward = 4,
  BackwardLeft = 5,
  Left = 6,
  ForwardLeft = 7
};
// Player entity
// Owns every component and state that belongs to the player
// character. Lifetime: constructed when ForgeGame is constructed
// torn down in onShutdown(). setup() is called from onInit()
// once engine systems are alive and asset root is set
//
// Components are public by design. ForgeGame retains autority over
// update order and calls into the components directly each frame
class PlayerEntity {
public:
  PlayerEntity() = default;

  // Build full character: skinned model load, transform,
  // capsule controller, animator + graph + clips, combat,
  // equipment, anim-event subscriptions, lua globals
  // DebugUI registration
  void setup(forge::PhysicsWorld& physics,
             forge::LuaState& lua,
             forge::CombatSystem& combat,
             forge::DebugUI& debugUI);

  // Release resources
  void teardown();

  // Components
  forge::SkinnedModelData skinnedModel;
  std::unique_ptr<forge::Transform> transform;
  std::unique_ptr<forge::CharacterController> controller;
  std::unique_ptr<forge::CombatComponent> combat;
  std::unique_ptr<forge::EquipmentComponent> equipment;
  std::unique_ptr<forge::Animator> animator;
  forge::ModelData weaponModel;

  glm::vec3 forward = { 0.0f, 0.0f, -1.0f };

  glm::quat dodgeFacingQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity
  bool inputLocked = false;

};
