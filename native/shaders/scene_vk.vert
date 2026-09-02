#version 450

layout(std140, set = 0, binding = 0) uniform SceneBlock {
    bool isInstanced;
    mat4 model;
    mat4 viewProjection;
    vec3 light_position;
    vec3 light_color;
    float light_intensity;
    float light_ambientStrength;
    float light_temperature;
    int light_isDirectional;
    vec3 light_direction;
    float light_constantAtten;
    float light_linearAtten;
    float light_quadraticAtten;
    vec3 viewPos;
    vec3 diffuseColor;
    vec3 specularColor;
    float shininess;
    float metallic;
    float roughness;
    float exposure;
    float materialAlpha;
    bool enableClearcoat;
    float clearcoatRoughness;
    float clearcoatIntensity;
    bool enableSheen;
    vec3 sheenColor;
    float sheenRoughness;
    bool enableTransmission;
    float transmissionFactor;
    bool enableMultipleScattering;
    bool enableEnergyConservation;
    bool enableImageBasedLighting;
    float iblIntensity;
    bool enableVolumetricLighting;
    float volumetricIntensity;
    int volumetricSteps;
    float volumetricScattering;
    bool enableSSAO;
    float ssaoIntensity;
    float ssaoRadius;
    float ssaoBias;
    int ssaoSampleCount;
    bool enableGlobalIllumination;
    float giIntensity;
    int giBounces;
    bool enableBloom;
    float bloomThreshold;
    float bloomIntensity;
    float bloomRadius;
    bool enableShadows;
    float shadowIntensity;
    float shadowSoftness;
    bool enablePerlinNoise;
    float noiseScale;
    int noiseOctaves;
    float noiseIntensity;
    bool enableHighQualityFiltering;
    int filteringQuality;
};

layout(location = 0) in vec3 inPosition; // Vertex position
layout(location = 1) in vec2 inTexCoord; // Texture Coordinate
layout(location = 2) in vec3 inNormal;   // Vertex normal
layout(location = 3) in mat4 instanceModel; // Instanced model matrix (locations 3,4,5,6)
layout(location = 7) in vec3 instanceColor; // Per-instance color (for voxels)





layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 FragPos;
layout(location = 3) out vec3 InstanceColor;

void main() {
    // Decide whether to use instanced or regular model matrix
    // For instanced rendering, we multiply the global model matrix by the instance matrix
    // This allows moving/scaling/rotating the entire group of instances using the model transform
    mat4 modelMatrix = isInstanced ? (model * instanceModel) : model;

    // High-precision world position calculation
    FragPos = vec3(modelMatrix * vec4(inPosition, 1.0));
    
    // Correct normal transformation using inverse transpose
    // For uniform scaling, we can use the upper-left 3x3 of the model matrix
    // For non-uniform scaling, this should be inverse(transpose(mat3(modelMatrix)))
    mat3 normalMatrix = mat3(modelMatrix);
    Normal = normalize(normalMatrix * inNormal);
    
    fragTexCoord = inTexCoord;
    
    // Pass instance color to fragment shader (default white if not instanced)
    InstanceColor = isInstanced ? instanceColor : vec3(1.0, 1.0, 1.0);

    // Final vertex position
    gl_Position = viewProjection * modelMatrix * vec4(inPosition, 1.0);
}

