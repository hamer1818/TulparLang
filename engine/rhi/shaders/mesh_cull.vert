#version 450
// DOLAYLI (indirect) yolun vertex shader'i. mesh.vert ile AYNI aritmetik; tek
// fark model/renk'in nereden geldigi: push sabiti yerine cizim SSBO'su, hangi
// kayit oldugunu cull gecisinin sikistirdigi gorunurluk listesi soyler.
// (Ayri shader olmasinin sebebi: mesh.vert'e dokunulmasin — cull KAPALIYKEN
// bugunku yol bit bit ayni SPIR-V ile kossun.)
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_nrm_oct; // oktahedral SNORM16x2
layout(location = 2) in vec2 in_uv;
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
// skin.z = bu kumenin gorunurluk listesi tabani (frustum * cizim kapasitesi +
// kumenin ilk cizimi). gl_InstanceIndex 0'dan baslar: firstInstance 0 zorunlu.
layout(push_constant) uniform Push { mat4 model; vec4 color; uvec4 skin; } pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_world;
layout(location = 4) out float v_viewz;
vec3 oct_decode(vec2 e) {
  vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
  float t = max(-n.z, 0.0);
  n.x += n.x >= 0.0 ? -t : t;
  n.y += n.y >= 0.0 ? -t : t;
  return normalize(n);
}
void main() {
  DrawItem D = u_draws.d[u_vis.v[pc.skin.z + uint(gl_InstanceIndex)]];
  vec4 world = D.model * vec4(in_pos, 1.0);
  gl_Position = u.viewproj * world;
  vec3 wn = normalize(mat3(D.model) * oct_decode(in_nrm_oct));
  v_nrm = wn;
  v_color = D.color.rgb;
  v_uv = in_uv;
  v_world = world.xyz;
  v_viewz = -(u.view * world).z;
}
