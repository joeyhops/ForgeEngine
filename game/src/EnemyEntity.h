#pragma once
#include <forge/AIComponent.h>
#include <forge/AnimationClip.h>
#include <forge/Animator.h>
#include <forge/AssetManager.h>
#include <forge/CharacterController.h>
#include <forge/CombatComponent.h>
#include <forge/EquipmentComponent.h>
#include <forge/Transform.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace forge {
  class PhysicsWorld;
  class LuaState;
  class CombatSystem;
  class DebugUI;
}

class EnemyEntity {
public:
  EnemyEntity() = default;

  void setup(forge::PhysicsWorld& physics,
             forge::LuaState& lua,
             forge::CombatSystem& combat,
             forge::DebugUI& debugUI);
  void teardown();

  void reset();

  forge::SkinnedModelData skinnedModel;
  std::unique_ptr<forge::Transform> transform;
  std::unique_ptr<forge::CharacterController> controller;;
  std::unique_ptr<forge::CombatComponent> combat;
  std::unique_ptr<forge::EquipmentComponent> equipment;
  std::unique_ptr<forge::AIComponent> ai;
  std::unique_ptr<forge::Animator> animator;
  forge::ModelData weaponModel;
  std::unordered_map<std::string, std::shared_ptr<forge::AnimationClip>> clips;

  bool active;
  bool respawns;
  int deathFlag = -1;

  glm::vec3 spawnPos = { 0.0f, 0.0f, -4.0f };

};
