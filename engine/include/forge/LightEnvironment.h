#pragma once
#include <glm/glm.hpp>

namespace forge {

struct Light {
  glm::vec3 posOrDir = { 0.0f, -1.0f, 0.0f }; // direction for directional; position for point
  glm::vec3 color = { 1.0f, 1.0f, 1.0f };
  float intensity = 1.0f;
  float range = 10.0f; // ignored for directional
};

class LightEnvironment {
public:
  static constexpr int k_maxPointLights = 8;
  static constexpr int k_bindingPoint = 0;

  LightEnvironment();
  ~LightEnvironment();

  LightEnvironment(const LightEnvironment&) = delete;
  LightEnvironment& operator=(const LightEnvironment&) = delete;

  // direction: normalized vector pointing FROM scene TOWARD the sun.
  void setDirectional(const glm::vec3& direction, const glm::vec3& color, float intensity);

  void addPointLight(const Light& light);
  void clearPointLights();

  void setAmbient(const glm::vec3& color, float intensity);

  // Accessor - used by DebugUI to read current s
  const Light& getDirectional() const { return m_directional; }
  const Light& getPointLight(int i) const { return m_pointLights[i]; }
  int getPointLightCount() const { return m_pointLightCount; }
  glm::vec3 getAmbientColor() const { return m_ambientColor; }
  float getAmbientIntensity() const { return m_ambientIntensity; }

  void upload();

private:
  unsigned int m_ubo = 0;

  Light m_directional;
  Light m_pointLights[k_maxPointLights];
  int m_pointLightCount = 0;

  glm::vec3 m_ambientColor = { 0.4f, 0.5f, 0.65f };
  float m_ambientIntensity = 0.05f;
};

}
