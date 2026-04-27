#include <forge/Application.h>
#include <forge/Logger.h>
#include <forge/PhysicsWorld.h>
#include <forge/LuaState.h>
#include <forge/CombatSystem.h>
#include <forge/FlagManager.h>
#include <forge/DebugUI.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <stdexcept>
#include <cstring>

namespace forge {

// Getters
PhysicsWorld& Application::getPhysics() { return *m_physics; }
LuaState& Application::getLua() { return *m_lua; }
CombatSystem& Application::getCombat() { return *m_combat; }
FlagManager& Application::getFlags() { return *m_flags; }
DebugUI& Application::getDebugUI() { return *m_debugUI; }
GLFWwindow* Application::getWindow() const { return m_window; }
int Application::getWidth() const { return m_width; }
int Application::getHeight() const { return m_height; }
float Application::getTotalTime() const { return m_totalTime; }

Application::Application(int width, int height, const char* title)
  : m_width(width), m_height(height)
{
  initWindow();
  initSystems();


  m_running = true;
}

Application::~Application() {
  shutdownSystems();
}

void Application::initWindow() {
  if (!glfwInit())
    throw std::runtime_error("Failed to initialize GLFW");

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
#ifdef __APPLE__
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  m_window = glfwCreateWindow(m_width, m_height, "Forge Engine", nullptr, nullptr);
  if (!m_window) { glfwTerminate(); throw std::runtime_error("Failed to created window"); }

  glfwMakeContextCurrent(m_window);
  glfwSwapInterval(1);

  if (glewInit() != GLEW_OK)
    throw std::runtime_error("Failed to initialize GLEW");

  glEnable(GL_DEPTH_TEST);

  LOG_INFO("Window created - {}x{}", m_width, m_height);
  LOG_INFO("OpenGL: {}", (const char*)glGetString(GL_VERSION));
  LOG_INFO("GPU: {}", (const char*)glGetString(GL_RENDERER));
}

void Application::initSystems() {
  m_physics = std::make_unique<PhysicsWorld>();
  m_lua = std::make_unique<LuaState>();
  m_combat = std::make_unique<CombatSystem>();
  m_combat->setPhysicsWorld(m_physics.get());
  m_combat->setLuaState(m_lua.get());
  m_flags = std::make_unique<FlagManager>();

  // ImGui - must come after OpenGL context is live (initWindow has ran)
  m_debugUI = std::make_unique<DebugUI>();
  m_debugUI->init(m_window);
  m_debugUI->setCombatSystem(m_combat.get());
  m_debugUI->setFlagManager(m_flags.get());
  m_debugUI->setLuaState(m_lua.get());
  LOG_INFO("Engine systems initialized");
}

void Application::shutdownSystems() {
  // ImGui first - no deps on any other systems
  if (m_debugUI) { m_debugUI->shutdown(); m_debugUI.reset(); }
  // Shutdown order matters - Lua first (Lua may reference other systems)
  m_lua.reset();
  m_combat.reset();
  m_physics.reset();
  m_flags.reset();

  if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
  glfwTerminate();
}

void Application::run() {
  // Call init where it is actually initialized
  onInit();
  auto lastTime = std::chrono::high_resolution_clock::now();

  while (m_running && !glfwWindowShouldClose(m_window)) {
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    m_totalTime += dt;

    glfwPollEvents();

    if (isKeyDown(GLFW_KEY_ESCAPE)) m_running = false;

    if (m_frameCount++ == 0) {
      glfwSwapBuffers(m_window);
      lastTime = std::chrono::high_resolution_clock::now();
      continue;
    }
    onUpdate(dt);

    // Render
    glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // build ImGui panels for this frame (must come before onRender so
    // the game can push its own panels in onRender if needed later)
    m_debugUI->begin(dt);

    onRender();

    //Flush ImGui draw data on top of scene
    m_debugUI->end();

    glfwSwapBuffers(m_window);
  }
  // ForgeGame still exists here
  onShutdown();
}

bool Application::isKeyDown(int key) const {
  if (key < 0 || key >= KEY_COUNT) return false;
  return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool Application::isKeyPressed(int key) {
  if (key < 0 || key >= KEY_COUNT) return false;
  bool curr = glfwGetKey(m_window, key) == GLFW_PRESS;
  bool prev = m_prevKeyStates[key] == GLFW_PRESS;
  m_prevKeyStates[key] = curr ? GLFW_PRESS : GLFW_RELEASE;
  return curr && !prev;
}

void Application::setClearColor(float r, float g, float b) {
  m_clearColor[0] = r;
  m_clearColor[1] = g;
  m_clearColor[2] = b;
}

}
