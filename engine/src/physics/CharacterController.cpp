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

CharacterController::CharacterController(PhysicsWorld& world, Transform& transform, float radius, float cylinderHeight)
  : m_world(world),
    m_transform(transform),
    m_radius(radius),
    m_cylinderHalfHeight(cylinderHeight * 0.5f)
{
  m_shape = std::make_unique<btCapsuleShape>(
    static_cast<btScalar>(radius),
    static_cast<btScalar>(cylinderHeight)
  );

  // Ghost object - the kinematic character controller uses this instead of a rigid body.
  m_ghost = std::make_unique<btPairCachingGhostObject>();
  m_ghost->setCollisionShape(m_shape.get());
  m_ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

  const float totalHalf = getTotalHalfHeight();
  glm::vec3 footPos = transform.getPosition();
  glm::vec3 capsuleCenter = footPos + glm::vec3(0.0f, totalHalf, 0.0f);

  // Start at transforms curr world pos
  btTransform startTrans;
  startTrans.setIdentity();
  startTrans.setOrigin(toBt(capsuleCenter));
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

  LOG_INFO("[CharController] Created - radius={:.2f}m height={:.2f}m", radius, cylinderHeight);
}

CharacterController::~CharacterController() {
  m_world.removeAction(m_controller.get());
  m_world.removeGhostObject(m_ghost.get());
}

// Per frame interface
void CharacterController::setWalkDirection(const glm::vec3& displacement) {
  m_controller->setWalkDirection(toBt(displacement));
}

void CharacterController::setGravity(float gravity) {
  m_controller->setGravity(btVector3(0.0f, gravity, 0.0f));
}

void CharacterController::syncTransform() {
  const btTransform& t = m_ghost->getWorldTransform();
  glm::vec3 capsuleCenter = toGlm(t.getOrigin());
  glm::vec3 footPos = capsuleCenter - glm::vec3(0.0f, getTotalHalfHeight(), 0.0f);
  m_transform.setPosition(footPos);
}

// Utils
void CharacterController::warp(const glm::vec3& footPos) {
  glm::vec3 capsuleCenter = footPos + glm::vec3(0.0f, getTotalHalfHeight(), 0.0f);
  m_controller->warp(toBt(capsuleCenter));
  m_transform.setPosition(footPos);
}

void CharacterController::warpAndSettle(const glm::vec3& footPos) {
  warp(footPos);

  m_ghost->activate(true);
  btVector3 aabbMin, aabbMax;
  m_ghost->getCollisionShape()->getAabb(m_ghost->getWorldTransform(),
                                        aabbMin,
                                        aabbMax);
  m_world.getBroadphase()->setAabb(
    m_ghost->getBroadphaseHandle(), aabbMin, aabbMax,
    m_world.getDispatcher()
  );

  m_controller->setWalkDirection(btVector3(0, 0, 0));
}

bool CharacterController::isOnGround() const {
  return m_controller->onGround();
}

glm::vec3 CharacterController::getPosition() const {
  const btTransform& t = m_ghost->getWorldTransform();
  glm::vec3 capsuleCenter = toGlm(t.getOrigin());
  return capsuleCenter - glm::vec3(0.0f, getTotalHalfHeight(), 0.0f);
}

glm::vec3 CharacterController::getCapsuleCenter() const {
  return toGlm(m_ghost->getWorldTransform().getOrigin());
}

}
