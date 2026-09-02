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
    mat4 projection;
    mat4 view;
    vec2 texelSize;
    float edgeThreshold;
    float edgeThresholdMin;
    float subpixelQuality;
};
layout(set = 0, binding = 1) uniform sampler2D skybox;
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 TexCoords;



void main() {
    vec3 dir = normalize(TexCoords);
    
    float theta = atan(dir.z, dir.x);
    float phi = asin(dir.y);
    
    float u = (theta + 3.14159265) / 6.28318531;
    float v = (phi + 1.57079633) / 3.14159265;
    
    FragColor = texture(skybox, vec2(u, v));
}
