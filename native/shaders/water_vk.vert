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

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPosition;








// Enhanced wave uniforms







// Simple noise function for vertex shader (GPU Gems Chapter 5)
float hash2D(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float perlinNoise(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; i++) {
        vec2 i_p = floor(p);
        vec2 f_p = fract(p);
        f_p = f_p * f_p * (3.0 - 2.0 * f_p); // Smoothstep
        
        float a = hash2D(i_p);
        float b = hash2D(i_p + vec2(1.0, 0.0));
        float c = hash2D(i_p + vec2(0.0, 1.0));
        float d = hash2D(i_p + vec2(1.0, 1.0));
        
        value += amplitude * mix(mix(a, b, f_p.x), mix(c, d, f_p.x), f_p.y);
        amplitude *= 0.5;
        p *= 2.0;
    }
    return value;
}

// GPU Gems enhanced Gerstner wave with wave sharpening
vec3 calculateGerstnerWave(vec3 position, vec3 direction, float amplitude, float frequency, float speed, float phase, float steepness, float time) {
    vec2 d = normalize(direction.xz);
    float wave = dot(d, position.xz) * frequency + time * speed + phase;
    float c = cos(wave);
    float s = sin(wave);
    
    // Gentle wave sharpening: slightly sharper peaks, wider troughs
    float k = 1.2; // Mild sharpening factor (1.0 = sine wave, >1.0 = sharper peaks)
    float sharpened_s = pow((s + 1.0) * 0.5, k) * 2.0 - 1.0; // Normalize to [-1,1] then sharpen
    
    // Q factor controls wave steepness (0 = sine wave, higher = sharper peaks)
    float Q = steepness / (frequency * amplitude * 6.0 + 0.01); // Prevent division by zero
    
    return vec3(
        Q * amplitude * d.x * c,      // Horizontal displacement X
        amplitude * sharpened_s,      // Sharpened vertical displacement
        Q * amplitude * d.y * c       // Horizontal displacement Z
    );
}

// GPU Gems normal calculation with wave sharpening
vec3 calculateGerstnerNormal(vec3 position, vec3 direction, float amplitude, float frequency, float speed, float phase, float steepness, float time) {
    vec2 d = normalize(direction.xz);
    
    float effectiveSpeed = speed * waveSpeedMultiplier;
    if (waveSpeedMultiplier <= 0.001) effectiveSpeed = speed; // Fallback if not set

    float wave = dot(d, position.xz) * frequency + time * effectiveSpeed + phase;
    float c = cos(wave);
    float s = sin(wave);
    
    // Derivative of sharpened wave function
    float k = 1.2;
    float sharpened_derivative = k * pow((s + 1.0) * 0.5, k - 1.0);
    
    float Q = steepness / (frequency * amplitude * 6.0 + 0.01);
    float WA = frequency * amplitude;
    
    return vec3(
        -d.x * WA * c * sharpened_derivative,    // Sharpened normal X component
        1.0 - Q * WA * s,                       // Normal Y component
        -d.y * WA * c * sharpened_derivative     // Sharpened normal Z component
    );
}

void main() {
    vec3 worldPos = vec3(model * vec4(inPosition, 1.0));
    
    // Calculate displacement and normals from all waves
    vec3 totalDisplacement = vec3(0.0);
    vec3 totalNormal = vec3(0.0, 1.0, 0.0);
    
    // Process 4 waves with height multiplier and randomness
    for (int i = 0; i < 4; i++) {
        // Add randomness to amplitude based on position and time
        float randomFactor = 1.0;
        if (waveRandomness > 0.001) {
            float randomNoise = perlinNoise(worldPos.xz * 0.01 + time * 0.1 + float(i), 2);
            randomFactor = mix(1.0, 0.5 + randomNoise, waveRandomness);
        }
        
        float adjustedAmplitude = waveAmplitudes[i] * waveHeightMultiplier * randomFactor;
        
        vec3 waveDisp = calculateGerstnerWave(
            worldPos,
            waveDirections[i],
            adjustedAmplitude,
            waveFrequencies[i],
            waveSpeeds[i],
            wavePhases[i],
            waveSteepness[i],
            time
        );
        
        vec3 waveNormal = calculateGerstnerNormal(
            worldPos,
            waveDirections[i],
            adjustedAmplitude,
            waveFrequencies[i],
            waveSpeeds[i],
            wavePhases[i],
            waveSteepness[i],
            time
        );
        
        totalDisplacement += waveDisp;
        totalNormal += waveNormal;
    }
    
    // Natural wave displacement for photorealistic appearance
    totalDisplacement.y *= 1.5; // Gentle vertical displacement to prevent artifacts
    
    // Add fine surface detail for realism (performance-optimized)
    vec2 detailCoord = worldPos.xz * 0.01 + time * 0.05;
    float surfaceDetail = (sin(detailCoord.x * 8.0) + sin(detailCoord.y * 6.0)) * 0.02;
    totalDisplacement.y += surfaceDetail * 15.0; // Subtle surface ripples
    
    // Apply displacement
    worldPos += totalDisplacement;
    
    // Normalize the accumulated normal
    totalNormal = normalize(totalNormal);
    
    fragPosition = worldPos;
    fragTexCoord = inTexCoord;
    fragNormal = totalNormal;
    
    gl_Position = viewProjection * vec4(worldPos, 1.0);
}
