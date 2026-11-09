#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

// Set 0: Global uniforms (view, proj, directional light, camera position)
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 viewPos;                   // xyz = camera position
    vec4 directionalLightDirection; // xyz = light direction (toward light)
    vec4 directionalLightColor;     // rgb = light color
} global;

// Set 1: Material textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicMap;
layout(set = 1, binding = 3) uniform sampler2D roughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap; // Ambient occlusion

// Set 1: Material properties (if textures are not used)
layout(set = 1, binding = 5) uniform Material {
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    int useAlbedoMap;
    int useNormalMap;
    int useMetallicMap;
    int useRoughnessMap;
    int useAoMap;
} material;

// Constants
const float PI = 3.14159265359;

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

// Geometry Function (Smith's method)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    // Sample material properties
    vec3 albedo = material.useAlbedoMap > 0 
        ? pow(texture(albedoMap, fragTexCoord).rgb, vec3(2.2)) // Gamma correction
        : material.albedo.rgb;
    
    float metallic = material.useMetallicMap > 0
        ? texture(metallicMap, fragTexCoord).r
        : material.metallic;
    
    float roughness = material.useRoughnessMap > 0
        ? texture(roughnessMap, fragTexCoord).r
        : material.roughness;
    
    float ao = material.useAoMap > 0
        ? texture(aoMap, fragTexCoord).r
        : material.ao;
    
    // Calculate normal from normal map or use vertex normal
    vec3 N = normalize(fragNormal);
    if (material.useNormalMap > 0) {
        vec3 normal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0; // Transform from [0,1] to [-1,1]
        N = normalize(fragTBN * normal);
    }
    
    vec3 V = normalize(global.viewPos.xyz - fragPos);
    
    // Calculate reflectance at normal incidence
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Directional light (set 0): direction points toward light, no attenuation
    vec3 L = normalize(global.directionalLightDirection.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = global.directionalLightColor.rgb;
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    
    // Ambient lighting
    vec3 ambient = vec3(0.03) * albedo * ao;
    
    vec3 color = ambient + Lo;
    
    // HDR tonemapping
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
