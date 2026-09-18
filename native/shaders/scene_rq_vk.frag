// Modern PBR-inspired Fragment Shader
#version 460
#extension GL_EXT_ray_query : require
#define AE3D_RAY_QUERY 1
layout(set = 0, binding = 5) uniform accelerationStructureEXT sceneAS;

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
    int rayShadows;
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
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 FragPos;
layout(location = 3) in vec3 InstanceColor;
layout(location = 4) in vec4 FragPosLightSpace;
layout(location = 6) in vec4 ClipNow;
layout(location = 7) in vec4 ClipPrev;
layout(location = 1) out vec2 outVelocity;


// How much of the sky this point can see, baked against the whole scene.
// Ambient is light arriving from everywhere, so it is the term this belongs
// to: without it a corner is as bright as an open wall and every surface reads
// flat wherever direct light does not reach.
layout(location = 5) in float Occlusion;







// The crowd's far tier as pictures (see VERTEX_CROWD): a picture is cut out
// by its alpha, and its colour is the figure's albedo and its normal map
// the figure's own normals, so it is lit below by the scene's lights the
// way the mesh beside it is.

// What a bake reads back instead of the lit picture: 1 the albedo -- the
// texture, the material and the tint, the surface's own colour before any
// light -- 2 the normal, world space, packed 0..1. Zero is the picture.
// tools/bake_impostor.ae draws its atlases through these.


// How far this camera can see. Everything that fades a feature out with distance
// measures against this rather than against a number of world units, so a scene
// laid out in metres and one laid out in centimetres behave alike.






// The clouds over the scene, for the shadow they throw: cover, drift time
// and the sun's direction, the same three the sky draws them with.




// How mirror-like the surface is. Zero is an ordinary matte surface whose
// highlight fades as the view grazes it. Above zero the surface reflects the
// scene's lights the way a wet road does -- brightest exactly where the view
// grazes it, so a lamp overhead smears into a bright streak down the road
// toward the viewer. Per material, so only the wet surfaces reflect.

// How wet the scene is, 0..1: rain on it. A wet surface is darker, its
// pores filled, and a mirror at a grazing angle -- the lamps smear down a
// wet road the way they never do a dry one. Upward-facing surfaces take
// it in full, walls hardly at all: water runs off them. Set by the
// weather; a scene without one is dry.


// Modern PBR Extensions
// The surface's shape rather than its colour. The tangent frame is worked out
// per pixel from the derivatives of the position and the UVs, so no tangent has
// to be stored on a vertex and nothing about the vertex format changes -- the
// cost is a handful of instructions on surfaces that have a map and nothing at
// all on those that do not.


// How much of the baked occlusion to apply. Zero is the lighting this renderer
// had before it could do this, which is what makes the difference measurable
// rather than a matter of opinion.











// Advanced Lighting Models





// Volumetric Lighting





// Global Illumination




// Bloom and HDR




// Distance haze. The same four names the water shader uses, because one scene
// has one atmosphere: a street that fades into the dark has to fade the water
// running down it by the same amount.






// GPU Gems Chapter 9 & 11: Shadow Volume Support with Antialiasing




// The way the light travelled when the map was drawn. The map is orthographic
// whatever kind of light cast it, so for a point light this is not the
// direction from the surface to the lamp: the offset a surface needs depends on
// the angle it makes with the map, and using the wrong one of the two striped
// every wall the map happened to graze.

// How much of the world one shadow-map texel covers. Everything about the map
// scales with the box it was fitted to, and this is the number that says by how
// much.

// Shadows by ray: where the Vulkan device has ray queries and the renderer
// has built the scene's acceleration structure, a ray from the surface
// toward the sun says what stands in the way, exactly, at any distance,
// with no map's texel to fit the world into. The map stays for what the
// structure does not hold (the crowd, the skinned) and for OpenGL; the
// two are combined, the darker winning. The block below is compiled into
// the Vulkan ray-query variant of this shader alone.



// GPU Gems Chapter 5: Improved Perlin Noise Support





// GPU Gems Chapter 2: light refracted by a water surface above the geometry









layout(location = 0) out vec4 FragColor;

// Convert color temperature (Kelvin) to RGB multiplier
// Optimized color temperature to RGB conversion using lookup approximation
// The light writes how far it can see into a colour target; this compares the
// fragment's own distance against it. Sampling a neighbourhood softens the edge.
float light_depth(float clipZ) {
    return clipZ;
}

