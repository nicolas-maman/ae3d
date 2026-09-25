#version 450

#ifdef VULKAN
#define VARYING(n) layout(location = n)
layout(push_constant) uniform OverlayFrame {
    vec4 overlayScreen;
    vec4 overlayStyle;
};
layout(set = 0, binding = 0) uniform sampler2D glyphAtlas;
#else
#define VARYING(n)
uniform vec4 overlayScreen;
uniform vec4 overlayStyle;
uniform sampler2D glyphAtlas;
#endif

VARYING(0) in vec2 atlasCoord;
VARYING(1) in vec4 colour;
VARYING(2) in float edge;

layout(location = 0) out vec4 FragColor;

void main() {
    // The field's value, and how much it changes across one screen pixel:
    // the edge is blended over that pixel whatever size the glyph is drawn
    // at, which is what keeps text sharp at 12 px and at 120 from the one
    // atlas. Read for a rectangle too, outside any branch, so the
    // derivative is taken where every pixel of the quad takes it.
    float d = texture(glyphAtlas, atlasCoord).r;
    float w = max(fwidth(d) * 0.5, 1.0 / 255.0);
    float ink = edge < 0.0 ? 1.0 : smoothstep(edge - w, edge + w, d);
    vec3 rgb = colour.rgb;
    // A target that encodes what it is given (an _SRGB swapchain) gets the
    // colour decoded first, so it shows as the display value asked for.
    if (overlayStyle.x > 0.5) rgb = pow(rgb, vec3(2.2));
    FragColor = vec4(rgb, colour.a * ink);
}
