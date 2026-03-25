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
#include <glm/gtc/matrix_transform.hpp>
#include <sol/forward.hpp>

#include <memory>

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
  // Third person camera - positioned behind and above player
  // angled down to see the level.
  float aspect = (float)getWidth() / (float)getHeight();
  m_camera = std::make_unique<forge::Camera>(60.0f, aspect, 0.1f, 200.0f);
  m_camera->setPosition({ 0.0f, 8.0f, 12.0f });
  m_camera->setTarget({ 0.0f, 1.0f, 0.0f });
}

void ForgeGame::setupPlayer() {
  m_player.model = forge::AssetManager::loadModel("models/characters/character-a.fbx"); 
  m_player.transform = std::make_unique<forge::Transform>();
  m_player.transform->setPosition({ 0.0f, 0.0f, 5.0f });
  m_player.transform->setScale({ 0.01f, 0.01f, 0.01f });

  m_player.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *m_player.transform,
    forge::CollisionShape::Capsule,
    glm::vec3(0.4f, 0.9f, 0.0f),
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
  m_enemy.model = forge::AssetManager::loadModel("models/characters/character-b.fbx"); 
  m_enemy.transform = std::make_unique<forge::Transform>();
  m_enemy.transform->setPosition({ 0.0f, 0.0f, -8.0f });
  m_enemy.transform->setScale({ 0.01f, 0.01f, 0.01f });

  m_enemy.body = std::make_unique<forge::RigidBodyComponent>(
    getPhysics(),
    *m_enemy.transform,
    forge::CollisionShape::Capsule,
    glm::vec3(0.4f, 0.9f, 0.0f),
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
      .entityName = "Hollow",
      .position = m_enemy.transform->getPosition()
    });
  };

  m_enemy.ai = std::make_unique<forge::AIComponent>(
    "Hollow",
    *m_enemy.transform,
    *m_enemy.combat
  );
  m_enemy.ai->setDetectionRadius(5.0f);
  m_enemy.ai->setAttackRadius(1.5f);

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
  m_floorModel = forge::AssetManager::loadModel("models/medieval/floor.fbx");
  m_wallModel = forge::AssetManager::loadModel("models/medieval/wall.fbx");
  m_towerModel = forge::AssetManager::loadModel("models/medieval/tower.fbx");

  // Floor - 7x7 grid of tiles centered on origin
  const float tileSize = 1.0f;
  const float modelScale = 0.01f;
  const int gridSize = 7;
  const float halfGrid = (gridSize - 1) * tileSize * 0.5f;

  for (int z = 0; z < gridSize; z++) {
    for (int x = 0; x < gridSize; x++) {
      LevelPiece piece;
      piece.model = m_floorModel; // Shared, no extra GPU mem

      piece.transform = std::make_unique<forge::Transform>();
      piece.transform->setPosition({
        x * tileSize - halfGrid, 
        0.001f,
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

void ForgeGame::onRender() {
  m_shader->bind();

  // Player
  drawModel(m_player.model, *m_player.transform);

  // Enemy (tinted red slightly to distinguish)
  m_shader->setVec3("u_tint", glm::vec3(1.0f, 0.7f, 0.7f));
  drawModel(m_enemy.model, *m_enemy.transform);

  // Level geometry
  for (auto& piece : m_level) {
      if (piece.model.mesh)
          drawModel(piece.model, *piece.transform);
  }

  m_shader->unbind();
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
      m_player.transform->setEulerAngles({ 0, glm::degrees(angle), 0 });
      m_player.forward = glm::normalize(glm::vec3(move.x, 0, move.z));
  }

  pos += move * dt;
  pos.y = m_player.transform->getPosition().y; // Let physics own Y
  m_player.transform->setPosition(pos);
  m_player.combat->setWorldData(pos, m_player.forward);
}

