#version 450
// Hareket vektoru + reactive maske. Cikti: RG = PIKSEL kaymasi (bu karenin
// konumu EKSI onceki karenin konumu; +x saga, +y asagi — Vulkan NDC y'si de
// asagi), B = ayrilmis (0), A = reactive maske.
//
// Gecisin temizleme rengi (0,0,0,0) SOZLESMENIN PARCASIDIR: nesne olmayan
// pikseller "hareketsiz + guvenilir" demektir (Tuzaklar 8ai).
layout(location = 0) in vec4 v_cur;
layout(location = 1) in vec4 v_prev;
layout(location = 2) flat in float v_reactive;
layout(set = 0, binding = 0) uniform Motion {
  mat4 viewproj_jit;
  mat4 viewproj;
  mat4 prev_viewproj;
  vec4 params; // xy: hedefin piksel olcusu
} u;
layout(location = 0) out vec4 o_mv;
void main() {
  float wc = abs(v_cur.w) < 1e-6 ? 1e-6 : v_cur.w;
  float wp = abs(v_prev.w) < 1e-6 ? 1e-6 : v_prev.w;
  vec2 c = v_cur.xy / wc;   // NDC [-1,1]
  vec2 p = v_prev.xy / wp;
  vec2 mv = (c - p) * 0.5 * u.params.xy; // NDC farki -> piksel
  o_mv = vec4(mv, 0.0, v_reactive);
}
