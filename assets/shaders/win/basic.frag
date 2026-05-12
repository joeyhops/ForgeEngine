#version 460 core

#include "../include/lights.glsl"

in vec3 v_worldPos;
in vec2 v_texCoord;
in mat3 v_TBN;

out vec4 fragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_normalMap;
uniform sampler2D u_metallicMap;
uniform sampler2D u_roughnessMap;
uniform sampler2D u_aoMap;

uniform bool      u_hasAlbedo;
uniform bool      u_hasNormalMap;
uniform bool      u_hasMetallicMap;
uniform bool      u_hasRoughnessMap;
uniform bool      u_hasAoMap;

uniform vec3      u_albedoTint; 
uniform float      u_metallic; 
uniform float      u_roughness; 
uniform vec3      u_cameraPos; 

void main() {
  vec3 albedo = u_hasAlbedo ? texture(u_albedo, v_texCoord).rgb : u_albedoTint;
  float metallic = u_hasMetallicMap ? texture(u_metallicMap, v_texCoord).r : u_metallic;
  float roughness = u_hasRoughnessMap ? texture(u_roughnessMap, v_texCoord).r : u_roughness;
  float ao = u_hasAoMap ? texture(u_aoMap, v_texCoord).r : 1.0;

  vec3 N = v_TBN[2];
  if (u_hasNormalMap) {
    vec3 sampledNormal = texture(u_normalMap, v_texCoord).rgb * 2.0 - 1.0;
    N = normalize(v_TBN * sampledNormal);
  }

  vec3 V = normalize(u_cameraPos - v_worldPos);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  vec3 Lo = vec3(0.0);

  // Directional Light
  {
    vec3 L = normalize(u_directional.posOrDir.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = u_directional.colorIntensity.rgb * u_directional.colorIntensity.w;
    Lo += cookTorranceBRDF(N, V, L, H, albedo, metallic, roughness, F0)
        * radiance * max(dot(N, L), 0.0);
  }

  // Point Lights
  for (int i = 0; i < u_pointLightCount; ++i) {
    vec3 lightPos = u_pointLights[i].posOrDir.xyz;
    vec3 lightCol = u_pointLights[i].colorIntensity.rgb;
    float intensity = u_pointLights[i].colorIntensity.w;
    float range = u_pointLights[i].range;

    vec3 toLight = lightPos - v_worldPos;
    float dist = length(toLight);
    vec3 L = normalize(toLight);
    vec3 H = normalize(V + L);

    float window = pow(max(1.0 - pow(dist / range, 4.0), 0.0), 2.0);
    float atten = window / (dist * dist + 0.0001);

    vec3 radiance = lightCol * intensity * atten;
    Lo += cookTorranceBRDF(N, V, L, H, albedo, metallic, roughness, F0)
        * radiance * max(dot(N, L), 0.0);
  }

  vec3 ambient = u_ambientColor.rgb * u_ambientIntensity * albedo * ao;
  vec3 color = ambient + Lo;

  // Reinhard tonemap + gamma
  color = color / (color + vec3(1.0));
  color = pow(color, vec3(1.0 / 2.2));

  fragColor = vec4(color, 1.0);
}
