#include <forge/Transform.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace forge {

void Transform::setEulerAngles(const glm::vec3& degreesXYZ) {
  m_rotation = glm::quat(glm::radians(degreesXYZ));
  m_dirty = true;
}

const glm::mat4& Transform::getModelMatrix() const {
  if (!m_dirty) return m_modelMatrix;

  // Build Matrix: Scale first, then rotate, then translate
  // Order matters - matrix math is not commutative
  glm::mat4 T = glm::translate(glm::mat4(1.0f), m_position);
  glm::mat4 R = glm::toMat4(m_rotation);
  glm::mat4 S = glm::scale(glm::mat4(1.0f), m_scale);

  m_modelMatrix = T * R * S;
  m_dirty = false;
  return m_modelMatrix;
}

}
