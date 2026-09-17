#version 450
// Golge atlasinin DOLAYLI yolu: shadow.vert ile ayni, model cizim SSBO'sundan.
// skin.y = kademe (atlas tile'i), skin.z = gorunurluk listesi tabani.
layout(location = 0) in vec3 in_pos;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj[3];
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 cascade_params;
} u;
struct DrawItem {
  mat4 model;
  vec4 color;
  vec4 sphere;
  uvec4 misc;
};
layout(std430, set = 0, binding = 5) readonly buffer Draws { DrawItem d[]; } u_draws;
layout(std430, set = 0, binding = 6) readonly buffer Visible { uint v[]; } u_vis;
layout(push_constant) uniform Push { mat4 model; vec4 color; uvec4 skin; } pc;
void main() {
  DrawItem D = u_draws.d[u_vis.v[pc.skin.z + uint(gl_InstanceIndex)]];
  gl_Position = u.light_viewproj[pc.skin.y] * D.model * vec4(in_pos, 1.0);
}
