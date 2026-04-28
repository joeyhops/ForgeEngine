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
#include <forge/map/MapScene.h>
#include <forge/map/EntityAssembler.h>

#include <memory>
#include <unordered_map>
#include <vector>

class ForgeGame : public forge::Application {
public:
  ForgeGame();
  void setInitialMap(const std::string& mapName) { m_initialMap = mapName; }

protected:
  void onInit() override;
  void onUpdate(float dt) override;
  void onRender() override;
  void onShutdown() override;

private:
  std::string m_initialMap = "bonfire";

  void setupRenderer();
  void setupPlayer();
  void setupEnemy();
  void setupLevel(const std::string& levelName = "level_01");
  void setupScripts();
  void handleInput(float dt);
  void applyMovement(float dt);
  void spawnWeaponPickup(const glm::vec3& pos, const std::string& weaponId, bool respawns);
  void drawModelAtMatrix(const forge::ModelData& model, const glm::mat4& matrix);

  void drawModel(const forge::ModelData& model,
                 const forge::Transform& transform);

  void drawSkinnedModel(const forge::SkinnedModelData& model,
                        const forge::Transform& transform,
                        const forge::Animator& animator);

  std::vector<forge::MapRenderObject> buildRenderObjects(const forge::EntityGeometry& geom);

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
    bool active = false;
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
  std::unique_ptr<forge::Transform> m_levelTransform; 
  std::unique_ptr<forge::RigidBodyComponent> m_levelPhysicsBody; // Collision
  forge::LevelData m_levelData; // parsed entities

  std::vector<std::unique_ptr<forge::TriggerVolume>> m_triggerVolumes;

  std::unique_ptr<forge::MapScene> m_mapScene;
  std::shared_ptr<forge::Shader> m_brushShader;
  forge::EntityAssembler m_assembler;
  std::vector<forge::EntityInstance> m_mapEntities;

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
  glm::vec3 m_moveDir = { 0.0f, 0.0f, 0.0f }; // desired direction, unit or zero
  float m_moveSpeed = 0.0f;
  bool m_dodgePending = false;
  float m_dodgeFacingAngle = 0.0f;
  bool m_inputLocked = false; // set by LockInput TAE event

  bool m_rmOverride = false;
  float m_rmScale = 1.0f;
  glm::bvec3 m_rmAxes = { true, false, true };
  bool m_kinematicMode = false;

  // Damage feedback
  float m_hitStopTimer = 0.0f;
  static constexpr float k_hitStopLight = 0.050f; // 4 frames
  static constexpr float k_hitStopHeavy = 0.100f; // 6frames

  float m_hitFlashAlpha = 0.0f;
  static constexpr float k_hitFlashDecayRate = 2.2f;

  float m_shakeTimer = 0.0f;
  float m_shakeMagnitude = 0.0f;
  static constexpr float k_shakeDuration = 0.28f; //seconds
  static constexpr float k_shakeMag = 0.10f;

  struct DamageNumber {
    glm::vec3 worldPos;
    float damage;
    float lifetime; // seconds remaining
    bool heavy;
  };
  std::vector<DamageNumber> m_damageNumbers;
  static constexpr float k_damNumberLifetime = 1.0f; // seconds visible
  static constexpr float k_damNumberRiseSpeed = 1.2f; // m/s upward drift

  struct InputState { bool j, k, l; } m_prevInput = {};

  static constexpr const char* k_saveFilePath = "saves/flags.json";
  
  // Debugging

  struct HitboxTrail {
    static constexpr int N = 10;
    glm::mat4 transforms[N];
    glm::vec3 halfExtents[N];
    int head = 0;
    int count = 0;

    void push(const glm::mat4& t, const glm::vec3& h) {
      transforms[head] = t;
      halfExtents[head] = h;
      head = (head + 1) % N;
      if (count < N) count++;
    }

    void clear() { head = 0; count = 0; }
  };
  HitboxTrail m_playerTrail;
  HitboxTrail m_enemyTrail;

  forge::CombatComponent::AttackPhase m_prevPlayerPhase = forge::CombatComponent::AttackPhase::None;
  forge::CombatComponent::AttackPhase m_prevEnemyPhase = forge::CombatComponent::AttackPhase::None;
};
