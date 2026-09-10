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
    float materialAlpha;
    bool hasNormalMap;
    float normalStrength;
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
    vec3 skyColor;
    vec3 horizonColor;
    bool enableWaterReflection;
    float waterReflectionIntensity;
    bool enableWaterDistortion;
    float waterDistortionIntensity;
    bool enableWaterNormalMapping;
    float waterNormalIntensity;
};

layout(location = 0) in vec3 inPosition; // Vertex position
layout(location = 1) in vec2 inTexCoord; // Texture Coordinate
layout(location = 2) in vec3 inNormal;   // Vertex normal
layout(location = 3) in mat4 instanceModel; // Instanced model matrix (locations 3,4,5,6)
layout(location = 7) in vec3 instanceColor; // Per-instance color (for voxels)
layout(location = 8) in vec4 inJoints;   // The four bones this vertex hangs off
layout(location = 9) in vec4 inWeights;  // How much of each, summing to one







// A skinned draw is posed by the palette rather than by the model matrix
// alone: each bone carries where it is now against where it was bound, and a
// vertex is moved by the blend of the four it belongs to. Bounded so the array
// fits the vertex uniform budget GL 3.3 guarantees, which is what a software
// rasteriser actually gives.



layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 FragPos;
layout(location = 3) out vec3 InstanceColor;
layout(location = 4) out vec4 FragPosLightSpace;

void main() {
    // Decide whether to use instanced or regular model matrix
    // For instanced rendering, we multiply the global model matrix by the instance matrix
    // This allows moving/scaling/rotating the entire group of instances using the model transform
    mat4 modelMatrix = isInstanced ? (model * instanceModel) : model;

    vec4 posed = vec4(inPosition, 1.0);
    vec3 posedNormal = inNormal;
    if (isSkinned) {
        mat4 skin = inWeights.x * bones[int(inJoints.x)]
                  + inWeights.y * bones[int(inJoints.y)]
                  + inWeights.z * bones[int(inJoints.z)]
                  + inWeights.w * bones[int(inJoints.w)];
        posed = skin * posed;
        posedNormal = mat3(skin) * inNormal;
    }

    // High-precision world position calculation
    FragPos = vec3(modelMatrix * posed);
    
    // Correct normal transformation using inverse transpose
    // For uniform scaling, we can use the upper-left 3x3 of the model matrix
    // For non-uniform scaling, this should be inverse(transpose(mat3(modelMatrix)))
    mat3 normalMatrix = mat3(modelMatrix);
    Normal = normalize(normalMatrix * posedNormal);
    
    fragTexCoord = inTexCoord;
    
    // Pass instance color to fragment shader (default white if not instanced)
    InstanceColor = (isInstanced && useInstanceColor) ? instanceColor : vec3(1.0, 1.0, 1.0);

    // Final vertex position
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = viewProjection * modelMatrix * posed;
}

