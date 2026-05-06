#include <forge/Material.h>

namespace forge {

void Material::bind(const Shader& shader, int unit) const {
  // Albedo
  if (albedo) {
    albedo->bind(unit);
    shader.setInt("u_albedo", unit);
    shader.setBool("u_hasAlbedo", true);
  } else {
    shader.setBool("u_hasAlbedo", false);
    shader.setVec3("u_albedoTint", albedoTint);
  }

  // Normal Map
  if (normalMap) {
    normalMap->bind(unit + 1);
    shader.setInt("u_normalMap", unit + 1);
    shader.setBool("u_hasNormalMap", true);
  } else {
    shader.setBool("u_hasNormalMap", false);
  }
  
  // Metallic Map
  if (metallicMap) {
    metallicMap->bind(unit + 2);
    shader.setInt("u_metallicMap", unit + 2);
    shader.setBool("u_hasMetallicMap", true);
  } else {
    shader.setBool("u_hasMetallicMap", false);
  }

  // Roughness Map
  if (roughnessMap) {
    roughnessMap->bind(unit + 3);
    shader.setInt("u_roughnessMap", unit + 3);
    shader.setBool("u_hasRoughnessMap", true);
  } else {
    shader.setBool("u_hasRoughnessMap", false);
  }

  // AO Map
  if (aoMap) {
    aoMap->bind(unit + 4);
    shader.setInt("u_aoMap", unit + 4);
    shader.setBool("u_hasAoMap", true);
  } else {
    shader.setBool("u_hasAoMap", false);
  }

  // scalar fallbacks uploaded unconditionally, shaders that don't declare
  // these uniforms recieve a silent no-op
  shader.setFloat("u_metallic", metallic);
  shader.setFloat("u_roughness", roughness);
}

Material Material::fromColor(const glm::vec3& color, float metallic, float roughness) {
  Material m;
  m.albedoTint = color;
  m.metallic = metallic;
  m.roughness = roughness;
  return m;
}

}
