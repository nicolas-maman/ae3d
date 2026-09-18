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
layout(set = 0, binding = 1) uniform sampler2D skybox;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 3) uniform sampler2D cloudWeather;
layout(set = 0, binding = 4) uniform sampler3D cloudShape;
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 TexCoords;


// The clouds over the painted sky: how much of it they cover (zero is a
// clear sky and no march at all), the time they drift by, and the sun
// that lights them, pointing at it, with its colour.




// Where the eye is: the layer stands in the world, over the ground it
// shadows, so the march starts from the camera and not from the origin.

// A sky drawn from the sun instead of read from the image: one, and the
// image is ignored. The sun is cloudSun, the same sun the clouds are lit
// by, so the sky, the clouds and the ground agree about where it is.

// The overcast: how far the sky, painted or procedural, is pulled toward
// a flat cast of skyOvercastColor at its own brightness -- the grey of a
// rainy day, the ochre of a dust storm -- which the clouds' ambient then
// takes too, since they are lit by the sky behind them.



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

// The clouds' noise, as textures: the weather over the world (x the cloud
// field, y the cloud's kind, z the banks) and the shape, a tileable cube
// whose red is Perlin-Worley and whose green, blue and alpha are Worley at
// three rising frequencies. Baked once by the engine from the functions
// above and their 3D kin; a fetch a sample where the march used to sum a
// fractal of forty hashes.



// The cloud's extinction, per metre at full density: the one constant
// the sun's shade and the eye's alpha both use, so a cloud that hides the
// sky hides the sun the same.
const float CLOUD_SIGMA = 0.025;

float remap(float v, float lo, float hi, float newLo, float newHi) {
    return newLo + (clamp(v, lo, hi) - lo) / max(hi - lo, 1e-5) * (newHi - newLo);
}

// The cloud's density at a point in the layer, the way a production sky
// builds it: the weather says where cloud is and what kind; the height
// profile gives the kind its shape, a low flat stratus or a tall cumulus;
// the Perlin-Worley shape carves the body, the Worley fractal erodes its
// edges into detail -- wispy at the base, billowing above.
float cloudDensity(vec3 p, float cover, float t, out float heightFraction) {
    vec3 weather = texture(cloudWeather, cloudWeatherUv(p.xz, t)).rgb;
    float cov = cloudCoverageFrom(vec2(weather.r, weather.b), cover);
    heightFraction = clamp((p.y - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE), 0.0, 1.0);
    if (cov <= 0.0) return 0.0;
    // A stratus fills the bottom third of the layer, a cumulus climbs to
    // its top; more cover, taller clouds.
    float kind = weather.g;
    float top = mix(0.30, 1.0, kind) * (0.6 + 0.4 * cov);
    float profile = smoothstep(0.0, 0.18, heightFraction) * (1.0 - smoothstep(top * 0.55, top, heightFraction));
    if (profile <= 0.0) return 0.0;
    // The shape, at a scale where a texel is twenty metres and the tile
    // a kilometre and a half, blown along by the wind.
    vec3 sp = p * (1.0 / 1500.0) + vec3(t * 0.004, 0.0, t * 0.0015);
    vec4 shape = texture(cloudShape, sp);
    float fbm = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float base = remap(shape.r, fbm - 1.0, 1.0, 0.0, 1.0) * profile;
    // The cover carves the base: full cover keeps all of it, a little cover
    // only the cores.
    float d = remap(base, 1.0 - cov, 1.0, 0.0, 1.0) * cov;
    if (d <= 0.0) return 0.0;
    // Detail: the Worley fractal at four times the scale, eating the edges;
    // inverted near the base, where a cloud is wisps, upright above, where
    // it billows.
    vec4 fine = texture(cloudShape, sp * 6.0 + vec3(0.37, 0.21, 0.63) + vec3(t * 0.01, 0.0, 0.0));
    float detail = fine.g * 0.625 + fine.b * 0.25 + fine.a * 0.125;
    float erosion = mix(detail, 1.0 - detail, clamp(heightFraction * 6.0, 0.0, 1.0));
    d = remap(d, erosion * 0.4, 1.0, 0.0, 1.0);
    return clamp(d, 0.0, 1.0);
}

