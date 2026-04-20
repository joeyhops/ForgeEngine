#pragma once
#include <forge/Transform.h>
#include <glm/glm.hpp>
#include <memory>

class btRigidBody;
class btCollisionShape;
class btDefaultMotionState;
class btTriangleMesh;

namespace forge {
class PhysicsWorld;

// shape of collision volume
enum class CollisionShape {
  Box, // half-extents define the size
  Sphere, // Radius defines the size
  Capsule, // radius + height - used for characters
  Plane, // Infinite static floor/wall
  Mesh,
};

class RigidBodyComponent {
public:
  // mass = 0 == static (immovable) -- floors, walls
  // mass > 0 == dynamic -- affected by gravity and forces
  RigidBodyComponent(PhysicsWorld& world,
                     Transform& transform,
                     CollisionShape shape,
                     const glm::vec3& shapeSize, // box: half-extents, sphere: (radius,0,0), capsule: (radius, height, 0)
                     float mass = 1.0f);

  RigidBodyComponent(PhysicsWorld& world,
                     Transform& transform,
                     const std::vector<glm::vec3>& positions,
                     const std::vector<uint32_t>& indices,
                     float mass = 0.0f);
  RigidBodyComponent(PhysicsWorld& world,
                     Transform& transform,
                     const std::vector<glm::vec3>& hullPoints,
                     float mass = 0.0f);

  ~RigidBodyComponent();

  RigidBodyComponent(const RigidBodyComponent&) = delete;
  RigidBodyComponent& operator=(const RigidBodyComponent&) = delete;

  // call after PhysicsWorld::step() -> syncs bullet transform -> engine transform
  void syncTransform();

  void applyImpulse(const glm::vec3& impulse);

  void setAngularForce(const glm::vec3& factor);

  btRigidBody* getBulletBody() { return m_body.get(); }

  // Instantly move body to new pos - used for AI/Scripted movement
  // Unlike forces, this bypasses the simulation and sets pos directly
  void teleport(const glm::vec3& pos);

  void setKinematic(bool kinematic);

private:

  void finalize(Transform& transform, float mass);

  PhysicsWorld& m_world;
  Transform& m_transform;

  std::unique_ptr<btTriangleMesh> m_triangleMesh;

  // Bullet owned butwe manage lifetime via unique_ptr + custom deleter
  std::unique_ptr<btCollisionShape> m_shape;
  std::unique_ptr<btDefaultMotionState> m_motionState;
  std::unique_ptr<btRigidBody> m_body;
};

}
