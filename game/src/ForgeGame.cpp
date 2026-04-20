#include "ForgeGame.h"

#include <forge/AssetManager.h>
#include <forge/Logger.h>
#include <forge/PhysicsWorld.h>
#include <forge/AnimationClip.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/FlagManager.h>
#include <forge/DebugUI.h>
#include <forge/DebugDraw.h>
#include <forge/LuaState.h>
#include <forge/CombatSystem.h>
#include <forge/AnimGraph.h>
#include <forge/Animator.h>
#include <forge/map/GeometryGenerator.h>
#include <forge/map/MapGeometryTypes.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <sol/forward.hpp>

#include <memory>
#include <algorithm>
#include "GameFlags.h"
#include "forge/CombatComponent.h"

ForgeGame::ForgeGame()
  : forge::Application(1280, 720, "Forge Engine - Soulslike demo")
{}

void ForgeGame::onInit() {
  LOG_INFO("[Game] Initializing ForgeGame");
#ifdef __APPLE__
  forge::AssetManager::setAssetRoot("assets/");
#else
  forge::AssetManager::setAssetRoot("../../../../assets/");
#endif

  setupRenderer();

  forge::AssetManager::loadWeponDefs("data/weapons.json");
  
  setupPlayer();
  setupEnemy();

  auto staticSolidFactory = [this](const forge::LevelEntity& ent,
                                   const forge::EntityGeometry& geom,
                                   forge::PhysicsWorld& physics,
                                   forge::LuaState& lua) {
    forge::EntityInstance inst;
    inst.renderObjects = buildRenderObjects(geom);
    if (!geom.collisionPositions.empty()) {
      forge::Transform ident; // Static identity transform
      inst.rigidBody = std::make_unique<forge::RigidBodyComponent>(
        physics, ident, geom.collisionPositions, geom.collisionIndices, 0.0f
      );
    }
    return inst;
  };
  m_assembler.registerFactory("worldspawn", staticSolidFactory);
  m_assembler.registerFactory("func_wall", staticSolidFactory);
  m_assembler.registerFactory("func_group", staticSolidFactory);

  m_assembler.registerFactory("func_illusionary", [this](const forge::LevelEntity& ent,
                                                         const forge::EntityGeometry& geom,
                                                         forge::PhysicsWorld& physics,
                                                         forge::LuaState& lua) {
    forge::EntityInstance inst;
    inst.renderObjects = buildRenderObjects(geom);                          
    LOG_INFO("[Factory] Loaded func_illusionary");
    return inst;                          
  });

  m_assembler.registerFactory("trigger_once", [this](const forge::LevelEntity& ent,
                                                         const forge::EntityGeometry& geom,
                                                         forge::PhysicsWorld& physics,
                                                         forge::LuaState& lua) {
                                forge::EntityInstance inst;
                                if (!geom.collisionPositions.empty()) {
                                  std::string target = ent.getProperty("target");
                                  inst.trigger = std::make_unique<forge::TriggerVolume>(
                                    physics, geom.collisionPositions,
                                    [target]() { // onEnter
                                      LOG_INFO("[Trigger] trigger_once entered! Firing target: {}", target);
                                      forge::EventBus::publish(forge::ScriptEvent{ "triggerActivated", target });
                                    }
                                  );
                                }
                                return inst;
                              });

  // Basic point entities
  auto pointFactory = [](const forge::LevelEntity& ent,
                         const forge::EntityGeometry& geom,
                         forge::PhysicsWorld& physics,
                         forge::LuaState& lua) {
    return forge::EntityInstance();
  };
  m_assembler.registerFactory("info_player_start", pointFactory);
  m_assembler.registerFactory("enemy_spawn", pointFactory);
  m_assembler.registerFactory("bonfire", pointFactory);
  m_assembler.registerFactory("weapon_pickup", pointFactory);
  m_assembler.registerFactory("fog_gate", pointFactory);
  m_assembler.registerFactory("flag_trigger", pointFactory);
  m_assembler.registerFactory("patrol_waypoint", pointFactory);

  setupLevel("bonfire");
  setupScripts();

#ifdef __APPLE__
  m_debugLineShader = forge::AssetManager::loadShader(
  "shaders/mac/debug_line.vert", 
  "shaders/mac/debug_line.frag"
  );
  m_brushShader = forge::AssetManager::loadShader(
    "shaders/mac/brush.vert",
    "shaders/mac/brush.frag"
  );
#else
  m_debugLineShader = forge::AssetManager::loadShader(
  "shaders/win/debug_line.vert", 
  "shaders/win/debug_line.frag"
  );
  m_brushShader = forge::AssetManager::loadShader(
    "shaders/win/brush.vert",
    "shaders/win/brush.frag"
  );
#endif

  forge::DebugDraw::init(m_debugLineShader.get());

  // Try loading previous save
  getFlags().loadFromFile(k_saveFilePath);
  LOG_INFO("[Game] init complete");
}

void ForgeGame::onShutdown() {
  getFlags().saveToFile(k_saveFilePath);
  forge::AssetManager::printStats();
  forge::AssetManager::clear();
}

