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
  void reset();

  // Config
  void addWaypoint(const glm::vec3& point);
  void clearWaypoints() { m_waypoints.clear(); m_currentWaypoint = 0; }
  int waypointCount() const { return static_cast<int>(m_waypoints.size()); }
  void setDetectionRadius(float r) { m_detectionRadius = r; }
  void setAttackRadius(float r) { m_attackRadius = r; }
  void setMoveSpeed(float s) { m_moveSpeed = s; }
  void setChaseSpeed(float s) { m_chaseSpeed = s; }
  void setAlertDuration(float d) { m_alertDuration = d; }
  void setRecoverDuration(float d) { m_recoverDuration = d; }

  // weapons
  void setWeaponConfig(const std::string& weaponId, bool drop, const std::string& otherDropId = "") {
    m_weaponId = weaponId;
    m_dropWeapon = drop;
    if (!drop) {
      if (!otherDropId.empty())
        m_dropOtherWeaponId = otherDropId;
    }
  }

  const std::string& getWeaponId() const { return m_weaponId; }
  const std::string& getDropId() const { return m_dropOtherWeaponId; }
  bool shouldDropWeapon() const { return (m_dropWeapon && !m_weaponId.empty()) || !m_dropOtherWeaponId.empty(); }
  bool dropsOwnWeapon() const { return (m_dropWeapon && !m_weaponId.empty()); }

  // Queries
  AIState getState() const { return m_fsm.current(); }
  std::string getStateName() const;
  float getDistToPlayer() const { return m_distToPlayer; }
  bool canSeePlayer() const { return m_distToPlayer <= m_detectionRadius; }
  bool inAttackRange() const { return m_distToPlayer <= m_attackRadius; }

  // Tuning value read-back used by AI Debug panel
  float getDetectionRadius() const { return m_detectionRadius; }
  float getAttackRadius() const { return m_attackRadius; }
  float getMoveSpeed() const { return m_moveSpeed; }
  float getChaseSpeed() const { return m_chaseSpeed; }

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
  float m_moveSpeed = 5.0f;
  float m_chaseSpeed = 8.0f;
  float m_alertDuration = 2.5f;
  float m_recoverDuration = 1.2f;

  // Timers
  float m_alertTimer = 0.0f;
  float m_attackTimer = 0.0f;
  float m_recoverTimer = 0.0f;

  // Runtime
  glm::vec3 m_playerPos = { 0,0,0 };
  float m_distToPlayer = 999.0f;

  // Weapons
  std::string m_weaponId;
  bool m_dropWeapon = true;
  std::string m_dropOtherWeaponId;
};
}
