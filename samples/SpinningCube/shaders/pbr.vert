#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out mat3 fragTBN;

// Set 0: Global uniforms (view, proj)
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 viewPos;      // xyz = camera position
    vec4 directionalLightDirection;  // xyz = light direction (toward light)
    vec4 directionalLightColor;      // rgb = light color
} global;

// Set 2: Model matrix data
layout(set = 2, binding = 0) uniform ModelUBO {
    mat4 model;
    mat4 normalMatrix;  // Inverse transpose of model matrix for normals
} model;

void main()
{
    fragPos = vec3(model.model * vec4(inPosition, 1.0));
    fragNormal = mat3(model.normalMatrix) * inNormal;
    fragTexCoord = inTexCoord;
    fragTangent = mat3(model.normalMatrix) * inTangent;
    fragBitangent = mat3(model.normalMatrix) * inBitangent;
    
    // TBN matrix for normal mapping (Tangent-Bitangent-Normal)
    fragTBN = mat3(
        normalize(fragTangent),
        normalize(fragBitangent),
        normalize(fragNormal)
    );
    
    mat4 mvp = global.proj * global.view * model.model;
    gl_Position = mvp * vec4(inPosition, 1.0);
}
