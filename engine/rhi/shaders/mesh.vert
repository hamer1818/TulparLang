#version 450
// Forward temel: pozisyon + normal; kare UBO (viewproj, isik, golge), push
// sabiti (model, renk). Golge icin isik uzayindaki konum da tasinir.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_nrm;
layout(location = 2) in vec2 in_uv;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;  // x: 1/boyut, y: sabit egilim, z: golge acik mi, w: normal kaydirma (dunya)
  vec4 cluster_params; // x: dilim olcegi, y: dilim sapmasi, z: tile genisligi px, w: tile yuksekligi px
  uvec4 cluster_grid;  // x, y, z, isik sayisi
} u;
layout(push_constant) uniform Push { mat4 model; vec4 color; } pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec4 v_light_pos;
layout(location = 3) out vec2 v_uv;
layout(location = 4) out vec3 v_world;
layout(location = 5) out float v_viewz;
void main() {
  vec4 world = pc.model * vec4(in_pos, 1.0);
  gl_Position = u.viewproj * world;
  vec3 wn = normalize(mat3(pc.model) * in_nrm);
  v_nrm = wn;
  v_color = pc.color.rgb;
  v_uv = in_uv;
  v_world = world.xyz;
  v_viewz = -(u.view * world).z; // ileri derinlik (>0), kume dilimi icin
  // Golge aramasi normal boyunca DUNYA BIRIMI kadar kaydirilir. Boru hattinin
  // depthBias'i sürücüye bagli birimdedir (Mali'de golgeyi tamamen yok etti);
  // bu kaydirma her cihazda ayni anlama gelir. shadow_params.w = metre.
  v_light_pos = u.light_viewproj * vec4(world.xyz + wn * u.shadow_params.w, 1.0);
}
