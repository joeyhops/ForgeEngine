#include <forge/CombatSystem.h>
#include <forge/CombatComponent.h>
#include <forge/Logger.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/LuaState.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>

namespace forge {

// Helpers
static const char* combatStateName(forge::CombatState s) {
  switch(s) {
    case forge::CombatState::Idle: return "idle";
    case forge::CombatState::Walking: return "Walking";
    case forge::CombatState::Attacking: return "Attacking";
    case forge::CombatState::Recovering: return "Recovering";
    case forge::CombatState::Guarding: return "Guarding";
    case forge::CombatState::GuardBroken: return "GuardBroken";
    case forge::CombatState::Staggered: return "Staggered";
    case forge::CombatState::Dead: return "Dead";
    default: return "Unknown";
  }
}

static const char* receptiveTypeName(forge::ReceptiveHitWindowType t) {
  switch (t) {
    case forge::ReceptiveHitWindowType::Parry: return "Parry";
    case forge::ReceptiveHitWindowType::Deflect: return "Deflect";
    default: return "None";
  }
}

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

// hit detection

static float segSegSqDist(
  const glm::vec3& P0, const glm::vec3& P1,
  const glm::vec3& Q0, const glm::vec3& Q1)
{
  glm::vec3 d1 = P1 - P0, d2 = Q1 - Q0, r = P0 - Q0;
  float a = glm::dot(d1, d1);
  float e = glm::dot(d2, d2);
  float f = glm::dot(d2, r);
  float s, t;

  if (a <= 1e-8f && e <= 1e-8f) { return glm::dot(r, r); }
  if (a <= 1e-8f) {
    s = 0.0f;
    t = glm::clamp(f / e, 0.0f, 1.0f);
  } else {
    float c = glm::dot(d1, r);
    if (e <= 1e-8f) {
      t = 0.0f;
      s = glm::clamp(-c / a, 0.0f, 1.0f);
    } else {
      float b = glm::dot(d1, d2);
      float denom = a * e - b * b;
      s = (denom > 1e-8f) ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0.0f) { t = 0.0f; s = glm::clamp(-c / a, 0.0f, 1.0f); }
      else if (t > 1.0f) { t = 1.0f; s = glm::clamp((b - c) / a, 0.0f, 1.0f); }
    }
  }
  glm::vec3 diff = (P0 + d1 * s) - (Q0 + d2 * t);
  return glm::dot(diff, diff);
}

bool CombatSystem::checkHit(const CombatComponent& attacker,
                            const CombatComponent& defender) const
{
  const auto& worldCaps = attacker.getWorldCapsules();
  if (worldCaps.empty()) return false;

  // Build defenders upright body capsule from stored dimensions + world pos
  // worldPos is feet; capsule axis runs vertically
  const glm::vec3 defPos = defender.getWorldPos();
  const float defR = defender.getBodyRadius();
  const float defHH = defender.getBodyHalfHeight();
  const glm::vec3 defP0 = defPos + glm::vec3(0.0f, defR, 0.0f);
  const glm::vec3 defP1 = defPos + glm::vec3(0.0f, defHH * 2.0f - defR, 0.0f);

  for (const auto& cap : worldCaps) {
    float sqDist = segSegSqDist(cap.worldP0, cap.worldP1, defP0, defP1);
    float radSum = cap.radius + defR;
    if (sqDist <= radSum * radSum)
      return true;
  }
  return false;
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

  glm::vec3 hitDir = (glm::length(defender.getWorldPos() - attacker.getWorldPos()) > 1e-5f)
                        ? glm::normalize(defender.getWorldPos() - attacker.getWorldPos())
                        : attacker.getWorldForward();
  HitContext ctx;
  ctx.hit = { atk.damage, atk.poiseDamage, hitDir, atk.damageType };
  ctx.defenderState = defender.getState();
  ctx.inReceptiveWindow = defender.isInReceptiveWindow();
  ctx.receptiveType = defender.getReceptiveWindowType();
  ctx.facingAngle = computeFacingAngle(attacker, defender);
  ctx.flags.perilous = attacker.isCurrentHitPerilous();

  LOG_INFO("[CombatSystem] {} hit {} with {}",
           attacker.getName(), defender.getName(), atk.name);

  HitResolution result = callLuaResolution(ctx);
  applyResolution(attacker, defender, ctx, result); 
}

HitResolution CombatSystem::callLuaResolution(const HitContext& ctx) {
  HitResolution fallback;
  fallback.type = "hit";
  fallback.damage = ctx.hit.damage;

  if (!m_lua) return fallback;

  sol::state& lua = m_lua->get();
  sol::protected_function fn = lua["onHitResolution"];
  if (!fn.valid()) return fallback;

  sol::table ctxTable = lua.create_table();
  ctxTable["damage"] = ctx.hit.damage;
  ctxTable["poiseDamage"] = ctx.hit.poiseDamage;
  ctxTable["damageType"] = ctx.hit.damageType;
  ctxTable["defenderState"] = combatStateName(ctx.defenderState);
  ctxTable["inReceptiveWindow"] = ctx.inReceptiveWindow;
  ctxTable["receptiveType"] = receptiveTypeName(ctx.receptiveType); 
  ctxTable["facingAngle"] = ctx.facingAngle; 
  ctxTable["perilous"] = ctx.flags.perilous;

  auto res = fn(ctxTable);
  if (!res.valid()) {
    sol::error err = res;
    LOG_ERROR("[CombatSystem] onHitResolution error: {}", err.what());
    return fallback;
  }

  sol::table resTable = res;
  HitResolution result;
  result.type = resTable.get_or("type", std::string("hit"));
  result.damage = resTable.get_or("damage", ctx.hit.damage);
  return result;
}

void CombatSystem::applyResolution(CombatComponent& attacker,
                                   CombatComponent& defender,
                                   const HitContext& ctx,
                                   const HitResolution& result)
{
  LOG_INFO("[CombatSystem] {} -> {} resolution: '{}' (dmg {:.0f})",
           attacker.getName(), defender.getName(), result.type, result.damage);

  if (result.type == "parry_success") {
    LOG_INFO("[CombatSystem] Parry! {} deflected {}", defender.getName(), attacker.getName());
    attacker.notifyHitLanded();
    return;
  }
  
  if (result.type == "parry_success") {
    LOG_INFO("[CombatSystem] Deflect! {} deflected {}", defender.getName(), attacker.getName());
    attacker.notifyHitLanded();
    return;
  }

  // "hit", "guard", "guard_broken" all apply a HitEvent
  // Phase 13 will split these into distinct paths with proper
  // guard break logic
  HitEvent appliedHit = ctx.hit;

  EventBus::publish(EntityHitEvent{
    .attackerName = attacker.getName(),
    .defenderName = defender.getName(), 
    .damage = appliedHit.damage,
    .damageType = appliedHit.damageType,
    .hitPosition = defender.getWorldPos()
  });

  defender.applyHit(appliedHit);
  attacker.notifyHitLanded();
}

float CombatSystem::computeFacingAngle(const CombatComponent& attacker,
                                       const CombatComponent& defender) const
{
  glm::vec3 toDefender = defender.getWorldPos() - attacker.getWorldPos();
  if (glm::length(toDefender) < 1e-5f) return 0.0f;
  toDefender = glm::normalize(toDefender);
  glm::vec3 fwd = glm::normalize(attacker.getWorldForward());
  float dot = glm::clamp(glm::dot(fwd, toDefender), -1.0f, 1.0f);
  return glm::degrees(glm::acos(dot));
}

}
