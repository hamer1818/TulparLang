#version 450
// Isiksiz/yayici (unlit/emissive) malzeme — UI'nin 3B sahne icindeki
// isaretleyicileri, VFX parlaklik kaynaklari, hata ayiklama gizmolari, "isik
// hesaba katilmasin" istenen her yer icin. mesh.vert / mesh_skin.vert ile
// DOGRUDAN eslesir (ayni tam varying seti okunur) — ayri bir vertex shader'a
// gerek yok. Her motorda bulunan en temel, en ucuz "shading model" budur.
//
// DERLEME/KABLOLA: bu YENI bir dosya (_spv.h yok). Kablolamak icin toon.frag
// ile ayni yol: mesh.vert'e eslesen ikinci bir renk-pipeline'i (pipe_unlit_)
// olustur, Material'a shading_model ekle.
layout(location = 0) in vec3 v_nrm;       // kullanilmaz (arayuz uyumu icin var)
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec4 v_light_pos; // kullanilmaz
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec3 v_world;     // sis icin kullanilir
layout(location = 5) in float v_viewz;    // kullanilmaz
layout(location = 6) in float v_roughness; // kullanilmaz
layout(location = 7) in float v_metallic;  // kullanilmaz
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 fog_params; // x: yogunluk, y: yukseklik sonumu, z: pozlama, w: zaman
  vec4 fog_color;
} u;
layout(set = 1, binding = 0) uniform sampler2D u_albedo;
layout(location = 0) out vec4 o_color;

// mesh.frag'taki KANITLI teknik (Is 4 turetimi): view = R*T(-eye) => eye = -R^T*t.
vec3 camera_world_pos() { return -transpose(mat3(u.view)) * u.view[3].xyz; }

void main() {
  vec3 c = texture(u_albedo, v_uv).rgb * v_color; // dogrudan yayici renk, aydinlatma hesaplanmaz
  // Is 6: sis (dogrusal uzayda, tonemap'ten once — mesh.frag ile ayni sozlesme).
  // Yayici nesneler icin bile uygulaniyor: uzaktaki bir VFX isareti de sise
  // gomulmeli, aksi halde sis icindeki her seyden daha "kesin" gorunur.
  if (u.fog_params.x > 0.0) {
    float dist = length(v_world - camera_world_pos());
    float fog_amt = clamp((1.0 - exp(-u.fog_params.x * dist)) * exp(-u.fog_params.y * v_world.y), 0.0, 1.0);
    c = mix(c, u.fog_color.rgb, fog_amt);
  }
  if (u.light_dir.w > 0.5) {
    c = clamp(c, 0.0, 1.0);
    c = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
  }
  o_color = vec4(c, 1.0);
}
