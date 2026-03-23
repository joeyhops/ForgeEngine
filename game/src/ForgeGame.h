#pragma once
#include <forge/Application.h>
#include <forge/Mesh.h>
#include <forge/Shader.h>
#include <forge/Camera.h>
#include <forge/Transform.h>
#include <forge/RigidBodyComponent.h>
#include <forge/CombatComponent.h>
#include <forge/AIComponent.h>

#include <memory>
#include <vector>

class ForgeGame : public forge::Application {
public:
  ForgeGame();

protected:
  void onInit() override;
  void onUpdate(float dt) override;
  void onRender() override;
  void onShutdown() override;

private:
  void setupPlayer();
  void setupEnemy();
  void setupLevel();
  void setupScripts();
  void handleInput(float dt);

  // Rendering
  std::unique_ptr<forge::Shader> m_shader;
  std::unique_ptr<forge::Camera> m_camera;

  // Player
  struct PlayerEntity {
    std::unique_ptr<forge::Mesh> mesh;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::RigidBodyComponent> body;
    std::unique_ptr<forge::CombatComponent> combat;
    glm::vec3 forward = { 0, 0, -1 };
  } m_player;

  // Enemy
  struct EnemyEntity {
    std::unique_ptr<forge::Mesh> mesh;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::RigidBodyComponent> body;
    std::unique_ptr<forge::CombatComponent> combat;
    std::unique_ptr<forge::AIComponent> ai;
  } m_enemy;

  // Level geometry
  struct LevelEntity {
    std::unique_ptr<forge::Mesh> mesh;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::RigidBodyComponent> body;
  };
  std::vector<LevelEntity> m_level;

  // Input
  struct InputState { bool j, k, l; } m_prevInput = {};
};