float shadow_factor() {
    vec3 surface = normalize(Normal);
    vec3 toLight = normalize(-shadowDirection);

    // How much depth one texel spans is the tangent of the angle between the
    // surface and the map, which runs away at grazing incidence: a wall lit
    // along its length spans many texels of depth and a floor lit from
    // overhead spans almost none. Bounded, because the tangent is not.
    float facing = max(dot(surface, toLight), 0.0);
    float slope = min(sqrt(1.0 - facing * facing) / max(facing, 0.02), 32.0);

    // Offset along the surface rather than into the depth. Pushing the
    // comparison deeper is what a depth bias does, and enough of it to stop a
    // grazing wall striping itself is enough to lift every shadow off the
    // ground with it. Moving the sample sideways by the width of a texel costs
    // the same and detaches nothing.
    vec4 lightSpace = lightSpaceMatrix *
        vec4(FragPos + surface * shadowTexelWorld * (1.0 + slope), 1.0);
    vec3 projected = lightSpace.xyz / lightSpace.w;
    projected.xy = projected.xy * 0.5 + 0.5;
    projected.z = light_depth(projected.z);
    if (projected.z > 1.0) {
        return 1.0;
    }
    // Outside the map sideways is outside the light box, where nothing was
    // drawn into the map: lit, not the edge texel's depth compared against
    // whatever happens to lie there. A floor running past the box was shaded
    // by the box's border, a dark band with a straight edge across it.
    if (projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    // At least one texel between taps. Below that the nine samples of the 3x3
    // land on the same texel and cost nine lookups to produce what one would,
    // and the edge is as hard as no filtering at all.
    float radius = max(shadowSoftness, 1.0);

    // The offset a surface needs in order not to shadow itself is the depth one
    // shadow texel spans, and a texel is worth the same fraction of the light
    // box however large the box is: the texel and the depth range scale
    // together. A constant in normalised depth does not, so it was sized for
    // one scene and swallowed whole figures in a larger one -- a street ninety
    // metres long left nothing standing in it casting anything at all. A
    // surface nearly edge-on to the light spans more depth across that texel,
    // which is what the slope term is for.
    // What is left for the depth comparison is the texel the sample landed in,
    // which the offset above has already taken the slope out of.
    float bias = 2.0 * radius / (2.0 * float(textureSize(shadowMap, 0).x));

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

#ifdef AE3D_RAY_QUERY
// One ray toward the sun from just off the surface: lit, or the shadow's
// share of the light. The origin steps out along the normal by a little
// more than the surface's own tessellation error, so a face does not
// shadow itself, and the ray is opaque-only and stops at its first hit.
float ray_shadow_factor() {
    vec3 surface = normalize(Normal);
    vec3 toLight = normalize(-shadowDirection);
    if (dot(surface, toLight) <= 0.0) return shadowIntensity;
    vec3 origin = FragPos + surface * 0.02;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, sceneAS,
                          gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                          0xFF, origin, 0.01, toLight, 500.0);
    rayQueryProceedEXT(query);
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
        return shadowIntensity;
    }
    return 1.0;
}
#endif

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

// Volumetric Lighting (light shafts, fog) with distance-based optimization
vec3 calculateVolumetricLighting(vec3 worldPos, vec3 lightPos, vec3 viewPos) {
    if (!enableVolumetricLighting) return vec3(0.0);

    // Light scattered by the air between the eye and the surface -- the haze
    // that stands around a lamp on a wet night, and the whole of what makes a
    // night read as atmosphere rather than as lit objects in the dark. The view
    // ray is marched from the eye to the surface and, at each step, the lamp's
    // light that would scatter back toward the eye is added, so the glow
    // gathers where the ray passes close to the lamp and thins away from it.
    vec3 toSurface = worldPos - viewPos;
    float rayLength = length(toSurface);
    if (rayLength < 0.001) return vec3(0.0);
    vec3 rayDir = toSurface / rayLength;

    int steps = clamp(volumetricSteps, 8, 32);

    // The lamp's own colour and warmth: the haze is its light, not a grey fog.
    vec3 tint = lights[0].color * kelvinToRGB(lights[0].temperature);

    float scatter = 0.0;
    float stepSize = rayLength / float(steps);
    for (int i = 0; i < steps && i < 32; i++) {
        vec3 samplePos = viewPos + rayDir * (stepSize * (float(i) + 0.5));
        float d = length(lightPos - samplePos);
        // Concentrated near the lamp -- a metre off it is bright, ten metres a
        // faint wash -- which gathers the glow into a halo rather than lifting
        // the whole frame. scattering is that falloff, in inverse metres.
        scatter += stepSize / (1.0 + d * d * volumetricScattering);
    }
    return tint * scatter * volumetricIntensity;
}

