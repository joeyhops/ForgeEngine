#include <forge/PhysicsWorld.h>
#include <forge/Logger.h>

#include <btBulletDynamicsCommon.h>
#include <memory>

namespace forge {
// Helpers to convert from GLM -> bullet vectors
static btVector3 toBt(const glm::vec3& v) {
  return btVector3(
    static_cast<btScalar>(v.x),
    static_cast<btScalar>(v.y),
    static_cast<btScalar>(v.z)
  ); 
}
static glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }

PhysicsWorld::PhysicsWorld() {
  m_config = std::make_unique<btDefaultCollisionConfiguration>();
  m_dispatcher = std::make_unique<btCollisionDispatcher>(m_config.get());
  m_broadphase = std::make_unique<btDbvtBroadphase>();
  m_solver = std::make_unique<btSequentialImpulseConstraintSolver>();

  m_world = std::make_unique<btDiscreteDynamicsWorld>(
    m_dispatcher.get(),
    m_broadphase.get(),
    m_solver.get(),
    m_config.get()
  );

  m_world->setGravity(btVector3(0.0f, -9.81f, 0.0f));
  LOG_INFO("[Physics] World initialized");
}

PhysicsWorld::~PhysicsWorld() {
  LOG_INFO("[Physics] World Destroyed");
}

// simulation
void PhysicsWorld::step(float dt, int maxSubSteps) {
  m_world->stepSimulation(dt, maxSubSteps, 1.0f / 60.0f);
}

void PhysicsWorld::addRigidBody(btRigidBody* body) {
  m_world->addRigidBody(body);
}

void PhysicsWorld::removeRigidBody(btRigidBody* body) {
  m_world->removeRigidBody(body);
}

void PhysicsWorld::setGravity(const glm::vec3& gravity) {
  m_world->setGravity(toBt(gravity));
}

// Raycast

RaycastHit PhysicsWorld::raycast(const glm::vec3& from, const glm::vec3& to) const {
  btVector3 btFrom = toBt(from);
  btVector3 btTo = toBt(to);

  // ClosestRayResultCallback fills itself with hit info
  btCollisionWorld::ClosestRayResultCallback callback(btFrom, btTo);
  m_world->rayTest(btFrom, btTo, callback);

  RaycastHit result;
  if (callback.hasHit()) {
    result.hit = true;
    result.point = toGlm(callback.m_hitPointWorld);
    result.normal = toGlm(callback.m_hitNormalWorld);
    result.distance = glm::length(result.point - from);
  }
  return result;
}

}
