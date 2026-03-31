#include "ForgeGame.h"

#include <forge/AssetManager.h>
#include <forge/Logger.h>
#include <forge/PhysicsWorld.h>
#include <forge/AnimationClip.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/FlagManager.h>
#include <forge/DebugUI.h>
#include <forge/LuaState.h>
#include <forge/AnimationClip.h>
#include <forge/CombatSystem.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <sol/forward.hpp>

#include <memory>
#include "GameFlags.h"
#include "forge/Animator.h"

ForgeGame::ForgeGame()
  : forge::Application(1280, 720, "Forge Engine - Soulslike demo")
{}

void ForgeGame::onInit() {
  LOG_INFO("[Game] Initializing ForgeGame");
  forge::AssetManager::setAssetRoot("../../../../assets/");

  setupRenderer();
  setupPlayer();
  setupEnemy();
  setupLevel();
  setupScripts();


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
  m_shader = forge::AssetManager::loadShader(
    "shaders/basic.vert",
    "shaders/basic.frag"
  );
  m_skinnedShader = forge::AssetManager::loadShader("shaders/skinned.vert",
                                                    "shaders/basic.frag"); 
  // Third person camera - positioned behind and above player
  // angled down to see the level.
  float aspect = (float)getWidth() / (float)getHeight();
  m_camera = std::make_unique<forge::Camera>(30.0f, aspect, 0.1f, 300.0f);
}

void ForgeGame::setupPlayer() {
  m_player.skinnedModel = forge::AssetManager::loadSkinnedModel("models/characters/anim/ybot_idle.glb"); 
  m_player.transform = std::make_unique<forge::Transform>();
  m_player.transform->setPosition({ 0.0f, 0.0f, 0.0f });
  m_player.transform->setScale({ 0.01f, 0.01f, 0.01f });
  m_player.transform->setEulerAngles({ 90.0f, 180.0f, 0.0f });

  m_player.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *m_player.transform,
    forge::CollisionShape::Capsule,
    glm::vec3(0.3f, 0.9f, 0.0f),
    80.0f
  );
  m_player.body->setAngularForce({ 0,0,0 });

  // Animator
  m_player.animator = std::make_unique<forge::Animator>();
  m_player.animator->setOwnerName("player");
  m_player.animator->setSkeleton(m_player.skinnedModel.skeleton);

  // Load anim clips
  // Idle is embadded in the base model - load the first animation from it
  m_player.clips["idle"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_idle.glb", "idle");

  m_player.clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash");
  m_player.clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death");

  if (m_player.clips["attack_r1"]) {
    auto& clip = m_player.clips["attack_r1"];
    clip->looping = false;
 
    forge::AnimEvent hitboxEvent;
    hitboxEvent.startTime = 14.0f / 60.0f;
    hitboxEvent.endTime   = 22.0f / 60.0f;
    hitboxEvent.type      = forge::AnimEventType::SpawnHitbox;
    hitboxEvent.payload   = "weapon_r";
 
    forge::AnimEvent sfxEvent;
    sfxEvent.startTime = 10.0f / 60.0f;
    sfxEvent.endTime   = 10.0f / 60.0f;   // one-shot
    sfxEvent.type      = forge::AnimEventType::SoundOneShot;
    sfxEvent.payload   = "sfx/swing_heavy.wav";
 
    clip->events.push_back(hitboxEvent);
    clip->events.push_back(sfxEvent);
 
    // Animator assumes sorted order — always sort after authoring
    std::sort(clip->events.begin(), clip->events.end(),
      [](const forge::AnimEvent& a, const forge::AnimEvent& b) {
        return a.startTime < b.startTime;
      });
  }
 
  if (m_player.clips["death"])
    m_player.clips["death"]->looping = false;

  m_player.combat = std::make_unique<forge::CombatComponent>(
    "Player",
    500.0f,
    100.0f,
    80.0f
  );

  m_player.combat->setAnimator(m_player.animator.get());

  m_player.combat->onAttackStart = [this](const std::string& attackName) {
    auto it = m_player.clips.find(attackName);
    if (it != m_player.clips.end() && it->second)
      m_player.animator->play(it->second, false, 0.1f);
    else
      LOG_WARN("[ForgeGame] No clip for attack '{}'", attackName);
  };

  // Wire up lua for player
  m_player.combat->onDeath = [this](){
    if (m_player.clips["death"])
      m_player.animator->play(m_player.clips["death"], false, 0.2f);
  };
  m_player.combat->onHit = [this](const forge::HitEvent& h) {
    getLua().callFunction("onPlayerHit", h.damage, h.damageType);
  };

  getCombat().registerCombatant(m_player.combat.get());
  getLua().get()["playerCombat"] = m_player.combat.get();
  getLua().get()["playerTransform"] = m_player.transform.get();

  if (m_player.clips["idle"])
    m_player.animator->play(m_player.clips["idle"], true);

  getDebugUI().registerAnimator(m_player.animator.get(), "Player");

  LOG_INFO("[ForgeGame] Player ready - skeleton: {} bones",
           m_player.skinnedModel.skeleton.size());
}

