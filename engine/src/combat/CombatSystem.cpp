#include <forge/CombatSystem.h>
#include <forge/CombatComponent.h>
#include <forge/Logger.h>
#include <forge/PhysicsWorld.h>
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

// hit detection (cone check)

bool CombatSystem::checkHit(const CombatComponent& attacker,
                            const CombatComponent& defender) const
{
  if (m_physicsWorld && attacker.getGhostObject() != nullptr && defender.getGhostObject() != nullptr) {
    const glm::mat4& wpnMat = attacker.getHitboxTransform();
    const glm::vec3& halfExt = attacker.getHitboxHalfExtents();

    btBoxShape hitboxShape(btVector3(
      static_cast<btScalar>(halfExt.x),
      static_cast<btScalar>(halfExt.y),
      static_cast<btScalar>(halfExt.z)
    ));

    btTransform hitboxBT;
    hitboxBT.setFromOpenGLMatrix(glm::value_ptr(wpnMat));

    btCollisionObject hitboxObj;
    hitboxObj.setCollisionShape(&hitboxShape);
    hitboxObj.setWorldTransform(hitboxBT);

    struct HitCallback : public btCollisionWorld::ContactResultCallback {
      bool hit = false;
      btScalar addSingleResult(btManifoldPoint&,
                               const btCollisionObjectWrapper*, int, int,
                               const btCollisionObjectWrapper*, int, int) override
      {
        hit = true;
        return btScalar(0);
      }
    } cb;

    m_physicsWorld->getBulletWorld()->contactPairTest(
      &hitboxObj,
      defender.getGhostObject(),
      cb
    );

    LOG_TRACE("[CombatSystem] checkHit (box): {} vs {} -> {}",
              attacker.getName(), defender.getName(), cb.hit ? "HIT" : "MISS");
    return cb.hit;
  }

  const AttackData& atk = attacker.getCurrentAttack();

  // 1. range check
  glm::vec3 toDefender = defender.getWorldPos() - attacker.getWorldPos();
  float distance = glm::length(toDefender);

  LOG_TRACE("[CombatSystem] checkHit: {} vs {} dist={:.2f} range={:.2f}",
            attacker.getName(), defender.getName(), distance, atk.range);

  if (distance > atk.range) {
    LOG_TRACE("[CombatSystem] -> MISS (Out of range)");
    return false;
  }

  // 2. Angle check (cone in front of attacker)
  // Normalize both vectors for dot product
  glm::vec3 forward = glm::normalize(attacker.getWorldForward());
  glm::vec3 direction = glm::normalize(toDefender);

  // dot product gives cos(angle between vectors)
  float dot = glm::dot(forward, direction);
  float halfAngle = glm::radians(atk.angle * 0.5f);
  float cosHalf = glm::cos(halfAngle);

  LOG_TRACE("[CombatSystem] -> dot={:.2f} cosHalf={:.2f} inCone={}",
            dot, cosHalf, dot >= cosHalf ? "YES" : "NO");

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
