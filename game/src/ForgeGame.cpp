#include "ForgeGame.h"

#include <forge/PhysicsWorld.h>
#include <forge/LuaState.h>
#include <forge/CombatSystem.h>
#include <forge/FlagManager.h>
#include <forge/RigidBodyComponent.h>
#include <forge/CombatComponent.h>
#include <forge/AIComponent.h>
#include <forge/EventBus.h>
#include <forge/Events.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <sol/forward.hpp>

#include "GameFlags.h"

// Helpers
static std::vector<forge::Vertex> makeCubeVerts() {
  return {
    // position               color
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.2f, 0.2f}}, // 0 back-bottom-left
    {{ 0.5f, -0.5f, -0.5f}, {0.2f, 1.0f, 0.2f}}, // 1 back-btm-right
    {{ 0.5f,  0.5f, -0.5f}, {0.2f, 0.2f, 1.0f}}, // 2 back-top-right
    {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.2f}}, // 3 back-top-left
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.2f, 1.0f}}, // 4 front-bottom-left
    {{ 0.5f, -0.5f,  0.5f}, {0.2f, 1.0f, 1.0f}}, // 5 front-btm-right
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}}, // 6 front-top-right
    {{-0.5f,  0.5f,  0.5f}, {0.5f, 0.5f, 1.0f}}, // 7 front-top-left
  };
}

static std::vector<unsigned int> makeCubeIdx() {
  return {
    0,1,2,  2,3,0, // back face
    4,5,6,  6,7,4, // front face
    0,4,7,  7,3,0, // left face
    1,5,6,  6,2,1, // right face
    3,2,6,  6,7,3, // top face
    0,1,5,  5,4,0, // bottom face
  };
}

ForgeGame::ForgeGame()
  : forge::Application(1280, 720, "Forge Engine - Soulslike demo")
{}

void ForgeGame::onInit() {
  LOG_INFO("[Game] Initializing ForgeGame");
  setupPlayer();
  setupEnemy();
  setupLevel();
  setupScripts();

  // Try loading previous save
  getFlags().loadFromFile("save.json");
}

void ForgeGame::setupPlayer() {
  m_player.mesh = std::make_unique<forge::Mesh>(makeCubeVerts(), makeCubeIdx());
  m_player.transform = std::make_unique<forge::Transform>();
  m_player.transform->setPosition({ 0.0f, 1.0f, 5.0f });

  m_player.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *m_player.transform,
    forge::CollisionShape::Box,
    glm::vec3(0.5f),
    1.0f
  );
  m_player.body->setAngularForce({ 0,0,0 });

  m_player.combat = std::make_unique<forge::CombatComponent>(
    "Player",
    400.0f,
    100.0f,
    60.0f
  );

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
}

void ForgeGame::setupEnemy() {
  m_enemy.mesh = std::make_unique<forge::Mesh>(makeCubeVerts(), makeCubeIdx());
  m_enemy.transform = std::make_unique<forge::Transform>();
  m_enemy.transform->setPosition({ 0.0f, 1.0f, -3.0f });

  m_enemy.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *m_enemy.transform,
    forge::CollisionShape::Box,
    glm::vec3(0.5f),
    1.0f
  );
  m_enemy.body->setAngularForce({ 0,0,0 });

  m_enemy.combat = std::make_unique<forge::CombatComponent>(
    "Dummy",
    200.0f,
    999.0f,
    30.0f
  );

  m_enemy.combat->onDeath = [this](){
    getFlags().set(GameFlags::BOSS_DUMMY_DEAD, true);
    forge::EventBus::publish(forge::EntityDiedEvent{
      .entityName = "Dummy",
      .position = m_enemy.transform->getPosition()
    });
  };

  m_enemy.ai = std::make_unique<forge::AIComponent>(
    "Dummy",
    *m_enemy.transform,
    *m_enemy.combat
  );

  // Patrol waypoints
  m_enemy.ai->addWaypoint({ -3.0f, 1.0f, -3.0f });
  m_enemy.ai->addWaypoint({  3.0f, 1.0f, -3.0f });
  m_enemy.ai->addWaypoint({  3.0f, 1.0f,  3.0f });
  m_enemy.ai->addWaypoint({ -3.0f, 1.0f,  3.0f });

  // Wire Lua cbs
  m_enemy.ai->onPlayerDetected = [this](){
    getLua().callFunction("onPlayerDetected");
  };
  m_enemy.ai->onChooseAttack = [this]() -> std::string {
    sol::protected_function fn = getLua().get()["onChooseAttack"];
    if (!fn.valid()) return "R1";
    auto r = fn();
    return r.valid() ? r.get<std::string>() : "R1";
  };

  getCombat().registerCombatant(m_enemy.combat.get());
  getLua().get()["enemyCombat"] = m_enemy.combat.get();
  getLua().get()["enemyAI"] = m_enemy.ai.get();
}

