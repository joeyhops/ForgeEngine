#pragma once
#include <forge/StateMachine.h>
#include <forge/AttackData.h>
#include <glm/glm.hpp>
#include <functional>
#include <string>

namespace forge {

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
  CombatComponent(const std::string& ownerName,
                  float maxHp, float maxStamina, float maxPoise);

  // Called every frame from CombatSystem
  void update(float dt);

  // Attack initiation
  // Resturns false if can't attack (dead, already attacking, no stamina)
  bool tryAttack(const AttackData& attack);

  void takeDamage(float amount) {
    applyHit({ amount, 0.0f, glm::vec3(0.0f), "script" });
  }

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

private:
  void setupFSM();
  void tickAttack(float dt);
  void tickStamina(float dt);
  void tickPoise(float dt);

  std::string m_ownerName;

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
  
  // Stagger state
  float m_staggerTimer = 0.0f;

  // World data
  glm::vec3 m_worldPos = { 0,0,0 };
  glm::vec3 m_worldForward = { 0,0,1 };

  // Stamina Regen constants
  static constexpr float STAMINA_REGEN_RATE = 30.0f; // per second
  static constexpr float STAMINA_REGEN_DELAY = 1.0f; // Seconds after use before regen starts
  float m_staminaRegenDelay = 0.0f;

  // Poise regen
  static constexpr float POISE_REGEN_RATE = 10.0f;
  static constexpr float POISE_REGEN_DELAY = 3.0f;
  float m_poiseRegenDelay = 0.0f;
};

}
