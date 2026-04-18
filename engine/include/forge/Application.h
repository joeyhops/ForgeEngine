#pragma once
#include <memory>
struct GLFWwindow;

namespace forge {

class PhysicsWorld;
class LuaState;
class CombatSystem;
class FlagManager;
class DebugUI;

class Application {

public:
  Application(int width, int height, const char* title);
  virtual ~Application();

  // Should only ever be one Application
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Runs main loop, calls virtual hooks each frame
  void run();

  // System accessors - Game subclass uses these in onInit
  PhysicsWorld& getPhysics();
  LuaState& getLua();
  CombatSystem& getCombat(); 
  FlagManager& getFlags(); 
  DebugUI& getDebugUI();
  GLFWwindow* getWindow() const; 
  int getWidth() const; 
  int getHeight() const; 
  float getTotalTime() const; 

  // Input polling helpers
  bool isKeyDown(int glfwKey) const;
  bool isKeyPressed(int glfwKey); // True only on first frame of press

protected:
  // Override these in actual game
  virtual void onInit() = 0; // Setup scene, entities, scripts
  virtual void onUpdate(float dt) = 0; // Per-frame game logic
  virtual void onRender() = 0; // Submit draw calls
  virtual void onShutdown() = 0; // Release game resources

  // Clear color - game can change this
  void setClearColor(float r, float g, float b);

private:
  void initWindow();
  void initSystems();
  void shutdownSystems();

  int m_frameCount = 0;
  GLFWwindow* m_window = nullptr;
  int m_width, m_height;
  float m_totalTime = 0.0f;
  bool m_running = false;
  float m_clearColor[3] = { 0.1f, 0.1f, 0.12f };

  // Prev key states for isKeyPressed
  static constexpr int KEY_COUNT = 512;
  int m_prevKeyStates[KEY_COUNT] = {};

  //Engine Systems - Owned here, accessed via getters
  std::unique_ptr<PhysicsWorld> m_physics;
  std::unique_ptr<LuaState> m_lua;
  std::unique_ptr<CombatSystem> m_combat;
  std::unique_ptr<FlagManager> m_flags;
  std::unique_ptr<DebugUI> m_debugUI;
};

}
