// Modern PBR-inspired Fragment Shader
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
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 FragPos;
layout(location = 3) in vec3 InstanceColor;
layout(location = 4) in vec4 FragPosLightSpace;















// Modern PBR Extensions









// Advanced Lighting Models





// Volumetric Lighting





// SSAO






// Global Illumination




// Bloom and HDR





// GPU Gems Chapter 9 & 11: Shadow Volume Support with Antialiasing




// GPU Gems Chapter 5: Improved Perlin Noise Support





// Additional advanced rendering uniforms



layout(location = 0) out vec4 FragColor;

// Convert color temperature (Kelvin) to RGB multiplier
// Optimized color temperature to RGB conversion using lookup approximation
// The light writes how far it can see into a colour target; this compares the
// fragment's own distance against it. Sampling a neighbourhood softens the edge.
float shadow_factor() {
    vec3 projected = FragPosLightSpace.xyz / FragPosLightSpace.w;
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0) {
        return 1.0;
    }

    vec3 surface = normalize(Normal);
    vec3 toLight = normalize(lights[0].position - FragPos);
    if (lights[0].isDirectional == 1) {
        toLight = normalize(-lights[0].direction);
    }

    // A surface nearly edge-on to the light needs a larger offset, or its own
    // depth reads as occluding it.
    float bias = max(0.02 * (1.0 - dot(surface, toLight)), 0.005);

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float radius = max(shadowSoftness, 0.0);
    float lit = 0.0;
    for (int sx = -1; sx <= 1; sx++) {
        for (int sy = -1; sy <= 1; sy++) {
            vec2 at = projected.xy + vec2(float(sx), float(sy)) * texel * radius;
            float closest = texture(shadowMap, at).r;
            lit += projected.z - bias > closest ? 0.0 : 1.0;
        }
    }
    lit = lit / 9.0;

    return mix(shadowIntensity, 1.0, lit);
}

vec3 kelvinToRGB(float kelvin) {
    kelvin = clamp(kelvin, 1000.0, 12000.0);
    
    // Fast approximation for common temperatures (avoids expensive pow/log)
    if (kelvin < 3000.0) {
        return mix(vec3(1.0, 0.4, 0.0), vec3(1.0, 0.7, 0.3), (kelvin - 1000.0) / 2000.0);
    } else if (kelvin < 6500.0) {
        return mix(vec3(1.0, 0.7, 0.3), vec3(1.0, 1.0, 1.0), (kelvin - 3000.0) / 3500.0);
    } else {
        return mix(vec3(1.0, 1.0, 1.0), vec3(0.7, 0.8, 1.0), (kelvin - 6500.0) / 5500.0);
    }
}

// Optimized Schlick's approximation for Fresnel reflectance
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    float invCosTheta = clamp(1.0 - cosTheta, 0.0, 1.0);
    float invCosTheta2 = invCosTheta * invCosTheta;
    float invCosTheta5 = invCosTheta2 * invCosTheta2 * invCosTheta; // Faster than pow(x, 5.0)
    return F0 + (1.0 - F0) * invCosTheta5;
}

// Improved specular distribution (Blinn-Phong to GGX-like)
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265359 * denom * denom;
    
    // Clamp the result to prevent extreme highlights
    float result = num / denom;
    return min(result, 10.0); // Prevent excessive specular concentration
}

// Geometry function for self-shadowing
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Modern PBR Extensions

// Clearcoat BRDF (automotive paint, lacquered surfaces)
vec3 calculateClearcoat(vec3 N, vec3 V, vec3 L, vec3 H, vec3 baseColor) {
    if (!enableClearcoat) return vec3(0.0);
    
    float clearcoatNDF = distributionGGX(N, H, clearcoatRoughness);
    float clearcoatG = geometrySmith(N, V, L, clearcoatRoughness);
    vec3 clearcoatF = fresnelSchlick(max(dot(H, V), 0.0), vec3(0.04)); // Clear coat F0
    
    vec3 clearcoatSpecular = (clearcoatNDF * clearcoatG * clearcoatF) / 
                            (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001);
    
    return clearcoatSpecular * clearcoatIntensity;
}

