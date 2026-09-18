#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D depth_tex;
layout(set = 0, binding = 1) uniform sampler2D normal_tex;
layout(set = 0, binding = 2) uniform sampler2D noise_tex;

layout(push_constant) uniform Push {
    float radius;
    float intensity;
} pc;

const int kernel_size = 16;
vec3 kernel[kernel_size] = vec3[](
    vec3(0.1, 0.2, 0.3), vec3(-0.1, 0.4, 0.1), vec3(0.5, -0.2, 0.4), vec3(-0.6, 0.1, 0.2),
    vec3(0.2, 0.7, -0.1), vec3(-0.3, -0.5, 0.6), vec3(0.8, 0.1, -0.3), vec3(-0.4, 0.6, 0.5),
    vec3(0.1, -0.8, 0.2), vec3(-0.7, -0.2, 0.1), vec3(0.4, 0.5, -0.6), vec3(-0.2, 0.3, 0.8),
    vec3(0.9, -0.1, 0.2), vec3(-0.1, -0.9, 0.3), vec3(0.3, 0.4, 0.7), vec3(-0.5, -0.4, 0.6)
);

void main() {
    float depth = texture(depth_tex, v_uv).r;
    vec3 normal = normalize(texture(normal_tex, v_uv).xyz * 2.0 - 1.0);
    vec2 noise_scale = vec2(textureSize(depth_tex, 0)) / 4.0;
    vec3 random_vec = normalize(texture(noise_tex, v_uv * noise_scale).xyz * 2.0 - 1.0);
    
    vec3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);
    
    float occlusion = 0.0;
    for(int i = 0; i < kernel_size; ++i) {
        vec3 sample_pos = tbn * kernel[i];
        sample_pos = sample_pos * pc.radius + vec3(v_uv, depth);
        
        float sample_depth = texture(depth_tex, sample_pos.xy).r;
        float range_check = smoothstep(0.0, 1.0, pc.radius / abs(depth - sample_depth));
        occlusion += (sample_depth >= sample_pos.z + 0.025 ? 1.0 : 0.0) * range_check;
    }
    occlusion = 1.0 - (occlusion / float(kernel_size)) * pc.intensity;
    out_color = vec4(vec3(occlusion), 1.0);
}
