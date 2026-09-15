#version 450
// Bitki ortusu (cim/yaprak/dal) GPU ruzgar sallanmasi — CPU/animasyon maliyeti
// SIFIR, statik mesh + instancing ile binlerce bitki tek cizimde hareket eder.
// Klasik teknik (Crysis/Unreal foliage'in temeli): sallanma miktari vertex
// basina bir agirlikle (kok=0, uc=1) olceklenir; GitHub'da bagimsiz
// uygulamalarda (orn. fremorie/wind — instanced grass/foliage, ruzgar
// sallanmasi vertex shader'da) ayni fikir tekrarlanir, motor-spesifik degil.
//
// Bu YENI bir dosya (_spv.h yok, glslc gerekir). Kablolamak icin:
// (1) FrameUbo.fog_params.w'yi "zaman (saniye)" olarak doldur (begin_frame'e
//     bir sayac eklenmeli — su an fog_params.w kullanilmiyor, 0),
// (2) Vertex duzenine bir "ruzgar agirligi" (float, 0..1) ekle — glTF vertex
//     color'unun bir kanalindan icerik hattinda doldurulabilir,
// (3) mesh.frag ile PAIR OLUR (asagidaki varying seti birebir eslesir) —
//     ayri bir fragment shader YAZMAYA gerek yok, PBR+sis+tonemap bedava gelir.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_nrm;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in float in_wind_weight; // 0 = govde/kok (sallanmaz), 1 = uc (tam sallanir)
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 fog_params; // w: zaman (s) — ruzgar fazi bunu okur
} u;
// Push, ana Push'un {model,color,pbr} on-ekiyle AYNI BAYT ARALIGINI kullanir,
// yalniz "pbr" alanini "wind" olarak yeniden yorumlar (bu shader'a ozel cizim
// yolu, ana Draw/record() akisindan BAGIMSIZ olabilir — renderer.hpp'deki
// "on-ek yeter" kuraliyla ayni mantik).
layout(push_constant) uniform Push {
  mat4 model;
  vec4 color;
  vec4 wind; // x: genlik (dunya birimi), y: frekans (Hz), z: dalga uzunlugu, w: ruzgar yonu (radyan, dunya XZ)
} pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec4 v_light_pos;
layout(location = 3) out vec2 v_uv;
layout(location = 4) out vec3 v_world;
layout(location = 5) out float v_viewz;
layout(location = 6) out float v_roughness; // foliage icin sabit (doku degil, Is 4'un malzeme-basina degeriyle ayni fikir)
layout(location = 7) out float v_metallic;

void main() {
  vec4 world = pc.model * vec4(in_pos, 1.0);
  vec2 wind_dir = vec2(cos(pc.wind.w), sin(pc.wind.w));
  // Faz, ruzgar yonundeki dunya konumuna gore kayar — komsu bitkiler AYNI ANDA
  // degil, bir DALGA halinde sallanir (gercekci "esinti" hissi; sabit fazli
  // sallanma "hepsi ayni anda titriyor" gibi yapay durur).
  float phase = dot(world.xz, wind_dir) / max(pc.wind.z, 0.001);
  float sway = sin(u.fog_params.w * pc.wind.y * 6.28318 + phase) * pc.wind.x;
  world.xz += wind_dir * sway * in_wind_weight;
  gl_Position = u.viewproj * world;
  vec3 wn = normalize(mat3(pc.model) * in_nrm);
  v_nrm = wn;
  v_color = pc.color.rgb;
  v_roughness = 0.85; // yaprak/cim: mat, parlak degil
  v_metallic = 0.0;
  v_uv = in_uv;
  v_world = world.xyz;
  v_viewz = -(u.view * world).z;
  v_light_pos = u.light_viewproj * vec4(world.xyz + wn * u.shadow_params.w, 1.0);
}
