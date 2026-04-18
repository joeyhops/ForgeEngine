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
  setupPlayer();
  setupEnemy();
  setupLevel();
  setupScripts();

#ifdef __APPLE__
  m_debugLineShader = forge::AssetManager::loadShader(
  "shaders/mac/debug_line.vert", 
  "shaders/mac/debug_line.frag"
  );
#else
  m_debugLineShader = forge::AssetManager::loadShader(
  "shaders/win/debug_line.vert", 
  "shaders/win/debug_line.frag"
  );
#endif

  forge::DebugDraw::init(m_debugLineShader.get());

  // Try loading previous save
  getFlags().loadFromFile("save.json");
  LOG_INFO("[Game] init complete");
}

void ForgeGame::onShutdown() {
  getFlags().saveToFile("save.json");
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

static std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  std::shared_ptr<forge::AnimationClip> idleClip,
  std::shared_ptr<forge::AnimationClip> walkClip, // nullptr -> use idle
  std::shared_ptr<forge::AnimationClip> sprintClip, // nullptr -> use idle
  std::shared_ptr<forge::AnimationClip> attackClip,
  std::shared_ptr<forge::AnimationClip> dodgeClip,
  std::shared_ptr<forge::AnimationClip> deathClip,
  const std::string& ownerName)
{
  using namespace forge;

  // Fall back to idle for missing walk/sprint clips
  auto effectiveWalk = walkClip ? walkClip : idleClip;
  auto effectiveSprint = sprintClip ? sprintClip : idleClip;

  auto idleNode = std::make_shared<ClipNode>(idleClip, true);
  auto walkNode = std::make_shared<ClipNode>(effectiveWalk, true); //todo walk clip
  auto sprintNode = std::make_shared<ClipNode>(effectiveSprint, true); //todo walk clip
  idleNode->setOwnerName(ownerName);
  walkNode->setOwnerName(ownerName);
  sprintNode->setOwnerName(ownerName);

  auto locomotionNode = std::make_shared<Blend1DNode>("moveSpeed", idleNode, walkNode);
  locomotionNode->addEntry(2.0f, sprintNode);

  // atk
  auto attackNode = std::make_shared<ClipNode>(attackClip, false);
  attackNode->setOwnerName(ownerName);

  auto actualDodgeClip = dodgeClip ? dodgeClip : idleClip;
  auto dodgeNode = std::make_shared<ClipNode>(actualDodgeClip, false);
  dodgeNode->setOwnerName(ownerName);

  // death
  auto deathNode = std::make_shared<ClipNode>(deathClip, false);
  deathNode->setOwnerName(ownerName);

  // graph root
  auto root = std::make_shared<StateMachineNode>();
  root->addState("Locomotion", locomotionNode);
  root->addState("Attacking", attackNode);
  root->addState("Dodging", dodgeNode);
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

  root->addTransition({
    "Locomotion", "Dodging",
    [](AnimParamTable& p) { return p.consumeTrigger("dodge"); },
    0.05f // fast blend time for responsiveness
  });

  root->addTransition({
    "Dodging", "Locomotion",
    [dodgeNode](AnimParamTable&) { return dodgeNode->isFinished(); },
    0.15f 
  });

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

  getCombat().registerCombatant(m_player.combat.get());
  getLua().get()["playerCombat"] = m_player.combat.get();
  getLua().get()["playerTransform"] = m_player.transform.get();
  getLua().get()["playerAnim"] = &m_player.animator->getParams();

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
  m_enemy.transform->setPosition({ 0.0f, k_enemyCapsuleHalfHeight, -4.0f });
  m_enemy.transform->setScale({ 0.01f, 0.01f, 0.01f });
  m_enemy.transform->setEulerAngles({ 90.0f, 0.0f, 0.0f });
 
  m_enemy.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *m_enemy.transform,
    forge::CollisionShape::Capsule,
    glm::vec3(0.3f, 0.9f, 0.0f),
    80.0f
  );
  m_enemy.body->setAngularForce({ 0, 0, 0 });
 
  // ── Animator ─────────────────────────────────────────────────────────
  m_enemy.animator = std::make_unique<forge::Animator>();
  m_enemy.animator->setOwnerName("enemy");
  m_enemy.animator->setSkeleton(m_enemy.skinnedModel.skeleton);
 
  // Share clips with the player — same file, same data
  m_enemy.clips["idle"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_idle.glb", "idle"); 
  m_enemy.clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash"); 
  m_enemy.clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death"); 

  auto graph = buildCharacterGraph(
    m_enemy.clips["idle"], 
    nullptr,
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
 
  getCombat().registerCombatant(m_enemy.combat.get());
  getDebugUI().registerAnimator(m_enemy.animator.get(), "Enemy");
  getDebugUI().registerAIComponent(m_enemy.ai.get());

  getLua().get()["enemyCombat"] = m_enemy.combat.get();
  getLua().get()["enemyTransform"] = m_enemy.transform.get();
  getLua().get()["enemyAnim"] = &m_enemy.animator->getParams();
  getLua().get()["enemyAI"] = m_enemy.ai.get();

  LOG_INFO("[ForgeGame] Enemy ready");
}

void ForgeGame::setupLevel() {
  const std::string root = forge::AssetManager::getAssetRoot();
  const std::string objPath = "levels/level_01.obj";
  const std::string mapPath = root + "levels/level_01.map";

  bool objLoaded = false;
  try {
    m_levelMesh = forge::AssetManager::loadModel(objPath);
    objLoaded = m_levelMesh.hasRenderData(); 
  } catch (const std::exception& e) {
    LOG_WARN("[Game] level_01.obj not found or failed to load: {}", e.what());
  }

  if (objLoaded) {
    LOG_INFO("[Game] TrenchBroom level OBJ loaded ({} physics tris)", m_levelMesh.indices.size() / 3);
    constexpr float k_mapScale = 1.0f / 64.0f;

    // Place level at origin at engine scale
    m_levelTransform = std::make_unique<forge::Transform>();
    m_levelTransform->setPosition({ 0.0f, 0.0f, 0.0f });
    m_levelTransform->setScale({ k_mapScale, k_mapScale, k_mapScale });

    std::vector<glm::vec3> scaledPositions;
    scaledPositions.reserve(m_levelMesh.positions.size());
    for (const auto& p : m_levelMesh.positions)
      scaledPositions.push_back(p * k_mapScale);

    // Triangle mesh collision - one body for the whole level
    if (m_levelMesh.hasPhysicsData()) {
      m_levelPhysicsBody = std::make_unique<forge::RigidBodyComponent>(
        getPhysics(),
        *m_levelTransform,
        scaledPositions,
        m_levelMesh.indices,
        0.0f // static
      );
    } else {
      LOG_WARN("[Game] Level OBJ loaded without physics data - using plane collision fallback.");
      m_levelPhysicsBody = std::make_unique<forge::RigidBodyComponent>(
        getPhysics(),
        *m_levelTransform,
        forge::CollisionShape::Plane,
        glm::vec3(0),
        0.0f
      );
    }

    // Parse entities from map file
    m_levelData = forge::LevelLoader::load(mapPath);

    if (m_levelData.valid) {
      // Player start - override default player spawn
      if (const auto& start = m_levelData.findFirst("info_player_start")) {
        glm::vec3 origin = start->origin;
        constexpr float k_playerCapsuleHalfHeight = 0.9f;
        origin.y += k_playerCapsuleHalfHeight;
        m_player.controller->warp(origin);
        if (start->angle != 0.0f) {
          float rad = glm::radians(start->angle);
          m_player.forward = glm::normalize(glm::vec3(sinf(rad), 0.0f, cosf(rad)));
          m_player.transform->setEulerAngles({ 90.0f, start->angle, 0.0f });
        }
        LOG_INFO("[Game] Player spawned at {:.2f},{:.2f},{:.2f} from info_player_start",
                 start->origin.x, start->origin.y, start->origin.z);
      }

      // Enemy spawns - relocate existing enemy to first spawn point
      const auto enemySpawns = m_levelData.getByClass("enemy_spawn");
      if (!enemySpawns.empty()) {
        const forge::LevelEntity* spawnEnt = enemySpawns[0];
        glm::vec3 spawnPos = spawnEnt->origin;

        constexpr float k_capsuleHalfHeight = 0.75f;
        constexpr float k_rayStart = 2.0f;
        constexpr float k_rayLength = 10.0f;

        glm::vec3 rayFrom = spawnPos + glm::vec3(0.0f, k_rayStart, 0.0f);
        glm::vec3 rayTo = spawnPos + glm::vec3(0.0f, -k_rayLength, 0.0f);

        forge::RaycastHit hit = getPhysics().raycast(rayFrom, rayTo);
        if (hit.hit) {
          spawnPos.y += hit.point.y + k_capsuleHalfHeight;
          LOG_INFO("[Game] Enemy snapped to floor at y={:.3f} (raycast hit at y={:.3f})", spawnPos.y, hit.point.y);
        } else {
          spawnPos.y -= k_capsuleHalfHeight;
          LOG_INFO("[Game] Floor raycast missed for enemy_spawn, using entity height + offset");
        }
        m_enemy.transform->setPosition(spawnPos);
        m_enemy.body->teleport(spawnPos);

        // Patrol waypoints for this spawn (matched by patrol_group key)
        std::string group = spawnEnt->props.count("patrol_group")
          ? spawnEnt->props.at("patrol_group") : "";

        if (!group.empty()) {
          // Clear default waypoints
          m_enemy.ai->clearWaypoints();
          for (const auto* wp : m_levelData.getByClass("patrol_waypoint")) {
            if (wp->props.count("patrol_group") && wp->props.at("patrol_group") == group) {
              m_enemy.ai->addWaypoint(wp->origin);
            }
          }
          LOG_INFO("[Game] Enemy patrol loaded: {} waypoints (group '{}')",
                   m_enemy.ai->waypointCount(), group);
        }

        LOG_INFO("[Game] Enemy spawned at {:.2f},{:.2f},{:.2f} from map",
                 spawnPos.x, spawnPos.y, spawnPos.z);
      }

      // Flag triggers - more in next phase
      for (const auto* t : m_levelData.getByClass("flag_trigger")) {
        int flagId = t->getInt("flag_id", -1);
        bool triggerOnce = t->getBool("trigger_once", true);
        float radius = t->getFloat("radius", 2.0f);
        if (flagId >= 0) {
          LOG_INFO("[Game] flag_trigger: flag={} radius={:.1f} once={} at {:.1f},{:.1f},{:.1f}",
                   flagId, radius, triggerOnce,
                   t->origin.x, t->origin.y, t->origin.z);
        }
      }

      LOG_INFO("[Game] Map entities loaded: {} total", m_levelData.entities.size());
    } else {
      LOG_WARN("[Game] level_01.map not found - entity data skipped.");
    }

    m_usingTBLevel = true;
    LOG_INFO("[Game] TrenchBroom level active.");
    return;
  }

  LOG_INFO("[Game] No TrenchBroom level found — using procedural courtyard.");

  m_floorModel = forge::AssetManager::loadModel("models/medieval/floor.fbx");
  m_wallModel = forge::AssetManager::loadModel("models/medieval/wall.fbx");
  m_towerModel = forge::AssetManager::loadModel("models/medieval/tower.fbx");

  // Floor - 7x7 grid of tiles centered on origin
  const float tileSize = 1.0f;
  const float modelScale = 0.01f;
  const int gridSize = 7;
  const float halfGrid = (gridSize * tileSize) / 2.0f - tileSize / 2.0f;

  for (int z = 0; z < gridSize; z++) {
    for (int x = 0; x < gridSize; x++) {
      LevelPiece piece;
      piece.model = m_floorModel; // Shared, no extra GPU mem

      piece.transform = std::make_unique<forge::Transform>();
      piece.transform->setPosition({
        x * tileSize - halfGrid, 
        0.0f,
        z * tileSize - halfGrid
      });
      piece.transform->setScale({ modelScale, modelScale, modelScale });

      // Static phys plane only needed once - invisible floor
      // handles collision for all tiles collectively
      piece.body = nullptr;

      m_level.push_back(std::move(piece));
    }
  }

  // Invisible floor plane
  {
    LevelPiece physicsFloor;
    physicsFloor.model = {};
    physicsFloor.transform = std::make_unique<forge::Transform>();
    physicsFloor.transform->setPosition({ 0,0,0 });
    physicsFloor.body = std::make_unique<forge::RigidBodyComponent>(
      getPhysics(),
      *physicsFloor.transform,
      forge::CollisionShape::Plane,
      glm::vec3(0),
      0.0f
    );
    m_level.push_back(std::move(physicsFloor));
  }

  // Walls -- perimeter of courtyard
  // North and south
  for (int x = 0; x < gridSize; x++) {
    float xPos = x * tileSize - halfGrid;

    // North
    LevelPiece north;
    north.model = m_wallModel;
    north.transform = std::make_unique<forge::Transform>();
    north.transform->setPosition({ xPos, 0.0f, -halfGrid - tileSize });
    north.transform->setScale({ modelScale, modelScale, modelScale });
    north.body = nullptr;
    m_level.push_back(std::move(north));

    // South
    LevelPiece south;
    south.model = m_wallModel;
    south.transform = std::make_unique<forge::Transform>();
    south.transform->setPosition({ xPos, 0.0f, halfGrid + tileSize });
    south.transform->setScale({ modelScale, modelScale, modelScale });
    south.transform->setEulerAngles({ 0, 180.0f, 0 });
    south.body = nullptr;
    m_level.push_back(std::move(south));
  }

  // East and west walls
  for (int z = 0; z < gridSize; z++) {
    float zPos = z * tileSize - halfGrid;

    // North
    LevelPiece west;
    west.model = m_wallModel;
    west.transform = std::make_unique<forge::Transform>();
    west.transform->setPosition({ -halfGrid - tileSize, 0.0f, zPos });
    west.transform->setScale({ modelScale, modelScale, modelScale });
    west.transform->setEulerAngles({ 0, 90.0f, 0 });
    west.body = nullptr;
    m_level.push_back(std::move(west));

    // South
    LevelPiece east;
    east.model = m_wallModel;
    east.transform = std::make_unique<forge::Transform>();
    east.transform->setPosition({ halfGrid + tileSize, 0.0f, zPos });
    east.transform->setScale({ modelScale, modelScale, modelScale });
    east.transform->setEulerAngles({ 0, -90.0f, 0 });
    east.body = nullptr;
    m_level.push_back(std::move(east));
  }

  const float corner = halfGrid + tileSize;
  for (float cx : { -corner, corner }) {
      for (float cz : { -corner, corner }) {
          LevelPiece tower;
          tower.model     = m_towerModel;
          tower.transform = std::make_unique<forge::Transform>();
          tower.transform->setPosition({ cx, 0.0f, cz });
          tower.transform->setScale({ modelScale, modelScale, modelScale });
          tower.body = nullptr;
          m_level.push_back(std::move(tower));
      }
  }

  m_usingTBLevel = false;
}

void ForgeGame::setupScripts() {
  // Load order matters, flags before quests, attacks before combat
  getLua().get()["Flags"] = &getFlags();

  const std::string root = forge::AssetManager::getAssetRoot();
  getLua().loadScript(root + "scripts/events/flags.lua");
  getLua().loadScript(root + "scripts/combat/attacks.lua");
  getLua().loadScript(root + "scripts/combat/player_combat.lua");
  getLua().loadScript(root + "scripts/ai/enemy_ai.lua");
  getLua().loadScript(root + "scripts/events/demo_quest.lua");

  getLua().callFunction("onAIInit");
}

void ForgeGame::onUpdate(float dt) {
  handleInput(dt);

  const float enemyY = m_enemy.transform->getPosition().y;

  m_enemy.body->teleport(m_enemy.transform->getPosition());

  // Physics
  getPhysics().step(dt);
  m_player.controller->syncTransform();
  m_enemy.body->syncTransform();

  glm::vec3 ep = m_enemy.transform->getPosition();
  ep.y = enemyY;
  m_enemy.transform->setPosition(ep);

  m_tpCamera->update(m_player.transform->getPosition());

  if (m_lockedOn) {
    m_lockOnEnemyPos = m_enemy.transform->getPosition() + glm::vec3(0.0f, 1.0f, 0.0f);
    if (!m_enemy.combat->isAlive()) {
      m_lockedOn = false;
      m_tpCamera->setLockOnTarget(nullptr);
      LOG_INFO("[Game] Lock-on released - enemy dead");
    }
  }

  glm::vec3 playerPos = m_player.transform->getPosition();
  m_player.combat->setWorldData(playerPos, m_player.forward);

  m_enemy.ai->update(dt, playerPos);
  m_player.animator->update(dt);
  m_enemy.animator->update(dt);

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
  if (isKeyPressed(GLFW_KEY_F5))
    getLua().loadScript("../../../../assets/scripts/combat/player_combat.lua");
}

void ForgeGame::onRender() {
  m_shader->bind();
  if (m_usingTBLevel) {
    if (m_levelMesh.hasRenderData())
      drawModel(m_levelMesh, *m_levelTransform);
  } else {
    for (auto& piece : m_level)
      if (piece.model.hasRenderData()) drawModel(piece.model, *piece.transform);
  }
  m_shader->unbind();

  m_skinnedShader->bind();
  drawSkinnedModel(m_player.skinnedModel, *m_player.transform, *m_player.animator);

  // tint enemy slightly red so we can see which they are
  {
    constexpr float k_enemyCapsuleRadius = 0.3f;
    constexpr float k_enemyCylinderHalfHeight = 0.45f;
    constexpr float k_enemyTotalHalfHeight = k_enemyCapsuleRadius + k_enemyCylinderHalfHeight;

    forge::Transform enemyVisualTransform;
    glm::vec3 footPos = m_enemy.transform->getPosition()
                      - glm::vec3(0.0f, k_enemyTotalHalfHeight, 0.0f);
    enemyVisualTransform.setPosition(footPos);
    enemyVisualTransform.setScale(m_enemy.transform->getScale());
    enemyVisualTransform.setRotation(m_enemy.transform->getRotation());

    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f, 0.7f, 0.7f));
    drawSkinnedModel(m_enemy.skinnedModel, enemyVisualTransform, *m_enemy.animator);
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

    {
      constexpr float kR = 0.3f;
      constexpr float kH = 0.45f;
      forge::DebugDraw::capsule(
        m_enemy.transform->getPosition(), 
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
  float walkSpeed = 5.0f;
  float sprintSpeed = 8.0f;
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

  m_player.controller->setWalkDirection(velocity * dt);

  m_player.combat->setWorldData(m_player.transform->getPosition(), m_player.forward);
}

