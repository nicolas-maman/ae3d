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

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPosition;

// Enhanced water shader uniforms





// How far this camera can see. Everything that fades a feature out with distance
// measures against this rather than against a number of world units, so a scene
// laid out in metres and one laid out in centimetres behave alike.



// Water appearance






// GPU Gems Chapter 2: Caustics uniforms



// Foam on the crests and in the wave trails. Its strength was three constants
// buried in the shading, so a caller could not turn it down for a still lake or
// up for a rough sea.



// The still level the waves rise and fall around, so a crest can be measured
// against the water rather than against however high the world's zero happens
// to be.



// Configurable fog parameters






// Configurable sky parameters



// GPU Gems Chapter 9 & 11: Shadow support for water with antialiasing




// Water Reflection and Refraction (inspired by Medium article)







// Custom transparency control

layout(location = 0) out vec4 FragColor;

// GPU Gems Chapter 5: Enhanced Perlin Noise for engine-wide use
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Chapter 5: Multi-octave Perlin noise moved after noise function

float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    // Use smoother interpolation (quintic instead of cubic)
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// GPU Gems Chapter 5: Multi-octave Perlin noise for enhanced detail
float perlinNoise(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

// Better FBM with smoother octaves
float fbm(vec2 st) {
    float value = 0.0;
    float amplitude = 0.5;
    
    // Use prime numbers and irrational numbers to break patterns
    mat2 rotation = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    
    for (int i = 0; i < 6; i++) { // More octaves for smoother result
        value += amplitude * noise(st);
        st = rotation * st * 2.31; // Prime-like number to avoid alignment
        amplitude *= 0.53; // Non-power-of-2 decay
    }
    return value;
}

// Domain warped noise for complex natural patterns
float warpedNoise(vec2 st) {
    vec2 warp = vec2(
        fbm(st + vec2(0.0, 0.0)),
        fbm(st + vec2(5.2, 1.3))
    );
    return fbm(st + warp * 0.3); // Reduced warp strength for smoother result
}

// Multi-octave ridged noise for foam patterns
float ridgedNoise(vec2 st) {
    float n = fbm(st);
    return 1.0 - abs(n * 2.0 - 1.0);
}

// Warped noise for complex patterns
float warpedNoise(vec2 st, float warpStrength) {
    vec2 warp = vec2(fbm(st + vec2(0.0, 0.0)), fbm(st + vec2(5.2, 1.3)));
    return fbm(st + warp * warpStrength);
}

// GPU Gems Chapter 2: Wave function gradient for caustics
// Based on the wave height function, compute surface gradients
vec2 computeWaveGradient(vec2 position, float time) {
    float epsilon = 0.1;
    
    // Sample wave height at multiple points to compute gradient
    float h0 = 0.0;  // Center height
    float hx = 0.0;  // Height offset in X
    float hy = 0.0;  // Height offset in Y
    
    // Simplified wave function for caustics (faster than full Gerstner calculation)
    for (int i = 0; i < 2; i++) { // Use first 2 waves for efficiency
        float freq = 0.02 + float(i) * 0.01;
        float amp = 1.0 - float(i) * 0.3;
        float speed = 0.5 + float(i) * 0.2;
        float phase = float(i) * 0.5;
        
        h0 += amp * sin(position.x * freq + position.y * freq * 0.7 + time * speed + phase);
        hx += amp * sin((position.x + epsilon) * freq + position.y * freq * 0.7 + time * speed + phase);
        hy += amp * sin(position.x * freq + (position.y + epsilon) * freq * 0.7 + time * speed + phase);
    }
    
    // Compute gradient (partial derivatives)
    return vec2((hx - h0) / epsilon, (hy - h0) / epsilon);
}

// GPU Gems Chapter 2: Ray-plane intersection for caustic projection
vec3 rayPlaneIntersection(vec3 rayOrigin, vec3 rayDirection, vec3 planeNormal, float planeDistance) {
    // GPU Gems optimized version (assumes plane normal always points up)
    float t = (planeDistance - rayOrigin.z) / rayDirection.z;
    return rayOrigin + rayDirection * t;
}

// Enhanced caustic pattern with multiple layers and randomness
float generateCaustics(vec3 worldPos, float time) {
    if (!enableCaustics) return 0.0;
    
    // Use Perlin noise for organic, non-repeating caustic patterns
    vec2 causticsCoord = worldPos.xz * causticsScale;
    
    // Layer 1: Main caustic pattern (warped by noise)
    vec2 warp1 = vec2(
        perlinNoise(causticsCoord * 0.5 + time * 0.02, 2),
        perlinNoise(causticsCoord * 0.5 + time * 0.025 + vec2(5.2, 1.3), 2)
    );
    float caustic1 = perlinNoise(causticsCoord + warp1 * 0.3 + time * 0.015, 3);
    
    // Layer 2: Secondary pattern at different scale
    vec2 warp2 = vec2(
        perlinNoise(causticsCoord * 0.8 - time * 0.018, 2),
        perlinNoise(causticsCoord * 0.8 - time * 0.022 + vec2(3.7, 2.1), 2)
    );
    float caustic2 = perlinNoise(causticsCoord * 1.3 + warp2 * 0.25 - time * 0.012, 3);
    
    // Layer 3: Fine detail
    float caustic3 = perlinNoise(causticsCoord * 2.0 + time * 0.03, 2);
    
    // Combine with different weights
    float combined = caustic1 * 0.5 + caustic2 * 0.3 + caustic3 * 0.2;
    
    // Apply contrast to create bright spots
    combined = pow(abs(combined), 2.5) * 1.3;
    combined = smoothstep(0.2, 0.8, combined);
    
    // Distance attenuation
    float distanceAttenuation = 1.0 / (1.0 + length(worldPos.xz - viewPos.xz) * 0.00005);
    
    return combined * causticsIntensity * distanceAttenuation;
}

void main() {
    vec3 norm = normalize(fragNormal);
    
    // Calculate light direction based on light type (directional vs point light)
    vec3 lightDir;
    if (lightDirection.x != 0.0 || lightDirection.y != 0.0 || lightDirection.z != 0.0) {
        // A directional light's direction points at the light.
        lightDir = normalize(lightDirection);
    } else {
        // Point light
        lightDir = normalize(lightPos - fragPosition);
    }
    
    vec3 viewDir = normalize(viewPos - fragPosition);

    float waveHeight = fragPosition.y;
    float distanceFromCamera = length(viewPos - fragPosition);
    
    // NO surface patterns or noise - completely clean water
    float temporalPhase = time * 0.01;  // Minimal temporal movement for caustics only
    float detailScale = 1.0;            // No distance scaling - uniform
    
    // How far the waves are allowed to tilt the surface away from flat. Averaging
    // four samples offset symmetrically in x and z used to stand here, which
    // cost five normalizes a fragment and returned the normal it was given: the
    // offsets cancel.
    if (enableWaterNormalMapping) {
        norm = normalize(mix(vec3(0.0, 1.0, 0.0), norm, waterNormalIntensity));
    }
    
    // Use uniform water color
    vec3 baseOceanColor = waterBaseColor;
    if (baseOceanColor.r == 0.0 && baseOceanColor.g == 0.0 && baseOceanColor.b == 0.0) {
        baseOceanColor = vec3(0.05, 0.15, 0.35); // Fallback default
    }
    
    // Uniform water color - no distance-based gradients
    vec3 waterColor = baseOceanColor; // Use consistent color across entire ocean
    
    // Enhanced foam system with wave-based trails
    float totalFoam = 0.0;
    
    if (enableFoam) {
        // Wave height foam (peaks). How high a crest has to be for foam depends
        // on how high the waves go at all: fixed at four hundred and fifty
        // units, it never appeared on any ocean this engine has ever drawn.
        //
        // The sum of the amplitudes is what the waves reach only where all four
        // crest together, which is rare, so measuring against a large fraction
        // of it asked for an alignment the sea does not often make: foam thinned
        // as the water got rougher and was gone entirely past twice the default
        // amplitude, which is backwards. A third of the sum is a crest that
        // actually occurs.
        float tallest = (waveAmplitudes[0] + waveAmplitudes[1] +
                         waveAmplitudes[2] + waveAmplitudes[3]) * waveHeightMultiplier;
        if (tallest > 0.0) {
            float crest = waveHeight - waterLevel;
            totalFoam += smoothstep(tallest * 0.30, tallest * 0.55, crest) * 0.15;
        }
        
        // Dynamic foam trails based on wave velocity
        vec2 waveVelocity = vec2(dFdx(fragPosition.y), dFdy(fragPosition.y));
        float waveSpeed = length(waveVelocity) * 100.0;
        float velocityFoam = smoothstep(0.3, 1.0, waveSpeed) * 0.12;
        
        // Foam persistence using noise (foam lingers)
        vec2 foamCoord = fragPosition.xz * 0.01 - time * 0.05;
        float foamPersistence = ridgedNoise(foamCoord) * 0.08;
        
        // Combine foam types
        totalFoam += velocityFoam * foamPersistence;
        // The cap scales with the setting as well as the sum: at 1.0 this is
        // the quarter it always was, and turning it up has to be able to show.
        totalFoam = clamp(totalFoam * foamIntensity, 0.0, 0.25 * foamIntensity);
    }
    
    // GPU Gems Chapter 19: Physically accurate Fresnel for water
    float NdotV = max(dot(norm, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0); // Physical water Fresnel
    fresnel = mix(0.02, 0.12, fresnel); // Realistic water reflectivity range
    
    // Natural water specular highlights
    float roughness = 0.15; // More realistic water surface roughness
    float NdotH = max(dot(norm, normalize(lightDir + viewDir)), 0.0);
    float roughnessAlpha = roughness * roughness;
    float alpha2 = roughnessAlpha * roughnessAlpha;
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (3.14159 * denom * denom);
    
    // Geometry function for water (reuse existing NdotV)
    float NdotL = max(dot(norm, lightDir), 0.0);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G = NdotV * NdotL / ((NdotV * (1.0 - k) + k) * (NdotL * (1.0 - k) + k));
    
    // Schlick Fresnel for specular
    vec3 F0 = vec3(0.02); // Water has low F0
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - NdotH, 5.0);
    
    // Cook-Torrance BRDF
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001);
    
    // Subtle caustics for natural water appearance - smooth transitions
    float causticsIntensityFactor = smoothstep(1.8, 2.5, lightIntensity);
    vec2 causticsCoord = fragPosition.xz * 0.005 * detailScale + temporalPhase * 0.03;
    float caustics = pow(noise(causticsCoord), 6.0) * 0.08 * causticsIntensityFactor;
    
    // Very subtle subsurface scattering - smooth transitions
    vec3 subsurfaceColor = vec3(0.05, 0.15, 0.25);
    float subsurfaceStrength = max(0.0, dot(-norm, lightDir)) * 0.1;
    vec3 subsurface = subsurfaceColor * subsurfaceStrength * causticsIntensityFactor;
    
    // No surface pattern color injection - clean water surface
    
    // Calculate lighting factors FIRST - needed for both fog and lighting
    // Smooth ambient lighting based on light intensity - NO hard transitions
    float nightFactor = smoothstep(0.35, 0.25, lightIntensity); // 0=day, 1=night
    float duskFactor = 1.0 - abs(lightIntensity - 0.55) / 0.55; // Peak at 0.55 intensity
    duskFactor = clamp(duskFactor, 0.0, 1.0);
    
    // Configurable atmospheric perspective for realistic sky-water transition
    float fogDistance = 0.0;
    
    if (enableFog) {
        fogDistance = smoothstep(fogStart, fogEnd, distanceFromCamera);
        
        // Smooth fog color transitions - NO hard conditionals. The time of day
        // decides the shade, and the colour the caller asked for tints it: a
        // local of the same name used to hide that uniform completely.
        vec3 nightFog = vec3(0.3, 0.4, 0.5);
        vec3 duskFog = mix(vec3(0.4, 0.5, 0.6), skyColor, 0.4);
        vec3 dayFog = vec3(0.4, 0.5, 0.6);
        
        vec3 timeOfDayFog = mix(dayFog, duskFog, duskFactor);
        timeOfDayFog = mix(timeOfDayFog, nightFog, nightFactor);
        vec3 shade = timeOfDayFog * fogColor * 2.0;
        
        // Smooth fog intensity scaling
        float nightFogScale = mix(1.0, 0.5, nightFactor);
        float duskFogScale = mix(1.0, 0.8, duskFactor);
        float adaptiveFogIntensity = fogIntensity * nightFogScale * duskFogScale;
        
        waterColor = mix(waterColor, shade, fogDistance * adaptiveFogIntensity * 0.3);
    }
    
    // Modern PBR lighting for realistic water
    vec3 sunlightColor = vec3(1.0, 0.98, 0.95);  // Natural sunlight
    float diffuse = max(dot(norm, lightDir), 0.0);
    
    // Calculate lighting factors FIRST - needed for both fog and lighting
    // Smooth ambient lighting based on light intensity - NO hard transitions
    
    vec3 nightAmbient = vec3(0.01, 0.015, 0.03) * lightIntensity * (1.0 + caustics * 0.05);
    nightAmbient = mix(nightAmbient, skyColor * 0.1, 0.3);
    
    vec3 duskAmbient = vec3(0.04, 0.05, 0.1) * lightIntensity * (1.0 + caustics * 0.1);
    duskAmbient = mix(duskAmbient, skyColor * 0.15, 0.4);
    
    vec3 dayAmbient = vec3(0.05, 0.06, 0.12) * lightIntensity * (1.0 + caustics * 0.15);
    
    // Smooth blend between lighting conditions
    vec3 ambientLight = mix(dayAmbient, duskAmbient, duskFactor);
    ambientLight = mix(ambientLight, nightAmbient, nightFactor);
    
    // PBR diffuse and specular lighting with sky influence
    vec3 diffuseLight = diffuse * lightColor * lightIntensity * 1.8; // Much more diffuse for natural water appearance
    
    // GPU Gems Chapter 14 & 15: Enhanced perspective-corrected reflections with visibility optimization
    vec3 sunReflectDir = reflect(-lightDir, norm);
    float viewReflectDot = max(dot(viewDir, sunReflectDir), 0.0);
    
    // Chapter 15: Distance-based visibility and LOD management
    float reflectionDistance = length(viewPos - fragPosition);
    float perspectiveCorrection = 1.0 / (1.0 + reflectionDistance * 0.000005); // Closer = sharper
    
    // Chapter 15: Dynamic LOD for massive scenes - reduce detail at distance
    float lodFactor = smoothstep(viewDistance, viewDistance * 10.0, reflectionDistance);
    float performanceFactor = mix(1.0, 0.3, lodFactor); // Reduce complexity for distant pixels
    
    // GPU Gems Chapter 14: Balanced reflection calculations for realistic sun
    float adaptiveSharpness = mix(16.0, 64.0, perspectiveCorrection * performanceFactor); // Balanced core
    float adaptiveWidth = mix(4.0, 16.0, perspectiveCorrection * performanceFactor);       // Natural spread
    float adaptiveGlitter = mix(1.0, 4.0, perspectiveCorrection * performanceFactor);      // Subtle glitter
    
    // Add wave-based variation to make reflection more organic
    vec2 waveOffset = vec2(sin(waveHeight * 0.05), cos(waveHeight * 0.03)) * 0.1;
    float organicReflectDot = viewReflectDot + length(waveOffset) * 0.02;
    organicReflectDot = clamp(organicReflectDot, 0.0, 1.0);
    
    // Smooth, natural reflection falloff
    float smoothReflectDot = smoothstep(0.05, 0.95, organicReflectDot);
    
    // Natural reflection layers with organic variation
    float sunReflection = pow(smoothReflectDot, adaptiveSharpness);
    sunReflection *= (1.0 + sin(waveHeight * 0.02) * 0.1); // Organic variation
    
    float wideReflection = pow(smoothReflectDot, adaptiveWidth);
    wideReflection *= (1.0 + cos(waveHeight * 0.015) * 0.05);
    
    float glitterReflection = pow(smoothReflectDot, adaptiveGlitter);
    glitterReflection *= (1.0 + sin(waveHeight * 0.01 + time * 0.5) * 0.03); // Gentle animation
    
         // Enhanced multi-layer gradient smoothing to eliminate triangle visibility
     vec2 waveGradient = vec2(dFdx(waveHeight), dFdy(waveHeight)) * performanceFactor;
     
     // Multiple gradient samples for superior triangle edge elimination
     vec2 gradient1 = vec2(dFdx(waveHeight * 0.85), dFdy(waveHeight * 0.85));
     vec2 gradient2 = vec2(dFdx(waveHeight * 0.95), dFdy(waveHeight * 0.95));
     vec2 gradient3 = vec2(dFdx(waveHeight * 1.05), dFdy(waveHeight * 1.05));
     vec2 gradient4 = vec2(dFdx(waveHeight * 1.15), dFdy(waveHeight * 1.15));
     
     // Weighted gradient combination for maximum smoothness
     vec2 smoothedGradient = (waveGradient * 0.4 + gradient1 * 0.2 + gradient2 * 0.2 + gradient3 * 0.15 + gradient4 * 0.05);
     
     float waveReflectionBoost = 1.0 + length(smoothedGradient) * 0.015; // Even gentler boost
     
     // Advanced gradient-based mesh smoothing with multiple passes
     float gradientVariation = length(smoothedGradient);
     float primarySmoothing = 1.0 - clamp(gradientVariation * 0.06, 0.0, 0.8 * performanceFactor);
     
     // Secondary smoothing based on directional gradient analysis
     float gradientDirectionality = abs(smoothedGradient.x) + abs(smoothedGradient.y);
     float secondarySmoothing = 1.0 - clamp(gradientDirectionality * 0.04, 0.0, 0.6);
     
     // Combined smoothing for maximum triangle hiding
     float waveSmoothing = primarySmoothing * secondarySmoothing;
    
    // GPU Gems Chapter 14: Smooth perspective-corrected reflection blending
    vec3 sunColor = lightColor * vec3(1.0, 0.95, 0.8); // Warm sun color
    
    // Reduced sun reflection intensity for more natural look
    vec3 enhancedSpecular = (
        sunReflection * 0.3 +          // Reduced sun core (was 0.8)
        wideReflection * 0.2 +         // Reduced spread (was 0.4)
        glitterReflection * 0.15       // Reduced glitter (was 0.2)
    ) * sunColor * waveReflectionBoost * waveSmoothing;
    
    // Multi-layer smoothing for very gradual transitions
    float smoothingFactor1 = smoothstep(0.0, 1.0, length(enhancedSpecular));
    float smoothingFactor2 = smoothstep(0.1, 0.8, smoothingFactor1);
    enhancedSpecular *= smoothingFactor2 * 0.5; // Reduced (was 0.8)
    
    // Add subtle wave-based reflections that dance on the surface
    float waveReflect1 = sin(fragPosition.x * 0.01 + time * 0.5) * cos(fragPosition.z * 0.008 + time * 0.3);
    float waveReflect2 = sin(fragPosition.x * 0.015 - time * 0.4) * cos(fragPosition.z * 0.012 - time * 0.25);
    float subtleWaveReflection = (waveReflect1 + waveReflect2) * 0.5 + 0.5;
    subtleWaveReflection = pow(subtleWaveReflection, 3.0) * 0.15 * fresnel;
    if (enableWaterDistortion) {
        subtleWaveReflection *= waterDistortionIntensity;
    }
    vec3 waveReflectionColor = sunColor * subtleWaveReflection * NdotL;
    
    // Enhanced quality scaling for more visible reflections
    float qualityScale = mix(0.4, 0.7, perspectiveCorrection); // Reduced (was 0.5-0.9)
    qualityScale = smoothstep(0.0, 1.0, qualityScale);
    vec3 specularLight = (specular * skyColor * 0.02 + enhancedSpecular * qualityScale * 0.1 + waveReflectionColor) * lightIntensity;
    
    // Subtle rim lighting
    float rimIntensity = pow(1.0 - dot(norm, viewDir), 3.0) * 0.05;
    vec3 rimColor = vec3(0.1, 0.2, 0.35) * rimIntensity;
    
    // NO sparkles or surface effects - clean natural water
    
    // Uniform water color - NO depth variation based on waves
    vec3 depthColor = waterColor; // Use the gradient color as-is, no wave-based modification
    float depthFactor = 0.5; // Constant depth factor for transparency calculations
     
     // Simple dithering approach to break up triangle patterns
     vec2 screenPos = gl_FragCoord.xy;
     float dither = fract(sin(dot(screenPos, vec2(12.9898, 78.233))) * 43758.5453);
     
     // Add very subtle dithering to break up geometric patterns
     depthColor += (dither - 0.5) * 0.008; // Very subtle random variation
     
     // Keep only the most essential smoothing
     vec2 colorGradient = vec2(dFdx(depthColor.b), dFdy(depthColor.b));
     float gradientMagnitude = length(colorGradient);
     float basicSmoothing = 1.0 - clamp(gradientMagnitude * 5.0, 0.0, 0.3);
     depthColor *= mix(1.0, basicSmoothing, 0.7);
    
    vec3 baseColor = depthColor * (ambientLight + diffuseLight);
    
    // GPU Gems Chapter 2: Enhanced caustics integration  
    float gpuGemsCaustics = generateCaustics(fragPosition, time);
    baseColor += vec3(gpuGemsCaustics) * sunlightColor * (1.0 - depthFactor * 0.3);
    
    // Enhanced reflections system
    vec3 reflectVector = reflect(-viewDir, norm);
    
    // Sky reflection
    vec3 skyReflection = skyColor * fresnel * 0.18;
    
    // Simulated environment reflection (horizon + zenith gradient)
    float horizonFactor = abs(reflectVector.y);
    vec3 horizonReflection = mix(horizonColor, skyColor, horizonFactor);
    vec3 environmentReflection = horizonReflection * fresnel * 0.12;
    
    // Combine sky and environment reflections
    vec3 totalReflection = skyReflection + environmentReflection;
    if (enableWaterReflection) {
        totalReflection *= waterReflectionIntensity;
    } else {
        totalReflection = vec3(0.0);
    }
    
    vec3 finalColor = baseColor + 
                     specularLight * 0.8 +     // Reduced specular (was 1.5)
                     totalReflection * 0.6 +   // Reduced reflection (was 0.9)
                     subsurface +              
                     rimColor;
    
    // Natural ocean lighting - much brighter for visible reflections
    finalColor *= clamp(lightIntensity * 1.5, 0.8, 2.0); // Higher multiplier and max brightness
    
    // Enhanced wave lighting with subtle highlights on wave peaks
    float waveFacing = max(0.0, dot(norm, lightDir));
    float waveSlope = length(vec2(dFdx(fragPosition.y), dFdy(fragPosition.y)));
    float waveHighlight = smoothstep(0.5, 2.0, waveSlope) * 0.08; 
    float waveLighting = 1.0 + waveFacing * 0.2 + waveHighlight; // Increased wave lighting
    finalColor *= waveLighting;
    
    // Very subtle, natural foam
    if (totalFoam > 0.05) {
        vec3 foamColor = vec3(0.6, 0.7, 0.8); // Brighter foam
        float foamMix = totalFoam * 0.2; 
        finalColor = mix(finalColor, foamColor, foamMix);
    }
    
    // Use the transparency uniform from the editor
    float alpha = waterOpacity;
    
    // If transparency uniform is 0 (fallback), use default
    if (alpha <= 0.01) {
        alpha = 0.85;
    }
    
    // Removed artificial darkening
    // finalColor *= 0.6; <--- REMOVED
    
    // GPU Gems Chapter 9 & 11: Apply realistic water shadows
    if (enableShadows) {
        float shadowFactor = 1.0;
        
        // For sun lighting: create subtle wave-based shadows, not distance falloff
        // Real ocean shadows come from wave height variations, not distance from sun
        vec2 shadowCoord = fragPosition.xz * 0.0001;
        float waveBasedShadow = sin(shadowCoord.x + waveHeight * 0.01) * 
                               sin(shadowCoord.y + waveHeight * 0.01) * 0.5 + 0.5;
        
        // Very subtle shadow intensity based on wave depth
        float waveDepthFactor = smoothstep(-50.0, 50.0, waveHeight);
        shadowFactor = 1.0 - shadowIntensity * 0.15 * waveBasedShadow * waveDepthFactor;
        
        // Chapter 11: Add minimal soft edges
        shadowFactor = mix(shadowFactor, 1.0, shadowSoftness * 0.1);
        
        finalColor *= shadowFactor;
    }
    
    // NO wave-based subsurface scattering - uniform water appearance
    
    // NO wave-based ambient occlusion - uniform lighting
    
    // Underwater camera effect (when camera is below water surface)
    float underwaterDepth = max(0.0, waterPlaneHeight - viewPos.y);
    if (underwaterDepth > 0.5) {
        // Underwater color tint (blue-green)
        vec3 underwaterTint = vec3(0.1, 0.3, 0.5);
        float tintStrength = clamp(underwaterDepth * 0.015, 0.0, 0.7);
        finalColor = mix(finalColor, underwaterTint, tintStrength);
        
        // Underwater fog/murkiness
        float underwaterFog = clamp(distanceFromCamera * 0.0002, 0.0, 0.85);
        finalColor = mix(finalColor, waterBaseColor * 0.4, underwaterFog);
        
        // Underwater caustics are more prominent
        finalColor += vec3(gpuGemsCaustics * 0.6);
        
        // God rays effect (simple volumetric light approximation)
        float godRayStrength = max(0.0, dot(lightDir, viewDir));
        godRayStrength = pow(godRayStrength, 4.0) * 0.1;
        finalColor += lightColor * godRayStrength * (1.0 - tintStrength);
    }
    
    alpha = clamp(alpha, 0.001, 0.98); // Allow almost complete transparency
    
    FragColor = vec4(finalColor, alpha);
}
