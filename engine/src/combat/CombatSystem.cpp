#include <forge/CombatSystem.h>
#include <forge/CombatComponent.h>
#include <forge/Logger.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace forge {

void CombatSystem::registerCombatant(CombatComponent* component) {
  m_combatants.push_back(component);
  LOG_INFO("[CombatSystem] Registered: {}", component->getName());
}

void CombatSystem::unregisterCombatant(CombatComponent* component) {
  m_combatants.erase(
    std::remove(m_combatants.begin(), m_combatants.end(), component),
    m_combatants.end()
  );
}

// Update - Main hit detection loop

void CombatSystem::update(float dt) {
  // update all components first
  for (auto* c : m_combatants)
    c->update(dt);

  // Now resolve hits
  // For each attacker with an active hitbox, check all other combatants
  for (auto* attacker : m_combatants) {
    if (!attacker->isAlive()) continue;
    if (!attacker->hasActiveHitbox()) continue;

    for (auto* defender : m_combatants) {
      if (defender == attacker) continue; // No self-harm!
      if (!defender->isAlive()) continue; // can't kill the dead either

      if (checkHit(*attacker, *defender)) {
        resolveHit(*attacker, *defender);
      }
    }
  }
}

// hit detection (cone check)

bool CombatSystem::checkHit(const CombatComponent& attacker,
                            const CombatComponent& defender) const
{
  const AttackData& atk = attacker.getCurrentAttack();

  // 1. range check
  glm::vec3 toDefender = defender.getWorldPos() - attacker.getWorldPos();
  float distance = glm::length(toDefender);
  if (distance > atk.range) return false;

  // 2. Angle check (cone in front of attacker)
  // Normalize both vectors for dot product
  glm::vec3 forward = glm::normalize(attacker.getWorldForward());
  glm::vec3 direction = glm::normalize(toDefender);

  // dot product gives cos(angle between vectors)
  float dot = glm::dot(forward, direction);
  float halfAngle = glm::radians(atk.angle * 0.5f);
  float cosHalf = glm::cos(halfAngle);

  // dot >= cosHalf means the defender is inside the cone
  return dot >= cosHalf;
}

void CombatSystem::resolveHit(CombatComponent& attacker,
                              CombatComponent& defender)
{
  // Hit landed is a flag on CombatComponent that prevents hitting
  // the same target twice in one active window. We access it via
  // the public tryAttack flow - for now, we use a simple check:
  // if attacker just started hitting (hitboxActive just became true)
  // we only allow one resolution per active window via the components
  // internal m_hitLanded flag
  //
  // we expose this cleanly through a method
  if (!attacker.consumeHitToken()) return;

  const AttackData& atk = attacker.getCurrentAttack();

  glm::vec3 hitDir = glm::normalize(
    defender.getWorldPos() - attacker.getWorldPos()
  );

  HitEvent hit{
    .damage = atk.damage,
    .poiseDamage = atk.poiseDamage,
    .direction = hitDir,
    .damageType = atk.damageType
  };

  LOG_INFO("[CombatSystem] {} hit {} with {}",
           attacker.getName(), defender.getName(), atk.name);

  defender.applyHit(hit);
}
}
