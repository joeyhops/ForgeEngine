#pragma once
#include <forge/Shader.h>
#include <forge/Texture.h>

#include <glm/glm.hpp>

#include <memory>

namespace forge {

struct Material {
  std::shared_ptr<Texture> albedo;
  std::shared_ptr<Texture> normalMap;
  std::shared_ptr<Texture> metallicMap;
  std::shared_ptr<Texture> roughnessMap;
  std::shared_ptr<Texture> aoMap;

  // Scalars
  glm::vec3 albedoTint = { 1.0f, 1.0f, 1.0f }; // Used when albedo texture is null
  float metallic = 0.0f; // 0 = dielectric, 1 = metal
  float roughness = 0.5f; // 0 = mirror, 1 = fully rough

  // Binds all textures and uploads all uniforms to the currently bound shader.
  // startTextureUnit is the first GL texture unit to occupy:
  //    unit + 0 = albedo, +1 = normalMap, +2 = metallicMap, +3 = roughnessMap, +4 = aoMap
  void bind(const Shader& shader, int startTextureUnit = 0) const;

  // Conveience method to generate flat color material (no textures)
  static Material fromColor(const glm::vec3& color,
                            float metallic = 0.0f,
                            float roughness = 0.5f);
};

}
