#version 450

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_uv;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 base_color;
    vec4 light;
} push;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_normal);
    float lambert = max(dot(normal, normalize(push.light.xyz)), 0.0);
    float ambient = push.light.w;
    vec3 lit = push.base_color.rgb * (ambient + (1.0 - ambient) * lambert);
    out_color = vec4(lit, push.base_color.a);
}