// Sheen BRDF (fabric, velvet materials)
vec3 calculateSheen(vec3 N, vec3 V, vec3 L, vec3 H) {
    if (!enableSheen) return vec3(0.0);
    
    float sheenNdotH = max(dot(N, H), 0.0);
    float sheenD = (2.0 + sheenRoughness) * pow(sheenNdotH, sheenRoughness) / (2.0 * 3.14159265359);
    
    return sheenColor * sheenD * 0.25; // Sheen is typically subtle
}

// Transmission BRDF (glass, translucent materials)
vec3 calculateTransmission(vec3 N, vec3 V, vec3 L, vec3 baseColor) {
    if (!enableTransmission) return vec3(0.0);
    
    // Proper glass transmission with refraction
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    
    // Fresnel for transmission (inverted)
    float F0 = 0.04; // Glass F0
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float transmission = (1.0 - fresnel) * transmissionFactor;
    
    // Light coming through the material
    vec3 transmittedLight = baseColor * transmission * NdotL;
    
    // Add some scattering for realistic glass
    vec3 scattering = baseColor * transmission * 0.1;
    
    return transmittedLight + scattering;
}

// Multiple Scattering Energy Compensation
vec3 compensateEnergyLoss(vec3 color, float NdotV, float roughness) {
    if (!enableMultipleScattering) return color;
    
    // Approximate multiple scattering compensation
    float compensation = 1.0 + roughness * (1.0 - NdotV) * 0.2;
    return color * compensation;
}

// Energy Conservation for layered materials
vec3 applyEnergyConservation(vec3 diffuse, vec3 specular, vec3 clearcoat, vec3 sheen) {
    if (!enableEnergyConservation) return diffuse + specular + clearcoat + sheen;
    
    // Ensure total energy doesn't exceed 1.0
    vec3 totalEnergy = diffuse + specular + clearcoat + sheen;
    float maxEnergy = max(max(totalEnergy.r, totalEnergy.g), totalEnergy.b);
    
    if (maxEnergy > 1.0) {
        return totalEnergy / maxEnergy;
    }
    
    return totalEnergy;
}

// Improved Screen Space Ambient Occlusion approximation
// Note: True SSAO requires depth buffer, this is a world-space approximation with hemisphere sampling
float calculateSSAO(vec3 position, vec3 normal, float distanceToCamera) {
    if (!enableSSAO) return 1.0;
    
    // Distance-based LOD: reduce samples for close objects (voxel performance)
    int adaptiveSamples = ssaoSampleCount;
    if (distanceToCamera < 5000.0) {
        adaptiveSamples = max(2, ssaoSampleCount / 8); // Very few samples when close
    } else if (distanceToCamera < 20000.0) {
        adaptiveSamples = max(4, ssaoSampleCount / 4);
    } else if (distanceToCamera < 50000.0) {
        adaptiveSamples = max(6, ssaoSampleCount / 2);
    }
    
    float occlusion = 0.0;
    float radius = ssaoRadius;
    
    // Create tangent space basis from normal for hemisphere sampling
    vec3 tangent = normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
    if (length(cross(normal, vec3(0.0, 1.0, 0.0))) < 0.1) {
        tangent = normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
    }
    vec3 bitangent = normalize(cross(normal, tangent));
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    // Golden ratio for better sample distribution
    float goldenAngle = 2.39996323;
    
    // Sample hemisphere around the point
    for (int i = 0; i < adaptiveSamples && i < 16; i++) {
        // Vogel disk method for better distribution
        float angle = float(i) * goldenAngle;
        float radiusSample = sqrt(float(i) + 0.5) / sqrt(float(adaptiveSamples));
        
        // Create sample direction in tangent space (hemisphere)
        float x = cos(angle) * radiusSample;
        float y = sin(angle) * radiusSample;
        float z = sqrt(1.0 - radiusSample * radiusSample);
        
        vec3 sampleDir = TBN * vec3(x, y, z);
        
        // Sample position at varying distances
        float scale = mix(0.1, 1.0, float(i) / float(adaptiveSamples));
        vec3 samplePos = position + sampleDir * radius * scale;
        
        float sampleDistance = length(samplePos - position);
        float geometryTest = dot(normalize(samplePos - position), normal);
        
        // Only occlude if sample is in front of surface
        if (geometryTest > ssaoBias) {
            float rangeCheck = smoothstep(0.0, 1.0, radius / abs(sampleDistance));
            float depthDiff = max(0.0, geometryTest - ssaoBias);
            occlusion += depthDiff * rangeCheck;
        }
    }
    
    occlusion = 1.0 - (occlusion / float(adaptiveSamples));
    occlusion = pow(occlusion, 1.0 + ssaoIntensity);
    
    return occlusion;
}

