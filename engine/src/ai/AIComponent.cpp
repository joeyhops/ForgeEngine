#include <forge/AIComponent.h>
#include <forge/CombatComponent.h>
#include <forge/Transform.h>
#include <forge/Logger.h>
#include <forge/AttackData.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

namespace forge {

AIComponent::AIComponent(const std::string& name,
                         Transform& transform,
                         CombatComponent& combat)
  : m_name(name), m_transform(transform), m_combat(combat)
{
  setupFSM();
  LOG_INFO("[AI] Component created for '{}'", name);
}

void AIComponent::setupFSM() {

  m_fsm.addState(AIState::Idle, {
    .onEnter = [this]{ LOG_TRACE("[AI] {} -> Idle", m_name); },
    .onUpdate = [this](float dt) {
      // Idle transitions to patrol if waypoints preset
      if (!m_waypoints.empty())
        m_fsm.transition(AIState::Patrol);  
      
      // Detection check from idle
      if (canSeePlayer())
        m_fsm.transition(AIState::Alert);
    },
    .onExit = nullptr
  });

  m_fsm.addState(AIState::Patrol, {
    .onEnter = [this]{ LOG_TRACE("[AI] {} -> Patrol", m_name); },
    .onUpdate = [this](float dt) {
      // Detection check while patrolling
      if (canSeePlayer()) {
        m_fsm.transition(AIState::Alert);
        return;  
      }
      tickPatrol(dt);
    },
    .onExit = nullptr
  });

  m_fsm.addState(AIState::Alert, {
    .onEnter = [this]{
      m_alertTimer = 0.0f;
      LOG_TRACE("[AI] {} is alert!", m_name); 
      if (onPlayerDetected) onPlayerDetected();
    },
    .onUpdate = [this](float dt) { tickAlert(dt); },
    .onExit = nullptr
  });

  m_fsm.addState(AIState::Chase, {
    .onEnter = [this]{ LOG_TRACE("[AI] {} is chasing player!", m_name); },
    .onUpdate = [this](float dt) {
      tickChase(dt);
    },
    .onExit = nullptr
  });

  m_fsm.addState(AIState::Attack, {
    .onEnter = [this]{
      m_attackTimer = 0.0f;
      LOG_INFO("[AI] {} attacking!", m_name);

      // Choose attack - Will look up from Lua table
      // for now set defaults

      AttackData chosenAttack;
      if (onChooseAttack) {
        std::string choice = onChooseAttack();

        chosenAttack.name = choice;
        chosenAttack.damage = (choice == "R2") ? 50.0f : 200.0f;
        chosenAttack.poiseDamage = (choice == "R2") ? 45.0f : 20.0f;
        chosenAttack.staminaCost = (choice == "R2") ? 30.0f : 15.0f;
        chosenAttack.startupTime = (choice == "R2") ? 0.4f : 0.2f;
        chosenAttack.activeTime = 0.12f;
        chosenAttack.recoveryTime = (choice == "R2") ? 0.6f : 0.3f;
        chosenAttack.range = m_attackRadius;
        chosenAttack.angle = 100.0f;
      } else {
        chosenAttack.name = "EnemySwipe";
        chosenAttack.damage = 40.f; 
        chosenAttack.poiseDamage = 20.f; 
        chosenAttack.staminaCost = 15.0f; 
        chosenAttack.startupTime = 0.2f; 
        chosenAttack.activeTime = 0.12f; 
        chosenAttack.recoveryTime = 0.35f; 
        chosenAttack.range = m_attackRadius; 
        chosenAttack.angle = 100.0f;
      }

      m_combat.tryAttack(chosenAttack);
    },
    .onUpdate = [this](float dt) { tickAttack(dt); },
    .onExit = nullptr
  });

  m_fsm.addState(AIState::Recover, {
    .onEnter = [this]{ m_recoverTimer = 0.0f; },
    .onUpdate = [this](float dt) { tickRecover(dt); },
    .onExit = nullptr
  });

  m_fsm.addState(AIState::Dead, {
    .onEnter = [this]{
      LOG_INFO("[AI] {} is dead", m_name);
    },
    .onUpdate  = nullptr,
    .onExit = nullptr
  });

  m_fsm.setInitialState(AIState::Idle);
}

void AIComponent::update(float dt, const glm::vec3& playerPos) {
  // Check if dead
  if (!m_combat.isAlive()) {
    if (m_fsm.current() != AIState::Dead)
      m_fsm.transition(AIState::Dead);
    return;
  }

  // Update player tracking
  m_playerPos = playerPos;
  m_distToPlayer = glm::length(playerPos - m_transform.getPosition());

  // Update combat components world data for hit detection
  glm::vec3 myPos = m_transform.getPosition();
  glm::vec3 toPlayer = (m_distToPlayer > 0.001f)
    ? glm::normalize(playerPos - myPos)
    : glm::vec3(0,0,1);
  m_combat.setWorldData(myPos, toPlayer);

  m_fsm.update(dt);
}

// Per state ticks
void AIComponent::tickPatrol(float dt) {
  if (m_waypoints.empty()) return;

  glm::vec3 target = m_waypoints[m_currentWaypoint];
  moveToward(target, dt);
  faceToward(target);

  // Waypoint reached?
  glm::vec3 diff = target - m_transform.getPosition();
  diff.y = 0.0f;
  float dist = glm::length(diff);
  if (dist < m_waypointThreshold) {
    m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
  }
}

void AIComponent::tickAlert(float dt) {
  m_alertTimer += dt;

  // If player still visible, chase time!
  if (canSeePlayer()) {
    m_fsm.transition(AIState::Chase);
    return;
  }

  // Lost player before committing - return to patrol after alert expires (search time)
  if (m_alertTimer >= m_alertDuration) {
    LOG_INFO("[AI] {} lost player, returning to patrol", m_name);
    if (onPlayerLost) onPlayerLost();
    m_fsm.transition(AIState::Patrol);
  }
}

void AIComponent::tickChase(float dt) {
  // Lost player - back to alert
  if (!canSeePlayer()) {
    m_fsm.transition(AIState::Alert);
    return;
  }

  // In attack range?
  if (inAttackRange()) {
    m_fsm.transition(AIState::Attack);
    return;
  }

  // Move toward player at chaseSpeed
  glm::vec3 myPos = m_transform.getPosition();
  glm::vec3 dir = glm::normalize(m_playerPos - myPos);

  constexpr float physStep = 1.0f / 60.0f;

  glm::vec3 newPos = myPos + dir * m_chaseSpeed * physStep;
  newPos.y = myPos.y; // Stay on ground
  m_transform.setPosition(newPos);
  faceToward(m_playerPos);
}

void AIComponent::tickAttack(float dt) {
  m_attackTimer += dt;
  
  // Face the players
  faceToward(m_playerPos);

  // Wait for combatComponent to finish the attack sequence
  if (!m_combat.isAttacking() && !m_combat.isRecovering()) {
    m_fsm.transition(AIState::Recover);
  }
}

void AIComponent::tickRecover(float dt) {
  m_recoverTimer += dt;
  if (m_recoverTimer >= m_recoverDuration) {
    // Decision, attack again or give chase?
    if (inAttackRange() && canSeePlayer()) {
      m_fsm.transition(AIState::Attack);
    } else if (canSeePlayer()) {
      m_fsm.transition(AIState::Chase);
    } else {
      m_fsm.transition(AIState::Patrol);
    }
  }
}

// Movement
void AIComponent::moveToward(const glm::vec3& target, float dt) {
  glm::vec3 myPos = m_transform.getPosition();
  glm::vec3 dir = target - myPos;
  dir.y = 0.0f; // Stay on ground
  
  float dist = glm::length(dir);
  if (dist < 0.001f) return;

  constexpr float physStep = 1.0f / 60.0f;

  dir = glm::normalize(dir);
  glm::vec3 newPos = myPos + dir * m_moveSpeed * physStep;
  newPos.y = myPos.y;
  m_transform.setPosition(newPos);
}

void AIComponent::faceToward(const glm::vec3& target) {
  glm::vec3 myPos = m_transform.getPosition();
  glm::vec3 dir = target - myPos;
  dir.y = 0.0f;

  if (glm::length(dir) < 0.001f) return;

  // atan2 gives angle around y axis
  float angle = atan2f(dir.x, dir.z);
  m_transform.setEulerAngles({ 90.0f, glm::degrees(angle),  0.0f });
}

// Utils
std::string AIComponent::getStateName() const {
  switch (m_fsm.current()) {
    case AIState::Idle: return "Idle";
    case AIState::Patrol: return "Patrol";
    case AIState::Alert: return "Alert";
    case AIState::Chase: return "Chase";
    case AIState::Attack: return "Attack";
    case AIState::Recover: return "Recover";
    case AIState::Dead: return "Dead";
    default: return "Unknown";
  }
}

void AIComponent::addWaypoint(const glm::vec3& point) {
  m_waypoints.push_back(point);
}

}
