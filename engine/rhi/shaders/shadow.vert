#version 450
// Golge haritasi: yalniz derinlik. Ayni vertex duzeni (normal kullanilmaz).
layout(location = 0) in vec3 in_pos;
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
layout(push_constant) uniform Push { mat4 model; vec4 color; uvec4 skin; } pc; // skin.y = kademe
void main() { gl_Position = u.light_viewproj[pc.skin.y] * pc.model * vec4(in_pos, 1.0); }
