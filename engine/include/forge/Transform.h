#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace forge {

class Transform {
public:
  Transform() = default;

  // Setters
  void setPosition(const glm::vec3& pos) { m_position = pos; m_dirty = true; }
  void setScale(const glm::vec3& scale) { m_scale = scale; m_dirty = true;}
  void setRotation(const glm::quat& rot) { m_rotation = rot; m_dirty = true; }

  // convenience rotation setter from Euler angles (degrees)
  void setEulerAngles(const glm::vec3& degreesXYZ);

  // Getters
  const glm::vec3& getPosition() const { return m_position; }
  const glm::vec3& getScale() const { return m_scale; }
  const glm::quat& getRotation() const { return m_rotation; }

  const glm::mat4& getModelMatrix() const;

private:
  glm::vec3 m_position = { 0.0f, 0.0f, 0.0f };
  glm::vec3 m_scale = { 1.0f, 1.0f, 1.0f };
  glm::quat m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity

  //Cached matrix - only rebuilt when something changes
  mutable glm::mat4 m_modelMatrix = glm::mat4(1.0f);
  mutable bool m_dirty = true;
};

}
