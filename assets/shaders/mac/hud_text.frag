#version 410 core
in vec2 v_uv;
uniform sampler2D u_fontAtlas;
uniform vec4 u_color;
out vec4 fragColor;

void main() {
  // font atlas is a single channel
  float alpha = texture(u_fontAtlas, v_uv).r;
  fragColor = vec4(u_color.rgb, u_color.a * alpha);
}
