#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 base_color;
    vec4 light;
} push;

layout(location = 0) out vec3 frag_normal;
layout(location = 1) out vec2 frag_uv;

void main() {
    gl_Position = push.mvp * vec4(in_position, 1.0);
    frag_normal = in_normal;
    frag_uv = in_uv;
}
