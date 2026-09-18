#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D color_tex;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;

layout(push_constant) uniform Push {
    float focus_distance;
    float aperture;
} pc;

void main() {
    float depth = texture(depth_tex, v_uv).r;
    float coc = abs(depth - pc.focus_distance) * pc.aperture; // Circle of Confusion
    
    vec4 sum = vec4(0.0);
    float total_weight = 0.0;
    
    // Basit bir Box Blur/Bokeh kopyası (performans için az sample)
    vec2 texel_size = 1.0 / vec2(textureSize(color_tex, 0));
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(x, y) * texel_size * coc;
            float weight = 1.0;
            sum += texture(color_tex, v_uv + offset) * weight;
            total_weight += weight;
        }
    }
    out_color = sum / total_weight;
}