// Volumetric Lighting (light shafts, fog) with distance-based optimization
vec3 calculateVolumetricLighting(vec3 worldPos, vec3 lightPos, vec3 viewPos) {
    if (!enableVolumetricLighting) return vec3(0.0);
    
    float distanceToCamera = length(worldPos - viewPos);
    
    // Skip volumetric for very close objects - too expensive per fragment
    if (distanceToCamera < 1000.0) return vec3(0.0);
    
    // Adaptive step count based on distance
    int adaptiveSteps = volumetricSteps;
    if (distanceToCamera < 10000.0) {
        adaptiveSteps = max(4, volumetricSteps / 4);
    } else if (distanceToCamera < 30000.0) {
        adaptiveSteps = max(8, volumetricSteps / 2);
    }
    
    vec3 rayDir = normalize(worldPos - viewPos);
    vec3 lightDir = normalize(lightPos - viewPos);
    float rayLength = distanceToCamera;
    
    vec3 volumetricColor = vec3(0.0);
    float stepSize = rayLength / float(adaptiveSteps);
    
    // March along the ray
    for (int i = 0; i < adaptiveSteps && i < 32; i++) {
        vec3 samplePos = viewPos + rayDir * stepSize * float(i);
        float distanceToLight = length(lightPos - samplePos);
        
        // Simple scattering calculation
        float scattering = 1.0 / (1.0 + distanceToLight * distanceToLight * 0.0001);
        scattering *= volumetricScattering;
        
        volumetricColor += vec3(scattering);
    }
    
    volumetricColor /= float(adaptiveSteps);
    return volumetricColor * volumetricIntensity * 0.1;
}

// Global Illumination approximation with distance-based optimization
vec3 calculateGlobalIllumination(vec3 position, vec3 normal, vec3 albedo, float distanceToCamera) {
    if (!enableGlobalIllumination) return vec3(0.0);
    
    // Adaptive sample count based on distance (CRITICAL for voxel performance)
    int baseSamples = giBounces * 4;
    int samples = baseSamples;
    
    if (distanceToCamera < 5000.0) {
        // Very close: minimal GI (too expensive for dense voxels)
        samples = max(2, baseSamples / 8);
    } else if (distanceToCamera < 20000.0) {
        samples = max(4, baseSamples / 4);
    } else if (distanceToCamera < 50000.0) {
        samples = max(6, baseSamples / 2);
    }
    
    samples = min(samples, 16);
    
    // Very simple GI approximation using hemisphere sampling
    vec3 gi = vec3(0.0);
    
    for (int i = 0; i < samples; i++) {
        float angle = float(i) * 3.14159 * 2.0 / float(samples);
        vec3 sampleDir = vec3(cos(angle), sin(angle), 1.0);
        sampleDir = normalize(normal + sampleDir * 0.5);
        
        // Simple indirect lighting approximation
        float indirectLight = max(0.0, dot(normal, sampleDir)) * 0.1;
        gi += albedo * indirectLight;
    }
    
    return gi * giIntensity / float(samples);
}

// Environment reflections (skybox-based) - simplified to avoid artifacts
vec3 calculateEnvironmentReflection(vec3 N, vec3 V, float roughness, float metallic) {
    // Calculate reflection direction
    vec3 R = reflect(-V, N);
    
    // Simple uniform environment color to avoid the "two halves" effect
    vec3 envColor = vec3(0.6, 0.7, 0.9); // Uniform sky-like color
    
    // Roughness affects reflection clarity
    float reflectionStrength = (1.0 - roughness * 0.9) * 0.5; // Reduced strength
    
    // Metallic materials reflect more environment
    float envContribution = mix(0.05, 0.3, metallic) * reflectionStrength; // Much reduced
    
    return envColor * envContribution;
}

