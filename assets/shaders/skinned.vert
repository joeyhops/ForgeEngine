#version 460 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texCoord;
layout(location = 3) in ivec4 a_boneIds;
layout(location = 4) in vec4 a_boneWeights;

out vec3 v_normal;
out vec2 v_texCoord;

uniform mat4 u_mvp;
uniform mat4 u_model;

// Bone palatte - 100 bones max
// Sent from Animator::getBoneMatrices() each frame
uniform mat4 u_boneMatrices[100];
uniform bool u_hasBones;

void main() {
  vec4 skinnedPosition = vec4(0.0);
  vec3 skinnedNormal = vec3(0.0);

  if (u_hasBones) {
    for (int i = 0; i < 4; i++) {
      if (a_boneIds[i] == -1) continue;

      mat4 boneMat = u_boneMatrices[a_boneIds[i]];
      float weight = a_boneWeights[i];

      skinnedPosition += boneMat * vec4(a_position, 1.0) * weight;
      skinnedNormal += mat3(boneMat) * a_normal * weight;
    }
  } else {
    skinnedPosition = vec4(a_position, 1.0);
    skinnedNormal = a_normal;
  }

  gl_position = u_mvp * skinnedPosition;
  v_normal = normalize(mat3(u_model) * skinnedNormal);
  v_texCoord = a_texCoord;
}
