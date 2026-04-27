#pragma once
#include "forge/CombatComponent.h"
#include <forge/AttackData.h>

#include <vector>
#include <string>

namespace forge {

class CombatComponent;
class PhysicsWorld;
class LuaState;

struct HitFlags {
  bool perilous = false;
};

// Assembled by CombatSystem before calling onHitResolution in Lua
// All fields are plain C++ types, HitContext is never exposed as a user type
struct HitContext {
  HitEvent hit;
  CombatState defenderState;
  bool inReceptiveWindow = false;
  ReceptiveHitWindowType receptiveType = ReceptiveHitWindowType::None;
  float facingAngle = 0.0f; // attacker fwd -> defender
  HitFlags flags;
};

// Returned by onHitResolution
struct HitResolution {
  std::string type = "hit";
  float damage = 0.0f;
};

class CombatSystem {
public:
  CombatSystem() = default;

  void setPhysicsWorld(PhysicsWorld* world) { m_physicsWorld = world; }
  void setLuaState(LuaState* lua) { m_lua = lua; }

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

  // Assemble context -> call Lua -> return resolution
  HitResolution callLuaResolution(const HitContext& ctx);

  // Apply resolution to the combatants
  void applyResolution(CombatComponent& attacker,
                       CombatComponent& defender,
                       const HitContext& ctx, const HitResolution& result);

  float computeFacingAngle(const CombatComponent& attacker,
                           const CombatComponent& defender) const;

  std::vector<CombatComponent*> m_combatants;
  PhysicsWorld* m_physicsWorld = nullptr;
  LuaState* m_lua = nullptr;
};

}
