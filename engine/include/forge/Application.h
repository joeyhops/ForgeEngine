#pragma once
#include <memory>
#include <string>
struct GLFWwindow;

namespace forge {
  class Shader;
  class Mesh;
  class Camera;
  class Transform;
  class LuaState;
  class PhysicsWorld;
  class RigidBodyComponent;

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

    // Cube
    std::unique_ptr<Mesh> m_cubeMesh;
    std::unique_ptr<Transform> m_cubeTransform;
    std::unique_ptr<RigidBodyComponent> m_cubeBody;

    // Floor (static - visual + physics)
    std::unique_ptr<Mesh> m_floorMesh;
    std::unique_ptr<Transform> m_floorTransform;
    std::unique_ptr<RigidBodyComponent>m_floorBody;

    float m_totalTime = 0.0f; // For rotation animation

    std::unique_ptr<PhysicsWorld> m_physicsWorld;
    std::unique_ptr<LuaState>m_lua;
    std::string m_scriptPath;
  };

}
