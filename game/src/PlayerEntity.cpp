#include "PlayerEntity.h"
#include "CharacterGraph.h"

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

  //clips["walk"] = forge::AssetManager::loadAnimationClip("anim/manny/am_Logo_Run_Fwd.glb", "walk");
  // Load anim clips
  // Idle is embadded in the base model - load the first animation from it

  clips["idle"] = forge::AssetManager::loadAnimationClip("anim/manny/idle/am_Stand_Idle_01.glb", "idle");
  clips["sprint"] = forge::AssetManager::loadAnimationClip("anim/manny/run/am_Loco_Run_Fwd_NoRM.glb", "sprint");
  clips["attack_r1"] = forge::AssetManager::loadAnimationClip("anim/manny/attacks/Anim_SwordV2_Slash1.glb", "attack_r1");
  clips["death"] = forge::AssetManager::loadAnimationClip("anim/manny/die/Anim_DSS3_Die_RM.glb", "death");
  clips["hit"] = forge::AssetManager::loadAnimationClip("anim/manny/hit/Anim_HSAS_Hit_Front_Small.glb", "hit");
  //clips["idle"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/idle.glb", "idle");
  //clips["walk"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/walk_f_2.glb", "walk");
  ////clips["walk"] = forge::AssetManager::loadAnimationClip("anim/manny/am_Logo_Run_Fwd.glb", "walk");
  //clips["sprint"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/sprint_f_rm.glb", "sprint");
  //clips["sprint"] = forge::AssetManager::loadAnimationClip("anim/manny/am_Loco_Run_Fwd.glb", "walk");
  //clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash");
  //clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death");
  //clips["dodge"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/roll_f_rm.glb", "dodge");
  clips["walk"] = forge::AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_Fwd_NoRM.glb", "walk");
  clips["walk_f"]  = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_Fwd_NoRM.glb",  "walk_f");
  clips["walk_fr"] = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_FwdRight_NoRM.glb",   "walk_fr");
  clips["walk_r"]  = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_Right_NoRM.glb",    "walk_r");
  clips["walk_br"] = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_BwdRight_NoRM.glb",  "walk_br");
  clips["walk_b"]  = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_Bwd_NoRM.glb","walk_b");
  clips["walk_bl"] = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_BwdLeft_NoRM.glb",  "walk_bl");
  clips["walk_l"]  = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_Left_NoRM.glb",    "walk_l");
  clips["walk_fl"] = AssetManager::loadAnimationClip("anim/manny/walk/am_Loco_Walk_FwdLeft_NoRM.glb",  "walk_fl");

  clips["roll_f"]  = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_F_L_Retargeted.glb",  "roll_f");
  clips["roll_fr"] = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_45_Retargeted.glb",   "roll_fr");
  clips["roll_r"]  = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_R_Retargeted.glb",    "roll_r");
  clips["roll_br"] = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_135_Retargeted.glb",  "roll_br");
  clips["roll_b"]  = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_B_L_Retargeted.glb","roll_b");
  clips["roll_bl"] = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_225_Retargeted.glb",  "roll_bl");
  clips["roll_l"]  = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_L_Retargeted.glb",    "roll_l");
  clips["roll_fl"] = AssetManager::loadAnimationClip("anim/manny/roll/A_DiveRoll_315_Retargeted.glb",  "roll_fl");

  for (const auto& key : { "attack_r1", "death", "hit",
                            "roll_f", "roll_fr", "roll_r", "roll_br",
                            "roll_b", "roll_bl", "roll_l", "roll_fl" }) {
    if (clips[key]) clips[key]->looping = false;
  }
  
  if (clips["attack_r1"]) {
    auto& clip = clips["attack_r1"];
    clip->looping = false;
    clip->events.push_back({ 14.0f/60.0f, 22.0f/60.0f,
                            forge::AnimEventType::SpawnHitbox, "weapon_r" });
    clip->events.push_back({ 10.0f/60.0f, 10.0f/60.0f,
                            forge::AnimEventType::SoundOneShot, "sfx/swing_heavy.wav" });
    std::sort(clip->events.begin(), clip->events.end(),
              [](const forge::AnimEvent& a, const forge::AnimEvent& b) { return a.startTime < b.startTime; });
  }
 
  CharacterClipSet clipSet;
  clipSet.idle       = clips["idle"];
  clipSet.walkFwd    = clips["walk"];
  clipSet.sprintFwd  = clips["sprint"];
  clipSet.attack     = clips["attack_r1"];
  clipSet.death      = clips["death"];
  clipSet.hitReaction = clips["hit"];

  clipSet.dodgeClips[(int)DodgeDir::Forward]       = clips["roll_f"];
  clipSet.dodgeClips[(int)DodgeDir::ForwardRight]  = clips["roll_fr"];
  clipSet.dodgeClips[(int)DodgeDir::Right]         = clips["roll_r"];
  clipSet.dodgeClips[(int)DodgeDir::BackwardRight] = clips["roll_br"];
  clipSet.dodgeClips[(int)DodgeDir::Backward]      = clips["roll_b"];
  clipSet.dodgeClips[(int)DodgeDir::BackwardLeft]  = clips["roll_bl"];
  clipSet.dodgeClips[(int)DodgeDir::Left]          = clips["roll_l"];
  clipSet.dodgeClips[(int)DodgeDir::ForwardLeft]   = clips["roll_fl"];

  clipSet.walkLocked[(int)DodgeDir::Forward]       = clips["walk_f"];
  clipSet.walkLocked[(int)DodgeDir::ForwardRight]  = clips["walk_fr"];
  clipSet.walkLocked[(int)DodgeDir::Right]         = clips["walk_r"];
  clipSet.walkLocked[(int)DodgeDir::BackwardRight] = clips["walk_br"];
  clipSet.walkLocked[(int)DodgeDir::Backward]      = clips["walk_b"];
  clipSet.walkLocked[(int)DodgeDir::BackwardLeft]  = clips["walk_bl"];
  clipSet.walkLocked[(int)DodgeDir::Left]          = clips["walk_l"];
  clipSet.walkLocked[(int)DodgeDir::ForwardLeft]   = clips["walk_fl"];

  auto graph = buildCharacterGraph(clipSet, "player");
  animator->setGraph(graph);

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


