#include <forge/Application.h>
#include <forge/Logger.h>
#include <forge/Shader.h>
#include <forge/Mesh.h>
#include <forge/Camera.h>
#include <forge/Transform.h>
#include <forge/LuaState.h>
#include <forge/PhysicsWorld.h>
#include <forge/RigidBodyComponent.h>

#include <glm/glm.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <memory>
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

  // Lua
  m_scriptPath = "../../../../game/scripts/cube_controller.lua";
  m_lua = std::make_unique<LuaState>();
  m_lua->get()["transform"] = m_cubeTransform.get();
  m_lua->loadScript(m_scriptPath);

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
  m_lua.reset();
  m_cubeBody.reset();
  m_floorBody.reset();
  m_physicsWorld.reset();
  m_cubeMesh.reset();
  m_cubeTransform.reset();
  m_floorMesh.reset();
  m_floorTransform.reset();
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
    LOG_INFO("Hot reloading script...");
    m_lua->loadScript(m_scriptPath);
  }

  // Physics setup
  // Bullet advances the simulation, resolves collisions
  m_physicsWorld->step(dt);

  // sync phys results back to transforms
  m_cubeBody->syncTransform();
  // Floor is static, no sync

  // Lua Note:
  // Lua script can still read transform position/rotation
  // but physics owns the cubes movement. script drives logic,
  // physics drives position.
  m_lua->callFunction("onUpdate", dt, m_totalTime);
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

  //* m_transform->getModelMatrix();

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
