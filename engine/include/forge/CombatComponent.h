#pragma once
#include <forge/StateMachine.h>
#include <forge/AttackData.h>
#include <forge/WeaponDef.h>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <memory>

class btPairCachingGhostObject;

namespace forge {

class Animator;
class AnimParamTable;

enum class CombatState {
  Idle,
  Walking,
  Attacking,
  Recovering,
  Guarding,
  GuardBroken,
  Staggered,
  Dead
};

// Fired when this component takes a hit - connect to audio, VFX, AI Reactions
struct HitEvent {
  float damage;
  float poiseDamage;
  glm::vec3 direction; // Direction hit came from (for knockback)
  std::string damageType;
};

class CombatComponent {
public:
  enum class AttackPhase { None, Startup, Active, Recovery };

  CombatComponent(const std::string& ownerName,
                  float maxHp, float maxStamina, float maxPoise);

  // Called every frame from CombatSystem
  void update(float dt);

  // Animation wiring
  // Set once in ForgeGame::setupPlayer/setupEnemy so CombatComponent can
  // call play() on state transitions. non-owning animator is owned by the entity struct
  // in ForgeGame
  void setAnimator(Animator* animator) { m_animator= animator; }
  void setParamTable(AnimParamTable* params) { m_paramTable = params; }

  // Attack initiation
  // Resturns false if can't attack (dead, already attacking, no stamina)
  bool tryAttack(const AttackData& attack);

  // Weapon system
  // Pass nullptr to revert to bare-hand AttackData
  // Called by EquipmentComponent::onEquip and onUnequip
  void setWeapon(const WeaponDef* weapon);

  // Returns attack data for the named attack ("r1", "r2")
  // Falls back to barehand r1 if name not found or no weapon equipped
  const AttackData& getAttackData(const std::string& name) const;

  void takeDamage(float amount) {
    applyHit({ amount, 0.0f, glm::vec3(0.0f), "script" });
  }

  void healToFull() { m_hp = m_maxHp; }
  
  // incoming Hit
  void applyHit(const HitEvent& hit);

  // Guards
  bool tryGuard();
  void releaseGuard();

  // Stamina
  bool consumeStamina(float amount); // Returns false if not enough

  // Queries
  bool isAlive() const { return m_hp > 0.0f; }
  bool isAttacking() const { return m_fsm.isIn(CombatState::Attacking); }
  bool isRecovering() const { return m_fsm.isIn(CombatState::Recovering); }
  bool isGuarding() const { return m_fsm.isIn(CombatState::Guarding); }
  bool isVulnerable() const; // can be staggered?

  float getHp() const { return m_hp; }
  float getMaxHp() const { return m_maxHp; }
  float getStamina() const { return m_stamina; }
  float getMaxStamina() const { return m_maxStamina; }
  float getPoise() const { return m_poise; }
  float getMaxPoise() const { return m_maxPoise; }

  CombatState getState() const { return m_fsm.current(); }
  std::string getStateName() const;

  // Active hitbox query
  // Returns true during active frames of attack
  bool hasActiveHitbox() const { return m_hitboxActive; }
  const AttackData& getCurrentAttack() const { return m_currentAttack; }

  // Owner pos/facing - set by the game each frame
  // CombatSystem uses these for hit detection geometry
  void setWorldData(const glm::vec3& pos, const glm::vec3& forward) {
    m_worldPos = pos;
    m_worldForward = forward;
  }
  const glm::vec3& getWorldPos() const { return m_worldPos; }
  const glm::vec3& getWorldForward() const { return m_worldForward; }

  void setGhostObject(btPairCachingGhostObject* ghost) { m_ghostObject = ghost; }
  btPairCachingGhostObject* getGhostObject() const { return m_ghostObject; }

  // Per-frame hitbox transform - setby game during active attack window
  void setHitboxTransform(const glm::mat4& worldTransform,
                          const glm::vec3& halfExtents) {
    m_hitboxTransform = worldTransform;
    m_hitboxHalfExtents = halfExtents;
  }
  const glm::mat4& getHitboxTransform() const { return m_hitboxTransform; }
  const glm::vec3& getHitboxHalfExtents() const { return m_hitboxHalfExtents; }

  // Callback - set from Lua or C++ to react to being hit
  std::function<void(const HitEvent&)> onHit;
  std::function<void()> onDeath;

  // Consume the one-hit-per-swing token. Returns false if already used
  bool consumeHitToken() {
    if (m_hitLanded) return false;
    m_hitLanded = true;
    return true;
  }

  const std::string& getName() const { return m_ownerName; }

  // Called from TAE eventbus subscription - sets m_hitboxActive
  // without touching any kind of timer logic
  void setHitboxActive(bool active) { m_hitboxActive = true; }

  AttackPhase getAttackPhase() const;
  float getAttackTimer() const { return m_attackTimer; }

  // Hit-flash support: set by the game when hit is confirmed
  void notifyHitLanded();
  bool getHitFlash() const { return m_hitFlash; }
private:
  void setupFSM();
  void tickAttack(float dt);
  void tickStamina(float dt);
  void tickPoise(float dt);
  void subscribeToTAEEvents();
  void writeTriggerForAttack(const std::string& attackName);

  std::string m_ownerName;
  Animator* m_animator = nullptr;
  AnimParamTable* m_paramTable = nullptr;

  // Stats
  float m_hp, m_maxHp;
  float m_stamina, m_maxStamina;
  float m_poise, m_maxPoise;

  // FSM
  StateMachine<CombatState> m_fsm;

  // Attack state
  AttackData m_currentAttack;
  float m_attackTimer = 0.0f; // counts up through startup + active + recovery
  bool m_hitboxActive = false;
  bool m_hitLanded = false; // Prevents multi-hit in one swing
  bool m_usingTAEHitboxes = false;
  bool m_taeAttackComplete = false;

  // Stagger state
  float m_staggerTimer = 0.0f;

  // World data
  glm::vec3 m_worldPos = { 0,0,0 };
  glm::vec3 m_worldForward = { 0,0,1 };

  // Bone attached hitbox (set by ForgeGame during active attack window)
  glm::mat4 m_hitboxTransform = glm::mat4(1.0f);
  glm::vec3 m_hitboxHalfExtents = { 0.08f, 0.45f, 0.08f };

  // non-owning pointer to entities phys ghost obj
  btPairCachingGhostObject* m_ghostObject = nullptr;

  // Stamina Regen constants
  static constexpr float STAMINA_REGEN_RATE = 30.0f; // per second
  static constexpr float STAMINA_REGEN_DELAY = 1.0f; // Seconds after use before regen starts
  float m_staminaRegenDelay = 0.0f;

  // Poise regen
  static constexpr float POISE_REGEN_RATE = 10.0f;
  static constexpr float POISE_REGEN_DELAY = 3.0f;
  float m_poiseRegenDelay = 0.0f;

  const WeaponDef* m_weapon = nullptr;

  static const AttackData k_bareHandR1;
  static const AttackData k_bareHandR2;

  // Hit flash
  bool m_hitFlash = false;
  float m_hitFlashTimer = 0.0f;
  static constexpr float HIT_FLASH_DURATION = 0.12f;
};

}
