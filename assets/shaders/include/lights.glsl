// engine/assets/shaders/include/lights.glsl
// Included in all lit fragment shaders. No #version directive

struct LightData {
  vec4 posOrDir; // xyz = direction-toward-light (dir) or world pos (point), w unused
  vec4 colorIntensity; // xyz = RGB color, W = intensity scalar
  float range; // Point light falloff radius; unused for directional
  float _pad0;
  float _pad1;
  float _pad2;
};

// No "binding = N" qualifier - requires GLSL 4.20+ wich is unavailable on OpenGL 4.1 (Mac)
// Binding point is set via glUniformBlockBinding() in Shader::setUniformBlockBinding()
layout(std140) uniform LightBlock {
  LightData u_directional;
  LightData u_pointLights[8];
  int u_pointLightCount;
  float u_ambientIntensity;
  vec3 u_ambientColor; // std140: vec3 occupies 16 bytes (12 data + 4 implicit pad)
  float _blockPad; // Occupies the 4 bytes after the vec3s 12 data bytes
};

// Cook-Torrance PBR BRDF

const float PI = 3.14159265359;

// Trowbridge-Reitz GGX normal distribution
float DistributionGGX(vec3 N, vec3 H, float roughness) {
  float a  = roughness * roughness;
  float a2 = a * a;
  float NdH = max(dot(N, H), 0.0);
  float denom = (NdH * NdH * (a2 - 1.0) + 1.0);
  return a2 / (PI * denom * denom);
}

// Schlick-GGX geometry function
float GeometrySchlickGGX(float NdV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return NdV / (NdV * (1.0 - k) + k);
}

// Smith's method combines view and light geometry terms
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
        * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Full cook-torrance BRDF for one light direction.
// Returns the outgoing radiance contribution (before multiplying by NdotL and radiance)
vec3 cookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 H,
                      vec3 albedo, float metallic, float roughness, vec3 F0) {
  float NDF = DistributionGGX(N, H, roughness);
  float G = GeometrySmith(N, V, L, roughness);
  vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

  vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

  float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
  vec3 specular = NDF * G * F / denom;

  return kD * albedo / PI + specular;
}

