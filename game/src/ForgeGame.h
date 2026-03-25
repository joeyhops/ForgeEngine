#pragma once
#include <forge/Application.h>
#include <forge/AssetManager.h>
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
  void setupRenderer();
  void setupPlayer();
  void setupEnemy();
  void setupLevel();
  void setupScripts();
  void handleInput(float dt);

  void drawModel(const forge::ModelData& model,
                 const forge::Transform& transform);

  // Rendering
  std::shared_ptr<forge::Shader> m_shader;
  std::unique_ptr<forge::Camera> m_camera;

  // Player
  struct PlayerEntity {
    forge::ModelData model;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::RigidBodyComponent> body;
    std::unique_ptr<forge::CombatComponent> combat;
    glm::vec3 forward = { 0, 0, -1 };
  } m_player;

  // Enemy
  struct EnemyEntity {
    forge::ModelData model;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::RigidBodyComponent> body;
    std::unique_ptr<forge::CombatComponent> combat;
    std::unique_ptr<forge::AIComponent> ai;
  } m_enemy;

  // Level geometry
  struct LevelPiece {
    forge::ModelData model;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::RigidBodyComponent> body;
  };
  std::vector<LevelPiece> m_level;

  forge::ModelData m_floorModel;
  forge::ModelData m_wallModel;
  forge::ModelData m_towerModel;

  // Input
  struct InputState { bool j, k, l; } m_prevInput = {};
};
