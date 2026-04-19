#pragma once
#include <forge/Application.h>
#include <forge/AssetManager.h>
#include <forge/Shader.h>
#include <forge/Camera.h>
#include <forge/ThirdPersonCamera.h>
#include <forge/CharacterController.h>
#include <forge/Transform.h>
#include <forge/RigidBodyComponent.h>
#include <forge/CombatComponent.h>
#include <forge/AIComponent.h>
#include <forge/Animator.h>
#include <forge/AnimationClip.h>
#include <forge/LevelLoader.h>
#include <forge/TriggerVolume.h>
#include <forge/WeaponDef.h>
#include <forge/EquipmentComponent.h>

#include <memory>
#include <unordered_map>
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
  void setupLevel(const std::string& levelName = "level_01");
  void setupScripts();
  void handleInput(float dt);
  void spawnWeaponPickup(const glm::vec3& pos, const std::string& weaponId, bool respawns);
  void drawModelAtMatrix(const forge::ModelData& model, const glm::mat4& matrix);

  void drawModel(const forge::ModelData& model,
                 const forge::Transform& transform);

  void drawSkinnedModel(const forge::SkinnedModelData& model,
                        const forge::Transform& transform,
                        const forge::Animator& animator);

  // Rendering
  std::shared_ptr<forge::Shader> m_shader; // basic.vert/frag
  std::shared_ptr<forge::Shader> m_skinnedShader; // skinned.vert/basic.frag
  std::shared_ptr<forge::Shader> m_debugLineShader;
  std::unique_ptr<forge::Camera> m_camera;
  std::unique_ptr<forge::ThirdPersonCamera> m_tpCamera;
  
  // Player
  struct PlayerEntity {
    forge::SkinnedModelData skinnedModel;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::CharacterController> controller;
    std::unique_ptr<forge::CombatComponent> combat;
    std::unique_ptr<forge::EquipmentComponent> equipment;
    std::unique_ptr<forge::Animator> animator;
    forge::ModelData weaponModel;
    std::unordered_map<std::string,
      std::shared_ptr<forge::AnimationClip>> clips;
    glm::vec3 forward = { 0, 0, -1 };
  } m_player;

  // Enemy
  struct EnemyEntity {
    forge::SkinnedModelData skinnedModel;
    std::unique_ptr<forge::Transform> transform;
    std::unique_ptr<forge::CharacterController> controller;
    std::unique_ptr<forge::CombatComponent> combat;
    std::unique_ptr<forge::EquipmentComponent> equipment;
    std::unique_ptr<forge::AIComponent> ai;
    std::unique_ptr<forge::Animator> animator;
    forge::ModelData weaponModel;
    std::unordered_map<std::string,
      std::shared_ptr<forge::AnimationClip>> clips;
  } m_enemy;

  forge::ModelData m_levelMesh; // OBJ Geometry
  std::unique_ptr<forge::Transform> m_levelTransform; 
  std::unique_ptr<forge::RigidBodyComponent> m_levelPhysicsBody; // Collision
  forge::LevelData m_levelData; // parsed entities

  std::vector<std::unique_ptr<forge::TriggerVolume>> m_triggerVolumes;

  struct BonfireVolume {
    std::unique_ptr<forge::TriggerVolume> trigger;
    int bonfireId = 0;
    int targetFlag = 0;
  };
  std::vector<BonfireVolume> m_bonfires;

  struct WeaponPickup {
    std::unique_ptr<forge::TriggerVolume> trigger;
    std::unique_ptr<forge::Transform> transform;
    forge::ModelData model;
    std::string weaponId;
    bool respawns = false;
    bool collected = false;
  };
  std::vector<WeaponPickup> m_weaponPickups;

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
  bool m_usingTBLevel = false;

  // Input
  
  double m_prevMouseX = 0.0;
  double m_prevMouseY = 0.0;
  bool m_firstMouse = true;
  bool m_uiMouseMode = false;

  float m_playerWalkSpeed = 3.0f;
  float m_playerSprintSpeed = 6.0f;

  // Lock on: stable buffer holding the enemys chest pos each frame
  glm::vec3 m_lockOnEnemyPos = { 0.0f, 0.0f, 0.0f };
  bool m_lockedOn = false;

  // Dodge state
  float m_dodgeTimer = 0.0f; // Counts down; > 0 == currently dodging
  glm::vec3 m_dodgeDir = { 0.0f, 0.0f, -1.0f }; // direction frozen at start of dodge
  float m_dodgeDuration = 0.45f;

  struct InputState { bool j, k, l; } m_prevInput = {};

  static constexpr const char* k_saveFilePath = "saves/flags.json";
};
