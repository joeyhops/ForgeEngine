#pragma once
#include <forge/CombatComponent.h>
#include <forge/AttackData.h>
#include <forge/FlagManager.h>
#include <forge/FlagIds.h>

#include <memory>
#include <string>
struct GLFWwindow;

namespace forge {
  class Shader;
  class Animator;
  class Mesh;
  class Camera;
  class Transform;
  class LuaState;
  class PhysicsWorld;
  class RigidBodyComponent;
  class CombatSystem;
  class AIComponent;
}

namespace forge {

  class Application {
  public:
    Application(int width, int height, const char* title);
    ~Application();

    // Should only ever be one Application
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

  private:
    void init();
    void shutdown();
    void update(float dt);
    void render();

    GLFWwindow* m_window = nullptr;
    bool m_running = false;
    int m_width;
    int m_height;
  
    // Renderer
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Animator>m_animator;

    // Cube
    std::unique_ptr<Mesh> m_cubeMesh;
    std::unique_ptr<Transform> m_cubeTransform;
    std::unique_ptr<RigidBodyComponent> m_cubeBody;

    // Floor (static - visual + physics)
    std::unique_ptr<Mesh> m_floorMesh;
    std::unique_ptr<Transform> m_floorTransform;
    std::unique_ptr<RigidBodyComponent>m_floorBody;

    std::unique_ptr<Mesh> m_boneMarkerMesh;

    float m_totalTime = 0.0f; // For rotation animation

    std::unique_ptr<PhysicsWorld> m_physicsWorld;
    
    std::unique_ptr<CombatSystem>m_combatSystem;
    std::unique_ptr<CombatComponent> m_playerCombat;
    glm::vec3 m_playerWorldPos = { 0.0f, 0.0f, 5.0f };

    std::unique_ptr<CombatComponent>m_enemyCombat;
    std::unique_ptr<AIComponent> m_enemyAI;

    struct InputState {
      bool attackLight = false;
      bool attackHeavy = false;
      bool guard = false;
    };
    InputState m_prevInput; // Track previous frame to detect key-down

    std::unique_ptr<LuaState>m_lua;
    std::string m_scriptPath;

    FlagManager m_flags;
  };

}
