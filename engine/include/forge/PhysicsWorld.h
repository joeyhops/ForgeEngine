#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Forward declare bullet types
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btRigidBody;
class btCollisionShape;
class btDispatcher;

class btActionInterface;
class btCollisionObject;

namespace forge {

// Result of raycast
struct RaycastHit {
  bool hit = false;
  glm::vec3 point = { 0,0,0 }; // World pos of impact
  glm::vec3 normal = { 0,0,0 }; // surface normal at impact
  float distance = 0.0f;
};

class PhysicsWorld {
public:
  PhysicsWorld();
  ~PhysicsWorld();

  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  // step sim forward by dt seconds
  // maxSubStep: how many sub-steps bullet can take if dt is large
  // prevents tunneling at low framerates
  void step(float dt, int maxSubSteps = 10);

  // add/remove rigid bodies from sim
  void addRigidBody(btRigidBody* body);
  void removeRigidBody(btRigidBody* body);

  // ghost objects
  // Character controller/trigger volumes
  // group/mask are btBroadphaseProxy filter constants
  void addGhostObject(btCollisionObject* ghost, int group, int mask);
  void removeGhostObject(btCollisionObject* ghost);

  // action interfaces
  void addAction(btActionInterface* action);
  void removeAction(btActionInterface* action);

  // Cast a ray 'from' to 'to', returns first hit
  RaycastHit raycast(const glm::vec3& from, const glm::vec3& to) const;

  // gravity - default is Earth-like (negative y)
  void setGravity(const glm::vec3& gravity);

  btDiscreteDynamicsWorld* getBulletWorld() { return m_world.get(); }
  btBroadphaseInterface* getBroadphase() const;
  btDispatcher* getDispatcher() const;

private:
  // Order matters, must be destroyed in reverse order of creation
  // Unique_ptr handles that automatically (destroyed bottom-up in class)
  std::unique_ptr<btDefaultCollisionConfiguration> m_config;
  std::unique_ptr<btCollisionDispatcher> m_dispatcher;
  std::unique_ptr<btBroadphaseInterface> m_broadphase;
  std::unique_ptr<btSequentialImpulseConstraintSolver> m_solver;
  std::unique_ptr<btDiscreteDynamicsWorld> m_world;
};

}
