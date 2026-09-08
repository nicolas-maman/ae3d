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
    bool useInstanceColor;
    mat4 model;
    mat4 viewProjection;
    mat4 lightSpaceMatrix;
    int lightCount;
    vec3 viewPos;
    float viewDistance;
    vec3 diffuseColor;
    vec3 specularColor;
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
    bool enableShadows;
    bool hasShadowMap;
    float shadowIntensity;
    float shadowSoftness;
    bool enablePerlinNoise;
    float noiseScale;
    int noiseOctaves;
    float noiseIntensity;
    bool enableCaustics;
    float causticsIntensity;
    float causticsScale;
    float causticsSpeed;
    float causticsWaterLevel;
    float causticsDepth;
    float causticsTime;
    mat4 projection;
    mat4 view;
    vec2 texelSize;
    float edgeThreshold;
    float edgeThresholdMin;
    float subpixelQuality;
    float time;
    float waveSpeedMultiplier;
    float waveHeightMultiplier;
    float waveRandomness;
    vec3 waveDirections[4];
    float waveAmplitudes[4];
    float waveFrequencies[4];
    float waveSpeeds[4];
    float wavePhases[4];
    float waveSteepness[4];
    vec3 lightPos;
    vec3 lightDirection;
    vec3 lightColor;
    float lightIntensity;
    vec3 waterBaseColor;
    float waterOpacity;
    bool enableFoam;
    float foamIntensity;
    float waterPlaneHeight;
    float waterLevel;
    bool enableFog;
    float fogStart;
    float fogEnd;
    vec3 fogColor;
    float fogIntensity;
    vec3 skyColor;
    vec3 horizonColor;
    bool enableWaterReflection;
    float waterReflectionIntensity;
    bool enableWaterDistortion;
    float waterDistortionIntensity;
    bool enableWaterNormalMapping;
    float waterNormalIntensity;
};
layout(set = 0, binding = 1) uniform sampler2D screenTexture;

layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;






void main() {
    vec3 color = texture(screenTexture, TexCoords).rgb;
    
    // Calculate luminance
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    
    // Extract bright parts only
    vec3 brightColor = vec3(0.0);
    if (luma > bloomThreshold) {
        brightColor = color * (luma - bloomThreshold) / (1.0 - bloomThreshold + 0.001);
    }
    
    // Simple 4-tap box blur on bright areas only (very fast)
    vec3 blur = brightColor;
    float offset = 2.0;
    blur += texture(screenTexture, TexCoords + vec2(texelSize.x * offset, 0.0)).rgb;
    blur += texture(screenTexture, TexCoords - vec2(texelSize.x * offset, 0.0)).rgb;
    blur += texture(screenTexture, TexCoords + vec2(0.0, texelSize.y * offset)).rgb;
    blur += texture(screenTexture, TexCoords - vec2(0.0, texelSize.y * offset)).rgb;
    blur *= 0.2; // Average of 5 samples
    
    // Combine: original + bloom glow
    vec3 finalColor = color + blur * bloomIntensity;
    
    FragColor = vec4(finalColor, 1.0);
}
