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
    bool isSkinned;
    mat4 bones[48];
    int lightCount;
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
    vec2 texelSize;
    float edgeThreshold;
    float edgeThresholdMin;
    float subpixelQuality;
    mat4 invViewProjection;
    float ssrRoadHeight;
    float ssrStrength;
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
    vec2 screenSize;
    float waterDepthFade;
    float waterShoreFoam;
    bool enableWaterDistortion;
    float waterDistortionIntensity;
    bool enableWaterNormalMapping;
    float waterNormalIntensity;
    int poseBankFrames;
};
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D poseBank;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in mat4 instanceModel; // per-instance placement (3,4,5,6)
layout(location = 7) in vec3 instanceColor; // per-instance tint
layout(location = 8) in vec4 inJoints;      // the four bones a vertex hangs off
layout(location = 9) in vec4 inWeights;     // how much of each
layout(location = 10) in float inOcclusion;
layout(location = 11) in float instancePhase; // where in the walk this one is, 0..1





// The baked walk: bones*4 texels wide (four texels a bone matrix), `frames`
// rows tall. Sampled by exact texel, never filtered between poses.



layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 FragPos;
layout(location = 3) out vec3 InstanceColor;
layout(location = 4) out vec4 FragPosLightSpace;
layout(location = 5) out float Occlusion;

// The bone's matrix at a frame, read as its four columns from the bank.
mat4 boneAt(int bone, int frame) {
    int x = bone * 4;
    return mat4(texelFetch(poseBank, ivec2(x + 0, frame), 0),
                texelFetch(poseBank, ivec2(x + 1, frame), 0),
                texelFetch(poseBank, ivec2(x + 2, frame), 0),
                texelFetch(poseBank, ivec2(x + 3, frame), 0));
}

// The vertex's four-bone skinning matrix at one baked frame.
mat4 skinAt(int frame) {
    return inWeights.x * boneAt(int(inJoints.x), frame)
         + inWeights.y * boneAt(int(inJoints.y), frame)
         + inWeights.z * boneAt(int(inJoints.z), frame)
         + inWeights.w * boneAt(int(inJoints.w), frame);
}

void main() {
    Occlusion = inOcclusion;
    mat4 modelMatrix = model * instanceModel;

    // The phase lands between two baked poses; blend them, so the walk is
    // continuous instead of snapping frame to frame. The clip loops, so the
    // frame after the last is the first.
    float fpos = instancePhase * float(poseBankFrames);
    int frame0 = int(floor(fpos));
    float blend = fpos - float(frame0);
    if (frame0 < 0) { frame0 = 0; blend = 0.0; }
    if (frame0 >= poseBankFrames) { frame0 = poseBankFrames - 1; blend = 0.0; }
    int frame1 = frame0 + 1;
    if (frame1 >= poseBankFrames) { frame1 = 0; }

    mat4 skin = skinAt(frame0) * (1.0 - blend) + skinAt(frame1) * blend;

    vec4 posed = skin * vec4(inPosition, 1.0);
    vec3 posedNormal = mat3(skin) * inNormal;

    FragPos = vec3(modelMatrix * posed);
    Normal = normalize(mat3(modelMatrix) * posedNormal);
    fragTexCoord = inTexCoord;
    InstanceColor = instanceColor;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = viewProjection * modelMatrix * posed;
}
