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
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D poseBank;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in mat4 instanceModel; // per-instance placement (3,4,5,6)
layout(location = 7) in vec3 instanceColor; // per-instance tint
layout(location = 8) in vec4 inJoints;      // the four bones a vertex hangs off
layout(location = 9) in vec4 inWeights;     // how much of each
layout(location = 10) in float inOcclusion;
layout(location = 11) in float instancePhase; // where in the walk this one is, 0..1





// The baked walk: bones*4 texels wide (four texels a bone matrix), `frames`
// rows tall. Sampled by exact texel, never filtered between poses.



// The crowd's far tier as impostors: the figure baked into an atlas of
// impostorCols views around it by impostorRows frames of its walk, and each
// instance an upright quad turned to the camera, impostorWidth by
// impostorHeight metres, showing the cell for the angle the camera sees it
// from and the frame its phase is at. Twenty thousand figures at a hundred
// and sixty-eight triangles are three million; as impostors they are forty
// thousand, and the horde can be twenty times the size.






// Last frame, for the motion vectors. A figure's previous slot in the
// stream is not its own -- the sort reorders every frame -- so where it
// was is worked out: it walked crowdTravel metres along its facing since
// last frame, and its walk was crowdPhaseStep earlier in the cycle.




layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 FragPos;
layout(location = 3) out vec3 InstanceColor;
layout(location = 4) out vec4 FragPosLightSpace;
layout(location = 5) out float Occlusion;
layout(location = 6) out vec4 ClipNow;
layout(location = 7) out vec4 ClipPrev;

// The bone's matrix at a frame, read as its four columns from the bank.
mat4 boneAt(int bone, int frame) {
    int x = bone * 4;
    return mat4(texelFetch(poseBank, ivec2(x + 0, frame), 0),
                texelFetch(poseBank, ivec2(x + 1, frame), 0),
                texelFetch(poseBank, ivec2(x + 2, frame), 0),
                texelFetch(poseBank, ivec2(x + 3, frame), 0));
}

// The vertex's four-bone skinning matrix at one baked frame.
mat4 skinAt(int frame) {
    return inWeights.x * boneAt(int(inJoints.x), frame)
         + inWeights.y * boneAt(int(inJoints.y), frame)
         + inWeights.z * boneAt(int(inJoints.z), frame)
         + inWeights.w * boneAt(int(inJoints.w), frame);
}

// The vertex's skinning matrix at a phase of the walk. The phase lands
// between two baked poses; blend them, so the walk is continuous instead
// of snapping frame to frame. The clip loops, so the frame after the last
// is the first.
mat4 skinAtPhase(float phase) {
    float fpos = fract(phase) * float(poseBankFrames);
    int frame0 = int(floor(fpos));
    float blend = fpos - float(frame0);
    if (frame0 < 0) { frame0 = 0; blend = 0.0; }
    if (frame0 >= poseBankFrames) { frame0 = poseBankFrames - 1; blend = 0.0; }
    int frame1 = frame0 + 1;
    if (frame1 >= poseBankFrames) { frame1 = 0; }
    return skinAt(frame0) * (1.0 - blend) + skinAt(frame1) * blend;
}

void main() {
    Occlusion = inOcclusion;
    mat4 modelMatrix = model * instanceModel;
    // The way the figure faces, which is the way it walks.
    vec3 walking = normalize(vec3(modelMatrix[0].x, 0.0, modelMatrix[0].z));

    if (impostor) {
        // Where the figure stands and which way it faces: the instance
        // matrix's origin and its local +X, on the ground plane -- the
        // crowd's yaw is measured from +X, and a figure walking that way is
        // at yaw zero, so the bake's first column is the figure seen from
        // its own +X.
        vec3 origin = vec3(modelMatrix[3]);
        vec3 facing = normalize(vec3(modelMatrix[0].x, 0.0, modelMatrix[0].z));
        vec3 toEye = viewPos - origin;
        vec3 level = normalize(vec3(toEye.x, 0.0, toEye.z));
        // The angle the camera sees the figure from, measured around its
        // facing: the cell column, with the bake's first view from the front.
        float angle = atan(dot(level, vec3(-facing.z, 0.0, facing.x)), dot(level, facing));
        float turn = fract(angle / 6.28318530718 + 1.0);
        int column = int(floor(turn * float(impostorCols) + 0.5)) % impostorCols;
        int row = int(floor(fract(instancePhase) * float(impostorRows))) % impostorRows;
        // An upright quad facing the eye: the mesh's x across it, y up it.
        vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), level));
        vec3 world = origin + right * (inPosition.x * impostorWidth) + vec3(0.0, inPosition.y * impostorHeight, 0.0);
        FragPos = world;
        // The picture's normals were baked in the figure's own frame; the
        // fragment turns them into the world by the facing, carried here.
        Normal = facing;
        // The cell in the atlas. The atlas is written with row 0 at its top
        // and loaded flipped, as every texture is, so a row counts down from
        // the top of texture space.
        fragTexCoord = vec2((float(column) + inTexCoord.x) / float(impostorCols),
                            1.0 - (float(row) + (1.0 - inTexCoord.y)) / float(impostorRows));
        InstanceColor = instanceColor;
        FragPosLightSpace = lightSpaceMatrix * vec4(world, 1.0);
        ClipNow = viewProjection * vec4(world, 1.0);
        ClipPrev = prevViewProjection * vec4(world - walking * crowdTravel, 1.0);
        gl_Position = ClipNow;
        return;
    }

    mat4 skin = skinAtPhase(instancePhase);
    vec4 posed = skin * vec4(inPosition, 1.0);
    vec3 posedNormal = mat3(skin) * inNormal;

    FragPos = vec3(modelMatrix * posed);
    Normal = normalize(mat3(modelMatrix) * posedNormal);
    fragTexCoord = inTexCoord;
    InstanceColor = instanceColor;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    ClipNow = viewProjection * modelMatrix * posed;
    // Last frame: the same vertex a step back in the walk, the figure a
    // step back along its way.
    vec4 posedPrev = skinAtPhase(instancePhase - crowdPhaseStep) * vec4(inPosition, 1.0);
    vec4 worldPrev = modelMatrix * posedPrev;
    worldPrev.xyz -= walking * crowdTravel;
    ClipPrev = prevViewProjection * worldPrev;
    gl_Position = ClipNow;
}
