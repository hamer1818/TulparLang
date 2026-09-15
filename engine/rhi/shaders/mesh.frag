#version 450
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world;
layout(location = 4) in float v_viewz;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj[3]; // golge kademeleri (Renderer::kMaxCascades)
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;  // x: 1/boyut, y: sabit egilim, z: golge acik mi, w: normal kaydirma (dunya)
  vec4 cluster_params; // x: dilim olcegi, y: dilim sapmasi, z: tile genisligi px, w: tile yuksekligi px
  uvec4 cluster_grid;  // x, y, z, isik sayisi
  vec4 cascade_params; // x: kademe sayisi, y: 1/kademe, z: atlas texel x, w: atlas texel y
} u;
layout(set = 0, binding = 1) uniform sampler2DShadow u_shadow;
layout(set = 1, binding = 0) uniform sampler2D u_albedo; // malzeme (klasik set, bindless yok)
struct PointLight { vec4 pos_radius; vec4 color_intensity; };
layout(set = 0, binding = 2) uniform Lights { PointLight l[32]; } u_lights;
layout(std430, set = 0, binding = 3) readonly buffer Clusters { uint mask[]; } u_clusters;

// Kumelenmis nokta isiklar: bu pikselin kumesinin 32-bit maskesi, set bitleri
// icin Lambert + pencereli ters-kare sonum (yaricapta sifira iner).
vec3 point_lights(vec3 n) {
  if (u.cluster_grid.w == 0u) return vec3(0.0);
  uint tx = min(uint(gl_FragCoord.x / u.cluster_params.z), u.cluster_grid.x - 1u);
  uint ty = min(uint(gl_FragCoord.y / u.cluster_params.w), u.cluster_grid.y - 1u);
  float fz = floor(log(max(v_viewz, 1e-6)) * u.cluster_params.x + u.cluster_params.y);
  uint tz = uint(clamp(fz, 0.0, float(u.cluster_grid.z - 1u)));
  uint mask = u_clusters.mask[(tz * u.cluster_grid.y + ty) * u.cluster_grid.x + tx];
  vec3 sum = vec3(0.0);
  while (mask != 0u) {
    int i = findLSB(mask);
    mask &= mask - 1u;
    PointLight L = u_lights.l[i];
    vec3 d = L.pos_radius.xyz - v_world;
    float dist2 = dot(d, d);
    float r = L.pos_radius.w;
    float x = dist2 / (r * r);
    float win = clamp(1.0 - x * x, 0.0, 1.0);
    float att = win * win / (dist2 + 1.0);
    float nl = max(dot(n, d * inversesqrt(max(dist2, 1e-8))), 0.0);
    sum += L.color_intensity.rgb * (L.color_intensity.w * att * nl);
  }
  return sum;
}
layout(location = 0) out vec4 o_color;

// Dogrusal aydinlatma (Filament): butun hesap dogrusal, hedef SRGB bicimliyse
// donanim kodlar; UNORM yedeginde (light_dir.w = 1) burada kodlanir.
vec3 linear_to_srgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// Kademeli golge (CSM), ATLAS yerlesimi: kademeler tek dokuda yan yana.
// Secim KAPSAMAYA gore: en yakin (en yuksek cozunurluklu) kademeden baslanir,
// isik uzayinda [0,1] icinde kalan ILK kademe kullanilir. Kamera frustum'u
// bolunmedigi icin projeksiyon matrisinin tersi gerekmez — on-dondurmeli
// (Android) projeksiyonda da dogru calisir. 3x3 PCF, donanim karsilastirmali
// ornekleme (compareOp LESS_OR_EQUAL): texture() 1.0 = isikli.
// Kenar kacagi: tile sinirindan yarim texel iceride kalinir, komsu kademenin
// derinligi okunmaz.
float cascade_sample(int idx, vec3 wpos, float bias) {
  vec4 lp = u.light_viewproj[idx] * vec4(wpos, 1.0);
  vec3 p = lp.xyz / lp.w;
  if (p.z <= 0.0 || p.z >= 1.0) return -1.0;
  vec2 uv = p.xy * 0.5 + 0.5;
  float inset = u.cascade_params.w; // bir texel
  if (uv.x < inset || uv.x > 1.0 - inset || uv.y < inset || uv.y > 1.0 - inset) return -1.0;
  // Atlas: x'i kademe tile'ina tasi.
  float inv_n = u.cascade_params.y;
  float base_x = (float(idx) + uv.x) * inv_n;
  float tx = u.cascade_params.z; // atlas texel x
  float ty = u.cascade_params.w; // atlas texel y
  float s = 0.0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
      s += texture(u_shadow, vec3(base_x + float(x) * tx, uv.y + float(y) * ty, p.z - bias));
  return s / 9.0;
}
float shadow_visibility(float nl, vec3 n) {
  if (u.shadow_params.z < 0.5) return 1.0;
  // Egik yuzeyde akne buyur: egilim normal-isik acisiyla olceklenir.
  float bias = u.shadow_params.y * clamp(1.0 - nl, 0.15, 1.0);
  // Normal boyunca DUNYA BIRIMI kaydirma (boru hattinin depthBias'i surucuye
  // bagli; Mali'de golgeyi tamamen silmisti — Tuzaklar 8q).
  vec3 wpos = v_world + n * u.shadow_params.w;
  int n_casc = int(u.cascade_params.x);
  for (int i = 0; i < 3; i++) {
    if (i >= n_casc) break;
    float v = cascade_sample(i, wpos, bias);
    if (v >= 0.0) return v;
  }
  return 1.0; // hicbir kademe kapsamiyor: isikli (golge hacmi sahneyi kapsamali)
}
void main() {
  vec3 n = normalize(v_nrm);
  float nl = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  float vis = shadow_visibility(nl, n);
  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  vec3 c = albedo * (u.ambient.rgb + nl * vis * u.ambient.a + point_lights(n));
  if (u.light_dir.w > 0.5) c = linear_to_srgb(c);
  o_color = vec4(c, 1.0);
}
