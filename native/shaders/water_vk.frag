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
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D sceneDepth;

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


// The sky the surface reflects: the scene's skybox image, given to the water
// model as its texture (the one texture every program binds), read as the
// equirect the skybox shader reads it as. Zero means no image, and the
// reflection stays the computed sky colour.


// What is under the water: the scene's depth as drawn before the water,
// so a fragment knows how much water lies between its surface and the
// ground beneath. Shallow water goes clear and pale and lays a line of foam
// along the shore; without the depth (a backend that has not captured it)
// the surface is what it always was.











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

// The same noise with its slope: the value in x, the derivative along
// each axis in yz, from the interpolation's own polynomial, so a surface
// ridden on it can shade without three lookups per point.
vec3 noised(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float k1 = b - a;
    float k2 = c - a;
    float k3 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k3 * u.x * u.y,
                du * vec2(k1 + k3 * u.y, k2 + k3 * u.x));
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

// The sea's surface at a point, summed per fragment: the four trains of
// the wave table, and eight octaves of wind waves below the shortest of
// them, each a sine with the same steepness and a direction fanned about
// the wind, moving at the speed deep water gives its length. The normal is
// the water's and not the mesh's, so a swell too long for the vertices to
// carry still shades as crest and trough, and the ripples too short for
// them still catch the sun. Each octave fades out where its wavelength is
// under a couple of hundredths of the distance, before it can alias into
// a sparkle; the wind waves also come and go in sets, under a slow noise.
// `swell` is the normal of the four trains alone, for the mirror: the sky
// bent by every ripple pulls the horizon down as white blotches.
void waveField(vec2 p, float t, float dist, out vec3 normal, out vec3 swell,
               out float height, out float steep) {
    float dhx = 0.0;
    float dhz = 0.0;
    float sx = 0.0;
    float sz = 0.0;
    float slopeMax = 0.0;
    float speedMul = max(waveSpeedMultiplier, 0.001);
    height = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 d = normalize(waveDirections[i].xz);
        float k = waveFrequencies[i];
        float A = waveAmplitudes[i] * waveHeightMultiplier * 1.5;
        float lambda = 6.28318531 / max(k, 0.000001);
        float fade = 1.0 - smoothstep(lambda * 40.0, lambda * 160.0, dist);
        float phase = dot(d, p) * k + t * waveSpeeds[i] * speedMul + wavePhases[i]
                    + noise(p * k * 0.2 + vec2(float(i) * 4.1, 0.0)) * 1.2;
        float c = cos(phase) * A * k * fade;
        height += A * sin(phase) * fade;
        sx += d.x * c;
        sz += d.y * c;
        slopeMax += A * k * fade;
    }
    dhx = sx;
    dhz = sz;
    vec2 wind = normalize(waveDirections[0].xz);
    float k0 = waveFrequencies[3];
    float sets = 0.55 + 0.9 * noise(p * k0 * 0.03 + vec2(t * 0.004, 0.0));
    // A sum of straight sines is a lattice from above, however many it
    // has: every crest is a ruled line. The phases are bent by a slow
    // noise, so each crest meanders, and the two noise fields turn with
    // the octave so no two octaves bend alike.
    vec2 warp = vec2(noise(p * k0 * 0.12 + vec2(3.1, 7.7)), noise(p * k0 * 0.12 + vec2(9.2, 1.3))) * 2.0 - 1.0;
    // Fanned a little about the wind, not a lot: two trains crossing at a
    // right angle print a diamond lattice on the sea, whatever bends them.
    for (int j = 1; j <= 4; j++) {
        float k = k0 * pow(1.65, float(j));
        float turn = sin(float(j) * 2.3 + float(j * j) * 0.61) * 0.5;
        vec2 d = vec2(wind.x * cos(turn) - wind.y * sin(turn), wind.x * sin(turn) + wind.y * cos(turn));
        float A = 0.028 / k;
        float lambda = 6.28318531 / k;
        float fade = (1.0 - smoothstep(lambda * 40.0, lambda * 160.0, dist)) * sets;
        float bend = (warp.x * d.y - warp.y * d.x) * 2.4;
        float phase = dot(d, p) * k + t * sqrt(9.81 * k) * speedMul + float(j) * 1.7 + bend;
        float c = cos(phase) * A * k * fade;
        height += A * sin(phase) * fade;
        dhx += d.x * c;
        dhz += d.y * c;
        slopeMax += A * k * fade;
    }
    // Under the sines, the chop. Not more sines: a sum of straight waves
    // is a lattice from above however it is fanned, and the short ones
    // made the whole sea a crosshatch. Three octaves of noise instead,
    // one cell per wavelength, blown along the wind at the speed deep
    // water gives that length, whose slope is read off the noise's own
    // derivative. Noise has no crest lines to cross.
    float kc = k0 * pow(1.65, 5.0);
    for (int j = 0; j < 3; j++) {
        float k = kc * pow(2.1, float(j));
        float lambda = 6.28318531 / k;
        float fade = (1.0 - smoothstep(lambda * 40.0, lambda * 160.0, dist)) * sets;
        if (fade <= 0.0) continue;
        float A = 0.16 / k;
        vec2 st = p * (k / 6.28318531) - wind * (t * sqrt(9.81 * k) * speedMul / 6.28318531)
                + vec2(float(j) * 17.3, float(j) * 5.9);
        vec3 n = noised(st);
        height += A * (n.x - 0.5) * fade;
        float slope = A * k / 6.28318531 * fade;
        dhx += n.y * slope;
        dhz += n.z * slope;
        slopeMax += slope * 1.5;
    }
    normal = normalize(vec3(-dhx, 1.0, -dhz));
    swell = normalize(vec3(-sx, 1.0, -sz));
    steep = clamp(length(vec2(dhx, dhz)) / max(slopeMax * 0.5, 0.0001), 0.0, 1.0);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// A stored scene depth as clip-space z: OpenGL keeps depth in 0..1 for a
