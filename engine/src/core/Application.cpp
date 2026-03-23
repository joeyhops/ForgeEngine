#include <forge/Application.h>
#include <forge/Logger.h>
#include <forge/Shader.h>
#include <forge/Mesh.h>
#include <forge/Camera.h>
#include <forge/Transform.h>
#include <forge/LuaState.h>
#include <forge/PhysicsWorld.h>
#include <forge/RigidBodyComponent.h>
#include <forge/CombatSystem.h>
#include <forge/CombatComponent.h>
#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/AIComponent.h>
#include <forge/Animator.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <memory>
#include <sol/forward.hpp>
#include <stdexcept>
#include <vector>

namespace forge {

Application::Application(int width, int height, const char* title)
  : m_width(width), m_height(height)
{
  init();
  m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }
  glfwMakeContextCurrent(m_window);
  glfwSwapInterval(1);
  if (glewInit() != GLEW_OK) {
    throw std::runtime_error("Failed to initialize GLEW");
  }

  LOG_INFO("Forge Engine started - {}x{}", m_width, m_height);
  LOG_INFO("OpenGL Version: {}", (const char*)glGetString(GL_VERSION));
  LOG_INFO("GPU: {}", (const char*)glGetString(GL_RENDERER));

  glEnable(GL_DEPTH_TEST); // Closer objects occlude farther ones

  m_shader = std::make_unique<Shader>(
    "../../../../assets/shaders/basic.vert",
    "../../../../assets/shaders/basic.frag"
  );
  
  // Camera
  float aspect = static_cast<float>(width) / static_cast<float>(height);
  m_camera = std::make_unique<Camera>(60.0f, aspect, 0.1f, 100.0f);
  m_camera->setPosition({ 0.0f, 4.0f, 8.0f });
  m_camera->setTarget({ 0.0f, 0.0f, 0.0f });

  // Procedural Bone Demo
  // Creates a 3-bone chain and waves it - all in code
  // Remove when we have a real asset pipeline and models
  Bone root, mid, tip;
  root.name = "root"; root.parentIndex = -1;
  root.offsetMatrix = glm::mat4(1.0f);
  root.localTransform = glm::mat4(1.0f);

  mid.name = "mid"; mid.parentIndex = 0;
  mid.offsetMatrix = glm::translate(glm::mat4(1.0f), { 0, 1, 0 });
  mid.localTransform = glm::translate(glm::mat4(1.0f), { 0, 1, 0 });

  tip.name = "tip"; tip.parentIndex = 1;
  tip.offsetMatrix = glm::translate(glm::mat4(1.0f), { 0, 2, 0 });
  tip.localTransform = glm::translate(glm::mat4(1.0f), { 0, 1, 0 });

  m_animator = std::make_unique<Animator>();
  m_animator->setSkeleton({ root, mid, tip });

  // Physics World
  m_physicsWorld = std::make_unique<PhysicsWorld>();

