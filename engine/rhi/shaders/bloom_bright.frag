#version 450
// SECICI bloom'un esigi: yalniz esigin USTUNDEKI enerji zincire girer. Esik
// altinda kalan yuzeyler (normal aydinlatilmis nesneler) hic yayilmaz — kapi
// bunu olcer (esik dusurulunce yayilmali, yuksekken yayilmamali).
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(set = 0, binding = 1) uniform sampler2D u_src2; // kullanilmaz (ortak duzen)
layout(push_constant) uniform Push {
  vec4 texel; // xy: 1/kaynak0, zw: 1/kaynak1
  vec4 p;     // x: esik, y: yumusak diz, z: -, w: -
  vec4 q;     // x: cozunurluk olcegi (HDR'nin dolu alt-dikdortgeni)
} pc;
layout(location = 0) out vec4 o_color;
void main() {
  // Dinamik cozunurlukte HDR hedefinin yalniz sol-ust q.x orani doludur.
  vec2 uv = clamp(v_uv * pc.q.x, pc.texel.xy * 0.5, vec2(pc.q.x) - pc.texel.xy * 0.5);
  vec3 c = texture(u_src, uv).rgb;
  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
  // Yumusak diz (Karis): esikte sert kesme bantlasma yapar; diz genisliginde
  // ikinci dereceden gecis.
  float knee = max(pc.p.y, 1e-4);
  float soft = clamp(luma - pc.p.x + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee);
  float w = max(soft, luma - pc.p.x) / max(luma, 1e-4);
  o_color = vec4(c * w, 1.0);
}
