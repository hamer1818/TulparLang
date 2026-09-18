#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D color_tex;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;
layout(set = 0, binding = 2) uniform sampler2D normal_tex;

layout(push_constant) uniform Push {
    mat4 inv_view_proj;
    mat4 view_proj;
    float step_size;
    float max_steps;
} pc;

void main() {
    // Screen Space Reflections (SSR) mock shader
    vec4 color = texture(color_tex, v_uv);
    out_color = color; // Asil raymarching mantigi eklenecek
}