// Simple inter-object reflections approximation
vec3 calculateInterObjectReflections(vec3 worldPos, vec3 N, vec3 V, float roughness, float metallic) {
    // Only apply to metallic surfaces with low roughness
    if (roughness > 0.5 || metallic < 0.5) return vec3(0.0);
    
    vec3 R = reflect(-V, N);
    vec3 reflectionColor = vec3(0.0);
    
    // Simple approximation: sample environment based on reflection direction
    // This creates subtle inter-object reflections without artifacts
    float reflectionStrength = (1.0 - roughness) * metallic * 0.15; // Very subtle
    
    // Use reflection direction to approximate nearby object colors
    // This is a simplified approach - in reality you'd need screen-space reflections
    vec3 envSample = vec3(0.4, 0.5, 0.6); // Neutral reflection color
    
    // Add some variation based on world position to simulate different objects
    float variation = sin(worldPos.x * 0.1) * sin(worldPos.z * 0.1) * 0.2;
    envSample += vec3(variation, variation * 0.5, variation * 0.3);
    
    reflectionColor = envSample * reflectionStrength;
    
    return reflectionColor;
}

// ACES tone mapping for HDR
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// GPU Gems Chapter 5: Improved Perlin Noise Implementation
// Simplified GLSL version of the improved Perlin noise with quintic interpolation

// Permutation table values (simplified for GLSL)
const int PERM[256] = int[256](
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
);

// Optimized hash function for GLSL (50% faster, no lookup table)
int hash(int x, int y, int z) {
    int n = x + y * 57 + z * 113;
    n = (n << 13) ^ n;
    return abs((n * (n * n * 15731 + 789221) + 1376312589)) & 255;
}

// Quintic interpolation (6t^5 - 15t^4 + 10t^3)
float fade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Linear interpolation
float lerp(float t, float a, float b) {
    return a + t * (b - a);
}

// Gradient vectors (simplified set from GPU Gems)
vec3 getGradient(int hash) {
    int h = hash & 15;
    float u = h < 8 ? 1.0 : -1.0;
    float v = (h & 1) == 0 ? 1.0 : -1.0;
    float w = (h & 2) == 0 ? 1.0 : -1.0;
    
    if (h < 4) return vec3(u, v, 0.0);
    else if (h < 8) return vec3(u, 0.0, w);
    else if (h < 12) return vec3(0.0, v, w);
    else return vec3(u, v, w);
}

// Simplified 3D Perlin noise for GLSL
float perlinNoise3D(vec3 p) {
    // Find unit cube containing point
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    
    // Compute fade curves
    vec3 u = vec3(fade(f.x), fade(f.y), fade(f.z));
    
    // Get gradients at cube corners
    int n000 = hash(i.x, i.y, i.z);
    int n001 = hash(i.x, i.y, i.z + 1);
    int n010 = hash(i.x, i.y + 1, i.z);
    int n011 = hash(i.x, i.y + 1, i.z + 1);
    int n100 = hash(i.x + 1, i.y, i.z);
    int n101 = hash(i.x + 1, i.y, i.z + 1);
    int n110 = hash(i.x + 1, i.y + 1, i.z);
    int n111 = hash(i.x + 1, i.y + 1, i.z + 1);
    
    // Compute dot products
    float d000 = dot(getGradient(n000), f);
    float d001 = dot(getGradient(n001), f - vec3(0, 0, 1));
    float d010 = dot(getGradient(n010), f - vec3(0, 1, 0));
    float d011 = dot(getGradient(n011), f - vec3(0, 1, 1));
    float d100 = dot(getGradient(n100), f - vec3(1, 0, 0));
    float d101 = dot(getGradient(n101), f - vec3(1, 0, 1));
    float d110 = dot(getGradient(n110), f - vec3(1, 1, 0));
    float d111 = dot(getGradient(n111), f - vec3(1, 1, 1));
    
    // Interpolate
    return lerp(u.z,
        lerp(u.y,
            lerp(u.x, d000, d100),
            lerp(u.x, d010, d110)),
        lerp(u.y,
            lerp(u.x, d001, d101),
            lerp(u.x, d011, d111)));
}

