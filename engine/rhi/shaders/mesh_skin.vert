#version 450
// Iskeletli (GPU skinning) mesh: mesh.vert'in 4 eklem/4 agirlik surumu. Eklem
// matrisleri kare basina SSBO'da (set 0, binding 4); push sabiti skin.x = bu
// cizimin matris ofseti. Sonuc mesh.frag'a ayni varyantlarla gider.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_nrm_oct; // oktahedral SNORM16x2
layout(location = 2) in vec2 in_uv;
layout(location = 3) in uvec4 in_joints;
layout(location = 4) in vec4 in_weights; // UNORM16, toplam 1
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj[3]; // golge kademeleri (Renderer::kMaxCascades)
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 cascade_params; // x: kademe sayisi, y: 1/kademe, z: atlas texel x, w: atlas texel y
} u;
layout(std430, set = 0, binding = 4) readonly buffer Skin { mat4 m[]; } u_skin;
layout(push_constant) uniform Push { mat4 model; vec4 color; uvec4 skin; } pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_world;
layout(location = 4) out float v_viewz;
// Oktahedral normal cozme (CPU tarafi: Renderer::oct_encode). snorm16 cifti
// birim kureye geri acilir; hata ~0.1 derece.
vec3 oct_decode(vec2 e) {
  vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
  float t = max(-n.z, 0.0);
  n.x += n.x >= 0.0 ? -t : t;
  n.y += n.y >= 0.0 ? -t : t;
  return normalize(n);
}
void main() {
  mat4 skin = in_weights.x * u_skin.m[pc.skin.x + in_joints.x] + in_weights.y * u_skin.m[pc.skin.x + in_joints.y] +
              in_weights.z * u_skin.m[pc.skin.x + in_joints.z] + in_weights.w * u_skin.m[pc.skin.x + in_joints.w];
  mat4 ms = pc.model * skin;
  vec4 world = ms * vec4(in_pos, 1.0);
  gl_Position = u.viewproj * world;
  vec3 wn = normalize(mat3(ms) * oct_decode(in_nrm_oct));
  v_nrm = wn;
  v_color = pc.color.rgb;
  v_uv = in_uv;
  v_world = world.xyz;
  v_viewz = -(u.view * world).z;
}