// A screen-space dither: each pixel starts its march a different fraction
// of a step into the layer, so the steps do not line up into stripes
// across the sky. Interleaved gradient noise, which tiles finely enough
// to read as grain rather than as a pattern.
float cloudDither(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Henyey-Greenstein: how much light a cloud throws toward the eye by the
// angle between the sun and the view, which is the silver lining on a
// cloud in front of the sun and the flat grey of one beside it.
float cloudPhase(float cosAngle, float g) {
    float gg = g * g;
    return (1.0 - gg) / (4.0 * 3.14159265 * pow(1.0 + gg - 2.0 * g * cosAngle, 1.5));
}

// How much cloud stands between a point and the sun: a short march toward
// it, six samples spaced wider as they go, each a texture fetch.
float cloudSunShade(vec3 p, vec3 sun, float cover, float t) {
    float shade = 0.0;
    float step = (CLOUD_TOP - CLOUD_BASE) * 0.02;
    float hf;
    for (int k = 0; k < 6; k++) {
        p += sun * step;
        shade += cloudDensity(p, cover, t, hf) * step;
        step *= 1.6;
    }
    return shade;
}

// Clouds along a view ray from the ground: the layer marched front to back
// until the sky behind is hidden. The march strides through empty air and
// steps finely once it is inside a cloud, so a clear stretch of sky costs
// a handful of fetches and a cloud's edge is found where it is. Each sample
// is lit by the sun through the cloud above it (Beer's law in three octaves
// of extinction, the way light that has scattered a few times still gets
// through, so the base of a cloud is grey and not black), brightened at
// the edge by the powder term and toward the sun by the phase, and lit
// from the sky by an ambient that darkens down the layer. The far clouds
// sit in the haze: every sample is mixed toward the sky by its distance
// and thinned by it.
vec4 cloudsAlong(vec3 dir, vec3 sky, float cover, float t, float dither) {
    if (cover <= 0.0 || dir.y <= 0.03) return vec4(0.0);
    float t0 = CLOUD_BASE / dir.y;
    // The march ends twelve kilometres into the layer: past that the layer
    // is haze under the horizon fade, and a ray along the horizon would
    // otherwise stride the layer's hundred kilometres in steps wider than
    // a cloud.
    float t1 = min(CLOUD_TOP / dir.y, t0 + 12000.0);
    float slant = 1.0 - clamp(dir.y, 0.0, 1.0);
    float fine = (t1 - t0) / (32.0 + 64.0 * slant * slant);
    float coarse = fine * 3.0;
    vec3 sun = normalize(cloudSun);
    vec3 sunLight = cloudSunColor * 1.0;
    // Forward scattering for the silver lining, a little back scattering
    // for the bright face of a cloud lit from behind the eye; scaled so a
    // cloud beside the sun is lit as if it scattered evenly.
    float cosAngle = dot(dir, sun);
    float phase = mix(cloudPhase(cosAngle, 0.5), cloudPhase(cosAngle, -0.25), 0.30) * 12.566;
    vec3 colour = vec3(0.0);
    float alpha = 0.0;
    float ray = t0 + fine * dither;
    float dt = coarse;
    int empties = 0;
    for (int i = 0; i < 160; i++) {
        if (ray > t1) break;
        vec3 p = vec3(viewPos.x, 0.0, viewPos.z) + dir * ray;
        float hf;
        float d = cloudDensity(p, cover, t, hf);
        if (d > 0.002) {
            if (dt > fine) {
                // Struck cloud on a coarse stride: back up to where the
                // stride began and walk it finely.
                ray -= dt - fine;
                dt = fine;
                empties = 0;
                continue;
            }
            empties = 0;
            float shade = cloudSunShade(p, sun, cover, t);
            // The sun through the cloud above: three octaves of Beer's law,
            // the way light that has scattered a few times still gets
            // through, so a base is grey and not black.
            float tau = shade * CLOUD_SIGMA;
            float beer = 0.50 * exp(-tau) + 0.32 * exp(-tau * 0.25) + 0.18 * exp(-tau * 0.0625);
            // Darker where the cloud is thin against the light: the powder
            // effect, the crevices of a cumulus reading darker than its
            // domes.
            float powder = 1.0 - 0.7 * exp(-d * 8.0);
            // The sky's light, from the blue above and the ground below,
            // dimmed down the layer and inside the cloud.
            // Lifted toward white by day, when the ground and the air
            // scatter light up into the layer, and not at night, when a
            // cloud lit only by a dark sky is a darker patch of it.
            float skyLuma = dot(sky, vec3(0.299, 0.587, 0.114));
            vec3 skyLight = mix(sky, vec3(1.0), 0.35 * clamp(skyLuma * 2.5, 0.0, 1.0));
            vec3 ambient = skyLight * mix(0.30, 0.50, hf) * (0.6 + 0.4 * exp(-tau * 0.5));
            vec3 c = sunLight * beer * powder * phase + ambient;
            // Rolled off, since the sky is drawn without the scene's tone
            // curve and a lit crown would otherwise clip to paper white.
            c = c / (1.0 + c * 0.45);
            float haze = 1.0 - exp(-ray * 0.00005);
            c = mix(c, sky, haze);
            float a = (1.0 - exp(-d * dt * CLOUD_SIGMA)) * (1.0 - 0.8 * haze);
            colour += c * a * (1.0 - alpha);
            alpha += a * (1.0 - alpha);
            if (alpha > 0.98) break;
        } else {
            empties++;
            if (empties > 3) dt = coarse;
        }
        ray += dt;
    }
    // Gone at the horizon, where the layer is a hundred kilometres deep and
    // the haze the sky is painted with has swallowed it.
    float horizon = smoothstep(0.04, 0.32, dir.y);
    return vec4(colour, alpha * horizon);
}

// A clear sky from the sun's position alone. Blue overhead and pale at the
// horizon by day; the horizon goes gold and then red as the sun nears it,
// most on the sun's side; a glow around the sun from the air's forward
// scattering, and the disc itself; a deep blue-grey once the sun is under,
// with the moon's worth of light the scenes keep. Not a measurement of an
// atmosphere -- the shapes a clear sky makes, at the cost of a gradient.
vec3 proceduralSky(vec3 dir, vec3 sun, vec3 sunColor) {
    float elevation = sun.y;
    // Day lasts until the sun is well under: the sky at sunset is still
    // bright, and goes dark through the twilight after.
    float day = smoothstep(-0.14, 0.06, elevation);
    float low = 1.0 - smoothstep(-0.05, 0.35, elevation);
    vec3 zenithDay = vec3(0.20, 0.42, 0.85);
    vec3 zenithDusk = vec3(0.16, 0.20, 0.45);
    vec3 zenithNight = vec3(0.02, 0.03, 0.08);
    vec3 horizonDay = vec3(0.72, 0.82, 0.93);
    vec3 horizonDusk = vec3(1.0, 0.52, 0.22);
    vec3 horizonNight = vec3(0.06, 0.07, 0.13);
    // The dusk colour sits on the sun's side of the sky and fades out
    // opposite it, where the horizon stays a cooler mauve.
    float toward = 0.5 + 0.5 * dot(normalize(vec2(dir.x, dir.z) + 1e-5), normalize(vec2(sun.x, sun.z) + 1e-5));
    vec3 horizon = mix(horizonDay, horizonDusk, low * (0.25 + 0.75 * toward * toward));
    horizon = mix(horizonNight, horizon, day);
    vec3 zenith = mix(zenithNight, mix(zenithDay, zenithDusk, low), day);
    float up = clamp(dir.y, 0.0, 1.0);
    vec3 sky = mix(horizon, zenith, pow(up, 0.55));
    // Below the horizon: the haze the ground would be seen through.
    if (dir.y < 0.0) sky = mix(horizon, horizon * 0.6, clamp(-dir.y * 4.0, 0.0, 1.0));
    // The glow and the disc.
    float cosAngle = dot(dir, sun);
    float glow = pow(max(cosAngle, 0.0), 4.0) * 0.16 + pow(max(cosAngle, 0.0), 40.0) * 0.5;
    sky += sunColor * glow * (0.3 + 0.7 * day) * (1.0 + low * 1.6);
    float disc = smoothstep(0.9993, 0.9997, cosAngle);
    sky += sunColor * disc * (0.6 + 1.4 * day);
    return sky;
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
    if (skyProcedural == 1) sky = proceduralSky(dir, normalize(cloudSun), cloudSunColor);
    if (skyOvercast > 0.0) {
        float luma = dot(sky, vec3(0.299, 0.587, 0.114));
        sky = mix(sky, skyOvercastColor * (0.35 + 0.65 * luma), clamp(skyOvercast, 0.0, 1.0));
    }
    vec4 clouds = cloudsAlong(dir, sky, cloudCover, cloudTime, cloudDither(gl_FragCoord.xy));
    sky = sky * (1.0 - clouds.a) + clouds.rgb;
    FragColor = vec4(sky, 1.0);
}
