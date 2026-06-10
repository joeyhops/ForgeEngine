#include "EnemyEntity.h"

#include <forge/CombatSystem.h>
#include <forge/DebugUI.h>
#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/Logger.h>
#include <forge/LuaState.h>
#include <forge/PhysicsWorld.h>

#include <algorithm>
#include <sstream>

void EnemyEntity::setup(forge::PhysicsWorld& physics,
                         forge::LuaState& lua,
                         forge::CombatSystem& combat,
                         forge::DebugUI& debugUI)
{
  using namespace forge;

  skinnedModel = AssetManager::loadSkinnedModel("models/characters/anim/ybot_idle.glb");

  transform = std::make_unique<Transform>();
  transform->setPosition({ 0.0f, 0.0f, -4.0f });
  transform->setScale({ 0.01f, 0.01f, 0.01f });
  transform->setEulerAngles({ 90.0f, 0.0f, 0.0f });

  controller = std::make_unique<CharacterController>(physics, *transform, 0.3f, 0.9f);
  

  animator = std::make_unique<forge::Animator>();
  animator->setOwnerName("enemy");
  animator->setSkeleton(skinnedModel.skeleton);

  auto graphDef = GraphDefinition::LoadFromJson("assets/anim/enemy_graph.json");
  if (!graphDef) {
    LOG_ERROR("[EnemyEntity] Failed toload enemy_graph.json");
    return;
  }
  animator->setGraph(GraphInstance::Create(graphDef));

  this->combat = std::make_unique<CombatComponent>("enemy", 400.0f, 100.0f, 60.0f);
  this->combat->setAnimator(animator.get());
  this->combat->setParamTable(&animator->getParams());

  this->combat->onDeath = [&lua, this]() {
    animator->getParams().setBool("isDead", true);
    lua.callFunction("onEnemyDeath");
  };
  this->combat->onHit = [&lua](const HitEvent& h) {
    lua.callFunction("onEnemyHit", h.damage, h.damageType);
  };

  ai = std::make_unique<forge::AIComponent>("enemy", *transform, *this->combat);

  equipment = std::make_unique<EquipmentComponent>("enemy");
  equipment->setAnimator(animator.get());

  equipment->onEquip = [this, &lua](EquipmentComponent::Slot slot, const WeaponDef* def) {
    if (slot == EquipmentComponent::RIGHT_HAND) {
      this->combat->setWeapon(def);
      if (!def->meshPath.empty())
        weaponModel = AssetManager::loadModel(def->meshPath);
      lua.callFunction("onEnemyEquip", static_cast<int>(slot), def->id);
    }
  };
  equipment->onUnequip = [this, &lua](EquipmentComponent::Slot slot) {
    if (slot == EquipmentComponent::RIGHT_HAND) {
      this->combat->setWeapon(nullptr);
      weaponModel = ModelData{};
      lua.callFunction("onEnemyUnequip", static_cast<int>(slot));
    }
  };

  combat.registerCombatant(this->combat.get());
  this->combat->setGhostObject(controller->getGhostObject());

  auto& L = lua.get();
  L["enemyCombat"] = this->combat.get();
  L["enemyTransform"] = transform.get();
  L["enemyAnim"] = &animator->getParams();
  L["enemyAI"] = ai.get();
  L["enemyEquipment"] = equipment.get();

  debugUI.registerAnimator(animator.get(), "Enemy");
  debugUI.registerAIComponent(ai.get());
//  debugUI.registerEquipmentComponent(equipment.get());

  LOG_INFO("[Enemy] ready - skeleton: {} bones", skinnedModel.skeleton.size());
}

void EnemyEntity::teardown() {}

void EnemyEntity::reset() {
  if (!combat || !controller || !ai) return;
  combat->revive();
  controller->warp(spawnPos);
  ai->reset();
}