void ForgeGame::setupRenderer() {
  // Shader and camera
#ifdef __APPLE__
  m_shader = forge::AssetManager::loadShader(
    "shaders/mac/basic.vert",
    "shaders/mac/basic.frag"
  );
  m_skinnedShader = forge::AssetManager::loadShader("shaders/mac/skinned.vert",
                                                    "shaders/mac/basic.frag"); 
#else
  m_shader = forge::AssetManager::loadShader(
    "shaders/win/basic.vert",
    "shaders/win/basic.frag"
  );
  m_skinnedShader = forge::AssetManager::loadShader("shaders/win/skinned.vert",
                                                    "shaders/win/basic.frag"); 
#endif
  // Third person camera - positioned behind and above player
  // angled down to see the level.
  float aspect = (float)getWidth() / (float)getHeight();
  m_camera = std::make_unique<forge::Camera>(60.0f, aspect, 0.1f, 300.0f);
  m_tpCamera = std::make_unique<forge::ThirdPersonCamera>(*m_camera, getPhysics());

  // Hide and capture cursor so all mouse mvmt drives the camera
  // ImGui panels remain accessible via F-key toggles and keyboard nav
  glfwSetInputMode(getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

std::vector<forge::MapRenderObject> ForgeGame::buildRenderObjects(const forge::EntityGeometry& geom) {
  std::vector<forge::MapRenderObject> objects;
  for (const auto& surf : geom.surfaces) {
    std::vector<forge::Vertex> meshVerts;
    meshVerts.reserve(surf.vertices.size());
    for (const auto& mv : surf.vertices) {
      forge::Vertex v;
      v.position[0] = mv.position.x; v.position[1] = mv.position.y; v.position[2] = mv.position.z;
      v.normal[0] = mv.normal.x; v.normal[1] = mv.normal.y; v.normal[2] = mv.normal.z;
      v.texCoord[0] = mv.texCoord.x; v.texCoord[1] = mv.texCoord.y;
      v.tangent[0] = mv.tangent.x; v.tangent[1] = mv.tangent.y; v.tangent[2] = mv.tangent.z;
        v.tangent[3] = mv.tangent.w;
      meshVerts.push_back(v);
    }
    forge::MapRenderObject ro;
    ro.mesh = std::make_shared<forge::Mesh>(meshVerts, surf.indices);
    ro.albedo = forge::AssetManager::loadMapTexture(surf.textureName);
    ro.normalMap = forge::AssetManager::loadMapNormalTexture(surf.textureName);
    objects.push_back(ro);
  }
  return objects;
}

static std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  std::shared_ptr<forge::AnimationClip> idleClip,
  std::shared_ptr<forge::AnimationClip> walkClip, 
  std::shared_ptr<forge::AnimationClip> sprintClip, // Optional - no sprint if null
  std::shared_ptr<forge::AnimationClip> attackClip,
  std::shared_ptr<forge::AnimationClip> dodgeClip, // optional - no dodge if null
  std::shared_ptr<forge::AnimationClip> deathClip,
  const std::string& ownerName)
{
  using namespace forge;

  if (!idleClip || !walkClip || !attackClip || !deathClip) {
    LOG_ERROR("[Game] buildCharacterGraph('{}') - required clip null (need idle/walk/attack/death)", ownerName);
    return nullptr;
  }
  auto idleNode = std::make_shared<ClipNode>(idleClip, true);
  auto walkNode = std::make_shared<ClipNode>(walkClip, true); //todo walk clip
  idleNode->setOwnerName(ownerName);
  walkNode->setOwnerName(ownerName);

  auto locomotionNode = std::make_shared<Blend1DNode>("moveSpeed");
  locomotionNode->addEntry(0.0f, idleNode);
  locomotionNode->addEntry(1.0f, walkNode);

  if (sprintClip) {
    auto sprintNode = std::make_shared<ClipNode>(sprintClip, true);
    sprintNode->setOwnerName(ownerName);
    locomotionNode->addEntry(2.0f, sprintNode);
  }

  // atk
  auto attackNode = std::make_shared<ClipNode>(attackClip, false);
  attackNode->setOwnerName(ownerName);

  // death
  auto deathNode = std::make_shared<ClipNode>(deathClip, false);
  deathNode->setOwnerName(ownerName);

  // graph root
  auto root = std::make_shared<StateMachineNode>();
  root->addState("Locomotion", locomotionNode);
  root->addState("Attacking", attackNode);
  root->addState("Dead", deathNode);

  // Locomotion-> attacking
  root->addTransition({
    "Locomotion", "Attacking",
    [](AnimParamTable& p) {
      return p.consumeTrigger("attackR1") || p.consumeTrigger("attackR2");
    },
    0.1f
  });

  // attacking -> locomotion
  root->addTransition({
    "Attacking", "Locomotion",
    [attackNode](AnimParamTable&) {
      return attackNode->isFinished();
    },
    0.2f
  });

  if (dodgeClip) {
    auto dodgeNode = std::make_shared<ClipNode>(dodgeClip, false);
    dodgeNode->setOwnerName(ownerName);
    root->addState("Dodging", dodgeNode);

    root->addTransition({
      "Locomotion", "Dodging",
      [](AnimParamTable& p) { return p.consumeTrigger("dodge"); },
      0.05f
    });

    root->addTransition({
      "Dodging", "Locomotion",
      [dodgeNode](AnimParamTable&) { return dodgeNode->isFinished(); },
      0.15f 
    });
  }

  // Any -> dead
  root->addTransition({
    "", "Dead",
    [](AnimParamTable& p) { return p.getBool("isDead"); },
    0.3f
  });

  root->setInitialState("Locomotion");
  return root;
}

void ForgeGame::setupPlayer() {
  m_player.skinnedModel = forge::AssetManager::loadSkinnedModel("models/characters/anim/ybot_idle.glb"); 
  m_player.transform = std::make_unique<forge::Transform>();
  m_player.transform->setPosition({ 0.0f, 0.0f, 0.0f });
  m_player.transform->setScale({ 0.01f, 0.01f, 0.01f });
  m_player.transform->setEulerAngles({ 90.0f, 180.0f, 0.0f });

  m_player.controller = std::make_unique<forge::CharacterController>(getPhysics(), *m_player.transform, 0.3f, 0.9f);

  // Animator
  m_player.animator = std::make_unique<forge::Animator>();
  m_player.animator->setOwnerName("player");
  m_player.animator->setSkeleton(m_player.skinnedModel.skeleton);

  // Load anim clips
  // Idle is embadded in the base model - load the first animation from it
  m_player.clips["idle"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_idle.glb", "idle");
  m_player.clips["walk"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_walk.glb", "walk");
  m_player.clips["sprint"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_sprint.glb", "sprint");
  m_player.clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash");
  m_player.clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death");
  m_player.clips["dodge"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_dodge.glb", "dodge");
  
  if (m_player.clips["attack_r1"]) {
    auto& clip = m_player.clips["attack_r1"];
    clip->looping = false;
    clip->events.push_back({ 14.0f/60.0f, 22.0f/60.0f,
                            forge::AnimEventType::SpawnHitbox, "weapon_r" });
    clip->events.push_back({ 10.0f/60.0f, 10.0f/60.0f,
                            forge::AnimEventType::SoundOneShot, "sfx/swing_heavy.wav" });
    std::sort(clip->events.begin(), clip->events.end(),
              [](const forge::AnimEvent& a, const forge::AnimEvent& b) { return a.startTime < b.startTime; });
  }
 
  auto& dodgeClip = m_player.clips["dodge"];
  if (!dodgeClip) {
    // irrelevant dodge clip is present. regardless
    dodgeClip = m_player.clips["idle"];
    LOG_WARN("[Game] ybot_dodge.glb not found - using idle as dodge stub.");
  }
  if (dodgeClip) {
    dodgeClip->looping = false;
    if (dodgeClip->duration < 0.6f || m_player.clips.count("dodge")) {
      dodgeClip->events.push_back({ 0.08f, 0.38f,
                                  forge::AnimEventType::IFrame, "dodge_iframe" });
      std::sort(dodgeClip->events.begin(), dodgeClip->events.end(),
                [](const forge::AnimEvent& a, const forge::AnimEvent& b){ return a.startTime < b.startTime; });
    }
    m_dodgeDuration = dodgeClip->duration;
  }

  if (m_player.clips["death"])
    m_player.clips["death"]->looping = false;

  auto graph = buildCharacterGraph(
    m_player.clips["idle"], 
    m_player.clips["walk"],
    m_player.clips["sprint"],
    m_player.clips["attack_r1"], 
    m_player.clips["dodge"],
    m_player.clips["death"], 
    "player"
  );
  m_player.animator->setGraph(graph);

  m_player.combat = std::make_unique<forge::CombatComponent>(
    "Player",
    500.0f,
    100.0f,
    80.0f
  );

  m_player.combat->setAnimator(m_player.animator.get());
  m_player.combat->setParamTable(&m_player.animator->getParams());

  // Wire up lua for player
  m_player.combat->onDeath = [this](){
    getLua().callFunction("onPlayerDeath");
  };
  m_player.combat->onHit = [this](const forge::HitEvent& h) {
    getLua().callFunction("onPlayerHit", h.damage, h.damageType);
  };

  m_player.equipment = std::make_unique<forge::EquipmentComponent>("player");

  // CombatComponent onEquip
  m_player.equipment->onEquip = [this](forge::EquipmentComponent::Slot slot, const forge::WeaponDef* def) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_player.combat->setWeapon(def);
      if (!def->meshPath.empty())
        m_player.weaponModel = forge::AssetManager::loadModel(def->meshPath);
      getLua().callFunction("onPlayerEquip", static_cast<int>(slot), def->id);
    }
  };

  m_player.equipment->onUnequip = [this](forge::EquipmentComponent::Slot slot) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_player.combat->setWeapon(nullptr);
      m_player.weaponModel = forge::ModelData{};
      getLua().callFunction("onPlayerUnequip", static_cast<int>(slot));
    }
  };

  getCombat().registerCombatant(m_player.combat.get());
  getLua().get()["playerCombat"] = m_player.combat.get();
  getLua().get()["playerTransform"] = m_player.transform.get();
  getLua().get()["playerAnim"] = &m_player.animator->getParams();
  getLua().get()["playerEquipment"] = m_player.equipment.get();

  getDebugUI().registerAnimator(m_player.animator.get(), "Player");

  LOG_INFO("[ForgeGame] Player ready - skeleton: {} bones",
           m_player.skinnedModel.skeleton.size());
}

void ForgeGame::setupEnemy() {
  // Reuse the same Y Bot mesh — different transform, same skeleton
  m_enemy.skinnedModel = forge::AssetManager::loadSkinnedModel(
    "models/characters/anim/ybot_idle.glb");
 
  constexpr float k_enemyCapsuleHalfHeight = 0.75f;
  m_enemy.transform = std::make_unique<forge::Transform>();
  m_enemy.transform->setPosition({ 0.0f, 0.0f, -4.0f });
  //m_enemy.transform->setPosition({ 0.0f, k_enemyCapsuleHalfHeight, -4.0f });
  m_enemy.transform->setScale({ 0.01f, 0.01f, 0.01f });
  m_enemy.transform->setEulerAngles({ 90.0f, 0.0f, 0.0f });
 
  m_enemy.controller = std::make_unique<forge::CharacterController>(
    getPhysics(), *m_enemy.transform, 0.3f, 0.9f);

  // ── Animator ─────────────────────────────────────────────────────────
  m_enemy.animator = std::make_unique<forge::Animator>();
  m_enemy.animator->setOwnerName("enemy");
  m_enemy.animator->setSkeleton(m_enemy.skinnedModel.skeleton);
 
  // Share clips with the player — same file, same data
  m_enemy.clips["idle"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_idle.glb", "idle"); 
  m_enemy.clips["walk"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_walk.glb", "walk"); 
  m_enemy.clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash"); 
  m_enemy.clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death"); 

  auto graph = buildCharacterGraph(
    m_enemy.clips["idle"], 
    m_enemy.clips["walk"],
    nullptr,
    m_enemy.clips["attack_r1"], 
    nullptr,
    m_enemy.clips["death"], 
    "enemy"
  );
  m_enemy.animator->setGraph(graph);
  // ── Combat ───────────────────────────────────────────────────────────
  m_enemy.combat = std::make_unique<forge::CombatComponent>(
    "enemy", 400.0f, 100.0f, 60.0f);
 
  m_enemy.combat->setAnimator(m_enemy.animator.get());
  m_enemy.combat->setParamTable(&m_enemy.animator->getParams());

  m_enemy.combat->onDeath = [this]() {
    m_enemy.animator->getParams().setBool("isDead", true);
    getLua().callFunction("onEnemyDeath");
  };
  
  m_enemy.combat->onHit = [this](const forge::HitEvent& h) {
    getLua().callFunction("onEnemyHit", h.damage, h.damageType);
  };

  // ── AI ────────────────────────────────────────────────────────────────
  m_enemy.ai = std::make_unique<forge::AIComponent>(
    "enemy", *m_enemy.transform, *m_enemy.combat);

  m_enemy.equipment = std::make_unique<forge::EquipmentComponent>("enemy");
 
  m_enemy.equipment->onEquip = [this](forge::EquipmentComponent::Slot slot, const forge::WeaponDef* def) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_enemy.combat->setWeapon(def);
      if (!def->meshPath.empty())
        m_enemy.weaponModel = forge::AssetManager::loadModel(def->meshPath);
      getLua().callFunction("onEnemyEquip", static_cast<int>(slot), def->id);
    }
  };

  m_enemy.equipment->onUnequip = [this](forge::EquipmentComponent::Slot slot) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_enemy.combat->setWeapon(nullptr);
      m_enemy.weaponModel = forge::ModelData{};
      getLua().callFunction("onEnemyUnequip", static_cast<int>(slot));
    }
  };

  getCombat().registerCombatant(m_enemy.combat.get());
  getDebugUI().registerAnimator(m_enemy.animator.get(), "Enemy");
  getDebugUI().registerAIComponent(m_enemy.ai.get());

  getLua().get()["enemyCombat"] = m_enemy.combat.get();
  getLua().get()["enemyTransform"] = m_enemy.transform.get();
  getLua().get()["enemyAnim"] = &m_enemy.animator->getParams();
  getLua().get()["enemyAI"] = m_enemy.ai.get();
  getLua().get()["enemyEquipment"] = m_enemy.equipment.get();

  LOG_INFO("[ForgeGame] Enemy ready");
}