void ForgeGame::setupLevel() {
  // Shader and camera
  m_shader = std::make_unique<forge::Shader>(
    "../../../../assets/shaders/basic.vert",
    "../../../../assets/shaders/basic.frag"
  );

  float aspect = (float)getWidth() / (float)getHeight();
  m_camera = std::make_unique<forge::Camera>(60.0f, aspect, 0.1f, 100.0f);
  m_camera->setPosition({ 0.0f, 6.0f, 10.0f });
  m_camera->setTarget({ 0.0f, 0.0f, 0.0f });


  // Floor
  std::vector<forge::Vertex> floorVerts = {
    {{-5.0f,0.0f,-5.0f},{0.3f,0.3f,0.3f}},
    {{ 5.0f,0.0f,-5.0f},{0.3f,0.3f,0.3f}},
    {{ 5.0f,0.0f, 5.0f},{0.3f,0.3f,0.3f}},
    {{-5.0f,0.0f, 5.0f},{0.3f,0.3f,0.3f}},
  };
  std::vector<unsigned int> floorIdx = {0,1,2, 2,3,0};
  
  LevelEntity floor;
  floor.mesh = std::make_unique<forge::Mesh>(floorVerts, floorIdx);
  floor.transform = std::make_unique<forge::Transform>();
  floor.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *floor.transform,
    forge::CollisionShape::Plane,
    glm::vec3(0.0f),
    0.0f
  );

  m_level.push_back(std::move(floor));
}

void ForgeGame::setupScripts() {
  // Load order matters, flags before quests, attacks before combat
  getLua().get()["Flags"] = &getFlags();

  getLua().loadScript("../../../../assets/scripts/events/flags.lua");
  getLua().loadScript("../../../../assets/scripts/combat/attacks.lua");
  getLua().loadScript("../../../../assets/scripts/combat/player_combat.lua");
  getLua().loadScript("../../../../assets/scripts/ai/enemy_ai.lua");
  getLua().loadScript("../../../../assets/scripts/events/demo_quest.lua");

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

  m_player.combat->setWorldData(
    m_player.transform->getPosition(),
    m_player.forward
  );

  m_enemy.ai->update(dt, m_player.transform->getPosition());
  
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
  glm::mat4 vp = m_camera->getViewProjection();

  m_shader->setMat4("u_mvp", vp * m_player.transform->getModelMatrix());
  m_player.mesh->draw();

  m_shader->setMat4("u_mvp", vp * m_enemy.transform->getModelMatrix());
  m_enemy.mesh->draw();

  for (auto& e : m_level) {
    m_shader->setMat4("u_mvp", vp * e.transform->getModelMatrix());
    e.mesh->draw();
  }

  m_shader->unbind();
}

void ForgeGame::onShutdown() {
  getFlags().saveToFile("save.json");
  LOG_INFO("[Game] ForgeGame shutdown");
}

void ForgeGame::handleInput(float dt) {
  float speed = 5.0f;
  glm::vec3 pos = m_player.transform->getPosition();

  if (isKeyDown(GLFW_KEY_W)) pos.z -= speed * dt;
  if (isKeyDown(GLFW_KEY_S)) pos.z += speed * dt;
  if (isKeyDown(GLFW_KEY_A)) pos.x -= speed * dt;
  if (isKeyDown(GLFW_KEY_D)) pos.x += speed * dt;

  m_player.transform->setPosition(pos);
}

