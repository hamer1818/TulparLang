#version 450
layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_color;
layout(location = 3) in vec3 v_world_pos;

// G-Buffer Çıkışları
layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_pbr; // r: metallic, g: roughness, b: ao

void main() {
    out_albedo = v_color; // Albedo texture or vertex color
    out_normal = vec4(normalize(v_normal), 1.0);
    out_pbr = vec4(0.0, 0.5, 1.0, 1.0); // Varsayılan PBR değerleri
}