void ForgeGame::setupLevel(const std::string& levelName) {
  const std::string root = forge::AssetManager::getAssetRoot();
  const std::string mapPath = root + "levels/" + levelName + ".map";

  m_levelData = forge::LevelLoader::load(mapPath);
  m_enemy.active = false;

  if (!m_levelData.valid) {
    throw std::runtime_error("[Game] Failed to load level map: " + mapPath);
  }

  m_mapScene = std::make_unique<forge::MapScene>();
  m_mapEntities.clear(); // Clear entities from previous level

  forge::GeometryGenerator gen;
  forge::GeometrySettings settings;

  // The master assembly loop
  for (const auto& ent : m_levelData.entities) {
    if (!m_assembler.hasFactory(ent.classname)) {
      if (ent.classname != "worldspawn" && ent.classname.find("func_") != 0) {
        LOG_WARN("[Game] No factory registered for entity: {}", ent.classname);
      }
      continue;
    }

    // 1. Generate geometry
    forge::EntityGeometry geom;
    if (!ent.brushes.empty()) {
      geom = gen.processEntity(ent, settings);
    }

    // 2. Assemble the Entity (Render objects + Phys + Triggers)
    forge::EntityInstance inst = m_assembler.assemble(ent,
                                                      geom,
                                                      getPhysics(),
                                                      getLua());
    // 3. Push render objects to the MapScene so they're drawn
    for (auto& ro : inst.renderObjects)
      m_mapScene->renderObjects.push_back(ro);

    // 4. Game specific Routing (point entities)
    if (inst.classname == "info_player_start") {
      glm::vec3 origin = inst.origin;
      constexpr float k_playerCapsuleHalfHeight = 0.9f;
      origin.y += k_playerCapsuleHalfHeight;
      m_player.controller->warp(origin);
      if (ent.angle != 0.0f) {
        float rad = glm::radians(ent.angle);
        m_player.forward = glm::normalize(glm::vec3(sinf(rad), 0.0f, cosf(rad)));
        m_player.transform->setEulerAngles({ 90.0f, ent.angle, 0.0f });
      }
      LOG_INFO("[Game] Player spawned at {:.2f},{:.2f},{:.2f}", origin.x, origin.y, origin.z);
      if (m_player.equipment) {
        m_player.equipment->equip(forge::EquipmentComponent::RIGHT_HAND,
                                  "longsword");
      }
    } else if (inst.classname == "enemy_spawn") {
      m_enemy.active = true;
      glm::vec3 spawnPos = inst.origin;

      constexpr float k_capsuleHalfHeight = 0.75f;
      constexpr float k_rayStart = 2.0f;
      constexpr float k_rayLength = 10.0f;

      glm::vec3 rayFrom = spawnPos + glm::vec3(0.0f, k_rayStart, 0.0f);
      glm::vec3 rayTo = spawnPos + glm::vec3(0.0f, -k_rayLength, 0.0f);

      forge::RaycastHit hit = getPhysics().raycast(rayFrom, rayTo);
      if (hit.hit) {
        spawnPos.y = hit.point.y + k_capsuleHalfHeight;
      } else {
        spawnPos.y -= k_capsuleHalfHeight;
      }
      m_enemy.controller->warp(spawnPos);

      // Patrol waypoints
      std::string group = ent.getProperty("patrol_group", "");
      if (!group.empty()) {
        m_enemy.ai->clearWaypoints();
        for (const auto* wp : m_levelData.getByClass("patrol_waypoint")) {
          if (wp->props.count("patrol_group") && wp->props.at("patrol_group") == group)
            m_enemy.ai->addWaypoint(wp->origin);
        }
      }

      // Equipment/Weapons
      std::string wepId = ent.getProperty("weaponId", "");
      bool dropOwn = (ent.getProperty("dropWeapon", "1") == "1");
      std::string otherWepId = !dropOwn ? ent.getProperty("dropId", "") : "";

      if (!dropOwn && !otherWepId.empty()) {
        m_enemy.ai->setWeaponConfig(wepId, false, otherWepId);
      } else {
        m_enemy.ai->setWeaponConfig(wepId, dropOwn);
      }

      if (!wepId.empty())
        m_enemy.equipment->equip(forge::EquipmentComponent::RIGHT_HAND, wepId);

      auto existingOnDeath = std::move(m_enemy.combat->onDeath);
      m_enemy.combat->onDeath = [this, existingOnDeath]() {
        if (existingOnDeath) existingOnDeath();
        if (m_enemy.ai->shouldDropWeapon()) {
          glm::vec3 dropPos = m_enemy.transform->getPosition();
          if (m_enemy.ai->dropsOwnWeapon()) {
            spawnWeaponPickup(dropPos, m_enemy.ai->getWeaponId(), false);
          } else if (!m_enemy.ai->getDropId().empty()) {
            spawnWeaponPickup(dropPos, m_enemy.ai->getDropId(), false);
          }
        }
      };
      LOG_INFO("[Game] Enemy spawned at {:.2f},{:.2f},{:.2f}", spawnPos.x, spawnPos.y, spawnPos.z);
    } else if (inst.classname == "bonfire") {
      BonfireVolume bf;
      bf.bonfireId = ent.getInt("bonfire_id", 0);
      bf.targetFlag = ent.getInt("targetFlag", 0);
      float radius = ent.getFloat("radius", 1.5f);

      bf.trigger = std::make_unique<forge::TriggerVolume>(getPhysics(), inst.origin, radius);
      m_bonfires.push_back(std::move(bf));
    } else if (inst.classname == "fog_gate") {
      glm::vec3 pos = inst.origin;
      int requiredFlag = ent.getInt("requiredFlag", 0);
      float width = ent.getFloat("width", 2.0f);
      float height = ent.getFloat("height", 3.0f);
      glm::vec3 halfExtents(width * 0.5f, height * 0.5f, 0.3f);

      auto vol = std::make_unique<forge::TriggerVolume>(
          getPhysics(), pos, halfExtents,
          [this, requiredFlag, pos]() {
              if (requiredFlag == 0 || getFlags().get(requiredFlag)) return;

              glm::vec3 playerPos = m_player.transform->getPosition();
              glm::vec3 pushBack = glm::normalize(playerPos - pos) * 2.0f;
              m_player.controller->warp(playerPos + pushBack);

              forge::EventBus::publish(forge::ScriptEvent{ "fogGateLocked", "" });
          }
      );
      m_triggerVolumes.push_back(std::move(vol));
    } else if (inst.classname == "weapon_pickup") {
      std::string wepId = ent.getProperty("weaponId", "");
      bool respawns = ent.getBool("respawns", false);
      if (!wepId.empty())
        spawnWeaponPickup(inst.origin, wepId, respawns);
    }

    m_mapEntities.push_back(std::move(inst));
  }

  LOG_INFO("[Game] Map assembly complete: {} entities active", m_mapEntities.size());
}

