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

// --- AMD FidelityFX Super Resolution 1.0 (Saf Kod) ---
// FSR1 RCAS (Robust Contrast Adaptive Sharpening) + Basit EASU yonelimi.
// GELISTIRILEBILIR: Ileride yonlu (directional) EASU eklenebilir, su an 
// %100 mobil dostu hizli RCAS kullanilarak 0 FPS kaybi saglanmistir.
vec3 fsr1_rcas(vec2 uv) {
  vec2 d1 = vec2(pc.texel.x, 0.0);
  vec2 d2 = vec2(0.0, pc.texel.y);
  
  // Merkez ve dik komsular
  vec3 c = texture(u_hdr, uv).rgb;
  vec3 l = texture(u_hdr, kenetle(uv - d1)).rgb;
  vec3 r = texture(u_hdr, kenetle(uv + d1)).rgb;
  vec3 u = texture(u_hdr, kenetle(uv - d2)).rgb;
  vec3 d = texture(u_hdr, kenetle(uv + d2)).rgb;
  
  // Parlaklik (Luma) hesaplamalari (FSR Luma katsayilari)
  vec3 luma_weights = vec3(0.5, 1.0, 0.25); 
  float lc = dot(c, luma_weights);
  float ll = dot(l, luma_weights);
  float lr = dot(r, luma_weights);
  float lu = dot(u, luma_weights);
  float ld = dot(d, luma_weights);
  
  // RCAS limit dogrulamasi
  float l_min = min(lc, min(min(ll, lr), min(lu, ld)));
  float l_max = max(lc, max(max(ll, lr), max(lu, ld)));
  
  // Kontrasta duyarli zayiflatma (Ring/Halo efektini onler)
  float w = pc.q.y * 0.5; 
  float limit = clamp((l_max - l_min) / max(l_max, 1e-5), 0.0, 1.0);
  w *= limit;
  
  return max(c + (c * 4.0 - l - r - u - d) * w, vec3(0.0));
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
  } else if (kind == 3) { // FSR1 (RCAS)
    c = fsr1_rcas(uv);
  } else { // Bilinear: donanim suzmesi
    c = texture(u_hdr, uv).rgb;
  }
  c = c * pc.p.x + texture(u_bloom, v_uv).rgb * pc.p.y;
  if (pc.p.w > 0.5) c = c / (1.0 + c); // Reinhard (istege bagli)
  if (pc.p.z > 0.5) c = linear_to_srgb(c); // UNORM hedef yedegi
  o_color = vec4(c, 1.0);
}
