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
    bool instancePoints;
    mat4 model;
    mat4 viewProjection;
    mat4 lightSpaceMatrix;
    bool isSkinned;
    mat4 bones[96];
    int lightCount;
    bool impostor;
    int captureChannel;
    vec3 viewPos;
    float viewDistance;
    vec3 diffuseColor;
    vec3 specularColor;
    float metallic;
    float roughness;
    float exposure;
    float cloudCover;
    float cloudTime;
    vec3 cloudSun;
    float materialAlpha;
    float reflectivity;
    float wetness;
    bool hasNormalMap;
    float normalStrength;
    float occlusionStrength;
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
    bool enableGlobalIllumination;
    float giIntensity;
    int giBounces;
    bool enableBloom;
    float bloomThreshold;
    float bloomIntensity;
    bool enableFog;
    float fogStart;
    float fogEnd;
    vec3 fogColor;
    float fogIntensity;
    bool enableShadows;
    bool hasShadowMap;
    float shadowIntensity;
    float shadowSoftness;
    vec3 shadowDirection;
    float shadowTexelWorld;
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
    vec3 cloudSunColor;
    int skyProcedural;
    float skyOvercast;
    vec3 skyOvercastColor;
    vec2 texelSize;
    float edgeThreshold;
    float edgeThresholdMin;
    float subpixelQuality;
    mat4 invViewProjection;
    float ssrRoadHeight;
    float ssrStrength;
    vec2 screenSize;
    float ssaoRadius;
    float ssaoIntensity;
    int depthSampleCount;
    mat4 prevViewProjection;
    vec2 jitter;
    float taaBlend;
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
    vec3 skyColor;
    vec3 horizonColor;
    bool enableWaterReflection;
    float waterReflectionIntensity;
    int hasSkyTexture;
    int hasSceneDepth;
    float waterDepthFade;
    float waterShoreFoam;
    bool enableWaterDistortion;
    float waterDistortionIntensity;
    bool enableWaterNormalMapping;
    float waterNormalIntensity;
    int poseBankFrames;
    int impostorCols;
    int impostorRows;
    float impostorWidth;
    float impostorHeight;
};
layout(set = 0, binding = 1) uniform sampler2D screenTexture;
layout(set = 0, binding = 2) uniform sampler2D depthTexture;
layout(set = 0, binding = 3) uniform sampler2D historyTexture;

layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;










// A stored scene depth as clip-space z: OpenGL keeps depth in 0..1 for a
// clip range of -1..1, Vulkan's clip range is the 0..1 it stores.
float taa_depth_clip(float depth) {
    return depth;
}

void main() {
    vec2 uv = TexCoords;
    vec2 px = 1.0 / screenSize;
    vec3 current = texture(screenTexture, uv).rgb;
    if (taaBlend >= 0.999) { FragColor = vec4(current, 1.0); return; }

    // Where this pixel's surface was on the screen last frame.
    float depth = texture(depthTexture, uv).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, taa_depth_clip(depth), 1.0);
    vec4 world = invViewProjection * clip;
    vec4 prev = prevViewProjection * (world / world.w);
    if (prev.w <= 0.0) { FragColor = vec4(current, 1.0); return; }
    vec2 prevUV = prev.xy / prev.w * 0.5 + 0.5;
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        FragColor = vec4(current, 1.0);
        return;
    }
    vec3 history = texture(historyTexture, prevUV).rgb;

    // The neighbourhood's range this frame, and the history held to it.
    vec3 low = current;
    vec3 high = current;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 c = texture(screenTexture, uv + vec2(float(x), float(y)) * px).rgb;
            low = min(low, c);
            high = max(high, c);
        }
    }
    history = clamp(history, low, high);
    // A history that had to be moved far -- a fast pan -- is trusted less.
    float travel = length((prevUV - uv) * screenSize);
    float blend = clamp(taaBlend + travel * 0.02, taaBlend, 1.0);
    FragColor = vec4(mix(history, current, blend), 1.0);
}
