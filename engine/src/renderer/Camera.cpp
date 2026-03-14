#include <forge/Camera.h>
#include <glm/gtc/matrix_transform.hpp>

namespace forge {

Camera::Camera(float fovDegrees, float aspectRatio, float nearClip, float farClip)
  : m_fov(fovDegrees), m_aspect(aspectRatio), m_near(nearClip), m_far(farClip)
{}

void Camera::setAspectRatio(float aspect) {
  m_aspect = aspect;
  m_projDirty = true;
}

const glm::mat4& Camera::getViewMatrix() const {
  if (!m_viewDirty) return m_view;
  // lookAt: camera position, what it's looking at, which way is "up"
  m_view = glm::lookAt(m_position, m_target, m_up);
  m_viewDirty = false;
  return m_view;
}

const glm::mat4& Camera::getProjectionMatrix() const {
  if (!m_projDirty) return m_projection;
  // perspective: vertical FOV, aspect ratio, near plane, far plane
  // Anything closer than nearClip or farther than farClip is clipped (not drawn)
  m_projection = glm::perspective(
    glm::radians(m_fov), m_aspect, m_near, m_far);
  m_projDirty = false;
  return m_projection;
}

glm::mat4 Camera::getViewProjection() const {
  return getProjectionMatrix() * getViewMatrix();
}
}
