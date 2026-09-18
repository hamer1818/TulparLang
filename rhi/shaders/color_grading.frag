#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D color_tex;
layout(set = 0, binding = 1) uniform sampler3D lut_tex; // 3D Color LUT

layout(push_constant) uniform Push {
    float intensity;
} pc;

void main() {
    // Color Grading / LUT mock shader
    vec4 base_color = texture(color_tex, v_uv);
    vec3 graded_color = texture(lut_tex, base_color.rgb).rgb;
    out_color = vec4(mix(base_color.rgb, graded_color, pc.intensity), base_color.a);
}
