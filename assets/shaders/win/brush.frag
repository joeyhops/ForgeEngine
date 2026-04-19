#version 460 core

in vec3 v_worldPos;
in vec2 v_texCoord;
in mat3 v_TBN;

out vec4 fragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_normalMap;
uniform bool u_hasAlbedo;
uniform bool u_hasNormalMap;
uniform vec3 u_lightDir;

void main() {
  vec3 albedo = u_hasAlbedo ? texture(u_albedo, v_texCoord).rgb : vec3(0.0);

  vec3 normal = v_TBN[2]; // Default to vertex normal
  if (u_hasNormalMap) {
    // Sample normal map and transform from [0,1] to [-1,1]
    vec3 sampledNormal = texture(u_normalMap, v_texCoord).rgb;
    sampledNormal = sampledNormal * 2.0 - 1.0;
    normal = normalize(v_TBN * sampledNormal);
  }

  // Blinn-Phong diffuse
  float diff = max(dot(normal, u_lightDir), 0.0);
  float ambient = 0.25;
  float lighting = ambient + diff * 0.75;

  fragColor = vec4(albedo * lighting, 1.0);
}
