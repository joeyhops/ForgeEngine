#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texCoord;

out vec3 v_worldNormal;
out vec2 v_texCoord;

uniform mat4 u_mvp;
uniform mat4 u_model;

void main() {
    gl_Position  = u_mvp * vec4(a_position, 1.0);

    // mat3(u_model) strips the translation — only rotation/scale affects normals
    // This is correct for uniform scaling; for non-uniform scale you'd need
    // transpose(inverse(u_model)) but that's more expensive and rarely needed
    v_worldNormal = normalize(mat3(u_model) * a_normal);
    v_texCoord    = a_texCoord;
}
