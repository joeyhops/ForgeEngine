#include <forge/PhysicsWorld.h>
#include <forge/TriggerVolume.h>

#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <btBulletDynamicsCommon.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace forge;

namespace {

btTransform at(glm::vec3 p) {
  btTransform t;
  t.setIdentity();
  t.setOrigin(btVector3(p.x, p.y, p.z));
  return t;
}

// Bullet doesn't own objects added to a world, and destroying a world with an
// object still registered is a use-after-free in ~btCollisionWorld. Both
// helpers remove themselves, and must be declared after the PhysicsWorld.
class StaticBox {
public:
  StaticBox(PhysicsWorld& world, glm::vec3 center, glm::vec3 halfExtents)
    : m_world(world), m_shape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z)) {
    m_motion = std::make_unique<btDefaultMotionState>(at(center));
    btRigidBody::btRigidBodyConstructionInfo info(0.0f, m_motion.get(), &m_shape);
    m_body = std::make_unique<btRigidBody>(info);
    m_world.addRigidBody(m_body.get());
  }
  ~StaticBox() { m_world.removeRigidBody(m_body.get()); }
  StaticBox(const StaticBox&) = delete;
  StaticBox& operator=(const StaticBox&) = delete;

private:
  PhysicsWorld& m_world;
  btBoxShape m_shape;
  std::unique_ptr<btDefaultMotionState> m_motion;
  std::unique_ptr<btRigidBody> m_body;
};

class FakeCharacter {
public:
  FakeCharacter(PhysicsWorld& world, glm::vec3 pos) : m_world(world), m_shape(0.3f, 1.2f) {
    m_ghost.setCollisionShape(&m_shape);
    m_ghost.setCollisionFlags(m_ghost.getCollisionFlags() | btCollisionObject::CF_CHARACTER_OBJECT);
    m_ghost.setWorldTransform(at(pos));
    m_world.addGhostObject(&m_ghost, btBroadphaseProxy::CharacterFilter, btBroadphaseProxy::AllFilter);
  }
  ~FakeCharacter() { m_world.removeGhostObject(&m_ghost); }
  FakeCharacter(const FakeCharacter&) = delete;
  FakeCharacter& operator=(const FakeCharacter&) = delete;

  void moveTo(glm::vec3 p) { m_ghost.setWorldTransform(at(p)); }

private:
  PhysicsWorld& m_world;
  btCapsuleShape m_shape;
  btPairCachingGhostObject m_ghost;
};

}

// 1/30 rather than 1/60: stepSimulation only runs collision detection when a
// full fixed substep has elapsed, and (1/60f)/(1/60f) is a float compare
TEST_CASE("TriggerVolume fires onEnter and onExit once each as a character passes through",
          "[component][physics][trigger]") {
  PhysicsWorld physics;
  int enters = 0, exits = 0;
  TriggerVolume trigger(physics, glm::vec3(0, 5, 0), glm::vec3(1, 1, 1),
                        [&] { ++enters; }, [&] { ++exits; });
  FakeCharacter hero(physics, { 30, 5, 0 });
  auto tick = [&] { physics.step(1.0f / 30.0f); trigger.update(); };

  tick();
  CHECK(enters == 0);
  CHECK_FALSE(trigger.isOverlapping());

  hero.moveTo({ 0, 5, 0 });
  tick();
  CHECK(enters == 1);
  CHECK(trigger.isOverlapping());
  tick();
  CHECK(enters == 1);

  hero.moveTo({ 30, 5, 0 });
  tick();
  CHECK(exits == 1);
  CHECK_FALSE(trigger.isOverlapping());
}

// Static bodies get group StaticFilter / mask AllFilter^StaticFilter, and the trigger's
// group 2 IS StaticFilter, so they never pair. Guards against the group being "fixed"
// to SensorTrigger, which would make level geometry occupy triggers
TEST_CASE("TriggerVolume does not count static level geometry as an occupant",
          "[component][physics][trigger][bug:C5]") {
  PhysicsWorld physics;
  StaticBox floor(physics, { 0, -0.5f, 0 }, { 10, 0.5f, 10 });
  int enters = 0;
  TriggerVolume trigger(physics, glm::vec3(0, 0.5f, 0), glm::vec3(1, 1, 1), [&] { ++enters; });

  physics.step(1.0f / 30.0f);
  trigger.update();
  CHECK(enters == 0);
  CHECK_FALSE(trigger.isOverlapping());
}

TEST_CASE("TriggerVolume does not count another overlapping trigger as an occupant",
          "[component][physics][trigger][!shouldfail][bug:C5]") {
  PhysicsWorld physics;
  int enters = 0;
  TriggerVolume trigger(physics, glm::vec3(0, 5, 0), glm::vec3(1, 1, 1), [&] { ++enters; });
  TriggerVolume neighbor(physics, glm::vec3(0, 5, 1.5f), glm::vec3(1, 1, 1));

  physics.step(1.0f / 30.0f);
  trigger.update();
  CHECK(enters == 0);
  CHECK_FALSE(trigger.isOverlapping());
}

