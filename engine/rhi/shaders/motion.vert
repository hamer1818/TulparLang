#version 450
// EKRAN UZAYI HAREKET VEKTORU — vertex asamasi (Faz 5).
//
// gl_Position JITTER'LI matristen gelir: MV pikselleri renk pikselleriyle ayni
// yere otursun. Hareket vektorunun kendisi JITTER'SIZ iki matristen hesaplanir
// (bu karenin viewproj'u ve onceki karenin viewproj'u) — alt-piksel kaydirma
// farki MV'ye SIZMAMALI; yukseltici (upscaler) jitter'i zaten kendi bilir.
//
// Cizim basina veri (model + onceki model + reactive) SSBO'dan gelir: push
// sabitinin garantili 128 baytina iki mat4 tam oturuyor ve reactive'e yer
// kalmiyordu. Push yalniz ornek indeksini tasir.
layout(location = 0) in vec3 in_pos;
layout(set = 0, binding = 0) uniform Motion {
  mat4 viewproj_jit;
  mat4 viewproj;
  mat4 prev_viewproj;
  vec4 params; // xy: hedefin piksel olcusu, zw: ayrilmis
} u;
struct Inst {
  mat4 model;
  mat4 prev_model;
  vec4 misc; // x: reactive
};
layout(set = 0, binding = 1) readonly buffer Insts { Inst v[]; } insts;
layout(push_constant) uniform Push { uvec4 idx; } pc;
layout(location = 0) out vec4 v_cur;
layout(location = 1) out vec4 v_prev;
layout(location = 2) flat out float v_reactive;
void main() {
  Inst it = insts.v[pc.idx.x];
  vec4 p = vec4(in_pos, 1.0);
  vec4 world = it.model * p;
  gl_Position = u.viewproj_jit * world;
  // Clip uzayi konumlari VARYAN olarak tasinir: donanimin perspektif dogru
  // interpolasyonu (x, y, w) uclusunu birlikte tasidigi icin fragment'ta
  // xy/w dogru ekran konumunu verir (standart MV tarifi).
  v_cur = u.viewproj * world;
  v_prev = u.prev_viewproj * (it.prev_model * p);
  v_reactive = it.misc.x;
}
