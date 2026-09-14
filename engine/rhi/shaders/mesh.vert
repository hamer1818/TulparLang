#version 450
// Forward temel: pozisyon + normal; kare UBO (viewproj, isik), push sabiti (model, renk).
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_nrm;
layout(set = 0, binding = 0) uniform Frame { mat4 viewproj; vec4 light_dir; vec4 ambient; } u;
layout(push_constant) uniform Push { mat4 model; vec4 color; } pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_color;
void main() {
  gl_Position = u.viewproj * pc.model * vec4(in_pos, 1.0);
  v_nrm = mat3(pc.model) * in_nrm;
  v_color = pc.color.rgb;
}
