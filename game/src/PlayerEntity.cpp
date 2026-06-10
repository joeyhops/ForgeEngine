#include "PlayerEntity.h"

#include <forge/CombatSystem.h>
#include <forge/DebugUI.h>
#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/Logger.h>
#include <forge/LuaState.h>
#include <forge/PhysicsWorld.h>

#include <algorithm>
#include <sstream>

void PlayerEntity::setup(forge::PhysicsWorld& physics,
                         forge::LuaState& lua,
                         forge::CombatSystem& combat,
                         forge::DebugUI& debugUI)
{
  using namespace forge;

  skinnedModel = AssetManager::loadSkinnedModel("models/bone_exp1.glb");
  //skinnedModel = AssetManager::loadSkinnedModel("models/another_skel.glb");

  transform = std::make_unique<Transform>();
  transform->setPosition({ 0.0f, 0.0f, 0.0f });
  //transform->setScale({ 0.01f, 0.01f, 0.01f });
  transform->setScale({ 1.0f, 1.0f, 1.0f });
  transform->setEulerAngles({ 0.0f, 180.0f, 0.0f });

  controller = std::make_unique<CharacterController>(physics, *transform, 0.3f, 0.9f);
  

  animator = std::make_unique<forge::Animator>();
  animator->setOwnerName("player");
  animator->setSkeleton(skinnedModel.skeleton);

  auto graphDef = forge::GraphDefinition::LoadFromJson("assets/anim/player_graph.json");
  if (!graphDef) {
    LOG_ERROR("[PlayerEntity] Failed toload player_graph.json");
    return;
  }
  animator->setGraph(GraphInstance::Create(graphDef));

  this->combat = std::make_unique<CombatComponent>("player", 500.0f, 100.0f, 80.0f);
  this->combat->setAnimator(animator.get());
  this->combat->setParamTable(&animator->getParams());

  this->combat->onDeath = [&lua]() {
    lua.callFunction("onPlayerDeath");
  };
  this->combat->onHit = [&lua](const HitEvent& h) {
    lua.callFunction("onPlayerHit", h.damage, h.damageType);
  };

  equipment = std::make_unique<EquipmentComponent>("player");
  equipment->setAnimator(animator.get());

  equipment->onEquip = [this, &lua](EquipmentComponent::Slot slot, const WeaponDef* def) {
    if (slot == EquipmentComponent::RIGHT_HAND) {
      this->combat->setWeapon(def);
      if (!def->meshPath.empty())
        weaponModel = AssetManager::loadModel(def->meshPath);
      lua.callFunction("onPlayerEquip", static_cast<int>(slot), def->id);
    }
  };
  equipment->onUnequip = [this, &lua](EquipmentComponent::Slot slot) {
    if (slot == EquipmentComponent::RIGHT_HAND) {
      this->combat->setWeapon(nullptr);
      weaponModel = ModelData{};
      lua.callFunction("onPlayerUnequip", static_cast<int>(slot));
    }
  };

  EventBus::subscribe<AnimEventActivated>([this](const AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type == AnimEventType::LockInput) inputLocked = true;
    if (e.type == AnimEventType::UnlockInput) inputLocked = false;
  });

  combat.registerCombatant(this->combat.get());
  this->combat->setGhostObject(controller->getGhostObject());

  auto& L = lua.get();
  L["playerCombat"] = this->combat.get();
  L["playerTransform"] = this->transform.get();
  L["playerAnim"] = &this->animator->getParams();
  L["playerEquipment"] = this->equipment.get();

  debugUI.registerAnimator(this->animator.get(), "Player");
  debugUI.registerEquipmentComponent(this->equipment.get());

  LOG_INFO("[Player] ready - skeleton: {} bones", skinnedModel.skeleton.size());
}

void PlayerEntity::teardown() {} 


