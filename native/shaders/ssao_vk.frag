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
layout(set = 0, binding = 2) uniform sampler2D depthTexture;

layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 FragColor;

// Ambient occlusion from the scene's own depth, drawn over the opaque pass
// as a multiply before anything transparent: the corners, the feet of
// things and the ground under a canopy go darker by how much of the
// hemisphere over them the depth says is filled. It replaces a term in
// the scene shader that took no depth at all -- it sampled a hemisphere
// against its own surface, which came out the same number everywhere,
// and darkened everything drawn with it by two and a half times while
// leaving what was drawn without it alone.









// A stored scene depth as clip-space z: OpenGL keeps depth in 0..1 for a
// clip range of -1..1, Vulkan's clip range is the 0..1 it stores.
float scene_depth_clip(float depth) {
    return depth;
}

// Which way the screen's y runs: up on OpenGL, down on Vulkan, which turns
// a normal built from the screen's two axes the other way. The generator
// rewrites this for the Vulkan build.
float screen_y_sign() {
    return -1.0; /* screen y down */
}

// The world position the depth at a screen UV was written from, and that depth.
vec3 worldAt(vec2 uv, out float depth) {
    depth = texture(depthTexture, uv).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, scene_depth_clip(depth), 1.0);
    vec4 world = invViewProjection * clip;
    return world.xyz / world.w;
}

// Interleaved gradient noise: a different turn of the sample spiral at
// every pixel, so the twelve samples read as grain and not as a pattern.
float ssaoDither(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec2 uv = gl_FragCoord.xy / screenSize;
    float depth;
    vec3 P = worldAt(uv, depth);
    if (depth >= 0.99999 || ssaoIntensity <= 0.0) { FragColor = vec4(1.0); return; }

    // The surface's normal from the slope of its own depth: the neighbour
    // a pixel over on each axis, whichever of the two sides is nearer in
    // depth, so a silhouette does not bend the normal of what stands in
    // front of it. Oriented by the screen's axes and not by facing it
    // toward the camera: at a grazing view of a rough surface the two are
    // a hair apart, and facing it flipped one pixel in fifty into the
    // ground, where every sample was buried and the pixel went black.
    vec2 px = 1.0 / screenSize;
    float dl, dr, dd, du;
    vec3 Pl = worldAt(uv - vec2(px.x, 0.0), dl);
    vec3 Pr = worldAt(uv + vec2(px.x, 0.0), dr);
    vec3 Pd = worldAt(uv - vec2(0.0, px.y), dd);
    vec3 Pu = worldAt(uv + vec2(0.0, px.y), du);
    vec3 dx = abs(dl - depth) < abs(dr - depth) ? P - Pl : Pr - P;
    vec3 dy = abs(dd - depth) < abs(du - depth) ? P - Pd : Pu - P;
    vec3 N = normalize(cross(dx, dy)) * screen_y_sign();
    float eyeDist = distance(viewPos, P);

    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    // Counted from the same corner on both backends, so the grain the two
    // draw is the same grain and not two noises the parity test reads as
    // a disagreement.
    vec2 pixel = gl_FragCoord.xy;
    if (screen_y_sign() < 0.0) pixel.y = screenSize.y - pixel.y;
    float turn = ssaoDither(pixel) * 6.2831853;

    float occlusion = 0.0;
    const int COUNT = 12;
    for (int i = 0; i < COUNT; i++) {
        float f = (float(i) + 0.5) / float(COUNT);
        float a = turn + f * 15.0796;  // two and a bit turns of a spiral
        float r = sqrt(f);
        float h = sqrt(1.0 - r * r);   // cosine-weighted over the hemisphere
        vec3 dir = T * (cos(a) * r) + B * (sin(a) * r) + N * h;
        // More samples close in, where a step or a foot shadows, and a few
        // out to the full radius.
        float len = ssaoRadius * mix(0.1, 1.0, f * f);
        vec3 S = P + dir * len;
        vec4 clip = viewProjection * vec4(S, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneDepth;
        vec3 sceneAt = worldAt(suv, sceneDepth);
        float sceneDist = distance(viewPos, sceneAt);
        float sampleDist = distance(viewPos, S);
        // The sample is inside something when the scene is nearer the eye
        // along its line than the sample is. The tolerance keeps a flat
        // floor from shadowing itself, and grows with the distance: a
        // hillside is facets a metre high, and far enough off that each
        // is a pixel they shadowed one another into a grain over the
        // whole slope, which is not what a hill looks like from there.
        if (sampleDist - sceneDist > 0.03 * len + eyeDist * 0.01) {
            // Only a neighbour: what stands far in front of the point --
            // a branch over a field -- is not touching it.
            float range = smoothstep(0.0, 1.0, ssaoRadius / max(abs(eyeDist - sceneDist), 0.0001));
            occlusion += range;
        }
    }
    occlusion /= float(COUNT);
    float ao = clamp(1.0 - occlusion * ssaoIntensity, 0.0, 1.0);
    // Gone in the distance, where the radius is a few pixels and the depth
    // has no room left to tell a corner from a plane.
    ao = mix(ao, 1.0, smoothstep(ssaoRadius * 40.0, ssaoRadius * 120.0, eyeDist));
    FragColor = vec4(ao, ao, ao, 1.0);
}
