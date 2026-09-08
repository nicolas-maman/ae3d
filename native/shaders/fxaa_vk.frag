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
    bool enableHighQualityFiltering;
    int filteringQuality;
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
    float waterTransparency;
    float waterPlaneHeight;
    bool enableFog;
    float fogStart;
    float fogEnd;
    vec3 fogColor;
    float fogIntensity;
    vec3 skyColor;
    vec3 horizonColor;
    bool enableWaterReflection;
    bool enableWaterRefraction;
    float waterReflectionIntensity;
    float waterRefractionIntensity;
    bool enableWaterDistortion;
    float waterDistortionIntensity;
    bool enableWaterNormalMapping;
    float waterNormalIntensity;
    float baseAlpha;
    float transparencyBoost;
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
    vec3 colorNE = texture(screenTexture, TexCoords + vec2(1.0, -1.0) * texelSize).rgb;
    vec3 colorNW = texture(screenTexture, TexCoords + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 colorSE = texture(screenTexture, TexCoords + vec2(1.0, 1.0) * texelSize).rgb;
    vec3 colorSW = texture(screenTexture, TexCoords + vec2(-1.0, 1.0) * texelSize).rgb;
    
    // Calculate luma for each sample
    float lumaCenter = dot(colorCenter, lumaCoeff);
    float lumaN = dot(colorN, lumaCoeff);
    float lumaS = dot(colorS, lumaCoeff);
    float lumaE = dot(colorE, lumaCoeff);
    float lumaW = dot(colorW, lumaCoeff);
    float lumaNE = dot(colorNE, lumaCoeff);
    float lumaNW = dot(colorNW, lumaCoeff);
    float lumaSE = dot(colorSE, lumaCoeff);
    float lumaSW = dot(colorSW, lumaCoeff);
    
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
    
    float lumaDownCorners = lumaNE + lumaNW + lumaSE + lumaSW;
    float lumaAcrossCorners = lumaNE + lumaSE + lumaNW + lumaSW;
    
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
    
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    
    // Calculate blend amount
    float lengthSign = is1Steepest ? sign(gradient1) : sign(gradient2);
    float subpixelBlend = subpixelOffset * lengthSign;
    
    // Sample offset in the edge direction
    vec2 offset = isHorizontal ? vec2(0.0, subpixelBlend * texelSize.y) : vec2(subpixelBlend * texelSize.x, 0.0);
    
    // Final color with FXAA applied
    vec3 colorFinal = texture(screenTexture, TexCoords + offset).rgb;
    
    FragColor = vec4(colorFinal, 1.0);
}
