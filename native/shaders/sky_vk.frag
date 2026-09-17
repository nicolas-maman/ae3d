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
    vec3 cloudSunColor;
    vec2 texelSize;
    float edgeThreshold;
    float edgeThresholdMin;
    float subpixelQuality;
    mat4 invViewProjection;
    float ssrRoadHeight;
    float ssrStrength;
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
    vec2 screenSize;
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

// A hash that is a hash on small integer lattices: the product-of-fracts
// one drifted smoothly across neighbouring cells, and the noise built on
// it was a gentle gradient with no cloud in it.
float cloudHash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
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
    vec2 p = xz * 0.0012 + vec2(t * 0.004, t * 0.0015);
    // The fbm of a value noise sits between 0.3 and 0.7 nearly everywhere;
    // stretched over 0..1 first, so the cover setting cuts it where it says.
    float shape = clamp((cloudFbm(vec3(p, 3.7)) - 0.3) / 0.4, 0.0, 1.0);
    // Weather has districts: a slower field gathers the clouds into
    // banks and leaves clearings between, so the sky is not one even
    // sprinkle of the same puff.
    float bank = cloudNoise(vec3(p * 0.13 + vec2(t * 0.001, 0.0), 8.1));
    float threshold = 1.0 - cover * (0.45 + 1.1 * bank);
    return clamp((shape - threshold) / 0.3, 0.0, 1.0);
}

// The cloud's density at a point in the layer: the coverage over it,
// eroded by 3D noise, thinned toward the layer's floor and ceiling so a
// cloud is a heap and not a slab.
float cloudDensity(vec3 p, float cover, float t) {
    float cov = cloudCoverage(p.xz, cover, t);
    if (cov <= 0.0) return 0.0;
    float h = clamp((p.y - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE), 0.0, 1.0);
    float profile = smoothstep(0.0, 0.2, h) * (1.0 - smoothstep(0.55, 1.0, h));
    // Flatter than tall: a cumulus is wider than it is high.
    vec3 q = vec3(p.x * 0.0028, p.y * 0.0055, p.z * 0.0028) + vec3(t * 0.01, 0.0, t * 0.004);
    float erosion = cloudFbm(q);
    float d = cov * profile - (1.0 - cov) * 0.3 - erosion * 0.6 + 0.28;
    return smoothstep(0.0, 0.45, d);
}

// Clouds along a view ray from the ground: the layer marched in a few
// dozen steps, each lit by a short march toward the sun through the
// cloud above it (Beer's law, with the brightening at the edge a thin
// cloud has), summed front to back until the sky behind is hidden.
vec4 cloudsAlong(vec3 dir, float cover, float t) {
    if (cover <= 0.0 || dir.y <= 0.05) return vec4(0.0);
    float t0 = CLOUD_BASE / dir.y;
    float t1 = CLOUD_TOP / dir.y;
    // More steps toward the horizon, where the ray crosses the layer at a
    // slant and the same count would stride over whole clouds.
    int steps = int(20.0 + 24.0 * (1.0 - clamp(dir.y, 0.0, 1.0)));
    float dt = (t1 - t0) / float(steps);
    vec3 sun = normalize(cloudSun);
    vec3 sunLight = cloudSunColor * 1.35;
    vec3 colour = vec3(0.0);
    float alpha = 0.0;
    // A little jitter along the ray, so the steps do not band.
    float jitter = fract(sin(dot(dir.xz, vec2(12.9898, 78.233))) * 43758.5453);
    float ray = t0 + dt * jitter * 0.3;
    for (int i = 0; i < steps; i++) {
        vec3 p = vec3(viewPos.x, 0.0, viewPos.z) + dir * ray;
        float d = cloudDensity(p, cover, t);
        if (d > 0.001) {
            // Toward the sun: how much cloud stands between here and it.
            float shade = 0.0;
            float ls = 90.0;
            for (int k = 1; k <= 4; k++) {
                shade += cloudDensity(p + sun * ls * float(k), cover, t) * ls;
            }
            // The sky lights the top of a cloud and little of its base: the
            // ambient darkens down the layer, which is what gives a cloud
            // its grey underside and its bright crown.
            float h = clamp((p.y - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE), 0.0, 1.0);
            vec3 ambient = mix(vec3(0.36, 0.40, 0.50), vec3(0.62, 0.68, 0.80), h);
            float light = exp(-shade * 0.014) * (1.0 - exp(-d * 2.0)) * 1.3 + 0.08;
            vec3 c = sunLight * light + ambient * (0.45 + 0.55 * exp(-shade * 0.004));
            // Rolled off, since the sky is drawn without the scene's tone
            // curve and a lit crown would otherwise clip to paper white.
            c = c / (1.0 + c * 0.3);
            float a = 1.0 - exp(-d * dt * 0.02);
            colour += c * a * (1.0 - alpha);
            alpha += a * (1.0 - alpha);
            if (alpha > 0.98) break;
        }
        ray += dt;
    }
    // Gone at the horizon, where the layer is a hundred kilometres deep and
    // the haze the sky is painted with has swallowed it.
    float horizon = smoothstep(0.05, 0.14, dir.y);
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
    vec4 clouds = cloudsAlong(dir, cloudCover, cloudTime);
    sky = sky * (1.0 - clouds.a) + clouds.rgb;
    FragColor = vec4(sky, 1.0);
}
