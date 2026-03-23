#include <forge/RigidBodyComponent.h>
#include <forge/PhysicsWorld.h>
#include <forge/Logger.h>

#include <btBulletDynamicsCommon.h>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace forge {
// conversion helpers
static btVector3 toBt(const glm::vec3& v) { return {v.x, v.y, v.z}; }
static btQuaternion toBt(const glm::quat& q) { return {q.x, q.y, q.z, q.w }; }
static glm::vec3 toGlm(const btVector3& v) { return { v.x(), v.y(), v.z() }; }
static glm::quat toGlm(const btQuaternion& q) { return { q.w(), q.x(), q.y(), q.z() }; }

RigidBodyComponent::RigidBodyComponent(PhysicsWorld& world,
                                       Transform& transform,
                                       CollisionShape shape,
                                       const glm::vec3& size,
                                       float mass)
  : m_world(world), m_transform(transform)
{
  switch (shape) {
    case CollisionShape::Box:
      m_shape = std::make_unique<btBoxShape>(toBt(size));
      break;
    case CollisionShape::Sphere:
      m_shape = std::make_unique<btSphereShape>(size.x);
      break;
    case CollisionShape::Capsule:
      m_shape = std::make_unique<btCapsuleShape>(size.x, size.y);
      break;
    case CollisionShape::Plane:
      m_shape = std::make_unique<btStaticPlaneShape>(btVector3(0,1,0), 0);
      break;
  }

  // set initial transform from Engine transform
  const glm::vec3& pos = transform.getPosition();
  const glm::quat& rot = transform.getRotation();

  btTransform startTransform;
  startTransform.setIdentity();
  startTransform.setOrigin(toBt(pos));
  startTransform.setRotation(toBt(rot));

  // MotionState is bullets interface for reading/writing transforms
  // For dynamic bodies, Bullet updates this every step()
  m_motionState = std::make_unique<btDefaultMotionState>(startTransform);

  // Calc Intertia
  // static bodies have zero inertia - no movement
  // dynamic bodies need inertia calculated from their shape
  btVector3 localIntertia(0, 0, 0);
  if (mass > 0.0f)
    m_shape->calculateLocalInertia(mass, localIntertia);

  // Create rigid body
  btRigidBody::btRigidBodyConstructionInfo info(
    mass, m_motionState.get(), m_shape.get(), localIntertia);

  m_body = std::make_unique<btRigidBody>(info);

  // Register with the world
  m_world.addRigidBody(m_body.get());

  LOG_INFO("[Physics] RigidBody created (mass={})", mass);
}

RigidBodyComponent::~RigidBodyComponent() {
  m_world.removeRigidBody(m_body.get());
}

// Sync
void RigidBodyComponent::syncTransform() {
  if (m_body->getMass() == 0.0f) return;

  btTransform t;
  m_motionState->getWorldTransform(t);

  // Write bullets updated pos/rotation back into Transform
  m_transform.setPosition(toGlm(t.getOrigin()));
  m_transform.setRotation(toGlm(t.getRotation()));
}

// Forces
void RigidBodyComponent::applyImpulse(const glm::vec3& impulse) {
  // Wake the body first - bullet puts idle bodies to sleep to save CPU time
  m_body->activate(true);
  m_body->applyCentralImpulse(toBt(impulse));
}

void RigidBodyComponent::setAngularForce(const glm::vec3& factor) {
  m_body->setAngularFactor(toBt(factor));
}

void RigidBodyComponent::teleport(const glm::vec3& pos) {
  btTransform t = m_body->getWorldTransform();
  t.setOrigin(toBt(pos));

  // Must update both the body AND the motion state
  // motion state is what syncTransform reads back from
  // Upating only one causes them to diverge
  m_body->setWorldTransform(t);
  m_motionState->setWorldTransform(t);

  // wake the body - Bullet puts idle bodies to sleep
  // A sleeping body ignores setWorldTransform completely
  m_body->activate(true);
}

}

