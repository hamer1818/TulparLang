#version 450
// Vertex Animation Texture (VAT) — karmasik/simule edilmis animasyonu (kalabalik
// karakter, yikim, kumas) CPU/iskelet maliyeti OLMADAN oynatir: pozisyon+normal
// build zamaninda bir dokuya bake edilir (satir=zaman, sutun=vertex id),
// runtime'da shader sadece OKUR. Mobilde kanitlanmis devrimsel teknik: Space
// Ape Games 4.900 karakteri TEK cizimde cevirdi. GLSL referansi: OpenVAT'in
// Godot sablonlari (openvat.org) ayni kodlama sozlesmesini kullanir (pozisyon
// [0,1] dokuya SNORM-benzeri kodlanir, shader'da [-boyut,+boyut]'a geri acilir).
//
// Bu YENI bir dosya (_spv.h yok, glslc gerekir). Kablolamak icin:
// (1) offline bir bake araci (Houdini/ozel; engine_texpack'e benzer bir
//     `engine_vatbake` eklenebilir) pozisyon+normal dokularini uretmeli,
// (2) mesh'in ikinci UV kanalinda vertex-id (0..1 normalize) tasinmali,
// (3) Material/RHI'ye bu iki YENI sampler icin binding eklenmeli (set 1,
//     binding 1/2 — su an yalniz binding 0/albedo var),
// (4) mesh.frag ile PAIR OLUR (ayni varying seti) — PBR/sis/tonemap bedava.
layout(location = 0) in vec3 in_pos;    // KULLANILMAZ (gercek pozisyon dokudan); bind-pozu/AABB kulling icin saklanir
layout(location = 1) in vec3 in_nrm;    // ayni sekilde yedek
layout(location = 2) in vec2 in_uv;     // gercek malzeme UV'si (albedo icin)
layout(location = 3) in vec2 in_vat_uv; // x: vertex id (0..1 normalize), y: rezerve
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 fog_params; // w: zaman (s) — animasyon fazi bunu okur
} u;
layout(set = 1, binding = 1) uniform sampler2D u_vat_pos; // satir=zaman(0..1 dongu), sutun=vertex id; RGB=SNORM-benzeri kodlu pozisyon
layout(set = 1, binding = 2) uniform sampler2D u_vat_nrm; // ayni duzen; RGB = normal (0..1 kodlu, *2-1 ile acilir)
layout(push_constant) uniform Push {
  mat4 model;
  vec4 color;
  vec4 vat; // x: klip suresi (s), y: pozisyon bbox yari-boyutu (kup varsayimi, tek skaler), z: faz ofseti (s), w: rezerve
} pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec4 v_light_pos;
layout(location = 3) out vec2 v_uv;
layout(location = 4) out vec3 v_world;
layout(location = 5) out float v_viewz;
layout(location = 6) out float v_roughness;
layout(location = 7) out float v_metallic;

void main() {
  float dur = max(pc.vat.x, 1e-4);
  float t = mod(u.fog_params.w + pc.vat.z, dur) / dur; // 0..1 donen faz, klip suresine gore
  vec2 vat_uv = vec2(in_vat_uv.x, t);
  // Bake araci pozisyonu mesh'in bounding box'ina normalize etmis olmali (Bolum
  // 0B'nin "vertex format" karariyla ayni SNORM+bbox mantigi — sayisal hassasiyet
  // orijinden uzaklastikca degil, bbox icinde tam kalir).
  vec3 local_pos = (texture(u_vat_pos, vat_uv).xyz * 2.0 - 1.0) * pc.vat.y;
  vec3 local_nrm = normalize(texture(u_vat_nrm, vat_uv).xyz * 2.0 - 1.0);
  vec4 world = pc.model * vec4(local_pos, 1.0);
  gl_Position = u.viewproj * world;
  vec3 wn = normalize(mat3(pc.model) * local_nrm);
  v_nrm = wn;
  v_color = pc.color.rgb;
  v_roughness = 0.6;
  v_metallic = 0.0;
  v_uv = in_uv;
  v_world = world.xyz;
  v_viewz = -(u.view * world).z;
  v_light_pos = u.light_viewproj * vec4(world.xyz + wn * u.shadow_params.w, 1.0);
}
