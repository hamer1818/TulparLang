#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D color_tex;
layout(set = 0, binding = 1) uniform sampler2D velocity_tex;

layout(push_constant) uniform Push {
    float amount;
} pc;

void main() {
    // Motion Blur mock shader
    vec2 velocity = texture(velocity_tex, v_uv).xy;
    vec4 result = texture(color_tex, v_uv);
    out_color = result;
}
