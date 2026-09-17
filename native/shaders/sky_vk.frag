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
    vec2 screenSize;
    float ssaoRadius;
    float ssaoIntensity;
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
};
layout(set = 0, binding = 1) uniform sampler2D skybox;
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 TexCoords;


// The clouds over the painted sky: how much of it they cover (zero is a
// clear sky and no march at all), the time they drift by, and the sun
// that lights them, pointing at it, with its colour.




// Where the eye is: the layer stands in the world, over the ground it
// shadows, so the march starts from the camera and not from the origin.


// Clouds, shared by the sky that draws them and the ground they shadow.
// A layer between CLOUD_BASE and CLOUD_TOP metres up, whose coverage is a
// 2D field of value noise (the same field the ground reads its shadow
// from) and whose body is that coverage eroded by a 3D noise, so the
// edges are ragged and the undersides lumpy.
const float CLOUD_BASE = 1400.0;
const float CLOUD_TOP = 1950.0;

// A hash on the lattice's integer corners, in integers: the sine hash the
// clouds were first built on is a sine of a number in the thousands, and
// the two backends' sines disagree out there (the Vulkan sky's clouds came
// out smeared into streaks), while the product-of-fracts one drifted
// smoothly across neighbouring cells and made no cloud at all. Bit mixing
// is exact on both.
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

float cloudNoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(cloudHash(i + vec3(0, 0, 0)), cloudHash(i + vec3(1, 0, 0)), f.x),
                   mix(cloudHash(i + vec3(0, 1, 0)), cloudHash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(cloudHash(i + vec3(0, 0, 1)), cloudHash(i + vec3(1, 0, 1)), f.x),
                   mix(cloudHash(i + vec3(0, 1, 1)), cloudHash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Octaves turned against each other, so the lattice of one is not the
// lattice of the next and a cloud is not a stack of dice.
float cloudFbm(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    mat3 turn = mat3(0.00, 0.80, 0.60,
                     -0.80, 0.36, -0.48,
                     -0.60, -0.48, 0.64);
    for (int i = 0; i < 5; i++) {
        v += a * cloudNoise(p);
        p = turn * p * 2.02 + vec3(11.0, 5.0, 3.0);
        a *= 0.5;
    }
    return v;
}

// How much cloud there is over a point of the ground, 0..1: the coverage
// field, drifting with the wind, shaped by the cover setting so 0.3 is a
// few fair-weather clouds and 0.8 an overcast with holes.
float cloudCoverage(vec2 xz, float cover, float t) {
    vec2 p = xz * 0.00075 + vec2(t * 0.003, t * 0.0011);
    // Bent before it is read: value noise is a lattice, and read straight
    // its clouds were squares with rounded corners. A slow warp of the
    // lookup by another noise turns the lattice into lobes.
    vec2 warp = vec2(cloudNoise(vec3(p * 1.6, 1.3)), cloudNoise(vec3(p * 1.6, 7.9))) - 0.5;
    p += warp * 0.55;
    // The fbm of a value noise sits between 0.3 and 0.7 nearly everywhere;
    // stretched over 0..1 first, so the cover setting cuts it where it says.
    float shape = clamp((cloudFbm(vec3(p, 3.7)) - 0.3) / 0.4, 0.0, 1.0);
    // Weather has districts: a slower field gathers the clouds into
    // banks and leaves clearings between, so the sky is not one even
    // sprinkle of the same puff.
    float bank = cloudNoise(vec3(p * 0.13 + vec2(t * 0.001, 0.0), 8.1));
    float threshold = 1.0 - cover * (0.45 + 1.1 * bank);
    return clamp((shape - threshold) / 0.35, 0.0, 1.0);
}

// The cloud's density at a point in the layer: the coverage over it,
// eroded by 3D noise, rounded off at the layer's floor and tapered toward
// its ceiling. A cumulus is a heap: flat and soft underneath, tallest
// where the coverage is thickest, so the small clouds are low domes and
// the big ones tower, instead of every cloud being one slab's height.
float cloudDensity(vec3 p, float cover, float t) {
    float cov = cloudCoverage(p.xz, cover, t);
    if (cov <= 0.0) return 0.0;
    float h = clamp((p.y - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE), 0.0, 1.0);
    float top = 0.3 + 0.7 * cov;
    float profile = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(top * 0.5, top, h));
    float base = cov * profile;
    if (base <= 0.0) return 0.0;
    // Flatter than tall: a cumulus is wider than it is high. The erosion
    // eats more of a thin cloud than a thick one, so the edges go to
    // wisps and the cores stay solid.
    vec3 q = vec3(p.x * 0.0022, p.y * 0.0045, p.z * 0.0022) + vec3(t * 0.01, 0.0, t * 0.004);
    float erosion = cloudFbm(q);
    float bite = erosion * (0.35 + 0.45 * (1.0 - base));
    float d = clamp((base - bite) / max(1.0 - bite, 0.001), 0.0, 1.0);
    return d * d;
}

// A screen-space dither: each pixel starts its march a different fraction
// of a step into the layer, so the steps do not line up into stripes
// across the sky. Interleaved gradient noise, which tiles finely enough
// to read as grain rather than as a pattern.
float cloudDither(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Clouds along a view ray from the ground: the layer marched in a few
// dozen steps, each lit by a short march toward the sun through the
// cloud above it (Beer's law in three octaves of extinction, the way
// light that has scattered a few times still gets through, so the base
// of a cloud is grey and not black), summed front to back until the sky
// behind is hidden. The far clouds sit in the haze the sky is painted
// with: every sample is mixed toward the sky's colour by its distance,
// which is what keeps the horizon from filling with a white wall.
vec4 cloudsAlong(vec3 dir, vec3 sky, float cover, float t, float dither) {
    if (cover <= 0.0 || dir.y <= 0.03) return vec4(0.0);
    float t0 = CLOUD_BASE / dir.y;
    float t1 = CLOUD_TOP / dir.y;
    // More steps toward the horizon, where the ray crosses the layer at a
    // slant and the same count would stride over whole clouds.
    int steps = int(24.0 + 40.0 * (1.0 - clamp(dir.y, 0.0, 1.0)));
    float dt = (t1 - t0) / float(steps);
    vec3 sun = normalize(cloudSun);
    vec3 sunLight = cloudSunColor * 1.5;
    vec3 colour = vec3(0.0);
    float alpha = 0.0;
    float ray = t0 + dt * dither;
    for (int i = 0; i < steps; i++) {
        vec3 p = vec3(viewPos.x, 0.0, viewPos.z) + dir * ray;
        float d = cloudDensity(p, cover, t);
        if (d > 0.002) {
            // Toward the sun: how much cloud stands between here and it.
            float shade = 0.0;
            float ls = 70.0;
            for (int k = 1; k <= 4; k++) {
                shade += cloudDensity(p + sun * ls * float(k), cover, t) * ls;
            }
            // The sky lights the top of a cloud and little of its base: the
            // ambient darkens down the layer, which is what gives a cloud
            // its grey underside and its bright crown.
            float h = clamp((p.y - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE), 0.0, 1.0);
            vec3 ambient = mix(vec3(0.42, 0.46, 0.56), vec3(0.70, 0.75, 0.86), h);
            float beer = 0.55 * exp(-shade * 0.016) + 0.30 * exp(-shade * 0.004) + 0.15 * exp(-shade * 0.001);
            float powder = 1.0 - exp(-d * 3.0);
            vec3 c = sunLight * beer * (0.35 + 0.65 * powder) + ambient * (0.5 + 0.5 * exp(-shade * 0.004));
            // Rolled off, since the sky is drawn without the scene's tone
            // curve and a lit crown would otherwise clip to paper white.
            c = c / (1.0 + c * 0.3);
            // Into the haze with distance: the colour toward the sky's, and
            // the cloud itself thinner, since the air between is what is
            // seen more and more of. Without the second the horizon filled
            // with a band of small white blocks the haze had only tinted.
            float haze = 1.0 - exp(-ray * 0.00016);
            c = mix(c, sky, haze);
            float a = (1.0 - exp(-d * dt * 0.03)) * (1.0 - 0.75 * haze);
            colour += c * a * (1.0 - alpha);
            alpha += a * (1.0 - alpha);
            if (alpha > 0.98) break;
        }
        ray += dt;
    }
    // Gone at the horizon, where the layer is a hundred kilometres deep and
    // the haze the sky is painted with has swallowed it.
    float horizon = smoothstep(0.03, 0.16, dir.y);
    return vec4(colour, alpha * horizon);
}

void main() {
    vec3 dir = normalize(TexCoords);

    float theta = atan(dir.z, dir.x);
    float phi = asin(dir.y);

    float u = (theta + 3.14159265) / 6.28318531;
    float v = (phi + 1.57079633) / 3.14159265;

    // Sample the base level. u wraps from 1 to 0 along one longitude, and the
    // derivative across that step is huge, so texture() chose the smallest,
    // darkest mip for that one column of pixels: a dark line down the sky
    // wherever the camera faced -X. A sky is a smooth gradient with nothing a
    // mip chain has to tame, so level 0 is right everywhere and seamless here.
    vec3 sky = textureLod(skybox, vec2(u, v), 0.0).rgb;
    vec4 clouds = cloudsAlong(dir, sky, cloudCover, cloudTime, cloudDither(gl_FragCoord.xy));
    sky = sky * (1.0 - clouds.a) + clouds.rgb;
    FragColor = vec4(sky, 1.0);
}