void ForgeGame::setupScripts() {
  // Load order matters, flags before quests, attacks before combat
  getLua().get()["Flags"] = &getFlags();

  const std::string root = forge::AssetManager::getAssetRoot();
  getLua().loadScript(root + "scripts/events/flags.lua");
  getLua().loadScript(root + "scripts/combat/attacks.lua");
  getLua().loadScript(root + "scripts/equipment.lua");
  getLua().loadScript(root + "scripts/combat/player_combat.lua");
  getLua().loadScript(root + "scripts/ai/enemy_ai.lua");
  getLua().loadScript(root + "scripts/events/demo_quest.lua");

  getLua().get()["loadLevel"] = [this](const std::string& name) {
    m_levelPhysicsBody.reset();
    m_levelTransform.reset();
    setupLevel(name);
  };
  getLua().get().set_function("setPlayerWalkSpeed",
                              [this](float s){ m_playerWalkSpeed = s; });
  getLua().get().set_function("setPlayerSprintSpeed",
                              [this](float s){ m_playerSprintSpeed = s; });
  getLua().get().set_function("getPlayerWalkSpeed",
                              [this]() -> float { return m_playerWalkSpeed; });
  getLua().get().set_function("getPlayerSprintSpeed",
                              [this]() -> float { return m_playerSprintSpeed; });

  getLua().callFunction("onAIInit");
}