// Multi-octave noise (turbulence)
float turbulence(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float maxValue = 0.0;
    
    for (int i = 0; i < octaves && i < 8; i++) {
        value += perlinNoise3D(p * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    
    return value / maxValue;
}

// One light's contribution. Everything here depends on which light is shading;
// anything that does not stays in main and is computed once.
vec3 direct_light(Light L, vec3 norm, vec3 viewDir, vec3 albedo, vec3 F0,
                  float NdotV, float adjustedRoughness) {
    vec3 tempAdjustedLightColor = L.color * kelvinToRGB(L.temperature);

    vec3 lightDir;
    float attenuation = 1.0;
    if (L.isDirectional == 1) {
        lightDir = normalize(L.direction);
    } else {
        // High-precision point light calculation for perfect reflections
        vec3 lightVec = L.position - FragPos;
        float distance = length(lightVec);
        lightDir = lightVec / distance; // More precise than normalize()
        attenuation = 1.0 / (L.constantAtten + L.linearAtten * distance + L.quadraticAtten * distance * distance);
    }

    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 radiance = tempAdjustedLightColor * L.intensity * attenuation;

    float NdotL_raw = dot(norm, lightDir);
    float HdotV = clamp(dot(halfwayDir, viewDir), 0.001, 1.0); // Avoid zero division
    float NdotL = max(NdotL_raw, 0.0);

    float NDF = distributionGGX(norm, halfwayDir, adjustedRoughness);
    float G = geometrySmith(norm, viewDir, lightDir, adjustedRoughness);
    vec3 F = fresnelSchlick(HdotV, F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic; // Metallic surfaces don't have diffuse reflection

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    // Apply view-dependent attenuation to make highlights more natural
    float viewAttenuation = pow(NdotV, 0.6);
    specular *= viewAttenuation * 0.5;

    vec3 clearcoat = calculateClearcoat(norm, viewDir, lightDir, halfwayDir, albedo);
    vec3 sheen = calculateSheen(norm, viewDir, lightDir, halfwayDir);
    vec3 transmission = calculateTransmission(norm, viewDir, lightDir, albedo);

    specular = compensateEnergyLoss(specular, NdotV, roughness);

    // Hemisphere lighting: standard NdotL for front faces, a fill for the back
    // so the terminator is not a hard cut.
    float hemisphereNdotL = max(NdotL_raw, 0.0);
    float fillLight = max(-NdotL_raw * 0.3, 0.0);

    vec3 lit = applyEnergyConservation(
        kD * albedo / 3.14159265359 * radiance * hemisphereNdotL,
        specular * radiance * hemisphereNdotL,
        clearcoat * radiance * hemisphereNdotL,
        sheen * radiance * hemisphereNdotL
    ) + transmission;

    return lit + fillLight * tempAdjustedLightColor * albedo * 0.2;
}

void main() {

    vec4 texColor = texture(textureSampler, fragTexCoord);
    
    // Check for emissive objects first - bypass all lighting for sun-like objects
    if (exposure > 10.0) {
        // For emissive objects like sun spheres - MAXIMUM brightness emission
        vec3 emissiveColor = vec3(1.0, 1.0, 1.0); // Pure white
        FragColor = vec4(emissiveColor, 1.0); // Full opacity, no tone mapping
        return; // Skip all lighting calculations
    }
    
    // Pre-calculate expensive operations once
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // Material properties
    vec3 albedo = diffuseColor * texColor.rgb * InstanceColor; // Apply per-instance color

    // GPU Gems Chapter 5: Apply Perlin noise for surface detail if enabled
    if (enablePerlinNoise) {
        vec3 noiseCoord = FragPos * noiseScale;
        float noiseValue = turbulence(noiseCoord, noiseOctaves);
        albedo = mix(albedo, albedo * (1.0 + noiseValue * 0.3), noiseIntensity);
    }

    // Calculate F0 (surface reflection at zero incidence) with realistic values
    vec3 F0 = vec3(0.04); // Default for dielectrics

    // Use realistic metallic F0 values based on material color
    if (metallic > 0.5) {
        // For metals, use color-based F0 values that are more realistic
        vec3 metalF0 = albedo;

        // Enhance metallic reflectance based on color
        if (albedo.r > albedo.g && albedo.r > albedo.b) {
            // Reddish metals (copper, gold)
            metalF0 = mix(vec3(0.95, 0.64, 0.54), albedo, 0.7); // Copper-like
        } else if (albedo.g > albedo.r && albedo.g > albedo.b) {
            metalF0 = mix(vec3(0.70, 0.78, 0.74), albedo, 0.7);
        } else if (albedo.b > albedo.r && albedo.b > albedo.g) {
            metalF0 = mix(vec3(0.66, 0.73, 0.80), albedo, 0.7);
        } else {
            metalF0 = mix(vec3(0.95, 0.93, 0.88), albedo, 0.7); // Silver-like
        }

        F0 = mix(F0, metalF0, metallic);
    } else {
        F0 = mix(F0, albedo, metallic);
    }

    float NdotV = clamp(dot(norm, viewDir), 0.001, 1.0); // Avoid zero division
    // Ensure minimum roughness to prevent point light artifacts
    float adjustedRoughness = max(roughness, 0.08); // Balanced minimum roughness

    // Every light in the scene contributes; the loop stops at lightCount, so a
    // scene with one light costs what it did before there could be four.
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        if (i >= lightCount) {
            break;
        }
        Lo += direct_light(lights[i], norm, viewDir, albedo, F0, NdotV, adjustedRoughness);
    }

    // Ambient belongs to the scene rather than to each light, so it comes from
    // the key light alone; summing it per light would wash the image out as
    // lights were added.
    vec3 keyColor = lights[0].color * kelvinToRGB(lights[0].temperature);
    vec3 ambient = lights[0].ambientStrength * keyColor * albedo * 0.8;
    vec3 fillLightContrib = vec3(0.0);

    vec3 color = ambient + fillLightContrib + Lo;
    
	// Calculate distance for performance scaling (CRITICAL for voxel terrain performance)
	float distanceToCamera = length(FragPos - viewPos);
	
	// Apply modern lighting effects with distance-based LOD
	
	// SSAO (Improved hemisphere sampling with distance-based LOD)
	float ssaoFactor = calculateSSAO(FragPos, norm, distanceToCamera);
	color *= ssaoFactor;
    
	// Volumetric lighting (with distance LOD built-in)
	vec3 volumetric = calculateVolumetricLighting(FragPos, lights[0].position, viewPos);
	color += volumetric;
    
	// Global Illumination (with distance LOD built-in)
	vec3 gi = calculateGlobalIllumination(FragPos, norm, albedo, distanceToCamera);
	color += gi;
    
	// Environment reflections (skybox-based)
    vec3 envReflection = calculateEnvironmentReflection(norm, viewDir, roughness, metallic);
	color += envReflection * 0.3; // More visible reflections
    
    // GPU Gems Chapter 2: Caustics are handled in water shader for now
    // Future: Add caustics support to default shader with proper uniform checking
    
    // HDR exposure and tone mapping for normal objects
    color = color * exposure;
    // GPU Gems Chapter 9 & 11: Apply shadows with proper sun behavior
    color = color * shadow_factor();
    
    // Apply bloom effect
    if (enableBloom) {
        // Extract bright areas for bloom
        vec3 brightColor = max(color - bloomThreshold, vec3(0.0));
        float brightness = dot(brightColor, vec3(0.2126, 0.7152, 0.0722));
        
        if (brightness > 0.0) {
            // Simple bloom approximation
            vec3 bloom = brightColor * bloomIntensity;
            color += bloom * 0.3; // Blend bloom back into the image
        }
    }
    
    color = ACESFilm(color);
    
    // Gamma correction (sRGB)
    color = pow(color, vec3(1.0/2.2));
    
    // Use material alpha for transparency
    float finalAlpha = texColor.a * materialAlpha;
    
    // Ensure opaque materials are fully opaque
    if (materialAlpha >= 0.99) {
        finalAlpha = 1.0; // Force fully opaque for materials that should be opaque
    }
    
    FragColor = vec4(color, finalAlpha);
}
