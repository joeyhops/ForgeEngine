#pragma once
#include <glm/glm.hpp>

namespace forge {

class Camera;
class PhysicsWorld;

// ThirdPersonCamera
// Wraps basic Camera with Yaw/Pitch orbit driven by mouse deltas
// Performs short raycast from the players chest to determine
// desired camera pos each frame so the camera adjusts to
// occluding geometry
//
// Lock-on (Tab): camera smoothly interpolates to sit behind player
// while looking at midpoint between player and locked on enemy.
// Movement becomes strafe-relative when locked.
//
// Shaders unchanged, no adjustments to Camera::getViewProjection()
//
// Frame usage:
//  tpCam.applyMouseDelta(dx, dy); // Raw GLFW cursor delta
//  tpCam.update(playerPos); // repos camera
//

class ThirdPersonCamera {
public:
  ThirdPersonCamera(Camera& camera, PhysicsWorld& physics);

  // Feed raw pixel delta from GLFW each frame
  // (no op when locked on)
  void applyMouseDelta(float dx, float dy);

  void update(const glm::vec3& playerPos);

  void setLockOnTarget(const glm::vec3* targetPos);
  bool isLockedOn() const { return m_lockTarget != nullptr; }

  // Camera relative horizontal vectors - used by handleInput so WASD moves
  // relative to the camera's direction, not world axes
  glm::vec3 getHorizontalForward() const;
  glm::vec3 getHorizontalRight() const;

  float getYaw() const { return m_yaw; }
  float getPitch() const { return m_pitch; }

  // Runtime tuning (wire into debug ui)
  void setDistance(float d) { m_distance = d; }
  void setSensitivity(float s) { m_sensitivity = s; }

private:
  Camera& m_camera;
  PhysicsWorld& m_physics;

  // Orbit state (degrees)
  float m_yaw = 180.0f; // 180 puts camera behind default player facing direction
  float m_pitch = 20.0f; // tild down; positive = looking down at player
  float m_distance = 6.0f; // meters from player chest

  float m_sensitivity = 0.15f; // deg per pixel
  
  static constexpr float k_pitchMin = -30.0f;
  static constexpr float k_pitchMax = 65.0f;

  static constexpr float k_chestOffset = 1.2f;
  static constexpr float k_occlusionMargin = 0.15f;
  static constexpr float k_minDistance = 0.5f;

  // lock on state
  const glm::vec3* m_lockTarget = nullptr;
  float m_lockYaw = 180.0f;
};

}
