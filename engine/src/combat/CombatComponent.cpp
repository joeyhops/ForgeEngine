#include <forge/CombatComponent.h>
#include <forge/Animator.h>
#include <forge/AnimGraph.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/AttackData.h>
#include <forge/Logger.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <string>

namespace forge {

CombatComponent::CombatComponent(const std::string& ownerName,
                                 float maxHp, float maxStamina, float maxPoise)
  : m_ownerName(ownerName)
  , m_hp(maxHp), m_maxHp(maxHp)
  , m_stamina(maxStamina), m_maxStamina(maxStamina)
  , m_poise(maxPoise), m_maxPoise(maxPoise)
{
  setupFSM();
  subscribeToTAEEvents();

  LOG_INFO("[Combat] Component created for '{}' HP:{} STA:{} POI:{}",
           ownerName, maxHp, maxStamina, maxPoise);
}

void CombatComponent::writeTriggerForAttack(const std::string& attackName) {
  if (!m_paramTable) return;
  std::string trigger = "attackR1";
  if (attackName.find("R2") != std::string::npos)
    trigger = "attackR2";
  m_paramTable->setTrigger(trigger);
  LOG_TRACE("[Combat] {} wrote trigger '{}'", m_ownerName, trigger);
}

// TAE Subscription
// Subscribe once at construction. The lambda captures this by pointer.
// EventBus::clear() should be called on scene change to release all handlers

void CombatComponent::subscribeToTAEEvents() {
  EventBus::subscribe<AnimEventActivated>([this](const AnimEventActivated& e){
    if (e.ownerName != m_ownerName) return; // Only reacts to own events
    if (e.type == AnimEventType::SpawnHitbox) {
      m_hitboxActive = true;
      m_hitLanded = false;
      m_usingTAEHitboxes = true;
      LOG_TRACE("[Combat] {} TAE Hitbox ACTIVE", m_ownerName);
    }
  });

  EventBus::subscribe<AnimEventDeactivated>([this](const AnimEventDeactivated& e) {
    if (e.ownerName != m_ownerName) return;

    if (e.type == AnimEventType::SpawnHitbox) {
      m_hitboxActive = false;
      m_taeAttackComplete = true;
      LOG_TRACE("[Combat] {} TAE hitbox CLOSED");
    }
  });
}

// FSM Setup
void CombatComponent::setupFSM() {
  m_fsm.addState(CombatState::Idle, {
    .onEnter = [this]{ LOG_TRACE("[Combat] {} -> Idle", m_ownerName); },
    .onUpdate = [this](float dt) {
      tickStamina(dt);
      tickPoise(dt);
    },
    .onExit = nullptr
  });

  // Attacking - counts through startup/active/recovery timing
  m_fsm.addState(CombatState::Attacking, {
    .onEnter = [this]{
      m_attackTimer = 0.0f;
      m_hitboxActive = false;
      m_hitLanded = false;
      m_taeAttackComplete = false;

      writeTriggerForAttack(m_currentAttack.name);

      LOG_TRACE("[Combat] {} -> Attacking ({})",
                m_ownerName, m_currentAttack.name);
    },
    .onUpdate = [this](float dt) { tickAttack(dt); },
    .onExit = [this]{
      m_hitboxActive = false;
    }
  });

  // Recovering - attack finished, briefly vulnerable
  m_fsm.addState(CombatState::Recovering, {
    .onEnter = [this]{ m_attackTimer = 0.0f; },
    .onUpdate = [this](float dt) {
      // Recovery handled inside tickAttack - just regen
      m_attackTimer += dt;
      tickStamina(dt);

      if (m_attackTimer >= m_currentAttack.recoveryTime)
        m_fsm.transition(CombatState::Idle);
    },
    .onExit = nullptr
  });

  // Guarding - blocking incoming hits
  m_fsm.addState(CombatState::Guarding, {
    .onEnter = [this]{ LOG_TRACE("[Combat] {} -> Guarding", m_ownerName); },
    .onUpdate = [this](float dt){
      // Drain stamina while guarding
      if (!consumeStamina(15.0f * dt))
        m_fsm.transition(CombatState::GuardBroken);
    },
    .onExit = nullptr
  });

  // GuardBroken - stamina depleted while guarding
  m_fsm.addState(CombatState::GuardBroken, {
    .onEnter = [this]{
      m_staggerTimer = 1.5f; // Longer stagger than normal
      LOG_WARN("[Combat] {} Guard Broken!", m_ownerName);
    },
    .onUpdate = [this](float dt){
      m_staggerTimer -= dt;
      if (m_staggerTimer <= 0.0f)
        m_fsm.transition(CombatState::Idle);
    },
    .onExit = nullptr
  });

  // Staggered - hit and poise broken
  m_fsm.addState(CombatState::Staggered, {
    .onEnter = [this]{ m_staggerTimer = 0.6f; },
    .onUpdate = [this](float dt){
      m_staggerTimer -= dt;
      if (m_staggerTimer <= 0.0f)
        m_fsm.transition(CombatState::Idle);
    },
    .onExit = [this]{
      // Restore poise on recovery
      m_poise = m_maxPoise;
      LOG_TRACE("[Combat] {} recovered from stagger", m_ownerName);
    }
  });

  // Dead - terminal state
  m_fsm.addState(CombatState::Dead, {
    .onEnter = [this]{
      m_hitboxActive = false;
      if (m_paramTable) m_paramTable->setBool("isDead", true);

      LOG_INFO("[Combat] {} died", m_ownerName);
      if (onDeath) onDeath();
    },
    .onUpdate = nullptr,
    .onExit = nullptr  
  });

  m_fsm.setInitialState(CombatState::Idle);
}

// Update
void CombatComponent::update(float dt) {
  if (!isAlive()) return;
  m_fsm.update(dt);
}

// Attack
bool CombatComponent::tryAttack(const AttackData& attack) {
  // Can only attack from Idle or Walking
  if (!m_fsm.isIn(CombatState::Idle) &&
      !m_fsm.isIn(CombatState::Walking)) return false;

  if (!isAlive()) return false;

  if (!consumeStamina(attack.staminaCost)) {
    LOG_TRACE("[Combat] {} not enough stamina to attack", m_ownerName);
    return false;
  }

  m_currentAttack = attack;
  m_fsm.transition(CombatState::Attacking);
  return true;
}

void CombatComponent::tickAttack(float dt) {
  m_attackTimer += dt;

  // if TAE events are driving the hitbox, just watch for the clip to end
  // then transition to recovery
  if (m_usingTAEHitboxes && m_animator && 
    (m_taeAttackComplete || m_animator->isFinished())) {
    m_taeAttackComplete = false;
    m_fsm.transition(CombatState::Recovering);
    return;
  }

  if (!m_usingTAEHitboxes) {
    const AttackData& atk = m_currentAttack;
    if (m_attackTimer < atk.startupTime) m_hitboxActive = false;
    else if (m_attackTimer < atk.startupTime + atk.activeTime) m_hitboxActive = true;
    else { m_hitboxActive = false; m_fsm.transition(CombatState::Recovering); }
  }
}

// Hit resolution
void CombatComponent::applyHit(const HitEvent& hit) {
  if (!isAlive()) return;

  // Guarding - reduce damage, costs stamina
  if (isGuarding()) {
    float guardedDamage = hit.damage * 0.1f; // Block 90% of damage
    m_hp -= guardedDamage;
    if (!consumeStamina(hit.damage * 0.5f)) {
      m_fsm.transition(CombatState::GuardBroken);
    }
    LOG_INFO("[Combat] {} blocked! ({:.0f} damage taked)", m_ownerName, guardedDamage);
    if (onHit) onHit(hit);
    return;
  }

  // Full hit
  m_hp = std::max(0.0f, m_hp - hit.damage);
  m_poise = std::max(0.0f, m_poise - hit.poiseDamage);
  m_poiseRegenDelay = POISE_REGEN_DELAY;

  LOG_INFO("[Combat] {} hit for {:.0f} damage HP:{:.0f}/{:.0f} Poise:{:.0f}",
           m_ownerName, hit.damage, m_hp, m_maxHp, m_poise);

  if (onHit) onHit(hit);

  if (m_hp <= 0.0f) {
    m_fsm.transition(CombatState::Dead);
    return;
  }

  // Poise broken - stagger
  if (m_poise <= 0.0f) {
    m_fsm.transition(CombatState::Staggered);
  }
}

// Guard

bool CombatComponent::tryGuard() {
  if (!m_fsm.isIn(CombatState::Idle) &&
      !m_fsm.isIn(CombatState::Walking)) return false;
  if (m_stamina < 10.0f) return false;
  m_fsm.transition(CombatState::Guarding);
  return true;
}

void CombatComponent::releaseGuard() {
  if (isGuarding())
    m_fsm.transition(CombatState::Idle);
}

// Stamina & Poise ticks

bool CombatComponent::consumeStamina(float amount) {
  if (m_stamina < amount) return false;
  m_stamina -= amount;
  m_staminaRegenDelay = STAMINA_REGEN_DELAY;
  return true;
}

void CombatComponent::tickStamina(float dt) {
  if (m_staminaRegenDelay > 0.0f) {
    m_staminaRegenDelay -= dt;
    return;
  }
  m_stamina = std::min(m_maxStamina, m_stamina + STAMINA_REGEN_RATE * dt);
}

void CombatComponent::tickPoise(float dt) {
  if (m_poiseRegenDelay > 0.0f) {
    m_poiseRegenDelay -= dt;
    return;
  }
  m_poise = std::min(m_maxPoise, m_poise + POISE_REGEN_RATE * dt);
}

// Utils
bool CombatComponent::isVulnerable() const {
  return m_fsm.isIn(CombatState::Recovering)||
         m_fsm.isIn(CombatState::Staggered)||
         m_fsm.isIn(CombatState::GuardBroken);
  
}

std::string CombatComponent::getStateName() const {
  switch (m_fsm.current()) {
    case CombatState::Idle: return "Idle";
    case CombatState::Walking: return "Walking";
    case CombatState::Attacking: return "Attacking";
    case CombatState::Recovering: return "Recovering";
    case CombatState::Guarding: return "Guarding";
    case CombatState::GuardBroken: return "GuardBroken";
    case CombatState::Staggered: return "Staggered";
    case CombatState::Dead: return "Dead";
    default: return "Unknown";
  }
}

}
