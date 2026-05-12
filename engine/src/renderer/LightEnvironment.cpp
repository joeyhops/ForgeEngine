#include <forge/LightEnvironment.h>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include <cassert>
#include <cstring>
#include <algorithm>

namespace forge {

// GPU-side struct - must match byte-for-byte identical to LightBlock in
// lights.glsl
struct GpuLightData {
  glm::vec4 posOrDir; // 16 bytes offset 0
  glm::vec4 colorIntensity; // 16 bytes, offset 16
  float range; // 4 bytes, offset 32
  float pad[3];
};

struct GpuLightBlock {
  GpuLightData directional; // 48 bytes, offset 0
  GpuLightData pointLights[8]; // 384 bytes, offset 48
  int pointLightCount; // 4 bytes, offset 432
  float ambientIntensity; // 4 bytes, offset436
  float pad0[2]; // 8 bytes, offset 440 <- pads ambientColor to 16-byte boundary
  glm::vec4 ambientColor; // 16 bytes, offset 448 (only .rgb used; .a = _blockPad)
};

static_assert(sizeof(GpuLightData) == 48, "GpuLightData size mismatch - check std140 packing");
static_assert(sizeof(GpuLightBlock) == 464, "GpuLightBlock size mismatch - check std140 packing");

LightEnvironment::LightEnvironment() {
  m_directional.posOrDir = glm::normalize(glm::vec3(0.6f, 1.0f, 0.4f));
  m_directional.color = { 1.0f, 0.95f, 0.88f };
  m_directional.intensity = 1.2f;

  glGenBuffers(1, &m_ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(GpuLightBlock), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

LightEnvironment::~LightEnvironment() {
  glDeleteBuffers(1, &m_ubo);
}

void LightEnvironment::setDirectional(const glm::vec3& direction,
                                      const glm::vec3& color,
                                      float intensity) 
{
  m_directional.posOrDir = direction;
  m_directional.color = color;
  m_directional.intensity = intensity;
}

void LightEnvironment::addPointLight(const Light& light) {
  if (m_pointLightCount < k_maxPointLights)
    m_pointLights[m_pointLightCount++] = light;
}

void LightEnvironment::clearPointLights() {
  m_pointLightCount = 0;
}

void LightEnvironment::setAmbient(const glm::vec3& color, float intensity) {
  m_ambientColor = color;
  m_ambientIntensity = intensity;
}

void LightEnvironment::upload() {
  GpuLightBlock block{};

  // Directional Light: posOrDir stores direction toward light (positive = toward sun)
  // in the shader: L = normalize(u_directional.posOrDir.xyz) - matches setDirectional convention
  block.directional.posOrDir = glm::vec4(m_directional.posOrDir, 0.0f);
  block.directional.colorIntensity = glm::vec4(m_directional.color, m_directional.intensity);
  block.directional.range = 0.0f;

  block.pointLightCount = m_pointLightCount;
  for (int i = 0; i < m_pointLightCount; ++i) {
    block.pointLights[i].posOrDir = glm::vec4(m_pointLights[i].posOrDir, 1.0f);
    block.pointLights[i].colorIntensity = glm::vec4(m_pointLights[i].color, m_pointLights[i].intensity);
    block.pointLights[i].range = m_pointLights[i].range;
  }

  block.ambientIntensity = m_ambientIntensity;
  block.ambientColor = glm::vec4(m_ambientColor, 0.0f);

  glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GpuLightBlock), &block);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // Bind this UBO to the global point that all lit shaders read from
  glBindBufferBase(GL_UNIFORM_BUFFER, k_bindingPoint, m_ubo);
}

}
