#version 460 core

in vec3 v_worldPos;
in vec2 v_texCoord;
in mat3 v_TBN;

out vec4 fragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_normalMap;
uniform sampler2D u_metallicMap;
uniform sampler2D u_roughnessMap;
uniform sampler2D u_aoMap;

uniform bool u_hasAlbedo;
uniform bool u_hasNormalMap;
uniform bool u_hasMetallicMap;
uniform bool u_hasRoughnessMap;
uniform bool u_hasAoMap;

uniform vec3 u_albedoTint;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_cameraPos;

const float PI = 3.14159265359;

// --- PBR HELPER FUNCTIONS ---

// Normal Distribution Function (NDF) - Trowbridge-Reitz GGX
// Approximates how many microfacets are aligned with the halfway vector.
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

// Geometry Function - Schlick-GGX
// Approximates how much the microfacets shadow or mask each other.
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Fresnel Equation - Schlick Approximation
// Describes the ratio of light that gets reflected vs. refracted.
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
  vec3 albedo = u_hasAlbedo ? texture(u_albedo, v_texCoord).rgb : u_albedoTint;
  float metallic = u_hasMetallicMap ? texture(u_metallicMap, v_texCoord).r : 0.0;
  float roughness = u_hasRoughnessMap ? texture(u_roughnessMap, v_texCoord).r : 0.5;
  float ao = u_hasAoMap ? texture(u_aoMap, v_texCoord).r : 1.0;

  vec3 N = v_TBN[2]; // Default to vertex normal
  if (u_hasNormalMap) {
    // Sample normal map and transform from [0,1] to [-1,1]
    vec3 sampledNormal = texture(u_normalMap, v_texCoord).rgb;
    sampledNormal = sampledNormal * 2.0 - 1.0;
    N = normalize(v_TBN * sampledNormal);
  }

  vec3 V = normalize(u_cameraPos - v_worldPos); // View vector
  vec3 L = normalize(u_lightDir); // Light vector
  vec3 H = normalize(V + L); //Halfway vector
  
  // 3. PBR calcs
  // Base reflectivity for dielectrics (non-metals) is ~4%
  vec3 F0 = vec3(0.04);
  F0 = mix(F0, albedo, metallic);

  // Cook-Torrance BRDF
  float NDF = DistributionGGX(N, H, roughness);
  float G = GeometrySmith(N, V, L, roughness);
  vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

  vec3 kS = F;
  vec3 kD = vec3(1.0) - kS;
  kD *= 1.0 - metallic;

  vec3 numerator = NDF * G * F;
  float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
  vec3 specular = numerator / denominator;

  float NdotL = max(dot(N, L), 0.0);
  vec3 Lo = (kD * albedo / PI + specular) * u_lightColor * NdotL;

  vec3 ambient = vec3(0.03) * albedo * ao;

  vec3 color = ambient + Lo;

  color = color / (color + vec3(1.0));
  color = pow(color, vec3(1.0/2.2));

  fragColor = vec4(color, 1.0);
}
