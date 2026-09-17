#version 450
// Birlestirme: HDR sahne + bloom -> CAGIRANIN hedefi. Bu gecis cagiranin
// render pass'inde (subpass 1) kosar; sozlesme degismesin diye sahne oraya
// dogrudan cizilmez, burada tek tam ekran ucgeniyle birlestirilir.
//
// FAZ 5: ayni gecis DINAMIK COZUNURLUGUN yukseltme (upscale) noktasidir.
// Sahne HDR hedefinin yalniz sol-ust q.x orani kadar alt-dikdortgeni doludur;
// burada tam hedefe yukseltilir. Yukseltici arayuzu q.z ile secilir:
//   0 None     — nokta ornekleme (texel merkezine kenetle): referans/kontrol
//   1 Bilinear — donanim dogrusal suzme (ucuz yol)
//   2 Sharpen  — dogrusal + unsharp maske (CAS benzeri)
// Arm ASR SDK'si depoda YOK: entegrasyon noktasi tam burasi (yeni bir kind +
// kendi hedefi); arayuz bu yuzden degerle degil TURLE secilir.
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_hdr;
layout(set = 0, binding = 1) uniform sampler2D u_bloom;
layout(push_constant) uniform Push {
  vec4 texel; // xy: 1/HDR olcusu, zw: 1/bloom olcusu
  vec4 p;     // x: poz, y: bloom yogunlugu, z: shader sRGB kodlasin mi, w: tonemap
  vec4 q;     // x: cozunurluk olcegi (uv carpani), y: keskinlik, z: upscaler turu
} pc;
layout(location = 0) out vec4 o_color;
vec3 linear_to_srgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
// Dolu alt-dikdortgenin DISINA tasma: olcek < 1 iken hedefin geri kalani
// tanimsizdir, komsu dokunuslar oraya kacmamali.
vec2 kenetle(vec2 uv) {
  return clamp(uv, pc.texel.xy * 0.5, vec2(pc.q.x) - pc.texel.xy * 0.5);
}
void main() {
  vec2 uv = kenetle(v_uv * pc.q.x);
  int kind = int(pc.q.z + 0.5);
  vec3 c;
  if (kind == 0) { // None: texel merkezine kenetlenmis nokta ornekleme
    vec2 sz = vec2(1.0) / pc.texel.xy;
    uv = (floor(uv * sz) + 0.5) * pc.texel.xy;
    c = texture(u_hdr, uv).rgb;
  } else if (kind == 2) { // Sharpen: 5 dokunus unsharp maske
    vec3 c0 = texture(u_hdr, uv).rgb;
    vec3 s = texture(u_hdr, kenetle(uv + vec2(pc.texel.x, 0.0))).rgb +
             texture(u_hdr, kenetle(uv - vec2(pc.texel.x, 0.0))).rgb +
             texture(u_hdr, kenetle(uv + vec2(0.0, pc.texel.y))).rgb +
             texture(u_hdr, kenetle(uv - vec2(0.0, pc.texel.y))).rgb;
    c = max(c0 + (c0 * 4.0 - s) * (pc.q.y * 0.25), vec3(0.0));
  } else { // Bilinear: donanim suzmesi
    c = texture(u_hdr, uv).rgb;
  }
  c = c * pc.p.x + texture(u_bloom, v_uv).rgb * pc.p.y;
  if (pc.p.w > 0.5) c = c / (1.0 + c); // Reinhard (istege bagli)
  if (pc.p.z > 0.5) c = linear_to_srgb(c); // UNORM hedef yedegi
  o_color = vec4(c, 1.0);
}