void ForgeGame::onUpdate(float dt) {
  handleInput(dt);

  // Physics
  getPhysics().step(dt);

  for (auto& vol : m_triggerVolumes) {
    vol->update();
  }

  for (auto& bf : m_bonfires) {
    bf.trigger->update();
  }

  // Update factory assembled triggers
  for (auto& inst : m_mapEntities) {
    if (inst.trigger)
      inst.trigger->update();
  }

  // Bonfire interaction
  if (isKeyPressed(GLFW_KEY_B)) {
    for (auto& bf : m_bonfires) {
      if (bf.trigger->isOverlapping()) {
        m_player.combat->healToFull();
        if (bf.targetFlag != 0)
          getFlags().set(bf.targetFlag, true);
        getFlags().saveToFile(k_saveFilePath);
        forge::EventBus::publish(forge::RestEvent{ bf.bonfireId });
        getLua().callFunction("onBonfireRest", bf.bonfireId);
        LOG_INFO("[Level] Bonfire {} rest — player healed, flags saved", bf.bonfireId);
        break;
      }
    }
  }

  for (auto& pk : m_weaponPickups) {
    if (!pk.collected) pk.trigger->update();
  }

  if (isKeyPressed(GLFW_KEY_E)) {
    for (auto& pk : m_weaponPickups) {
      if (!pk.collected && pk.trigger->isOverlapping()) {
        m_player.equipment->equip(forge::EquipmentComponent::RIGHT_HAND, pk.weaponId);
        if (!pk.respawns) {
          pk.collected = true;
          pk.trigger->setEnabled(false);
        }
        LOG_INFO("[Level] Player picked up '{}'", pk.weaponId);
        break;
      }
    }
  }

  if (m_player.equipment) {
    m_player.equipment->update(
      m_player.transform->getModelMatrix(),
      m_player.animator->getGlobalTransforms(),
      m_player.skinnedModel);
  }
  m_player.controller->syncTransform();

  m_tpCamera->update(m_player.transform->getPosition());

  glm::vec3 playerPos = m_player.transform->getPosition();
  m_player.combat->setWorldData(playerPos, m_player.forward);
  m_player.animator->update(dt);

  if (m_enemy.active) {
    if (m_enemy.equipment && m_enemy.combat->isAlive()) {
      m_enemy.equipment->update(
        m_enemy.transform->getModelMatrix(),
        m_enemy.animator->getGlobalTransforms(),
        m_enemy.skinnedModel);
    }

    m_enemy.controller->syncTransform();

    if (m_lockedOn) {
      m_lockOnEnemyPos = m_enemy.transform->getPosition() + glm::vec3(0.0f, 1.0f, 0.0f);
      if (!m_enemy.combat->isAlive()) {
        m_lockedOn = false;
        m_tpCamera->setLockOnTarget(nullptr);
        LOG_INFO("[Game] Lock-on released - enemy dead");
      }
    }

    glm::vec3 preMovePos = m_enemy.transform->getPosition();

    m_enemy.ai->update(dt, playerPos);

    glm::vec3 postMovePos = m_enemy.transform->getPosition();
    glm::vec3 displacement = postMovePos - preMovePos;
    displacement.y = 0.0f;
    m_enemy.transform->setPosition(preMovePos);
    m_enemy.controller->setWalkDirection(displacement);

    float enemyMoveSpeed = (glm::length(displacement) > 0.0001f) ? 1.0f : 0.0f;
    m_enemy.animator->getParams().setFloat("moveSpeed", enemyMoveSpeed);

    m_enemy.animator->update(dt);
  }
  // Input -> Lua
  bool j = isKeyDown(GLFW_KEY_J);
  bool k = isKeyDown(GLFW_KEY_K);
  bool l = isKeyDown(GLFW_KEY_L);
  auto inputTable = getLua().get().create_table();
  inputTable["attackLight"] = j && !m_prevInput.j;
  inputTable["attackHeavy"] = k && !m_prevInput.k;
  inputTable["guard"] = l;
  m_prevInput = { j, k, l };

  getLua().callFunction("onCombatUpdate", dt, inputTable);
  getCombat().update(dt);
  getLua().callFunction("onQuestUpdate", dt);

  if (isKeyPressed(GLFW_KEY_B))
    getLua().callFunction("onBonfireReset");
  if (isKeyPressed(GLFW_KEY_F9))
    getLua().loadScript(forge::AssetManager::getAssetRoot() + "scripts/combat/player_combat.lua");
}