// Global Illumination approximation with distance-based optimization
vec3 calculateGlobalIllumination(vec3 position, vec3 normal, vec3 albedo, float distanceToCamera) {
    if (!enableGlobalIllumination) return vec3(0.0);
    
    // Adaptive sample count based on distance (CRITICAL for voxel performance)
    int baseSamples = giBounces * 4;
    int samples = baseSamples;
    
    if (distanceToCamera < viewDistance * 0.5) {
        // Very close: minimal GI (too expensive for dense voxels)
        samples = max(2, baseSamples / 8);
    } else if (distanceToCamera < viewDistance * 2.0) {
        samples = max(4, baseSamples / 4);
    } else if (distanceToCamera < viewDistance * 5.0) {
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

// Clouds, shared by the sky that draws them and the ground they shadow.
// A layer between CLOUD_BASE and CLOUD_TOP metres up. Where cloud is, over
// the world, is the weather field: a fractal of tileable 2D value noise
// gathered into banks by a slower one, a tile of CLOUD_TILE metres that
// repeats without a seam. The sky reads it from the weather texture the
// engine bakes from this very function (native/ae3d_cloudnoise.c); the
// ground computes it here for its cloud shadow, so the shadow under a
// cloud is the cloud. Both use the one hash, in integers, exact on both
// backends.
const float CLOUD_BASE = 1400.0;
const float CLOUD_TOP = 2600.0;
const float CLOUD_TILE = 24000.0;

float cloudHash(vec3 p) {
    uvec3 v = uvec3(ivec3(floor(p))) * uvec3(1597334677u, 3812015801u, 2798796415u);
    uint n = (v.x ^ v.y ^ v.z) * 1597334677u;
    n ^= n >> 16u;
    n *= 0x7feb352du;
    n ^= n >> 15u;
    n *= 0x846ca68bu;
    n ^= n >> 16u;
    return float(n) * (1.0 / 4294967296.0);
}

// Value noise on a 2D lattice of `period` cells to the tile, wrapped so it
// tiles; the field's seed rides in z, as the baker's does.
float cloudNoise2(vec2 x, float period, float seed) {
    vec2 i = floor(x);
    vec2 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    vec2 i0 = mod(i, period);
    vec2 i1 = mod(i + 1.0, period);
    float a = cloudHash(vec3(i0.x, i0.y, seed));
    float b = cloudHash(vec3(i1.x, i0.y, seed));
    float c = cloudHash(vec3(i0.x, i1.y, seed));
    float d = cloudHash(vec3(i1.x, i1.y, seed));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// The weather over a point of the tile, uv in 0..1: x is the cloud field
// before the cover threshold, stretched over 0..1, y is the banks.
vec2 cloudWeatherAt(vec2 uv) {
    float shape = 0.0;
    float amp = 0.5;
    float freq = 6.0;
    for (int o = 0; o < 5; o++) {
        shape += amp * cloudNoise2(uv * freq, freq, float(41 + o * 17));
        amp *= 0.5;
        freq *= 2.0;
    }
    shape = clamp((shape - 0.3) / 0.4, 0.0, 1.0);
    float bank = cloudNoise2(uv * 3.0, 3.0, 8.0);
    return vec2(shape, bank);
}

// Where on the weather tile a point of the world is, with the wind's drift.
vec2 cloudWeatherUv(vec2 xz, float t) {
    return xz / CLOUD_TILE + vec2(t * 0.00012, t * 0.00004);
}

// The cover threshold over the field and the banks: 0.3 is a few
// fair-weather clouds, 0.8 an overcast with holes.
float cloudCoverageFrom(vec2 weather, float cover) {
    float threshold = 1.0 - cover * (0.45 + 1.1 * weather.y);
    // A soft ramp that stops short of one: at full coverage the cloud
    // still has its cells, and does not flatten into a sheet.
    return smoothstep(threshold, threshold + 0.5, weather.x) * 0.9;
}

// How much cloud there is over a point of the ground, 0..1, computed.
float cloudCoverage(vec2 xz, float cover, float t) {
    return cloudCoverageFrom(cloudWeatherAt(cloudWeatherUv(xz, t)), cover);
}

// The clouds' shadow at a point: the coverage over it, looked up where the
// sun's ray through it meets the cloud layer, so the shadow drifts with
// the clouds and leans with the sun. One minus most of the coverage: the
// sky still lights the ground under a cloud.
float cloudShadow(vec3 worldPos) {
    if (cloudCover <= 0.0 || cloudSun.y <= 0.05) return 1.0;
    vec3 sun = normalize(cloudSun);
    float up = (CLOUD_BASE + (CLOUD_TOP - CLOUD_BASE) * 0.35 - worldPos.y) / max(sun.y, 0.05);
    vec2 at = worldPos.xz + sun.xz * up;
    return 1.0 - 0.65 * cloudCoverage(at, cloudCover, cloudTime);
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

// GPU Gems Chapter 2: the bright web a water surface casts on what lies under
// it. Two noise fields drift apart and the pattern is where they cross, which
// is what gives caustics their thin moving lines rather than smooth blobs.
// One web of the caustic pattern: where two drifting noise fields cross,
// a thin bright line, which is what the focused light off a wave crest
// draws on the floor.
float caustic_web(vec2 uv, float t) {
    vec2 drift = vec2(0.7, 0.3) * causticsSpeed * t;
    float a = perlinNoise3D(vec3(uv + drift, t * causticsSpeed * 0.5));
    float b = perlinNoise3D(vec3(uv * 1.7 - drift * 0.8, t * causticsSpeed * 0.4 + 5.3));
    return pow(clamp(1.0 - abs(a - b) * 3.0, 0.0, 1.0), 8.0);
}

vec3 caustic_light(vec3 worldPos, vec3 norm) {
    if (!enableCaustics) {
        return vec3(0.0);
    }

    float depth = causticsWaterLevel - worldPos.y;
    if (depth <= 0.0) {
        return vec3(0.0);
    }

    // Two webs, the finer one half as bright, so the pattern is a network
    // with nodes where they cross and not one set of parallel worms; and
    // each colour a hair to one side, the fringe light through water has.
    vec2 uv = worldPos.xz * causticsScale;
    float t = causticsTime;
    vec2 fringe = vec2(0.006, 0.004);
    vec3 web;
    web.r = caustic_web(uv + fringe, t) + 0.5 * caustic_web(uv * 2.3 + fringe * 2.0 + 11.0, t);
    web.g = caustic_web(uv, t) + 0.5 * caustic_web(uv * 2.3 + 11.0, t);
    web.b = caustic_web(uv - fringe, t) + 0.5 * caustic_web(uv * 2.3 - fringe * 2.0 + 11.0, t);

    // The light falls from the surface straight down, so a floor catches the
    // whole pattern, a wall catches a grazing fraction of it, and depth of
    // water swallows what is left. How fast it is swallowed is the caller's to
    // say: a scene measured in metres and one measured in centimetres cannot
    // share a fixed rate.
    float facing = clamp(norm.y, 0.0, 1.0);
    float attenuation = exp(-depth / max(causticsDepth, 0.001));

    vec3 keyColor = lights[0].color * kelvinToRGB(lights[0].temperature);
    return keyColor * lights[0].intensity * web * facing * attenuation * causticsIntensity;
}

// The tangent frame of a surface, worked out from how the position and the UVs
// change across the screen.
//
// The usual way is a tangent stored on every vertex, which means a wider vertex
// for every mesh in the scene whether or not it has a map. This costs a few
// instructions on the surfaces that do have one and nothing on the rest, and
// gets the same frame -- the derivatives of a position against the derivatives
// of its UVs are what a tangent is.
mat3 cotangent_frame(vec3 normal, vec3 position, vec2 uv) {
    vec3 dpx = dFdx(position);
    vec3 dpy = dFdy(position);
    vec2 duvx = dFdx(uv);
    vec2 duvy = dFdy(uv);

    vec3 perp_y = cross(dpy, normal);
    vec3 perp_x = cross(normal, dpx);
    vec3 tangent = perp_y * duvx.x + perp_x * duvy.x;
    vec3 bitangent = perp_y * duvx.y + perp_x * duvy.y;

    float scale = inversesqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
    return mat3(tangent * scale, bitangent * scale, normal);
}

vec3 mapped_normal(vec3 normal, vec3 position, vec2 uv) {
    vec3 sampled = texture(normalMap, uv).xyz * 2.0 - 1.0;
    sampled.xy *= normalStrength;
    return normalize(cotangent_frame(normal, position, uv) * sampled);
}

// The wet's own grazing reflection at this pixel, worked out once in main
// from the wetness and the surface's facing, and read by every light.
float wetMirror = 0.0;

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

    // A matte surface's highlight fades as the view grazes it. A wet or mirror
    // surface does the opposite: the grazing reflection is the brightest it
    // throws, which is why a lamp smears furthest down a wet road seen at a
    // shallow angle. reflectivity picks between the two -- zero leaves an
    // ordinary surface exactly as it was, above zero lifts the grazing
    // reflection and scales it by how wet the surface is.
    float viewAttenuation = pow(NdotV, 0.6);
    float mirror = max(reflectivity, wetMirror);
    if (mirror > 0.001) {
        float grazing = 1.0 - NdotV;
        specular *= mirror * (0.5 + grazing * grazing * 4.0);
    } else {
        specular *= viewAttenuation * 0.5;
    }

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


// Where this pixel's surface was last frame, for the temporal passes: the
// clip positions the vertex stage carried, this frame's unnudged (the
// frame is drawn through the jittered projection; the jitter is taken back
// out) less the last frame's, in texture space. A pixel nothing drew keeps
// the target's clear, zero. See docs/rendering.md, "Motion vectors".
vec2 velocity(vec4 now, vec4 prev, vec2 nudge) {
    if (now.w <= 0.0 || prev.w <= 0.0) return vec2(0.0);
    vec2 uvNow = now.xy / now.w * 0.5 + 0.5 - nudge;
    vec2 uvPrev = prev.xy / prev.w * 0.5 + 0.5;
    return uvNow - uvPrev;
}

void main() {

    vec4 texColor = texture(textureSampler, fragTexCoord);
    
    // An emissive surface is its own light source, so it skips shading. It does
    // not skip having a colour: this returned a hardcoded white, which made an
    // emissive model the one thing in the engine that could not be coloured --
    // diffuseColor, the texture and the per-instance tint were all discarded.
    // An accretion disc whose whole point is that its inner edge is blue-white
    // and its rim is red came out uniformly white.
    //
    // It also skipped tone mapping and gamma, so an emissive surface sat in a
    // different colour space from every lit surface beside it. Both now run,
    // which is also what lets a colour brighter than 1.0 (an instance colour is
    // a float attribute, so it can carry one) roll off to white through ACES
    // instead of clipping per channel and shifting hue on the way.
    //
    // exposure is the emissive strength, scaled so the 10.0 that opens this
    // branch means 1x. Below that the surface is lit normally.
    if (exposure > 10.0) {
        vec3 emissive = diffuseColor * texColor.rgb * InstanceColor * (exposure * 0.1);
        emissive = ACESFilm(emissive);
        FragColor = vec4(pow(emissive, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    // Pre-calculate expensive operations once
    vec3 norm = normalize(Normal);
    if (impostor) {
        // The picture: cut out by its alpha, its normal read from its own
        // atlas in the figure's frame (x its facing, y up, z to its right)
        // and turned into the world by the facing the vertex carried, and
        // lit from there as any surface is.
        if (texColor.a < 0.5) discard;
        vec3 facing = normalize(Normal);
        vec3 baked = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
        norm = normalize(baked.x * facing + vec3(0.0, baked.y, 0.0) + baked.z * vec3(-facing.z, 0.0, facing.x));
    } else if (hasNormalMap) {
        norm = mapped_normal(norm, FragPos, fragTexCoord);
    }
    // Before the capture channels return: a channel's frame carries the
    // motion too.
    outVelocity = velocity(ClipNow, ClipPrev, jitter);
    if (captureChannel == 1) {
        FragColor = vec4(diffuseColor * texColor.rgb * InstanceColor, texColor.a);
        return;
    }
    if (captureChannel == 2) {
        FragColor = vec4(norm * 0.5 + 0.5, 1.0);
        return;
    }
    // The capture channel for the motion: the vector in pixels, a hundred
    // pixels either way across the byte, so a test can read a known move.
    if (captureChannel == 3) {
        FragColor = vec4(outVelocity * screenSize / 200.0 + 0.5, 0.0, 1.0);
        return;
    }
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
    // A dielectric reflects about four percent of what hits it head on, tinted
    // by the material's own specular colour, which is white unless it says so.
    vec3 F0 = vec3(0.04) * specularColor;

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
    // Rain on the surface: the flatter it lies, the more it holds. Darker,
    // smoother, and a mirror at a grazing angle.
    float wet = wetness * clamp(norm.y, 0.0, 1.0);
    wet *= wet;
    if (wet > 0.001) {
        albedo *= 1.0 - 0.35 * wet;
        adjustedRoughness = max(mix(adjustedRoughness, 0.05, wet), 0.08);
        wetMirror = wet * 0.7;
    } else {
        wetMirror = 0.0;
    }

    // Every light in the scene contributes; the loop stops at lightCount, so a
    // scene with one light costs what it did before there could be four.
    // A shadow takes the direct light and leaves the sky's fill alone:
    // multiplied over the whole colour, ambient included, as it was, a
    // shadow went to a third of black and the shadowed side of a hill at
    // dusk was a hole in the picture. The one shadow map is the key
    // light's, and it is applied to every light's direct term all the
    // same: a lamp has no map of its own, and what stands in the key
    // light's shadow is what stands in the way of the lamp too, near
    // enough that a figure on a lamp-lit road keeps a shadow under it.
    // The clouds' shadow is the key light's alone.
    float shaded = 1.0;
    if (hasShadowMap && enableShadows) shaded = shadow_factor();
#ifdef AE3D_RAY_QUERY
    if (rayShadows == 1 && enableShadows) shaded = min(shaded, ray_shadow_factor());
#endif
    float sunlit = cloudShadow(FragPos);
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        if (i >= lightCount) {
            break;
        }
        vec3 lit = direct_light(lights[i], norm, viewDir, albedo, F0, NdotV, adjustedRoughness);
        Lo += (i == 0 ? lit * sunlit : lit) * shaded;
    }

    // Ambient belongs to the scene rather than to each light, so it comes from
    // the key light alone; summing it per light would wash the image out as
    // lights were added.
    vec3 keyColor = lights[0].color * kelvinToRGB(lights[0].temperature);
    // Ambient is light arriving from every direction, so what a point can see
    // of the sky is exactly what scales it. This is the term occlusion belongs
    // to and the only one: darkening the direct light as well would put a
    // shadow where a lamp is plainly shining.
    float shut = mix(1.0, Occlusion, occlusionStrength);
    vec3 ambient = lights[0].ambientStrength * keyColor * albedo * 0.8 * shut;
    vec3 fillLightContrib = vec3(0.0);

    // The direct light, under the clouds' shadow where a cloud drifts over.
    vec3 color = ambient + fillLightContrib + Lo;
    
	// Calculate distance for performance scaling (CRITICAL for voxel terrain performance)
	float distanceToCamera = length(FragPos - viewPos);
	
	// Apply modern lighting effects with distance-based LOD
	
	// Volumetric lighting (with distance LOD built-in)
	vec3 volumetric = calculateVolumetricLighting(FragPos, lights[0].position, viewPos);
	color += volumetric;
    
	// Global Illumination (with distance LOD built-in)
	vec3 gi = calculateGlobalIllumination(FragPos, norm, albedo, distanceToCamera);
	color += gi;
    
	// Environment reflections (skybox-based), which is what image based lighting
	// means here: light arriving from the surroundings rather than from a lamp.
    if (enableImageBasedLighting) {
        vec3 envReflection = calculateEnvironmentReflection(norm, viewDir, roughness, metallic);
        color += envReflection * 0.3 * iblIntensity;
    }
    
    color += caustic_light(FragPos, norm) * albedo;

    
    // HDR exposure and tone mapping for normal objects
    color = color * exposure;
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
    
    // The air between the eye and the surface. After tone mapping and gamma,
    // because fog is what is seen rather than another light in the scene: put
    // in before them and the tone curve pulls the horizon back out again.
    if (enableFog) {
        float haze = smoothstep(fogStart, fogEnd, distanceToCamera) * fogIntensity;
        color = mix(color, fogColor, clamp(haze, 0.0, 1.0));
    }

    // Use material alpha for transparency
    float finalAlpha = texColor.a * materialAlpha;
    
    // Ensure opaque materials are fully opaque
    if (materialAlpha >= 0.99) {
        finalAlpha = 1.0; // Force fully opaque for materials that should be opaque
    }
    
    FragColor = vec4(color, finalAlpha);
}
