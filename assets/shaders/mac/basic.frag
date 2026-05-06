#version 410 core

in vec3 v_worldNormal;
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_albedo;
uniform bool      u_hasAlbedo;
uniform vec3      u_albedoTint; 
void main() {
    // Base color — texture or flat grey fallback
    vec3 albedo = u_hasAlbedo
        ? texture(u_albedo, v_texCoord).rgb
        : u_albedoTint;

    // Simple single directional light — good enough for now
    // Phase 8 will replace this with a proper lighting system
    vec3  lightDir = normalize(vec3(0.6, 1.0, 0.4));
    float diff     = max(dot(normalize(v_worldNormal), lightDir), 0.0);
    float ambient  = 0.35;
    float lighting = ambient + diff * 0.65;

    fragColor = vec4(albedo * lighting, 1.0);
}
