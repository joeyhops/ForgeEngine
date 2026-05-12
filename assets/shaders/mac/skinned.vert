#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texCoord;
layout(location = 3) in ivec4 a_boneIds;
layout(location = 4) in vec4 a_boneWeights;
layout(location = 5) in vec4 a_tangent;

out vec3 v_worldPos;
out vec2 v_texCoord;
out mat3 v_TBN;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat4 u_boneMatrices[100];
uniform bool u_hasBones;

void main() {
  vec4 skinnedPosition = vec4(0.0);
  vec3 skinnedNormal = vec3(0.0);
  vec3 skinnedTangent = vec3(0.0);

  if (u_hasBones) {
    for (int i = 0; i < 4; i++) {
      if (a_boneIds[i] == -1) continue;
      mat4 boneMat = u_boneMatrices[a_boneIds[i]];
      float w = a_boneWeights[i];
      skinnedPosition += boneMat * vec4(a_position, 1.0) * w;
      skinnedNormal += mat3(boneMat) * a_normal * w;
      skinnedTangent += mat3(boneMat) * a_tangent.xyz * w;
    }
  } else {
    skinnedPosition = vec4(a_position, 1.0);
    skinnedNormal = a_normal;
    skinnedTangent = a_tangent.xyz;
  }

  vec4 worldPos = u_model * skinnedPosition;
  v_worldPos = worldPos.xyz;
  v_texCoord = a_texCoord;
  gl_Position = u_mvp * skinnedPosition;

  mat3 normalMatrix = mat3(u_model);
  vec3 T = normalize(normalMatrix * skinnedTangent);
  vec3 N = normalize(normalMatrix * skinnedNormal);
  T = normalize(T - dot(T, N) * N);
  vec3 B = cross(N, T) * a_tangent.w;
  v_TBN = mat3(T, B, N);
}
