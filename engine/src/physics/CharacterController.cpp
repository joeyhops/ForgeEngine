#include <forge/CharacterController.h>
#include <forge/PhysicsWorld.h>
#include <forge/Logger.h>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>

namespace forge {

// helpers
//
static btVector3 toBt(const glm::vec3& v) {
  return btVector3(
    static_cast<btScalar>(v.x),
    static_cast<btScalar>(v.y),
    static_cast<btScalar>(v.z)
  ); 
}
static glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }

CharacterController::CharacterController(PhysicsWorld& world, Transform& transform, float radius, float height)
  : m_world(world), m_transform(transform)
{
  m_shape = std::make_unique<btCapsuleShape>(
    static_cast<btScalar>(radius),
    static_cast<btScalar>(height)
  );

  // Ghost object - the kinematic character controller uses this instead of a rigid body.
  m_ghost = std::make_unique<btPairCachingGhostObject>();
  m_ghost->setCollisionShape(m_shape.get());
  m_ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

  // Start at transforms curr world pos
  btTransform startTrans;
  startTrans.setIdentity();
  startTrans.setOrigin(toBt(transform.getPosition()));
  m_ghost->setWorldTransform(startTrans);

  // Register ghost with broadphase so other objects can collide with it
  world.addGhostObject(
    m_ghost.get(), 
    btBroadphaseProxy::CharacterFilter, 
    btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter
  );

  // Build char controller
  m_controller = std::make_unique<btKinematicCharacterController>(
    m_ghost.get(),
    m_shape.get(),
    static_cast<btScalar>(0.35f)
  );

  m_controller->setGravity(btVector3(0.0f, -20.0f, 0.0f));
  m_controller->setMaxSlope(static_cast<btScalar>(glm::radians(46.0f)));

  // Register controller as action so Bullet calls updateAction each step
  world.addAction(m_controller.get());

  LOG_INFO("[CharController] Created - radius={:.2f}m height={:.2f}m", radius, height);
}

CharacterController::~CharacterController() {
  m_world.removeAction(m_controller.get());
  m_world.removeGhostObject(m_ghost.get());
}

// Per frame interface
void CharacterController::setWalkDirection(const glm::vec3& displacement) {
  m_controller->setWalkDirection(toBt(displacement));
}

void CharacterController::syncTransform() {
  const btTransform& t = m_ghost->getWorldTransform();
  m_transform.setPosition(toGlm(t.getOrigin()));
}

// Utils
void CharacterController::warp(const glm::vec3& pos) {
  m_controller->warp(toBt(pos));
  m_transform.setPosition(pos);
}

bool CharacterController::isOnGround() const {
  return m_controller->onGround();
}

glm::vec3 CharacterController::getPosition() const {
  return toGlm(m_ghost->getWorldTransform().getOrigin());
}

}
