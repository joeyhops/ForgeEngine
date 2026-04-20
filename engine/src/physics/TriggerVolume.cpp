#include <forge/TriggerVolume.h>
#include <forge/PhysicsWorld.h>
#include <forge/Logger.h>

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <glm/gtc/type_ptr.hpp>

namespace forge {

// Helpers

static btTransform toBullet(const glm::vec3& pos) {
  btTransform t;
  t.setIdentity();
  t.setOrigin(btVector3(pos.x, pos.y, pos.z));
  return t;
}

TriggerVolume::TriggerVolume(PhysicsWorld& physics,
                             const glm::vec3& center,
                             float radius,
                             std::function<void()> onEnter,
                             std::function<void()> onExit)
    : m_physics(physics)
    , m_onEnter(std::move(onEnter))
    , m_onExit(std::move(onExit))
{
  init(physics, center, new btSphereShape(radius));
}


TriggerVolume::TriggerVolume(PhysicsWorld& physics,
                             const glm::vec3& center,
                             const glm::vec3& halfExtents,
                             std::function<void()> onEnter,
                             std::function<void()> onExit)
    : m_physics(physics)
    , m_onEnter(std::move(onEnter))
    , m_onExit(std::move(onExit))
{
  init(physics, center,
       new btBoxShape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z)));
}

TriggerVolume::TriggerVolume(PhysicsWorld& physics,
                             const std::vector<glm::vec3>& hullVertices,
                             std::function<void()> onEnter,
                             std::function<void()> onExit)
    : m_physics(physics)
    , m_onEnter(std::move(onEnter))
    , m_onExit(std::move(onExit))
{
  auto* hull = new btConvexHullShape();

  for (const auto& v : hullVertices) {
    hull->addPoint(btVector3(v.x, v.y, v.z), false);
  }

  hull->recalcLocalAabb();
  hull->optimizeConvexHull();

  init(physics, glm::vec3(0.0f, 0.0f, 0.0f), hull);
}

// Shared init
void TriggerVolume::init(PhysicsWorld& physics, const glm::vec3& center, btCollisionShape* shape) {
  m_center = center;
  m_shape = shape;

  m_ghost = new btPairCachingGhostObject();
  m_ghost->setCollisionShape(m_shape);
  m_ghost->setWorldTransform(toBullet(center));

  // CF_NO_CONTACT_RESPONSE: ghost objects sense overlap but do not push things
  m_ghost->setCollisionFlags(m_ghost->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);

  // Group 2 (sensor), mask -1 (collide with everything)
  physics.addGhostObject(m_ghost, 2, -1);

  m_ghost->setActivationState(DISABLE_DEACTIVATION);
  LOG_INFO("[TriggerVolume] Created at ({:.1f},{:.1f},{:.1f})",
           center.x, center.y, center.z);
}

TriggerVolume::~TriggerVolume() {
  m_physics.removeGhostObject(m_ghost);
  delete m_ghost;
  delete m_shape;
}

void TriggerVolume::update() {
  if (!m_enabled) return;

  bool nowOverlapping = (m_ghost->getOverlappingPairCache()->getNumOverlappingPairs() > 0);

  for (int i = 0; i < m_ghost->getNumOverlappingObjects(); i++) {
    btCollisionObject* obj = m_ghost->getOverlappingObject(i);

    if (obj->getCollisionFlags() & btCollisionObject::CF_CHARACTER_OBJECT) {
      nowOverlapping = true;
      break;
    }
  }

  if (nowOverlapping && !m_isOverlapping) {
    if (m_onEnter) m_onEnter();
  } else if (!nowOverlapping && m_isOverlapping) {
    if (m_onExit) m_onExit();
  }

  m_isOverlapping = nowOverlapping;
}

}
