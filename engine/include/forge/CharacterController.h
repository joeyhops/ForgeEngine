#pragma once
#include <forge/Transform.h>
#include <glm/glm.hpp>
#include <memory>

class btCapsuleShape;
class btPairCachingGhostObject;
class btKinematicCharacterController;

namespace forge {

class PhysicsWorld;

// CharacterController
// Wraps btKinematicCharacterController to handle slope walking,
// step-up and stable floor contact natively

// frame usage:
// 1. controller.setWalkDirection(velocity * dt); // BEFORE physics.step()
// 2. physics.sted(dt);
// 3. controller.syncTransform();  // pull position back to Transform

class CharacterController {
public:

  //radius - capsule radius in meters (collision cylinder radius)
  //height - cylinder-only height in meters; total height = height + 2 * radius
  CharacterController(PhysicsWorld& world, Transform& transform, float radius = 0.3f, float height = 1.0f);
  ~CharacterController();

  CharacterController(const CharacterController&) = delete;
  CharacterController& operator=(const CharacterController&) = delete;

  // Set displacement for next physics step
  void setWalkDirection(const glm::vec3& displacement);

  void syncTransform();

  void warp(const glm::vec3& pos);

  bool isOnGround() const;
  glm::vec3 getPosition() const;
private:
  PhysicsWorld& m_world;
  Transform& m_transform;

  std::unique_ptr<btCapsuleShape> m_shape;
  std::unique_ptr<btPairCachingGhostObject> m_ghost;
  std::unique_ptr<btKinematicCharacterController> m_controller;
};
}
