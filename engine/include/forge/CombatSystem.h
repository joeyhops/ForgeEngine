#pragma once
#include "forge/CombatComponent.h"
#include <forge/AttackData.h>
#include <vector>

namespace forge {

class CombatComponent;
class PhysicsWorld;

class CombatSystem {
public:
  CombatSystem() = default;

  void setPhysicsWorld(PhysicsWorld* world) { m_physicsWorld = world; }

  // Register a combatant - must be called before update
  void registerCombatant(CombatComponent* component);
  void unregisterCombatant(CombatComponent* component);

  // Call once per frame - resolves all active hitboxes against all defenders
  void update(float dt);

  // Read only access for debug panels
  const std::vector<CombatComponent*>& getCombatants() const { return m_combatants; }
private:
  // Check if attackers active hitbox hits defender
  // Uses cone-based detection: winithin range AND within angle in front of attacker
  bool checkHit(const CombatComponent& attacker,
                const CombatComponent& defender) const;

  void resolveHit(CombatComponent& attacker, CombatComponent& defender);

  std::vector<CombatComponent*> m_combatants;
  PhysicsWorld* m_physicsWorld = nullptr;
};

}
