#version 410 core
layout(location = 0) in vec2 aPos;

uniform mat4 u_projection;
uniform vec4 u_rect;
uniform vec4 u_color;

out vec4 v_color;

void main() {
  vec2 pos = u_rect.xy + aPos * u_rect.zw;
  gl_Position = u_projection * vec4(pos, 0.0, 1.0);
  v_color = u_color;
}
