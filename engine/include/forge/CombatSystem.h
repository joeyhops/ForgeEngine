#pragma once
#include "forge/CombatComponent.h"
#include <forge/AttackData.h>
#include <vector>

namespace forge {

class CombatComponent;

class CombatSystem {
public:
  CombatSystem() = default;

  // Register a combatant - must be called before update
  void registerCombatant(CombatComponent* component);
  void unregisterCombatant(CombatComponent* component);

  // Call once per frame - resolves all active hitboxes against all defenders
  void update(float dt);

private:
  // Check if attackers active hitbox hits defender
  // Uses cone-based detection: winithin range AND within angle in front of attacker
  bool checkHit(const CombatComponent& attacker,
                const CombatComponent& defender) const;

  void resolveHit(CombatComponent& attacker, CombatComponent& defender);

  std::vector<CombatComponent*> m_combatants;
};

}