void ForgeGame::onRender() {
  m_shader->bind();

  // Draw pickups
  for (const auto& pk : m_weaponPickups) {
    if (!pk.collected && pk.model.hasRenderData())
      drawModelAtMatrix(pk.model, pk.transform->getModelMatrix());
  }

  // Draw player weapon
  if (m_player.equipment && m_player.equipment->hasWeapon(forge::EquipmentComponent::RIGHT_HAND))
    drawModelAtMatrix(m_player.weaponModel, 
                      m_player.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND));

  // Draw enemy weapon
  if (m_enemy.active && m_enemy.equipment && m_enemy.combat->isAlive()
    && m_enemy.equipment->hasWeapon(forge::EquipmentComponent::RIGHT_HAND)) {
    drawModelAtMatrix(m_enemy.weaponModel,
                      m_enemy.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND));
  }
  m_shader->unbind();

  // Draw Native Map
  if (m_mapScene) {
    m_mapScene->render(*m_camera, m_brushShader.get());
  }

  m_skinnedShader->bind();
  drawSkinnedModel(m_player.skinnedModel, *m_player.transform, *m_player.animator);

  if (m_enemy.active) {
    forge::Transform enemyVisualTransform;
    enemyVisualTransform.setPosition(m_enemy.transform->getPosition());
    enemyVisualTransform.setScale(m_enemy.transform->getScale());
    enemyVisualTransform.setRotation(m_enemy.transform->getRotation());

    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f, 0.7f, 0.7f));
    drawSkinnedModel(m_enemy.skinnedModel, enemyVisualTransform, *m_enemy.animator);
    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));
  } else {
    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));
  }

  m_skinnedShader->unbind();

  // Physics debug
  if (getDebugUI().isPhysicsDebugEnabled()) {
    // Player capsule - CharacterController gives the true phys center
    // via  getCapsuleCenter(), separate from the foot-position transform
    forge::DebugDraw::capsule(
      m_player.controller->getCapsuleCenter(), 
      m_player.controller->getRadius(), 
      m_player.controller->getCylinderHalfHeight(),
      { 0.0f, 1.0f, 0.0f } // green
    );

    if (m_enemy.active) {
      constexpr float kR = 0.3f;
      constexpr float kH = 0.45f;
      forge::DebugDraw::capsule(
        m_enemy.controller->getCapsuleCenter(), 
        kR, kH,
        { 1.0f, 0.3f, 0.3f }
      );
    }

    forge::DebugDraw::flush(m_camera->getViewProjection());
  }
}

