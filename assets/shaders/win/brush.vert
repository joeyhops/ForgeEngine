#version 460 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texCoord;
layout(location = 3) in vec4 a_tangent;

out vec3 v_worldPos;
out vec2 v_texCoord;
out mat3 v_TBN;

uniform mat4 u_mvp;
uniform mat4 u_model;

void main() {
  vec4 worldPos = u_model * vec4(a_position, 1.0);
  v_worldPos = worldPos.xyz;
  v_texCoord = a_texCoord;
  gl_Position = u_mvp * vec4(a_position, 1.0);

  //Construct Tangent-Bitangent-Normal matrix for normal mapping
  mat3 normalMatrix = mat3(u_model);
  vec3 T = normalize(normalMatrix * a_tangent.xyz);
  vec3 N = normalize(normalMatrix * a_normal);

  // Re-orthoganlize T with respect to N
  T = normalize(T - dot(T, N) * N);

  // Compute Bitangent and apply handedness
  vec3 B = cross(N, T) * a_tangent.w;
  v_TBN = mat3(T, B, N);
}