  // Cube Mesh
  std::vector<Vertex> cubeVerts = {
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
  std::vector<unsigned int> cubeIdx = {
    0,1,2,  2,3,0, // back face
    4,5,6,  6,7,4, // front face
    0,4,7,  7,3,0, // left face
    1,5,6,  6,2,1, // right face
    3,2,6,  6,7,3, // top face
    0,1,5,  5,4,0, // bottom face
  };
  m_cubeMesh = std::make_unique<Mesh>(cubeVerts, cubeIdx);
  m_cubeTransform = std::make_unique<Transform>();
  m_cubeTransform->setPosition({ 0.0f, 5.0f, 0.0f }); // Start 5 units up

  // Dynamic Box - half-extents match our unit cube (0.5 in each axis)
  m_cubeBody = std::make_unique<RigidBodyComponent>(
    *m_physicsWorld, *m_cubeTransform,
    CollisionShape::Box, glm::vec3(0.5f),
    1.0f // 1kg - dynamic
  );
  // Lock rotation so cube doesn't tumble
  m_cubeBody->setAngularForce({ 0.0f, 0.0f, 0.0f });

  // Floor
  // A wide, flat box to visualize floor
  std::vector<Vertex> floorVerts = {
    {{-5.0f,0.0f,-5.0f},{0.3f,0.3f,0.3f}},
    {{ 5.0f,0.0f,-5.0f},{0.3f,0.3f,0.3f}},
    {{ 5.0f,0.0f, 5.0f},{0.3f,0.3f,0.3f}},
    {{-5.0f,0.0f, 5.0f},{0.3f,0.3f,0.3f}},
  };
  std::vector<unsigned int> floorIdx = {0,1,2, 2,3,0};
  m_floorMesh = std::make_unique<Mesh>(floorVerts, floorIdx);
  m_floorTransform = std::make_unique<Transform>();
  // static plane - mass 0, shape size ignored for plane
  m_floorBody = std::make_unique<RigidBodyComponent>(
    *m_physicsWorld, *m_floorTransform,
    CollisionShape::Plane, glm::vec3(0.0f),
    0.0f // mass=0 -> static
  );

  // Bone marker mesh (small cube, reused for all bones)
  std::vector<Vertex> markerVerts = {
    {{-0.5f,-0.5f,-0.5f}, {1.0f, 0.8f, 0.0f}}, // Gold color
    {{ 0.5f,-0.5f,-0.5f}, {1.0f, 0.8f, 0.0f}},
    {{ 0.5f, 0.5f,-0.5f}, {1.0f, 0.8f, 0.0f}},
    {{-0.5f, 0.5f,-0.5f}, {1.0f, 0.8f, 0.0f}},
    {{-0.5f,-0.5f, 0.5f}, {1.0f, 0.9f, 0.2f}},
    {{ 0.5f,-0.5f, 0.5f}, {1.0f, 0.9f, 0.2f}},
    {{ 0.5f, 0.5f, 0.5f}, {1.0f, 0.9f, 0.2f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.9f, 0.2f}},
  };
  std::vector<unsigned int> markerIdx = {
    0,1,2, 2,3,0, 4,5,6, 6,7,4,
    0,4,7, 7,3,0, 1,5,6, 6,2,1,
    3,2,6, 6,7,3, 0,1,5, 5,4,0,
  };
  m_boneMarkerMesh = std::make_unique<Mesh>(markerVerts, markerIdx);

  m_lua = std::make_unique<LuaState>();

  // Combat system
  m_combatSystem = std::make_unique<CombatSystem>();

  // Player combat component
  m_playerCombat = std::make_unique<CombatComponent>(
    "Player", 400.0f, 100.0f, 60.0f); // HP, Stam, Poise

  // Wire death/hit callbacks to lua
  m_playerCombat->onDeath = [this]() {
    m_lua->callFunction("onPlayerDeath");
  };
  m_playerCombat->onHit = [this](const HitEvent& hit){
    m_lua->callFunction("onPlayerHit", hit.damage, hit.damageType);
  };

  // Enemy combat component (dummy cube)
  m_enemyCombat = std::make_unique<CombatComponent>(
    "Dummy", 200.0f, 999.0f, 30.0f);

  //register both with the combat system
  m_combatSystem->registerCombatant(m_playerCombat.get());
  m_combatSystem->registerCombatant(m_enemyCombat.get());

  m_enemyCombat->onDeath = [this]() {
    LOG_INFO("Enemy Defated!");

    m_flags.set(forge::Flags::BOSS_DUMMY_DEAD, true);

    EventBus::publish(EntityDiedEvent{
      .entityName = "Dummy",
      .position = m_enemyCombat->getWorldPos()
    });
  };

  // AI Setup

  m_enemyAI = std::make_unique<AIComponent>(
    "Dummy", *m_cubeTransform, *m_enemyCombat);

  // Patrol in square around scene
  m_enemyAI->addWaypoint({ -3.0f, 0.0f, -3.0f });
  m_enemyAI->addWaypoint({  3.0f, 0.0f, -3.0f });
  m_enemyAI->addWaypoint({  3.0f, 0.0f,  3.0f });
  m_enemyAI->addWaypoint({ -3.0f, 0.0f,  3.0f });

  // AI Lua Callbacks
  m_enemyAI->onPlayerDetected = [this](){
    m_lua->callFunction("onPlayerDetected");
  };
  m_enemyAI->onPlayerLost = [this](){
    m_lua->callFunction("onPlayerLost");
  };
  m_enemyAI->onChooseAttack = [this]() -> std::string {
    sol::protected_function fn = m_lua->get()["onChooseAttack"];
    if (!fn.valid()) return "R1";
    auto result = fn();
    if (!result.valid()) return "R1";
    return result.get<std::string>();
  };

  // Expose AI To Lua
  m_lua->get()["enemyAI"] = m_enemyAI.get();

  EventBus::subscribe<FlagChangeEvent>([](const FlagChangeEvent& e) {
    LOG_INFO("[Event] Flag {} changed -> {}", e.flagId, e.newValue);
  });

  EventBus::subscribe<ScriptEvent>([](const ScriptEvent& e) {
    LOG_INFO("[Event] Script event: '{}' data='{}'", e.name, e.data);
  });

  m_lua->get()["Flags"] = &m_flags;

  // Expose combat to Lua
  m_lua->get()["playerCombat"] = m_playerCombat.get();
  m_lua->get()["enemyCombat"] = m_enemyCombat.get();

  // Load attack defs first, then combat script
  m_lua->loadScript("../../../../game/scripts/combat/attacks.lua");
  m_lua->loadScript("../../../../game/scripts/combat/player_combat.lua");
  m_lua->loadScript("../../../../game/scripts/events/flags.lua");
  m_lua->loadScript("../../../../game/scripts/events/demo_quest.lua");
  // Load AI Script
  m_lua->loadScript("../../../../game/scripts/ai/enemy_ai.lua");
  m_lua->callFunction("onAIInit");
  // Lua
  m_scriptPath = "../../../../game/scripts/cube_controller.lua";
  m_lua->get()["transform"] = m_cubeTransform.get();
  m_lua->loadScript(m_scriptPath);

  m_flags.loadFromFile("save.json");

  m_running = true;
}

Application::~Application() {
  shutdown();
}

void Application::init() {
  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

void Application::shutdown() {
  m_flags.saveToFile("save.json");

  m_lua.reset();
  m_cubeBody.reset();
  m_floorBody.reset();
  m_physicsWorld.reset();
  m_cubeMesh.reset();
  m_cubeTransform.reset();
  m_floorMesh.reset();
  m_floorTransform.reset();
  m_boneMarkerMesh.reset();
  m_animator.reset();
  m_combatSystem.reset();
  m_playerCombat.reset();
  m_enemyCombat.reset();
  m_enemyAI.reset();
  m_shader.reset();
  m_camera.reset();

  if (m_window) {
    glfwDestroyWindow(m_window);
    m_window = nullptr;
  }
  glfwTerminate();
}

void Application::update(float dt) {
  m_totalTime += dt;

  if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    m_running = false;

  if (glfwGetKey(m_window, GLFW_KEY_F5) == GLFW_PRESS) {
    m_lua->loadScript(m_scriptPath);
  }

  float playerSpeed = 4.0f;
  if (glfwGetKey(m_window, GLFW_KEY_UP) == GLFW_PRESS)
    m_playerWorldPos.z -= playerSpeed * dt;
  if (glfwGetKey(m_window, GLFW_KEY_DOWN) == GLFW_PRESS)
    m_playerWorldPos.z += playerSpeed * dt;
  if (glfwGetKey(m_window, GLFW_KEY_LEFT) == GLFW_PRESS)
    m_playerWorldPos.x -= playerSpeed * dt;
  if (glfwGetKey(m_window, GLFW_KEY_RIGHT) == GLFW_PRESS)
    m_playerWorldPos.x += playerSpeed * dt;

  // AI Update
  m_enemyAI->update(dt, m_playerWorldPos);
  m_lua->callFunction("onAIUpdate", dt);

  m_cubeBody->teleport(m_cubeTransform->getPosition());

  // Physics setup
  m_physicsWorld->step(dt);
  m_cubeBody->syncTransform();

  // Animation Demo
  if (m_animator) {
    float wave = sinf(m_totalTime * 2.0f);

    // Manually drive bone matrices with sine-waves
    // Simulates real animation clip
    auto& globals = const_cast<std::vector<glm::mat4>&>(
      m_animator->getGlobalTransforms());
    globals.resize(3);

    globals[0] = glm::rotate(glm::mat4(1.0f), wave * 0.3f, glm::vec3(0,0,1));
    globals[1] = globals[0]
      * glm::translate(glm::mat4(1.0f), {0,1,0}) 
      * glm::rotate(glm::mat4(1.0f), wave * 0.5f, glm::vec3(0,0,1));
    globals[2] = globals[1]
      * glm::translate(glm::mat4(1.0f), {0,1,0}) 
      * glm::rotate(glm::mat4(1.0f), wave * 0.8f, glm::vec3(0,0,1));

    auto& matrices = const_cast<std::vector<glm::mat4>&>(
      m_animator->getBoneMatrices());
    matrices.resize(3);
    matrices[0] = globals[0];
    matrices[1] = globals[1];
    matrices[2] = globals[2];
  }

  // Player combat world data
  glm::vec3 playerFwd = { 0.0f, 0.0f, -1.0f };
  m_playerCombat->setWorldData(m_playerWorldPos, playerFwd);

  // Input -> Lua
  bool jPressed = glfwGetKey(m_window, GLFW_KEY_J) == GLFW_PRESS;
  bool kPressed = glfwGetKey(m_window, GLFW_KEY_K) == GLFW_PRESS;
  bool lPressed = glfwGetKey(m_window, GLFW_KEY_L) == GLFW_PRESS;

  auto inputTable = m_lua->get().create_table();
  inputTable["attackLight"] = jPressed && !m_prevInput.attackLight; 
  inputTable["attackHeavy"] = kPressed && !m_prevInput.attackHeavy; 
  inputTable["guard"] = lPressed; 
  m_prevInput = { jPressed, kPressed, lPressed };

  m_lua->callFunction("onCombatUpdate", dt, inputTable);

  m_combatSystem->update(dt);

  m_lua->callFunction("onQuestUpdate", dt);

  static bool bwasPressed = false;
  bool bPressed = glfwGetKey(m_window, GLFW_KEY_B) == GLFW_PRESS;
  if (bPressed && !bwasPressed)
    m_lua->callFunction("onBonfireReset");
  bwasPressed = bPressed;
}

void Application::render() {
  glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  m_shader->bind();
  glm::mat4 vp = m_camera->getViewProjection(); 
  
  // Draw cube
  m_shader->setMat4("u_mvp", vp * m_cubeTransform->getModelMatrix());
  m_cubeMesh->draw();

  // draw floor
  m_shader->setMat4("u_mvp", vp * m_floorTransform->getModelMatrix());
  m_floorMesh->draw();

  // Bone chain visualization
  if (m_animator && !m_animator->getGlobalTransforms().empty()) {
    const auto& globals = m_animator->getGlobalTransforms();

    for (const glm::mat4& globalTransform : globals) {
      // Extract bones world pos from col 3 of matrix
      glm::vec3 bonePos = glm::vec3(globalTransform[3]);

      // Build model matrix: place small cube at bone pos
      glm::mat4 markerModel = glm::translate(glm::mat4(1.0f), bonePos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.15f));

      m_shader->setMat4("u_mvp", vp * markerModel);
      m_boneMarkerMesh->draw();
    }
  }


  m_shader->unbind();
  glfwSwapBuffers(m_window);
}

void Application::run() {
  auto lastTime = std::chrono::high_resolution_clock::now();

  while (m_running && !glfwWindowShouldClose(m_window)) {
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    glfwPollEvents();
    update(dt);
    render();
  }
}

}
