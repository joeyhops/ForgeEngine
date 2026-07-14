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

// Bare hand attack data
const AttackData CombatComponent::k_bareHandR1 = {
  "r1", // name
  25.0f, // damage
  5.0f, // poiseDamage
  10.0f, // staminaCost
  "blunt" // damageType
};

const AttackData CombatComponent::k_bareHandR2 = {
  "r2", // name
  40.0f, // damage
  10.0f, // poiseDamage
  15.0f, // staminaCost
  "blunt" // damageType
};

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
      m_hitboxActive = false;
      m_hitLanded = false;
      m_attackAnimStarted = false;

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
    .onEnter = [this]{},
    .onUpdate = [this](float dt) {
      // Recovery handled inside tickAttack - just regen
      tickStamina(dt);
      if (m_animator && m_animator->getCurrentStateName() != "Attacking")
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
    .onEnter = [this]{
      m_staggerTimer = 2.0f;
      if (m_paramTable) m_paramTable->setTrigger("hitReaction");
      LOG_TRACE("[Combat] {} -> Staggered (poise break)", m_ownerName);
    },
    .onUpdate = [this](float dt){
      m_staggerTimer -= dt;
      bool animDone = m_animator && m_animator->getCurrentStateName() != "HitReaction";
      if (m_staggerTimer <= 0.0f || animDone)
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
  if (m_animator) m_events.refresh(m_animator->sampledEvents());

  // Hitbox: level state. Capsules come from the sampled event itself
  bool hitboxNow = false;
  for (const auto& e : m_animator->sampledEvents().FilteredRange(
        [](const SampledEvent& e){ return e.type == AnimEventType::SpawnHitbox
                                        && e.weight >= 0.5f
                                        && HasFlag(e.flags, SampledEvent::Flags::FromActiveBranch); })) {
    hitboxNow = true;
    m_localCapsules = e.capsules;
    break;
  }
  if (hitboxNow && !m_hitboxActive) m_hitLanded = false; // reset on open edge
  m_hitboxActive = hitboxNow;
  if (!hitboxNow) { m_localCapsules.clear(); m_worldCapsules.clear(); }

  // Receptive/Perilous windows: pure level state
  m_inReceptiveWindow = m_events.active(AnimEventType::ParryWindow)
                      || m_events.active(AnimEventType::DeflectWindow);
  m_receptiveWindowType = m_events.active(AnimEventType::ParryWindow) ? ReceptiveHitWindowType::Parry
                        : m_events.active(AnimEventType::DeflectWindow) ? ReceptiveHitWindowType::Deflect
                        : ReceptiveHitWindowType::None;
  m_activeHitPerilous = m_events.active(AnimEventType::PerilousMark);

  // Combo window
  for (const auto& e : m_animator->sampledEvents().FilteredRange(
          [](const SampledEvent& e) { return e.type == AnimEventType::ComboWindow && e.weight >= 0.5f; })) {
    m_inComboWindow = true;
    m_comboWindowAction = e.payload;
    break;
  }
  if (m_events.closedType(AnimEventType::ComboWindow)) {
    if (!m_comboBuffer.empty()) startCombo(m_comboBuffer);
    m_comboBuffer.clear();
    m_comboWindowAction.clear();
    m_inComboWindow = false;
  }

  // RecoveryBegin: one-shot -> present exactly the crossing frame
  if (m_events.activeType(AnimEventType::RecoveryBegin) && m_fsm.isIn(CombatState::Attacking))
    m_fsm.transition(CombatState::Recovering);

  if (!isAlive()) return;
  m_fsm.update(dt);
  if (m_hitFlash) {
    m_hitFlashTimer -= dt;
    if (m_hitFlashTimer <= 0.0f) m_hitFlash = false;
  }
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

void CombatComponent::setWeapon(const WeaponDef* weapon) {
  m_weapon = weapon;
  if (weapon) {
    LOG_INFO("[Combat] '{}' weapon set to '{}'", m_ownerName, weapon->id);
  } else {
    LOG_INFO("[Combat] '{}' reverted to bare-hand", m_ownerName);
  }
}

void CombatComponent::revive() {
  m_hp = m_maxHp;
  m_stamina = m_maxStamina;
  m_poise = m_maxPoise;

  // Clear all transient attack state
  m_hitboxActive = false;
  m_hitLanded = false;
  m_staggerTimer = 0.0f;
  m_hitFlash = false;
  m_hitFlashTimer = 0.0f;

  // Clear combat windows in case we died mid attack
  m_inComboWindow = false;
  m_comboWindowAction.clear();
  m_comboBuffer.clear();
  m_inReceptiveWindow = false;
  m_receptiveWindowType = ReceptiveHitWindowType::None;
  m_activeHitPerilous = false;

  // Transition FSM then clear the param so graph can respond
  m_fsm.transition(CombatState::Idle);
  if (m_paramTable) m_paramTable->setBool("isDead", false);

  LOG_INFO("[Combat] {} revived", m_ownerName);
}

const AttackData& CombatComponent::getAttackData(const std::string& name) const {
  if (m_weapon) {
    auto it = m_weapon->attacks.find(name);
    if (it != m_weapon->attacks.end()) return it->second;
    LOG_WARN("[Combat] '{}' no attack '{}' in weapon '{}' - using bare-hands",
             m_ownerName, name, m_weapon->id);
  }
  return (name == "r2") ? k_bareHandR2 : k_bareHandR1;
} 

void CombatComponent::tickAttack(float dt) {
  if (!m_attackAnimStarted) {
    if (m_animator && m_animator->getCurrentStateName() == "Attacking")
      m_attackAnimStarted = true;
    return;
  }
  if (m_animator && m_animator->getCurrentStateName() != "Attacking") {
    m_fsm.transition(CombatState::Idle);
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

CombatComponent::AttackPhase CombatComponent::getAttackPhase() const {
  if (m_fsm.isIn(CombatState::Attacking)) {
    return m_hitboxActive ? AttackPhase::Active : AttackPhase::Startup;
  }
  if (m_fsm.isIn(CombatState::Recovering)) return AttackPhase::Recovery;
  return AttackPhase::None;
}

void CombatComponent::notifyHitLanded() {
  m_hitFlash = true;
  m_hitFlashTimer = HIT_FLASH_DURATION;
}

void CombatComponent::bufferCombo(const std::string& actionKey) {
  if (!m_inComboWindow) return;
  if (actionKey != m_comboWindowAction) return;
  m_comboBuffer = actionKey;
  LOG_TRACE("[Combat] {} buffered combo '{}'", m_ownerName, actionKey);
}

void CombatComponent::startCombo(const std::string& actionKey) {
  if (!m_paramTable) return;

  m_paramTable->setTrigger(actionKey);

  m_hitboxActive = false;
  m_hitLanded = false;

  LOG_TRACE("[Combat] {} started combo '{}'", m_ownerName, actionKey);
}

}
