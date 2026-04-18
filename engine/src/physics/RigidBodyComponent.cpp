#include <LinearMath/btQuaternion.h>
#include <LinearMath/btVector3.h>
#include <cstdint>
#include <forge/RigidBodyComponent.h>
#include <forge/PhysicsWorld.h>
#include <forge/Logger.h>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace forge {
// conversion helpers
static btVector3 toBt(const glm::vec3& v) {
  return btVector3(
    static_cast<btScalar>(v.x),
    static_cast<btScalar>(v.y),
    static_cast<btScalar>(v.z)
  ); 
}
static btQuaternion toBt(const glm::quat& q) {
  return btQuaternion(
    static_cast<btScalar>(q.x),
    static_cast<btScalar>(q.y),
    static_cast<btScalar>(q.z),
    static_cast<btScalar>(q.w)
  ); 
}
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
    case CollisionShape::Mesh:
      LOG_ERROR("[Physics] CollisionShape::Mesh requires the mesh constructor overload. "
                " Using box fallback");
      m_shape = std::make_unique<btBoxShape>(toBt(size));
      break;
  }
  
  finalize(transform, mass);
}

RigidBodyComponent::RigidBodyComponent(PhysicsWorld& world,
                                       Transform& transform,
                                       const std::vector<glm::vec3>& positions,
                                       const std::vector<uint32_t>& indices,
                                       float mass)
  : m_world(world), m_transform(transform)
{
  if (mass != 0.0f) {
    LOG_WARN("[Physics] Triangle mesh bodies must be static (mass=0). Forcing mass = 0");
    mass = 0.0f;
  }

  if (positions.empty() || indices.empty() || indices.size() % 3 != 0) {
    LOG_ERROR("[Physics] Invalid mesh data for RigidBodyComponent - falling back to plane.");
    m_shape = std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0);
    finalize(transform, 0.0f);
    return;
  }

  m_triangleMesh = std::make_unique<btTriangleMesh>();
  m_triangleMesh->preallocateVertices(static_cast<int>(positions.size()));
  m_triangleMesh->preallocateIndices(static_cast<int>(indices.size()));

  const size_t triCount = indices.size() / 3;
  for (size_t t = 0; t < triCount; ++t) {
    uint32_t i0 = indices[t * 3 + 0];
    uint32_t i1 = indices[t * 3 + 1];
    uint32_t i2 = indices[t * 3 + 2];

    m_triangleMesh->addTriangle(
      toBt(positions[i0]), 
      toBt(positions[i1]), 
      toBt(positions[i2])
    );
  }

  auto bvh = std::make_unique<btBvhTriangleMeshShape>(m_triangleMesh.get(), true);
  m_shape = std::move(bvh);

  LOG_INFO("[Physics] Triangle mesh collision built: {} triangles", triCount);
  finalize(transform, 0.0f);
}

void RigidBodyComponent::finalize(Transform& transform, float mass) {
  const glm::vec3& pos = transform.getPosition();
  const glm::quat& rot = transform.getRotation();

  btTransform startTransform;
  startTransform.setIdentity();
  startTransform.setOrigin(toBt(pos));
  startTransform.setRotation(toBt(rot));

  m_motionState = std::make_unique<btDefaultMotionState>(startTransform);

  btVector3 localInertia(0, 0, 0);
  if (mass > 0.0f)
    m_shape->calculateLocalInertia(mass, localInertia);

  btRigidBody::btRigidBodyConstructionInfo info(mass, m_motionState.get(), m_shape.get(), localInertia);

  m_body = std::make_unique<btRigidBody>(info);
  m_world.addRigidBody(m_body.get());

  LOG_INFO("[Physics] RigidBody created (mass={}{})", mass, m_triangleMesh ? ", mesh collision" : "");
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
  m_body->setLinearVelocity(btVector3(0,0,0));
  m_body->setAngularVelocity(btVector3(0,0,0));
  m_body->activate(true);
}

}

