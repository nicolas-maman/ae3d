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
    int instanceBillboard;
    vec3 viewPos;
    mat4 model;
    mat4 viewProjection;
    mat4 lightSpaceMatrix;
    mat4 prevModel;
    mat4 prevViewProjection;
    bool isSkinned;
    mat4 bones[96];
    vec2 jitter;
    vec2 screenSize;
    int lightCount;
    bool impostor;
    int captureChannel;
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
    float ssaoRadius;
    float ssaoIntensity;
    int depthSampleCount;
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
    float crowdTravel;
    float crowdPhaseStep;
};
layout (location = 0) in vec3 inPosition;
layout (location = 3) in mat4 instanceModel;
layout (location = 8) in vec4 inJoints;
layout (location = 9) in vec4 inWeights;











void main() {
    // The same transform the lit pass builds. Reading instanceModel alone left
    // an instanced model casting its shadow from wherever its own transform was
    // not applied.
    mat4 modelMatrix = isInstanced ? (model * instanceModel) : model;
    if (isInstanced && instancePoints) {
        vec4 point = instanceModel[0];
        modelMatrix = mat4(model[0] * point.w, model[1] * point.w, model[2] * point.w,
                           vec4(point.xyz, 1.0));
        // A billboard: the mesh turned to the eye, its +Z toward the camera
        // -- upright, spun about the world's up alone, for a streak of rain
        // that stays a streak; or full, tipped to face the eye as well, for
        // a flake. The model's own scale stays, its rotation does not.
        if (instanceBillboard > 0) {
            vec3 toEye = viewPos - point.xyz;
            vec3 up = vec3(0.0, 1.0, 0.0);
            vec3 forward;
            if (instanceBillboard == 1) {
                forward = normalize(vec3(toEye.x, 0.0, toEye.z));
            } else {
                forward = normalize(toEye);
            }
            vec3 right = normalize(cross(up, forward));
            up = cross(forward, right);
            float sx = length(vec3(model[0])) * point.w;
            float sy = length(vec3(model[1])) * point.w;
            float sz = length(vec3(model[2])) * point.w;
            modelMatrix = mat4(vec4(right * sx, 0.0), vec4(up * sy, 0.0), vec4(forward * sz, 0.0),
                               vec4(point.xyz, 1.0));
        }
    }
    // And the same pose. A shadow pass that skipped this drew the bind pose,
    // so a figure threw the shadow of a mannequin standing where it started.
    vec4 posed = vec4(inPosition, 1.0);
    if (isSkinned) {
        mat4 skin = inWeights.x * bones[int(inJoints.x)]
                  + inWeights.y * bones[int(inJoints.y)]
                  + inWeights.z * bones[int(inJoints.z)]
                  + inWeights.w * bones[int(inJoints.w)];
        posed = skin * posed;
    }
    gl_Position = lightSpaceMatrix * modelMatrix * posed;
}
