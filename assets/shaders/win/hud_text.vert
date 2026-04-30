#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 u_projection;
out vec2 v_uv;

void main() {
  gl_Position = u_projection * vec4(aPos, 0.0, 1.0);
    v_uv = aUV;
}
