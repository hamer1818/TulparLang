#version 450
// SDF METIN ORNEKLEME (Faz 4). Atlasin ALFA kanali isaretli mesafe alanidir:
// 0.5 = glif kenari, buyuk = ic. Kapsama smoothstep ile mesafeden uretilir ve
// gecis genisligi EKRAN UZAYI turevinden (fwidth) gelir — yani atlas kac kat
// buyutulurse buyutulsun kenar ~1 piksel kalir. Bitmap atlasin buyutunce
// bulaniklasan kenarindan farki tam olarak budur (kapi bunu olcer).
//
// VARSAYILAN KAPALI: bu boru hatti yalniz ui_set_sdf(true) ile isaretlenmis
// dortgen varsa TEMBEL yaratilir. SDF atlas URETIMI (content tarafi) bu
// dosyanin isi degil; burasi yalniz ORNEKLEME yolu.
layout(constant_id = 0) const float kSharpness = 1.0; // gecis genisligi carpani
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in float v_encode;
layout(set = 1, binding = 0) uniform sampler2D u_atlas;
layout(location = 0) out vec4 o_color;
vec3 srgb_to_linear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 linear_to_srgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
void main() {
  const float d = texture(u_atlas, v_uv).a;
  const float w = max(fwidth(d) * kSharpness, 1e-5);
  const float a = smoothstep(0.5 - w, 0.5 + w, d);
  vec4 c = vec4(srgb_to_linear(v_color.rgb), v_color.a * a);
  if (v_encode > 0.5) c.rgb = linear_to_srgb(c.rgb);
  o_color = c;
}
