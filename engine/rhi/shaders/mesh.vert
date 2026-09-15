#version 450
// Forward temel: pozisyon + normal; kare UBO (viewproj, isik, golge), push
// sabiti (model, renk). Golge icin isik uzayindaki konum da tasinir.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_nrm_oct; // oktahedral SNORM16x2
layout(location = 2) in vec2 in_uv;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj[3]; // golge kademeleri (Renderer::kMaxCascades)
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;  // x: 1/boyut, y: sabit egilim, z: golge acik mi, w: normal kaydirma (dunya)
  vec4 cluster_params; // x: dilim olcegi, y: dilim sapmasi, z: tile genisligi px, w: tile yuksekligi px
  uvec4 cluster_grid;  // x, y, z, isik sayisi
  vec4 cascade_params; // x: kademe sayisi, y: 1/kademe, z: atlas texel x, w: atlas texel y
} u;
layout(push_constant) uniform Push { mat4 model; vec4 color; } pc;
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
  vec4 world = pc.model * vec4(in_pos, 1.0);
  gl_Position = u.viewproj * world;
  vec3 wn = normalize(mat3(pc.model) * oct_decode(in_nrm_oct));
  v_nrm = wn;
  v_color = pc.color.rgb;
  v_uv = in_uv;
  v_world = world.xyz;
  v_viewz = -(u.view * world).z; // ileri derinlik (>0), kume dilimi icin
  // Golge aramasi fragment'ta yapilir: KADEME secimi piksel basinadir (kapsayan
  // ilk kademe kazanir), tek bir isik-uzayi varyani yetmez.
}