void ForgeGame::drawModel(const forge::ModelData& model,
                          const forge::Transform& transform)
{
  if (!model.mesh) return;

  glm::mat4 modelMat = transform.getModelMatrix();
  glm::mat4 mvp      = m_camera->getViewProjection() * modelMat;

  m_shader->setMat4("u_mvp",   mvp);
  m_shader->setMat4("u_model", modelMat);
  m_shader->setVec3("u_tint",  glm::vec3(1.0f));  // White tint = no tint

  if (!model.subMeshes.empty()) {
    for (const auto& sub : model.subMeshes) {
      if (!sub.mesh) continue;

      if (sub.texture) {
        sub.texture->bind(0);

        m_shader->setInt("u_texture", 0);
        m_shader->setBool("u_hasTexture", true);
      } else {
        m_shader->setBool("u_hasTexture", false);
      }

      sub.mesh->draw();
    }
  } else {
    if (model.hasTexture()) {
        model.texture->bind(0);           // Bind to texture unit 0
        m_shader->setInt ("u_texture",    0);
        m_shader->setBool("u_hasTexture", true);
    } else {
        m_shader->setBool("u_hasTexture", false);
    }

    model.mesh->draw();
  }
}


// Draw Skinned Model
void ForgeGame::drawSkinnedModel(const forge::SkinnedModelData& model,
                                 const forge::Transform& transform,
                                 const forge::Animator& animator)
{
  if (!model.mesh) return;

  glm::mat4 modelMat = transform.getModelMatrix();
  glm::mat4 mvp = m_camera->getViewProjection() * modelMat;

  m_skinnedShader->setMat4("u_mvp", mvp);
  m_skinnedShader->setMat4("u_model", modelMat);
  //m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));

  // Upload bone palette - the heart of skinning
  const auto& bones = animator.getBoneMatrices();
  if (!bones.empty()) {
    m_skinnedShader->setBool("u_hasBones", true);
    m_skinnedShader->setMat4Array(
      "u_boneMatrices",
      static_cast<int>(std::min(bones.size(), (size_t)forge::MAX_BONES)),
      bones.data()
    );
  } else {
    m_skinnedShader->setBool("u_hasBones", false);
  }

  if (model.hasTexture()) {
    model.texture->bind(0);
    m_skinnedShader->setInt("u_texture", 0);
    m_skinnedShader->setBool("u_hasTexture", true);
  } else {
    m_skinnedShader->setBool("u_hasTexture", false);
  }

  model.mesh->draw();
}

// Input - full rewrite
//
// Responsibilities:
//    - Mouse delta -> third person camera yaw/pitch
//    - Tab -> toggle lock-on to nearest enemy
//    - WASD -> Camera relative movement (no lock on)
//          -> strafe-relative movement (locked on)
//    - Shift -> sprint (moveSpeed param > 1.0 drives Blend1DNode)
//    - Space -> Dodge roll (AnimGraph trigger + timed velocity override)
//
// Writes CharacterController:setWalkDirection(velocity * dt) each frame.
// Never sets transform.position directly, controller owns X&Z and bullet owns Y.