// clip range of -1..1, Vulkan's clip range is the 0..1 it stores.
float scene_depth_clip(float depth) {
    return depth;
}

// How much water lies along the view ray between this fragment and what
// the scene drew behind it, in metres; zero without a depth to read.
float water_below(vec3 fragmentPos, vec3 eye) {
    if (hasSceneDepth != 1) return 1000.0;
    vec2 suv = gl_FragCoord.xy / screenSize;
    float d = texture(sceneDepth, suv).r;
    if (d >= 0.99999) return 1000.0;
    vec4 clip = vec4(suv * 2.0 - 1.0, scene_depth_clip(d), 1.0);
    vec4 world = invViewProjection * clip;
    vec3 ground = world.xyz / world.w;
    return max(distance(eye, ground) - distance(eye, fragmentPos), 0.0);
}

// What the surface reflects: the skybox image where the reflected ray meets
// it, read as the equirect the skybox shader reads, or the two sky colours
// graded from horizon to zenith when the scene has no image.
vec3 reflectedSky(vec3 ray) {
    ray.y = abs(ray.y);
    if (hasSkyTexture == 1) {
        float theta = atan(ray.z, ray.x);
        float phi = asin(clamp(ray.y, -1.0, 1.0));
        vec3 shown = textureLod(textureSampler,
                                vec2((theta + 3.14159265) / 6.28318531,
                                     (phi + 1.57079633) / 3.14159265), 0.0).rgb;
        return pow(shown, vec3(2.2));
    }
    vec3 zenith = pow(skyColor, vec3(2.2));
    vec3 rim = pow(horizonColor, vec3(2.2));
    return mix(rim, zenith, smoothstep(0.0, 0.35, ray.y));
}

