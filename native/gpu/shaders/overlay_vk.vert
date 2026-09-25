#version 450

#ifdef VULKAN
#define VARYING(n) layout(location = n)
layout(push_constant) uniform OverlayFrame {
    vec4 overlayScreen;
    vec4 overlayStyle;
};
#else
#define VARYING(n)
uniform vec4 overlayScreen;
uniform vec4 overlayStyle;
#endif

layout(location = 0) in vec2 inPixel;
layout(location = 1) in vec2 inAtlas;
layout(location = 2) in vec4 inColour;
layout(location = 3) in float inEdge;

VARYING(0) out vec2 atlasCoord;
VARYING(1) out vec4 colour;
VARYING(2) out float edge;

void main() {
    // Pixels, y down from the top-left, to clip space: overlayScreen is
    // (2/width, -2/height, -1, 1) on OpenGL, whose clip y is up, and
    // (2/width, 2/height, -1, -1) on Vulkan, whose clip y is down.
    gl_Position = vec4(inPixel * overlayScreen.xy + overlayScreen.zw, 0.0, 1.0);
    atlasCoord = inAtlas;
    colour = inColour;
    edge = inEdge;
}
