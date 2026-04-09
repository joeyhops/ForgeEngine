#include <forge/ThirdPersonCamera.h>
#include <forge/Camera.h>
#include <forge/PhysicsWorld.h>
#include <forge/Logger.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace forge {

ThirdPersonCamera::ThirdPersonCamera(Camera& camera, PhysicsWorld& physics)
  : m_camera(camera), m_physics(physics)
{}

// Input
void ThirdPersonCamera::applyMouseDelta(float dx, float dy) {
  if (m_lockTarget) return;

  m_yaw += dx * m_sensitivity;
  m_pitch -= dy * m_sensitivity;
  m_pitch = std::clamp(m_pitch, k_pitchMin, k_pitchMax);
}

// Per frame
void ThirdPersonCamera::update(const glm::vec3& playerPos) {
  const glm::vec3 chestPos = playerPos + glm::vec3(0.0f, k_chestOffset, 0.0f);

  float effectiveYaw = m_yaw;
  float effectivePitch = m_pitch;
  glm::vec3 lookTarget = chestPos;

  if (m_lockTarget) {
    // Lock on: smoothly orbit camera behind player looking at midpoint between enemy and player chest
    glm::vec3 toEnemy = *m_lockTarget - playerPos;
    toEnemy.y = 0.0f;

    if (glm::length(toEnemy) > 0.001f) {
      toEnemy = glm::normalize(toEnemy);
      // Desired yaw = opposite direction (camera is behind player relative to enemy)
      float desiredYaw = glm::degrees(atan2f(-toEnemy.x, -toEnemy.z));

      // Wrap delta to [-180, 180] before lerping to avoid shortest path issues
      float diff = desiredYaw - m_lockYaw;
      while (diff > 180.0f) diff -= 360.0f;
      while (diff < -180.0f) diff += 360.0f;

      m_lockYaw += diff * 0.12f;
    }

    effectiveYaw = m_lockYaw;
    effectivePitch = 25.0f; // Fixed slightly doiwn angle when locked on

    lookTarget = (chestPos + *m_lockTarget) * 0.5f;
  }

  const float yawRad = glm::radians(effectiveYaw);
  const float pitchRad = glm::radians(effectivePitch);
  const float cosP = cosf(pitchRad);

  glm::vec3 offset = {
    sinf(yawRad) * cosP * m_distance,
    sinf(pitchRad) * m_distance,
    cosf(yawRad) * cosP * m_distance,
  };

  glm::vec3 desiredPos = chestPos + offset;

  // Occlusion raycast: shorten camera distance if geometry in the way
  RaycastHit hit = m_physics.raycast(chestPos, desiredPos);
  glm::vec3 finalPos = desiredPos;
  if (hit.hit) {
    float safeDist = hit.distance - k_occlusionMargin;
    if (safeDist < k_minDistance) safeDist = k_minDistance;

    glm::vec3 dir = glm::normalize(desiredPos - chestPos);
    finalPos = chestPos + dir * safeDist;
  }

  m_camera.setPosition(finalPos);
  m_camera.setTarget(lookTarget);
}

// Camera realative direction vectors
glm::vec3 ThirdPersonCamera::getHorizontalForward() const {
  const float yr = glm::radians(m_yaw);
  return glm::normalize(glm::vec3(-sinf(yr), 0.0f, -cosf(yr)));
}

glm::vec3 ThirdPersonCamera::getHorizontalRight() const {
  return glm::normalize(glm::cross(getHorizontalForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

// Lock on
void ThirdPersonCamera::setLockOnTarget(const glm::vec3* targetPos) {
  if (targetPos && !m_lockTarget) {
    // init smooth follow yaw from current orbit yaw
    m_lockYaw = m_yaw;
  }
  if (!targetPos && m_lockTarget) {
    m_yaw = m_lockYaw;
  }
  m_lockTarget = targetPos;
}

}
