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
    bool isSkinned;
    mat4 bones[96];
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

layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;




// FXAA quality settings




// FXAA 3.11 algorithm (simplified for performance)
void main() {
    vec3 colorCenter = texture(screenTexture, TexCoords).rgb;
    
    // Luma coefficients (perceptual brightness)
    const vec3 lumaCoeff = vec3(0.299, 0.587, 0.114);
    
    // Sample neighboring pixels
    vec3 colorN  = texture(screenTexture, TexCoords + vec2(0.0, -1.0) * texelSize).rgb;
    vec3 colorS  = texture(screenTexture, TexCoords + vec2(0.0, 1.0) * texelSize).rgb;
    vec3 colorE  = texture(screenTexture, TexCoords + vec2(1.0, 0.0) * texelSize).rgb;
    vec3 colorW  = texture(screenTexture, TexCoords + vec2(-1.0, 0.0) * texelSize).rgb;
    // Four corner samples used to be fetched here as well, added into two sums
    // that were the same sum, and never looked at again: four of every nine
    // reads this pass made were for nothing.
    
    // Calculate luma for each sample
    float lumaCenter = dot(colorCenter, lumaCoeff);
    float lumaN = dot(colorN, lumaCoeff);
    float lumaS = dot(colorS, lumaCoeff);
    float lumaE = dot(colorE, lumaCoeff);
    float lumaW = dot(colorW, lumaCoeff);
    // Find min/max luma
    float lumaMin = min(lumaCenter, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaCenter, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;
    
    // Skip anti-aliasing if contrast is below threshold
    if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold)) {
        FragColor = vec4(colorCenter, 1.0);
        return;
    }
    
    // Subpixel anti-aliasing
    float lumaDown = lumaN + lumaS;
    float lumaAcross = lumaE + lumaW;
    
    float lumaTotal = lumaDown + lumaAcross;
    float lumaAvg = lumaTotal * 0.25;
    
    // Calculate blend factor based on local contrast
    float subpixelOffset = abs(lumaAvg - lumaCenter) / lumaRange;
    subpixelOffset = clamp(subpixelOffset, 0.0, 1.0);
    subpixelOffset = smoothstep(0.0, 1.0, subpixelOffset);
    subpixelOffset = subpixelOffset * subpixelOffset * subpixelQuality;
    
    // Edge direction detection
    float edgeHorizontal = abs(-2.0 * lumaW + lumaCenter) + abs(-2.0 * lumaCenter + lumaE) * 2.0 + abs(-2.0 * lumaE + lumaCenter);
    float edgeVertical = abs(-2.0 * lumaN + lumaCenter) + abs(-2.0 * lumaCenter + lumaS) * 2.0 + abs(-2.0 * lumaS + lumaCenter);
    
    bool isHorizontal = edgeHorizontal >= edgeVertical;
    
    // Sample along the edge
    float luma1 = isHorizontal ? lumaS : lumaE;
    float luma2 = isHorizontal ? lumaN : lumaW;
    
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;
    
    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    
    // Calculate blend amount
    float lengthSign = is1Steepest ? sign(gradient1) : sign(gradient2);
    float subpixelBlend = subpixelOffset * lengthSign;
    
    // Sample offset in the edge direction
    vec2 offset = isHorizontal ? vec2(0.0, subpixelBlend * texelSize.y) : vec2(subpixelBlend * texelSize.x, 0.0);
    
    // Final color with FXAA applied
    vec3 colorFinal = texture(screenTexture, TexCoords + offset).rgb;
    
    FragColor = vec4(colorFinal, 1.0);
}
