#version 450

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
    float ambientStrength;
    float temperature;
    int isDirectional;
    vec3 direction;
    float constantAtten;
    float linearAtten;
    float quadraticAtten;
};

layout(std140, set = 0, binding = 0) uniform SceneBlock {
    Light lights[4];
    bool isInstanced;
    mat4 model;
    mat4 viewProjection;
    mat4 lightSpaceMatrix;
    int lightCount;
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
    mat4 projection;
    mat4 view;
    vec2 texelSize;
    float edgeThreshold;
    float edgeThresholdMin;
    float subpixelQuality;
};
layout (location = 0) in vec3 inPosition;
layout (location = 3) in mat4 instanceModel;





void main() {
    mat4 modelMatrix = isInstanced ? instanceModel : model;
    gl_Position = lightSpaceMatrix * modelMatrix * vec4(inPosition, 1.0);
}