void ForgeGame::setupEnemy() {
  // Reuse the same Y Bot mesh — different transform, same skeleton
  m_enemy.skinnedModel = forge::AssetManager::loadSkinnedModel(
    "models/characters/anim/ybot_idle.glb");
 
  m_enemy.transform = std::make_unique<forge::Transform>();
  m_enemy.transform->setPosition({ 4.0f, 0.0f, 0.0f });
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
  m_enemy.clips = m_player.clips;
 
  // ── Combat ───────────────────────────────────────────────────────────
  m_enemy.combat = std::make_unique<forge::CombatComponent>(
    "enemy", 400.0f, 100.0f, 60.0f);
 
  m_enemy.combat->setAnimator(m_enemy.animator.get());
 
  m_enemy.combat->onAttackStart = [this](const std::string& attackName) {
    auto it = m_enemy.clips.find(attackName);
    if (it != m_enemy.clips.end() && it->second)
      m_enemy.animator->play(it->second, false, 0.1f);
  };
 
  m_enemy.combat->onDeath = [this]() {
    if (m_enemy.clips["death"])
      m_enemy.animator->play(m_enemy.clips["death"], false, 0.2f);
    forge::EventBus::publish(forge::EntityDiedEvent{
      "enemy", m_enemy.transform->getPosition() });
  };
 
  // ── AI ────────────────────────────────────────────────────────────────
  m_enemy.ai = std::make_unique<forge::AIComponent>(
    "enemy", *m_enemy.transform, *m_enemy.combat);
 
  m_enemy.ai->addWaypoint({ -3.0f, 0.0f,  3.0f });
  m_enemy.ai->addWaypoint({  3.0f, 0.0f,  3.0f });
  m_enemy.ai->addWaypoint({  3.0f, 0.0f, -3.0f });
  m_enemy.ai->addWaypoint({ -3.0f, 0.0f, -3.0f });
 
  // ── Play idle ────────────────────────────────────────────────────────
  if (m_enemy.clips["idle"])
    m_enemy.animator->play(m_enemy.clips["idle"], true);
 
  getDebugUI().registerAnimator(m_enemy.animator.get(), "Enemy");
  getDebugUI().registerAIComponent(m_enemy.ai.get());
 
  getCombat().registerCombatant(m_enemy.combat.get());
  getLua().get()["enemyCombat"] = m_enemy.combat.get();
  getLua().get()["enemyAI"] = m_enemy.ai.get();

  LOG_INFO("[ForgeGame] Enemy ready");
}

void ForgeGame::setupLevel() {
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

  m_player.body->teleport(m_player.transform->getPosition());
  m_enemy.body->teleport(m_enemy.transform->getPosition());

  // Physics
  getPhysics().step(dt);
  m_player.body->syncTransform();
  m_enemy.body->syncTransform();

  glm::vec3 playerPos = m_player.transform->getPosition();
  m_camera->setPosition(playerPos + glm::vec3(0, 8, 12));
  m_camera->setTarget(playerPos + glm::vec3(0, 1, 0));

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
  for (auto& piece : m_level) {
    if (piece.model.mesh) {
      drawModel(piece.model, *piece.transform);
    }
  }
  m_shader->unbind();

  m_skinnedShader->bind();
  drawSkinnedModel(m_player.skinnedModel, *m_player.transform, *m_player.animator);

  // tint enemy slightly red so we can see which they are
  m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f, 0.7f, 0.7f));
  drawSkinnedModel(m_enemy.skinnedModel, *m_enemy.transform, *m_enemy.animator);
  m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));
  m_skinnedShader->unbind();
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

  if (model.hasTexture()) {
      model.texture->bind(0);           // Bind to texture unit 0
      m_shader->setInt ("u_texture",    0);
      m_shader->setBool("u_hasTexture", true);
  } else {
      m_shader->setBool("u_hasTexture", false);
  }

  model.mesh->draw();
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
  m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));

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

void ForgeGame::handleInput(float dt) {
  const float speed = 5.0f;
  glm::vec3   pos   = m_player.transform->getPosition();
  glm::vec3   move  = { 0, 0, 0 };

  if (isKeyDown(GLFW_KEY_W)) move.z -= 1.0f;
  if (isKeyDown(GLFW_KEY_S)) move.z += 1.0f;
  if (isKeyDown(GLFW_KEY_A)) move.x -= 1.0f;
  if (isKeyDown(GLFW_KEY_D)) move.x += 1.0f;

  if (glm::length(move) > 0.001f) {
      move = glm::normalize(move) * speed;
      // Face the direction of movement
      float angle = atan2f(move.x, move.z);
      m_player.transform->setEulerAngles({ 90.0f, glm::degrees(angle), 0 });
      m_player.forward = glm::normalize(glm::vec3(move.x, 0, move.z));
  }

  pos += move * dt;
  pos.y = m_player.transform->getPosition().y; // Let physics own Y
  m_player.transform->setPosition(pos);
  m_player.combat->setWorldData(pos, m_player.forward);
}

