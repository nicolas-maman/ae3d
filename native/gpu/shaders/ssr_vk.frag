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
    float spotCosOuter;
    float spotCosInner;
};

layout(std140, set = 0, binding = 0) uniform SceneBlock {
    Light lights[16];
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
    float frameExposure;
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
    float sunAngle;
    float rayOcclusion;
    float rayOcclusionStrength;
    float rayLampRadius;
    float rayReach;
    int rayFrame;
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
    int cloudFrame;
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
layout(set = 0, binding = 1) uniform sampler2D screenTexture;
layout(set = 0, binding = 2) uniform sampler2D depthTexture;

layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;









// The world position the depth at a screen UV was written from.
vec3 worldFromDepth(vec2 uv) {
    float d = texture(depthTexture, uv).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, d, 1.0);
    vec4 world = invViewProjection * clip;
    return world.xyz / world.w;
}

// A wet road is not a mirror. Puddles are: still water, a sharp reflection.
// The damp tarmac between them reflects too, dimmer and blurred by its own
// grain. Where the puddles lie is value noise over the road's metres, so
// they are the same from every camera and every frame.
float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x),
               mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}
// 0 on the damp tarmac, 1 in a puddle, the edge a hand's width wide.
float puddle(vec2 xz) {
    float n = valueNoise(xz * 0.35) * 0.6 + valueNoise(xz * 0.9) * 0.3 + valueNoise(xz * 2.7) * 0.1;
    return smoothstep(0.55, 0.62, n);
}

// The reflection at a hit, blurred by the surface's roughness over the
// distance the ray travelled: a cone, not a line, so a far facade mirrored
// in damp tarmac is a soft shape and a near lamp in a puddle is sharp.
vec3 reflectionAt(vec2 uv, float radius, vec2 seed) {
    if (radius < 0.0005) return texture(screenTexture, uv).rgb;
    // Eight taps on a disc, turned by a per-pixel angle so the eight do
    // not line up from one pixel to the next.
    float turn = hash2(seed) * 6.2831853;
    float c = cos(turn), s = sin(turn);
    vec2 aspect = vec2(1.0, float(textureSize(screenTexture, 0).x) / float(textureSize(screenTexture, 0).y));
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        float a = float(i) * 0.7853982;
        float r = radius * (0.35 + 0.65 * float(i + 1) / 8.0);
        vec2 d = vec2(cos(a), sin(a)) * r;
        d = vec2(d.x * c - d.y * s, d.x * s + d.y * c) * aspect;
        sum += texture(screenTexture, clamp(uv + d, vec2(0.001), vec2(0.999))).rgb;
    }
    return sum * 0.125;
}

void main() {
    vec3 scene = texture(screenTexture, TexCoords).rgb;
    if (ssrStrength <= 0.0) { FragColor = vec4(scene, 1.0); return; }

    vec3 P = worldFromDepth(TexCoords);
    // Only the flat wet surface reflects; everything else is left as it was.
    if (abs(P.y - ssrRoadHeight) > 0.06) { FragColor = vec4(scene, 1.0); return; }

    vec3 V = normalize(P - viewPos);
    vec3 R = reflect(V, vec3(0.0, 1.0, 0.0));
    // Water is a dielectric: little straight down, most of the light at a
    // grazing look. The mirror the road was is what a camera looking down
    // at it never sees. The base is well above water's 0.02 because the
    // frame is display values: a lamp a thousand times brighter than the
    // tarmac is white in it, and two percent of white is nothing, where two
    // percent of the lamp is the streak every wet street has.
    float cosLook = clamp(-V.y, 0.0, 1.0);
    float fresnel = 0.25 + 0.75 * pow(1.0 - cosLook, 5.0);
    float pool = puddle(P.xz);
    float wet = mix(0.6, 1.0, pool);
    float rough = mix(0.35, 0.02, pool);
    float stepLen = 0.25;
    vec3 pos = P + R * stepLen;
    vec3 prev = P;
    vec3 hit = scene;
    float edgeFade = 0.0;
    for (int i = 0; i < 64; i++) {
        vec4 clip = viewProjection * vec4(pos, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        vec3 sceneAt = worldFromDepth(uv);
        float behind = distance(viewPos, pos) - distance(viewPos, sceneAt);
        // A hit is the ray entering a surface, not the ray anywhere behind
        // one. Without a thickness every step that passed behind a figure
        // counted, so a road pixel well below a zombie reflected its legs:
        // the ray from there crosses the figure's column on screen far
        // behind it in depth, and the figure smeared into a stripe down
        // to the bottom of the frame.
        if (behind > 0.03 && behind < stepLen * 2.0 + 0.2) {
            // Bisect between the last step outside and this one inside, so
            // the hit is the surface and not a quarter-metre band on it.
            vec3 lo = prev;
            vec3 hi = pos;
            for (int k = 0; k < 4; k++) {
                vec3 mid = (lo + hi) * 0.5;
                vec4 mclip = viewProjection * vec4(mid, 1.0);
                vec2 muv = (mclip.xy / mclip.w) * 0.5 + 0.5;
                if (distance(viewPos, mid) > distance(viewPos, worldFromDepth(muv))) hi = mid; else lo = mid;
            }
            vec4 hclip = viewProjection * vec4(hi, 1.0);
            uv = (hclip.xy / hclip.w) * 0.5 + 0.5;
            // The cone's footprint at the hit, in screen space: wider the
            // rougher the surface and the further the ray went, narrower the
            // further the hit is from the eye.
            float travelled = distance(P, hi);
            float radius = min(rough * travelled / max(distance(viewPos, hi), 0.5) * 0.35, 0.06);
            hit = reflectionAt(uv, radius, TexCoords * 1024.0);
            // Fade toward the screen edges so the reflection does not cut hard.
            float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
            edgeFade = clamp(edge * 8.0, 0.0, 1.0);
            break;
        }
        prev = pos;
        pos += R * stepLen;
        // Longer steps further out, so the march reaches the far facades
        // within its budget while the near ones are still found finely.
        stepLen *= 1.05;
    }

    // Add only the reflection's *excess* brightness -- the lamps and lit
    // windows mirrored on the wet tarmac -- and never subtract. Reflecting the
    // dark night sky then costs nothing, so a wet road gains its bright streaks
    // without the surface going dark, which is both the look and a change the
    // numbers can only read as more light where a light is mirrored.
    vec3 glint = max(hit - scene, vec3(0.0));
    FragColor = vec4(scene + ssrStrength * fresnel * wet * edgeFade * glint, 1.0);
}
