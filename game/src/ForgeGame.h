#pragma once

#include "PlayerEntity.h"
#include "EnemyEntity.h"
#include "InputManager.h"

#include <forge/Application.h>
#include <forge/AssetManager.h>
#include <forge/Shader.h>
#include <forge/Camera.h>
#include <forge/ThirdPersonCamera.h>
#include <forge/Transform.h>
#include <forge/RigidBodyComponent.h>
#include <forge/CombatComponent.h>
#include <forge/LevelLoader.h>
#include <forge/TriggerVolume.h>
#include <forge/WeaponDef.h>
#include <forge/HitboxTypes.h>
#include <forge/map/MapScene.h>
#include <forge/map/EntityAssembler.h>

#include <stb_truetype.h>

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
  enum class GameState { Playing, DeathSequence, YouDied, Respawning };
  std::string m_initialMap = "bonfire";

  void setupRootMotionAnimEvents();
  void setupLevel(const std::string& levelName = "level_01");
  void setupScripts();
  void registerEntityFactories();
  void handleInput(float dt);
  void applyMovement(float dt);
  void spawnWeaponPickup(const glm::vec3& pos, const std::string& weaponId, bool respawns);

  void renderScene();
  void renderDebugOverlays();
  void renderHUD();
  void renderDeathOverlay();

  std::vector<forge::MapRenderObject> buildRenderObjects(const forge::EntityGeometry& geom);
  
  void initHUD();
  void drawHUDRect(float x, float y, float w, float h,
                   float r, float g, float b, float a);
  void drawHUDBar(float x, float y, float w, float h,
                  float fill, float r, float g, float b);
  void drawHUDText(float x, float y, float pixelHeight,
                   const char* text, float r, float g, float b, float a);

  // Rendering
  std::shared_ptr<forge::Shader> m_hudShader;
  std::shared_ptr<forge::Shader> m_hudTextShader;
  
  std::unique_ptr<forge::Camera> m_camera;
  std::unique_ptr<forge::ThirdPersonCamera> m_tpCamera;
  
  unsigned int m_hudVAO = 0;
  unsigned int m_hudVBO = 0;
  unsigned int m_textVAO = 0;
  unsigned int m_textVBO = 0;
  unsigned int m_fontAtlasTexture = 0;

  static constexpr int k_fontAtlasW = 512;
  static constexpr int k_fontAtlasH = 512;
  static constexpr int k_fontFirstChar = 32; // ascii space
  static constexpr int k_fontNumChars = 96; // space - tilde

  stbtt_bakedchar m_glyphData[96]; // one entry per baked char

  PlayerEntity m_player;
  EnemyEntity m_enemy;
  InputManager m_input;

  struct BonfireVolume {
    std::unique_ptr<forge::TriggerVolume> trigger;
    int bonfireId = 0;
    int targetFlag = 0;
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };

    // light properties
    glm::vec3 lightColor = { 1.0f, 0.45f, 0.05f };
    float lightIntensity = 8.0f;
    float lightRange = 12.0f;
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
  forge::EntityAssembler m_assembler;
  std::vector<forge::EntityInstance> m_mapEntities;
  std::vector<std::pair<forge::ModelData, glm::mat4>> m_staticProps;

  bool m_uiMouseMode = false;

  float m_playerWalkSpeed = 5.0f;
  float m_playerSprintSpeed = 10.0f;

  // Lock on: stable buffer holding the enemys chest pos each frame
  glm::vec3 m_lockOnEnemyPos = { 0.0f, 0.0f, 0.0f };
  bool m_lockedOn = false;

  // Per-frame movement output - written by handleInput() consumed by applyMovement()
  glm::vec3 m_moveDir = { 0.0f, 0.0f, 0.0f }; // desired direction, unit or zero
  float m_moveSpeed = 0.0f;
  float m_smoothMoveX = 0.0f;
  float m_smoothMoveZ = 0.0f;

  glm::vec3 m_facingTarget = { 0.0f, 0.0f, -1.0f }; // desired forward (updated in handleInput)
  static constexpr float k_turnRateDeg = 540.0f; // deg/s in free mvmt
  static constexpr float k_lockOnTurnRate = 1080.0f; // deg/s when locked on
  static constexpr float k_locoParamSmoothing = 10.0f;

  // Death and Respawn
  GameState m_gameState = GameState::Playing;
  float m_gameStateTimer = 0.0f;
  float m_gameStateDuration = 0.0f;

  glm::vec3 m_lastBonfirePos = { 0.0f, 1.0f, 0.0f }; // updated on bonfire rest

  void enterDeathSequence();
  void enterYouDied();
  void respawnPlayer();

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

  static constexpr const char* k_saveFilePath = "saves/flags.json";
  
  // Debugging

  struct HitboxTrail {
    static constexpr int N = 10;
    std::array<std::vector<forge::WorldCapsule>, N> entries;
    int head = 0;
    int count = 0;

    void push(const std::vector<forge::WorldCapsule>& caps) {
      entries[head] = caps;
      head = (head + 1) % N;
      if (count < N) count++;
    }

    void clear() {
      head = 0; count = 0; 
      for (auto& e : entries) e.clear();
    }
  };
  HitboxTrail m_playerTrail;
  HitboxTrail m_enemyTrail;

  forge::CombatComponent::AttackPhase m_prevPlayerPhase = forge::CombatComponent::AttackPhase::None;
  forge::CombatComponent::AttackPhase m_prevEnemyPhase = forge::CombatComponent::AttackPhase::None;
};
