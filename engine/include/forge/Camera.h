#pragma once
#include <glm/glm.hpp>

namespace forge {

class Camera {
public:
  Camera(float fovDegrees, float aspectRatio, float nearClip, float farClip);

  // Call when window resized
  void setAspectRatio(float aspect);

  // Position the camera in world space
  void setPosition(const glm::vec3& pos) { m_position = pos; m_viewDirty = true; }
  void setTarget(const glm::vec3& target) { m_target = target; m_viewDirty = true; }

  const glm::mat4& getViewMatrix() const;
  const glm::mat4& getProjectionMatrix() const;

  // Convenience: returns projection * view (what we send to the shader as u_mvp
  // combined with the model matrix from Transform)
  glm::mat4 getViewProjection() const;

  glm::vec3 getPosition() const { return m_position; }

private:
  float m_fov;
  float m_aspect;
  float m_near;
  float m_far;

  glm::vec3 m_position = { 0.0f, 0.0f, 3.0f };
  glm::vec3 m_target = { 0.0f, 0.0f, 0.0f };
  glm::vec3 m_up = { 0.0f, 1.0f, 0.0f };

  mutable glm::mat4 m_view = glm::mat4(1.0f);
  mutable glm::mat4 m_projection = glm::mat4(1.0f);
  mutable bool m_viewDirty = true;
  mutable bool m_projDirty = true;
};

}
