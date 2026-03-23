#pragma once
#include <forge/StateMachine.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

namespace forge {

class CombatComponent;
class Transform;

enum class AIState {
  Idle, // Standing, doing nothing
  Patrol, // Walking between waypoints
  Alert, // Heard/Saw something - investigating
  Chase, // Player found, in pursuit
  Attack, // Executing an attack
  Recover, // Brief pause after attack
  Dead // Dead
};

class AIComponent {
public:
  AIComponent(const std::string& name,
                     Transform& transform,
                     CombatComponent& combat);
  
  // Call every frame - playerPos comes from APplicaiton
  void update(float dt, const glm::vec3& playerPos);

  // Config
  void addWaypoint(const glm::vec3& point);
  void setDetectionRadius(float r) { m_detectionRadius = r; }
  void setAttackRadius(float r) { m_attackRadius = r; }
  void setMoveSpeed(float s) { m_moveSpeed = s; }
  void setAlertDuration(float d) { m_alertDuration = d; }
  void setRecoverDuration(float d) { m_recoverDuration = d; }

  // Queries
  AIState getState() const { return m_fsm.current(); }
  std::string getStateName() const;
  float getDistToPlayer() const { return m_distToPlayer; }
  bool canSeePlayer() const { return m_distToPlayer <= m_detectionRadius; }
  bool inAttackRange() const { return m_distToPlayer <= m_attackRadius; }

  // Lua callbacks
  // Use these in Lua to override default behavior
  std::function<void()> onPlayerDetected;
  std::function<void()> onPlayerLost;
  // Returns name of attack to use
  std::function<std::string()> onChooseAttack;

  const std::string& getName() const { return m_name; }

private:
  void setupFSM();

  // Per-state tick functions -- called from FSM onUpdate
  void tickPatrol(float dt);
  void tickAlert(float dt);
  void tickChase(float dt);
  void tickAttack(float dt);
  void tickRecover(float dt);

  // Movement
  void moveToward(const glm::vec3& target, float dt);
  void faceToward(const glm::vec3& target);

  std::string m_name;
  Transform& m_transform;
  CombatComponent& m_combat;

  StateMachine<AIState> m_fsm;

  // Patrol
  std::vector<glm::vec3> m_waypoints;
  int m_currentWaypoint = 0;
  float m_waypointThreshold = 0.4f; // How close == "reached"

  // Tuning
  float m_detectionRadius = 7.0f;
  float m_attackRadius = 2.0f;
  float m_moveSpeed = 2.5f;
  float m_chaseSpeed = 4.0f;
  float m_alertDuration = 2.5f;
  float m_recoverDuration = 1.2f;

  // Timers
  float m_alertTimer = 0.0f;
  float m_attackTimer = 0.0f;
  float m_recoverTimer = 0.0f;

  // Runtime
  glm::vec3 m_playerPos = { 0,0,0 };
  float m_distToPlayer = 999.0f;
};
}
