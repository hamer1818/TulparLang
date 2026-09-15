#version 450
// Yari cozunurluge indirgeme: 4 bilinear ornek (kaynagin yarim texel'inde),
// yani 2x2 blok ortalamasi. Mali'de ornek sayisi dogrudan bant genisligi;
// 13-tap yerine 4-tap secildi (zincirde zaten mip basina yumusama var).
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(set = 0, binding = 1) uniform sampler2D u_src2; // kullanilmaz (ortak duzen)
layout(push_constant) uniform Push { vec4 texel; vec4 p; } pc;
layout(location = 0) out vec4 o_color;
void main() {
  // Yarim kaynak texel: hedef texel merkezi kaynakta 2x2 blogun ORTASINA duser,
  // +-0.5 texel kaydirma tam olarak dort kaynak texel merkezini okur (kutu).
  vec2 o = pc.texel.xy * 0.5; // 1 / kaynak olcusu
  vec3 c = texture(u_src, v_uv + vec2(-o.x, -o.y)).rgb;
  c += texture(u_src, v_uv + vec2(o.x, -o.y)).rgb;
  c += texture(u_src, v_uv + vec2(-o.x, o.y)).rgb;
  c += texture(u_src, v_uv + vec2(o.x, o.y)).rgb;
  o_color = vec4(c * 0.25, 1.0);
}