void main() {
    // Light direction points at the light for a sun, else at the point light.
    vec3 lightDir;
    if (lightDirection.x != 0.0 || lightDirection.y != 0.0 || lightDirection.z != 0.0) {
        lightDir = normalize(lightDirection);
    } else {
        lightDir = normalize(lightPos - fragPosition);
    }
    vec3 viewDir = normalize(viewPos - fragPosition);
    float distanceFromCamera = length(viewPos - fragPosition);
    float waveHeight = fragPosition.y;
    // How much of the view this fragment is far across: ripples, foam and
    // the mirror's sharpness all fall off with it.
    float far = smoothstep(0.0, viewDistance * 0.35, distanceFromCamera);

    // The surface here, summed from the wave field; flattened as asked.
    vec3 norm;
    vec3 swell;
    float fieldHeight;
    float steep;
    waveField(fragPosition.xz, time, distanceFromCamera, norm, swell, fieldHeight, steep);
    if (enableWaterNormalMapping) {
        norm = normalize(mix(vec3(0.0, 1.0, 0.0), norm, waterNormalIntensity));
        swell = normalize(mix(vec3(0.0, 1.0, 0.0), swell, waterNormalIntensity));
    }
    float rippleFreq = waveFrequencies[3] * 9.0 / 6.28318531;
    float rippleTime = time * waveSpeeds[3] * max(waveSpeedMultiplier, 0.001) * 0.35;
    // How far the ripples bend the mirror: the distortion setting, its
    // default of a fifth being the bend that reads as water without pulling
    // the horizon down into it.
    // Less far off: the horizon mirrored in every far ripple is a sea of
    // pale specks, and from a distance a sea is a mirror of the swell.
    float bend = enableWaterDistortion ? clamp(waterDistortionIntensity, 0.0, 1.0) * 0.6 : 0.12;
    bend *= 1.0 - 0.7 * far;
    vec3 mirrorNorm = normalize(mix(swell, norm, bend));

    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotV = max(dot(norm, viewDir), 0.001);
    float NdotL = max(dot(norm, lightDir), 0.0);
    float NdotH = max(dot(norm, halfDir), 0.0);
    float VdotH = max(dot(viewDir, halfDir), 0.0);

    // Water reflects two percent looking straight down and nearly all of
    // the sky at a grazing look toward the horizon.
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(mirrorNorm, viewDir), 0.001), 5.0);
    float mirror = enableWaterReflection ? clamp(waterReflectionIntensity, 0.0, 1.0) : 0.0;

    // Everything below is radiance in the scene shader's units: the sun's
    // colour times its intensity, tone mapped and gamma-corrected at the
    // end the way the terrain beside the water is, so the two agree.
    vec3 sun = lightColor * lightIntensity;
    vec3 skyLight = pow(skyColor, vec3(2.2));

    // The sun's glitter: GGX, the surface a little rougher far off so the
    // highlight there is a path of light and not a scatter of aliased points.
    float roughness = mix(0.07, 0.22, far);
    float alphaR = roughness * roughness;
    float alpha2 = alphaR * alphaR;
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (3.14159265 * denom * denom);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    float Fs = 0.02 + 0.98 * pow(1.0 - VdotH, 5.0);
    vec3 glitter = sun * (D * G * Fs) / (4.0 * NdotL * NdotV + 0.0001) * NdotL;

    // The body of the water: its own colour lit by the sky from above and
    // the sun where the swell faces it.
    vec3 base = waterBaseColor;
    if (base.r == 0.0 && base.g == 0.0 && base.b == 0.0) base = vec3(0.02, 0.08, 0.14);
    vec3 body = base * (skyLight * 0.7 + sun * (0.12 + 0.30 * NdotL));

    // Light through the crests: where a wave stands between the eye and the
    // sun the water glows from inside, greener and brighter than its body.
    float tallest = (waveAmplitudes[0] + waveAmplitudes[1] +
                     waveAmplitudes[2] + waveAmplitudes[3]) * waveHeightMultiplier;
    // Against the table's sum: the field reaches past it only where every
    // train crests at once, so a crest at the sum is a tall one and an
    // ordinary crest is half of it.
    float crest = clamp(fieldHeight / max(tallest, 0.001), 0.0, 1.0);
    float through = pow(clamp(dot(viewDir, -lightDir), 0.0, 1.0), 3.0) * (0.3 + 0.7 * crest)
                  + crest * 0.25 * NdotL;
    vec3 scatter = (base * 2.5 + vec3(0.0, 0.06, 0.04)) * sun * 0.30 * through;

    // Foam where a crest breaks, torn up by the ripple noise so it is
    // patches and not a band along every crest. The noise thins it and
    // never removes it: a sea whose waves are kilometres long puts the
    // whole view inside one cell of that noise, and its crests still break.
    float foam = 0.0;
    if (enableFoam && tallest > 0.0) {
        float torn = ridgedNoise(fragPosition.xz * rippleFreq * 0.08 + time * 0.02);
        // What breaks is the steep face of a crest, not the broad top of a
        // long swell: the field's slope here against the steepest it can
        // make, so a whitecap is a few metres on a short wave and not a
        // field on a long one.
        float breaking = smoothstep(0.45, 0.9, steep) * (0.4 + 0.6 * crest);
        foam = breaking * (0.35 + 0.65 * smoothstep(0.35, 0.8, torn));
        // Lace, not paint: the fine ripple noise eats holes in it.
        foam *= 0.55 + 0.45 * noise(fragPosition.xz * rippleFreq * 1.5 + rippleTime * 0.5);
        foam *= clamp(foamIntensity, 0.0, 2.0) * (1.0 - 0.7 * far);
        foam = clamp(foam, 0.0, 1.0);
    }

    vec3 skyHit = reflectedSky(reflect(-viewDir, mirrorNorm));
    vec3 finalColor = mix(body + scatter, skyHit, fresnel * mirror) + glitter * (1.0 - 0.6 * foam);
    vec3 foamLit = vec3(0.85) * (sun * 0.5 * max(lightDir.y, 0.0) + skyLight * 0.6);
    finalColor = mix(finalColor, foamLit, foam);

    // The caustic web, for a scene that lights its surface with it.
    float gpuGemsCaustics = generateCaustics(fragPosition, time);
    finalColor += gpuGemsCaustics * sun * 0.1;

    finalColor = pow(ACESFilm(finalColor), vec3(1.0 / 2.2));

    // The air between the eye and the surface, after the tone curve, the
    // same as the scene shader does it, so the sea and the shore fade alike.
    if (enableFog) {
        float haze = smoothstep(fogStart, fogEnd, distanceFromCamera) * fogIntensity;
        finalColor = mix(finalColor, fogColor, clamp(haze, 0.0, 1.0));
    }

    // The shore. Where the ground is close under the surface the water goes
    // clear, so the sand shows through it, and pale, since the light comes
    // back off that sand; and a line of foam runs along the waterline, torn
    // by the ripple noise, from the wash of the last wave.
    float below = water_below(fragPosition, viewPos);
    float clear = 1.0 - exp(-below / max(waterDepthFade, 0.01));
    if (hasSceneDepth == 1 && below < 999.0) {
        float wash = 1.0 - smoothstep(0.0, max(waterShoreFoam, 0.01), below);
        float lace = 0.5 + 0.5 * noise(fragPosition.xz * rippleFreq * 0.8 + rippleTime * 0.3);
        float shoreFoam = wash * wash * lace * 0.9;
        vec3 shoreLit = pow(vec3(0.9) * (sun * 0.5 * max(lightDir.y, 0.0) + skyLight * 0.6), vec3(1.0 / 2.2));
        finalColor = mix(finalColor, shoreLit, clamp(shoreFoam, 0.0, 1.0));
        foam = max(foam, shoreFoam);
    }

    // What reflects is not seen through: the surface goes opaque as the
    // mirror takes over, and under foam; and seen through where the ground
    // is close beneath it.
    float alpha = waterOpacity;
    if (alpha <= 0.01) alpha = 0.85;
    alpha = mix(alpha, 1.0, fresnel * mirror);
    alpha *= clear;
    alpha = mix(alpha, 1.0, foam);

    // Underwater camera effect (when camera is below water surface)
    float underwaterDepth = max(0.0, waterPlaneHeight - viewPos.y);
    if (underwaterDepth > 0.5) {
        // The surface from below is the sky coming through the swell, not a
        // ceiling lit by the fill. Straight overhead the sky comes through
        // brightest (Snell's window); toward the horizon the underside
        // reflects the water's own colour back, and the swell's normals
        // sweep that boundary about, which is what says "waves" from
        // underneath. The tint deepens with the diver's depth.
        vec3 through = mix(skyColor, waterBaseColor, 0.35);
        // Softened: the field's every ripple would flip the window between
        // sky and water, and the underside became a hard two-tone print.
        vec3 under = normalize(mix(vec3(0.0, 1.0, 0.0), norm, 0.35));
        float overhead = clamp(dot(under, -viewDir), 0.0, 1.0);
        float window = smoothstep(0.15, 0.75, overhead);
        vec3 underside = mix(waterBaseColor * 0.55, through, window);
        float deep = clamp(underwaterDepth * 0.0025, 0.0, 0.5);
        finalColor = mix(underside, waterBaseColor * 0.5, deep);
        // The sun through the surface: a glitter where the swell's normals
        // point it at the eye, and the caustic web on the underside.
        vec3 refracted = normalize(reflect(-viewDir, under));
        float glitter = pow(max(0.0, dot(refracted, lightDir)), 48.0);
        finalColor += lightColor * glitter * 0.8 * window;
        finalColor += vec3(gpuGemsCaustics * 0.35) * window;

        // Underwater murk: the surface fades with distance into the scene's
        // own fog when it has one, the same murk the seabed and everything
        // on it fade into -- a fade to 0.4x the water colour was a dark band
        // along the horizon under a sea that was otherwise one colour.
        float underwaterFog = clamp(distanceFromCamera * 0.0002, 0.0, 0.85);
        vec3 murk = waterBaseColor * 0.4;
        if (enableFog) {
            underwaterFog = smoothstep(fogStart, fogEnd, distanceFromCamera) * fogIntensity;
            murk = fogColor;
        }
        finalColor = mix(finalColor, murk, underwaterFog);
    }
    
    alpha = clamp(alpha, 0.001, 0.98); // Allow almost complete transparency
    
    FragColor = vec4(finalColor, alpha);
}