void ForgeGame::handleInput(float dt) {
  if (isKeyPressed(GLFW_KEY_GRAVE_ACCENT)) {
    m_uiMouseMode = !m_uiMouseMode;
    glfwSetInputMode(getWindow(), GLFW_CURSOR,
                     m_uiMouseMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    if (!m_uiMouseMode)
      m_firstMouse = true;
    LOG_INFO("[Game] UI Mouse Mode: {}", m_uiMouseMode ? "ON" : "OFF");
  }
  if (!m_player.combat->isAlive()) {
    m_player.controller->setWalkDirection({ 0.0f, 0.0f, 0.0f });
    return;
  }

  // Mouse delta
  double mx, my;
  glfwGetCursorPos(getWindow(), &mx, &my);

  if (!m_firstMouse && !m_uiMouseMode && !getDebugUI().isCapturingMouse()) {
    float dx = static_cast<float>(mx - m_prevMouseX);
    float dy = static_cast<float>(my - m_prevMouseY);
    m_tpCamera->applyMouseDelta(dx, dy);
  }
  m_firstMouse = false;
  m_prevMouseX = mx;
  m_prevMouseY = my;

  // Tab: Toggle lock on
  if (isKeyPressed(GLFW_KEY_TAB)) {
    if (m_lockedOn) {
      m_lockedOn = false;
      m_tpCamera->setLockOnTarget(nullptr);
      LOG_INFO("[Game] Lock-on released");
    } else {
      glm::vec3 pp = m_player.transform->getPosition();
      glm::vec3 ep = m_enemy.transform->getPosition();
      float dist = glm::length(ep - pp);
      if (dist < 15.0f && m_enemy.combat->isAlive()) {
        m_lockedOn = true;
        m_lockOnEnemyPos = ep + glm::vec3(0.0f, 1.0f, 0.0f);
        m_tpCamera->setLockOnTarget(&m_lockOnEnemyPos);
        LOG_INFO("[Game] Locked on to enemy at dist={:.1f}m", dist);
      }
    }
  }

  glm::vec3   move  = { 0, 0, 0 };

  if (!m_lockedOn) {
    glm::vec3 camFwd = m_tpCamera->getHorizontalForward();
    glm::vec3 camRight = m_tpCamera->getHorizontalRight();

    if (isKeyDown(GLFW_KEY_W)) move += camFwd;
    if (isKeyDown(GLFW_KEY_S)) move -= camFwd;
    if (isKeyDown(GLFW_KEY_A)) move -= camRight;
    if (isKeyDown(GLFW_KEY_D)) move += camRight;
  } else {
    // Lock-on: strafe around locked enemy
    glm::vec3 pp = m_player.transform->getPosition();
    glm::vec3 toTarget = m_lockOnEnemyPos - pp;
    toTarget.y = 0.0f;
    if (glm::length(toTarget) > 0.001f) toTarget = glm::normalize(toTarget);
    glm::vec3 strafeRight = glm::normalize(glm::cross(toTarget, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (isKeyDown(GLFW_KEY_W)) move += toTarget;
    if (isKeyDown(GLFW_KEY_S)) move -= toTarget;
    if (isKeyDown(GLFW_KEY_A)) move -= strafeRight;
    if (isKeyDown(GLFW_KEY_D)) move += strafeRight;
  }

  // Sprint
  bool sprinting = isKeyDown(GLFW_KEY_LEFT_SHIFT);
  float walkSpeed = m_playerWalkSpeed;
  float sprintSpeed = m_playerSprintSpeed;
  float speed = sprinting ? sprintSpeed : walkSpeed;

  // Dodge
  bool currentlyDodging = (m_dodgeTimer > 0.0f);

  if (isKeyPressed(GLFW_KEY_SPACE)&& !currentlyDodging && !m_player.combat->isAttacking()) {
    // Lock-in the dodge direction: movement dir if moving else player forward
    m_dodgeDir = (glm::length(move) > 0.001f)
      ? glm::normalize(move)
      : m_player.forward;

    m_dodgeTimer = m_dodgeDuration;
    m_player.animator->getParams().setTrigger("dodge");
    LOG_INFO("[Game] Dodge initiated");
  }

  // Final velocity
  glm::vec3 velocity = { 0.0f, 0.0f, 0.0f };

  if (m_dodgeTimer > 0.0f) {
    m_dodgeTimer -= dt;
    float t = m_dodgeTimer / m_dodgeDuration;
    float dodgeSpeed = 10.0f * glm::smoothstep(0.0f, 0.25f, t); 
    //float dodgeSpeed = glm::mix(5.0f, 10.0f, t); // starts fast, eases out
    velocity = m_dodgeDir * dodgeSpeed;
  } else if (glm::length(move) > 0.001f) {
    move = glm::normalize(move);
    velocity = move * speed;

    glm::vec3 faceDir;
    if (m_lockedOn) {
      // face towards locked enemy regardless of mvmt dir
      faceDir = m_lockOnEnemyPos - m_player.transform->getPosition();
    } else {
      faceDir = move;
    }
    faceDir.y = 0.0f;
    if (glm::length(faceDir) > 0.001f) {
      float angle = atan2f(faceDir.x, faceDir.z);
      m_player.transform->setEulerAngles({ 90.0f, glm::degrees(angle), 0.0f });
      m_player.forward = glm::normalize(glm::vec3(faceDir.x, 0.0f, faceDir.z));
    }

    float moveSpeedParam = sprinting ? 2.0f : (glm::length(velocity) / walkSpeed);
    m_player.animator->getParams().setFloat("moveSpeed", moveSpeedParam);
  } else {
    m_player.animator->getParams().setFloat("moveSpeed", 0.0f);

    if (m_lockedOn) {
      glm::vec3 faceDir = m_lockOnEnemyPos - m_player.transform->getPosition();
      faceDir.y = 0.0f;
      if (glm::length(faceDir) > 0.001f) {
        float angle = atan2f(faceDir.x, faceDir.z);
        m_player.transform->setEulerAngles({ 90.0f, glm::degrees(angle), 0.0f });
        m_player.forward = glm::normalize(glm::vec3(faceDir.x, 0.0f, faceDir.z));
      }
    }
  }

  constexpr float physStep = 1.0f / 60.0f;
  m_player.controller->setWalkDirection(velocity * physStep);

  m_player.combat->setWorldData(m_player.transform->getPosition(), m_player.forward);
}

void ForgeGame::spawnWeaponPickup(const glm::vec3& pos, const std::string& weaponId, bool respawns) {
  const forge::WeaponDef* def = forge::AssetManager::getWeaponDef(weaponId);
  if (!def) {
    LOG_ERROR("[Level] spawnWeaponPickup: unknown weaponId '{}'", weaponId);
    return;
  }

  WeaponPickup pickup;
  pickup.weaponId = weaponId;
  pickup.respawns = respawns;

  pickup.transform = std::make_unique<forge::Transform>();
  pickup.transform->setPosition(pos);

  // Load weapon mesh
  if (!def->meshPath.empty())
    pickup.model = forge::AssetManager::loadModel(def->meshPath);

  pickup.trigger = std::make_unique<forge::TriggerVolume>(getPhysics(), pos, 1.0f);

  m_weaponPickups.push_back(std::move(pickup));
  LOG_INFO("[Level] Spawned '{}' pickup at ({:.1f},{:.1f},{:.1f})",
           weaponId, pos.x, pos.y, pos.z);
}

void ForgeGame::drawModelAtMatrix(const forge::ModelData& model, const glm::mat4& matrix) {
  if (!model.hasRenderData()) return;

  glm::mat4 mvp = m_camera->getViewProjection() * matrix;
  m_shader->setMat4("u_mvp", mvp);
  m_shader->setMat4("u_model", matrix);
  m_shader->setVec3("u_tint", glm::vec3(1.0f));

  for (const auto& sub : model.subMeshes) {
    if (sub.texture) {
      sub.texture->bind(0);
      m_shader->setInt("u_texture", 0);
      m_shader->setBool("u_hasTexture", true);
    } else {
      m_shader->setBool("u_hasTexture", false);
    }
    sub.mesh->draw();
  }
}
