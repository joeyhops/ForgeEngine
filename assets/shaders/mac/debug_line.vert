#version 410 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_vp;

void main() {
  gl_Position = u_vp * vec4(a_position, 1.0);
}
