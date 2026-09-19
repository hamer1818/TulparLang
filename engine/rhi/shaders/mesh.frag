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
// Malzeme PBR parametreleri (klasik set 1, bindless YOK — Dusuk sinif cihaz
// descriptorIndexing vermiyor, PLAN REV-3). Malzeme basina 64 bayt UBO
// (eskiden 32; adim boyu cihazin UBO hizasi oldugu icin GPU'da bedava —
// olculen deger Renderer::material_ubo_stride()).
layout(set = 1, binding = 1) uniform MatBlock {
  vec4 pbr;      // x metallic, y ALGISAL puruzluluk, z dielektrik yansitirlik, w model (0 Lambert, 1 PBR)
  vec4 emissive; // rgb DOGRUSAL isima, w isima gucu carpani (strength)
  vec4 tex;      // x ORM var, y normal var, z isima dokusu var, w normal olcegi
  vec4 tex2;     // x occlusion gucu (ORM.R), y/z/w bos
} u_mat;
// Doku basina degisen malzeme kanallari (glTF 2.0). Hepsi MALZEME BASINA
// TEKDUZE bir dalin ardinda: maske 0 iken bu sampler'lar HIC okunmaz, yani
// dokusuz malzeme bugunku bant genisligini ve ALU'sunu aynen odemeye devam
// eder (A/B md5 kapisi bunu olcuyor). Dal uniform tabanli oldugu icin dalga
// ici ayrisma da yok — TBDR'da eklenen tek sey GERCEKTEN kullanan malzemenin
// ornekleme maliyeti.
//
// KANAL SOZLESMESI glTF 2.0 spec'ten (cgltf saf ayristiricidir, esleme
// tasimaz): metallicRoughness dokusunda G = ROUGHNESS, B = METALLIC,
// R = occlusion (occlusionTexture cogunlukla ayni goruntu). Dokular
// CARPANLARLA CARPILIR. Kapi: content_gltf_orm_channel_mapping.
layout(set = 1, binding = 2) uniform sampler2D u_orm;      // DOGRUSAL (UNORM) yuklenir
layout(set = 1, binding = 3) uniform sampler2D u_normal;   // DOGRUSAL (UNORM) yuklenir
layout(set = 1, binding = 4) uniform sampler2D u_emissive; // sRGB yuklenir (renk)
// KONTROL kipi (UiSortMode::BlendFirst ile ayni ruh): 1 = GGX'i BILEREK yanlis
// normalize et (a^2 payi yok). Yalniz enerji kapisinin kendi duyarliligini
// olcmek icin; urun yolunda her zaman 0.
layout(constant_id = 0) const int PBR_NDF = 0;
struct PointLight { vec4 pos_radius; vec4 color_intensity; };
layout(set = 0, binding = 2) uniform Lights { PointLight l[32]; } u_lights;
layout(std430, set = 0, binding = 3) readonly buffer Clusters { uint mask[]; } u_clusters;
// Stokastik tile isiklandirma (PLAN EK A.1): kume basina ornege alinan KUYRUK
// bitleri (x) ve kuyrukta KAC isik oldugu (y). Telafi agirligi = y / bitCount(x).
// x = ornege alinan KUYRUK bitleri, y = telafi agirliginin FLOAT BIT DESENI
// (oran tahmin edicisi: kuyruk onemi / secilen onem — cluster.cpp).
layout(std430, set = 0, binding = 7) readonly buffer Stoch { uvec2 t[]; } u_stoch;
// 0 = kapali. Bu bir OZELLESTIRME sabiti: kapaliyken asagidaki kuyruk dali ve
// u_stoch okumasi boru hatti kurulumunda TAMAMEN elenir — yani kapali yol ne
// fazladan bant genisligi harcar ne de aritmetigi degisir (bit bit ayni).
layout(constant_id = 1) const int STOCHASTIC = 0;

// Kumelenmis nokta isiklar: bu pikselin kumesinin 32-bit maskesi, set bitleri
// icin Lambert + pencereli ters-kare sonum (yaricapta sifira iner).
vec3 point_lights(vec3 n) {
  if (u.cluster_grid.w == 0u) return vec3(0.0);
  uint tx = min(uint(gl_FragCoord.x / u.cluster_params.z), u.cluster_grid.x - 1u);
  uint ty = min(uint(gl_FragCoord.y / u.cluster_params.w), u.cluster_grid.y - 1u);
  float fz = floor(log(max(v_viewz, 1e-6)) * u.cluster_params.x + u.cluster_params.y);
  uint tz = uint(clamp(fz, 0.0, float(u.cluster_grid.z - 1u)));
  uint ci = (tz * u.cluster_grid.y + ty) * u.cluster_grid.x + tx;
  uint mask = u_clusters.mask[ci];
  if (STOCHASTIC == 0) {
    // KAPALI YOL — asagidaki dongu eski koddan kelimesi kelimesine ayni.
    // Bilerek kopyalandi: agirlik carpani eklenmis TEK bir dongu, 1.0 ile de
    // olsa, surucude farkli sirada katlanabilirdi; "bit bit ayni" iddiasi
    // ancak ifade ayni kalirsa savunulabilir.
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
  // STOKASTIK: kafa bitleri agirlik 1, ornege alinan kuyruk bitleri agirlik
  // (kuyruktaki toplam / ornek sayisi) — yansiz tahmin edici.
  uvec2 st = u_stoch.t[ci];
  uint tail = st.x & mask;
  float wt = uintBitsToFloat(st.y);
  vec3 sum = vec3(0.0);
  while (mask != 0u) {
    uint bit = mask & (~mask + 1u);
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
    float w = (tail & bit) != 0u ? wt : 1.0;
    sum += L.color_intensity.rgb * (L.color_intensity.w * att * nl * w);
  }
  return sum;
}

// --- PBR: Cook-Torrance mikroyuzey (metallic-roughness, glTF 2.0) ----------
// Mobil butce (PLAN §8/10): compute YOK, bindless YOK, LUT dokusu YOK. Butun
// terimler ALU; ortam icin analitik split-sum kullanilir (asagida gerekce).
//
// ENERJI BIRIMI — motorun mevcut sozlesmesi korunur:
//   Lambert yolu: cikti = albedo * NoL * S   (S = isik olcegi, 1/PI iceride)
// yani S = E_isik/PI. Fizikte spekuler = D*V*F*NoL*E_isik = PI*D*V*F*NoL*S.
// Bu yuzden D'nin 1/PI'si SADELESIR: asagidaki d_ggx_pi() dogrudan PI*D
// dondurur ve iki terim ayni birimde toplanir. (Ayri bir "PI" carpani yok:
// katlanmis olani tekrar carpmak enerjiyi PI^2 kaydirirdi.)
float d_ggx_pi(float NoH, float a) {
  // Filament'in yeniden duzenlemesi: (NoH*NoH - 1) float16'da hassasiyet
  // kaybediyor; (NoH*a - NoH)*NoH + 1 ayni sonucu yarim duyarlilikta korur.
  float a2 = a * a;
  float f = (NoH * a2 - NoH) * NoH + 1.0;
  if (PBR_NDF == 1) return 1.0 / (f * f); // KONTROL: a^2 normalizasyonu YOK
  return a2 / (f * f);
}
// Smith-GGX yukseklik-iliskili gorunurluk: V = G / (4 NoL NoV) — 4 NoL NoV
// boleni ICERIDE, yani spekuler = D * V * F.
float v_smith(float NoV, float NoL, float a) {
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
  float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
vec3 f_schlick(vec3 f0, float u) {
  float f = pow(1.0 - u, 5.0);
  return f0 + (vec3(1.0) - f0) * f; // f90 = 1
}
// Ortam spekuleri: split-sum'in DFG terimi ANALITIK (Karis'in mobil uyumu).
// NEDEN LUT DEGIL: DFG dokusu TBDR'da fazladan bir sampler + descriptor
// baglamasi + tile disi okuma demek; bu uyum LUT'tan ~%1 sapiyor ve 6 ALU
// tutuyor. NEDEN TEK TERIMLI "F0 * ortam" DEGIL: o yaklasiklamada puruzluluk
// hicbir sey yapmaz (mat metal ile ayna ayni parlar) ve grazing acida Fresnel
// yukselisi kaybolur — kapinin olctugu iki sey de olurdu.
vec2 env_dfg(float rough, float NoV) {
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}
// Kumelenmis nokta isiklar, PBR: dagilimli ve spekuler AYNI dongude toplanir
// (maske bir kez okunur, isik basina tek gecis). Sonum/pencere Lambert
// yolundakiyle AYNI formul — iki yol arasindaki fark yalniz BRDF.
void point_lights_pbr(vec3 n, vec3 vdir, float NoV, float a, vec3 f0, out vec3 dif, out vec3 spc) {
  dif = vec3(0.0);
  spc = vec3(0.0);
  if (u.cluster_grid.w == 0u) return;
  uint tx = min(uint(gl_FragCoord.x / u.cluster_params.z), u.cluster_grid.x - 1u);
  uint ty = min(uint(gl_FragCoord.y / u.cluster_params.w), u.cluster_grid.y - 1u);
  float fz = floor(log(max(v_viewz, 1e-6)) * u.cluster_params.x + u.cluster_params.y);
  uint tz = uint(clamp(fz, 0.0, float(u.cluster_grid.z - 1u)));
  uint ci = (tz * u.cluster_grid.y + ty) * u.cluster_grid.x + tx;
  uint mask = u_clusters.mask[ci];
  // Stokastik telafi PBR yolunda da gecerli (ayni maske, ayni agirlik).
  uint tail = 0u;
  float wt = 1.0;
  if (STOCHASTIC == 1) {
    uvec2 st = u_stoch.t[ci];
    tail = st.x & mask;
    wt = uintBitsToFloat(st.y);
  }
  while (mask != 0u) {
    uint bit = mask & (~mask + 1u);
    int i = findLSB(mask);
    mask &= mask - 1u;
    PointLight L = u_lights.l[i];
    vec3 d = L.pos_radius.xyz - v_world;
    float dist2 = dot(d, d);
    float r = L.pos_radius.w;
    float x = dist2 / (r * r);
    float win = clamp(1.0 - x * x, 0.0, 1.0);
    float att = win * win / (dist2 + 1.0);
    vec3 ldir = d * inversesqrt(max(dist2, 1e-8));
    float nl2 = max(dot(n, ldir), 0.0);
    if (nl2 <= 0.0) continue;
    float w = (STOCHASTIC == 1 && (tail & bit) != 0u) ? wt : 1.0;
    vec3 e = L.color_intensity.rgb * (L.color_intensity.w * att * nl2 * w);
    vec3 h = normalize(vdir + ldir);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float VoH = clamp(dot(vdir, h), 0.0, 1.0);
    dif += e;
    spc += e * (d_ggx_pi(NoH, a) * v_smith(NoV, nl2, a)) * f_schlick(f0, VoH);
  }
}
// Kamera dunya konumu: gorunum matrisi katidir (R|t, R ortonormal), yani
// kamera = -R^T * t. Tam 4x4 tersi almaya gerek yok; tamamen uniform
// aritmetik oldugu icin surucu bunu skaler birime tasiyabilir. Ayri bir
// interpolant (v_view) eklenmedi: TBDR'da parametre tamponu bant genisligidir.
vec3 camera_world() { return -(transpose(mat3(u.view)) * u.view[3].xyz); }

// --- Teget uzayi: TUREVDEN, ek vertex verisi YOK ---------------------------
// Mikkelsen'in ortonormalize edilmemis ters teget cercevesi (Schuler'in
// "cotangent frame" turetimi). Neden vertex TANGENT'i DEGIL: paketlenmis GPU
// vertex'i bugun 20 bayt (pos float3 + oktahedral snorm16x2 normal + yarim UV)
// ve tek bir GLOBAL yerlesim — teget eklemek 4 bayt daha demek, yani normal
// haritasi KULLANMAYAN her mesh'in de vertex okumasi %20 artar. Mali'de vertex
// tamponu binning ve render gecislerinde IKI KEZ okunur, yani bedeli iki katina
// cikar. Buradaki maliyet ise yalnizca normal haritali malzemenin fragment'inda
// ~20 ALU + 4 turev. Olcum ve gerekce: renderer_normal_map_tangent_budget kapisi.
//
// Turev komutlari TEKDUZE akista olmali: dal u_mat.tex.y (UBO degeri) uzerine,
// yani cizim boyunca sabit — SPIR-V'nin "uniform control flow" sarti saglanir.
vec3 apply_normal_map(vec3 n, vec2 uv, float scale) {
  vec3 dp1 = dFdx(v_world), dp2 = dFdy(v_world);
  vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, n);
  vec3 dp1perp = cross(n, dp1);
  vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
  float inv = inversesqrt(max(dot(t, t), dot(b, b)));
  if (inv > 1e12) return n; // dejenere UV (sifir turev): geometrik normalde kal
  vec3 m = texture(u_normal, uv).xyz * 2.0 - 1.0;
  m.xy *= scale;
  return normalize(t * (inv * m.x) + b * (inv * m.y) + n * m.z);
}

vec3 shade_pbr(vec3 n_geo, vec3 albedo, float nl_geo, float vis) {
  // Doku kanallari CARPAN uzerine CARPILIR (glTF 2.0). Maske 0 iken asagidaki
  // ifadeler tam olarak eski hallerine iner: rough = clamp(u_mat.pbr.y, ...),
  // metallic = clamp(u_mat.pbr.x, ...), occ = 1.0, n = n_geo, nl = nl_geo.
  float metallic = u_mat.pbr.x;
  float rough = u_mat.pbr.y;
  float occ = 1.0;
  if (u_mat.tex.x > 0.5) {
    vec3 orm = texture(u_orm, v_uv).rgb; // R occlusion, G roughness, B metallic
    rough *= orm.g;
    metallic *= orm.b;
    occ = mix(1.0, orm.r, u_mat.tex2.x); // strength 0 = occlusion yok
  }
  vec3 n = n_geo;
  float nl = nl_geo;
  if (u_mat.tex.y > 0.5) {
    n = apply_normal_map(n_geo, v_uv, u_mat.tex.w);
    nl = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  }
  metallic = clamp(metallic, 0.0, 1.0);
  // Algisal puruzluluk (glTF) -> a = rough^2. Alt sinir: a -> 0'da D patlar
  // (tek piksellik sonsuz parlama, zamansal titreme); 0.045 Filament'in
  // onerdigi mobil tabanidir.
  rough = clamp(rough, 0.045, 1.0);
  float a = rough * rough;
  float reflectance = clamp(u_mat.pbr.z, 0.0, 1.0);
  // Metal dagilimli yansitmaz; dielektrigin F0'i renksizdir (Filament tarifi:
  // F0 = 0.16 * reflectance^2, reflectance 0.5 -> %4).
  vec3 diffuse_color = albedo * (1.0 - metallic);
  vec3 f0 = mix(vec3(0.16 * reflectance * reflectance), albedo, metallic);
  vec3 vdir = normalize(camera_world() - v_world);
  float NoV = clamp(dot(n, vdir), 1e-4, 1.0);
  // Gunes: Lambert yolundaki carpanin TA KENDISI (nl * golge * ambient.a).
  float sun = nl * vis * u.ambient.a;
  vec3 l = normalize(u.light_dir.xyz);
  vec3 h = normalize(vdir + l);
  float NoH = clamp(dot(n, h), 0.0, 1.0);
  float VoH = clamp(dot(vdir, h), 0.0, 1.0);
  vec3 spec_sun = (d_ggx_pi(NoH, a) * v_smith(NoV, nl, a)) * f_schlick(f0, VoH) * sun;
  // ORTAM: u.ambient.rgb GI sozlesmesinin ta kendisi — "albedo 1 Lambert
  // yuzeyin o yonde dondurdugu renk" = tekduze ortamin isimasi L
  // (content/gi.hpp: yuz degeri E(n)/PI). Dagilimli terim DOGRUDAN oradan
  // gelir; spekuler AYNI L'yi split-sum DFG ile agirliklandirir. Ikinci bir
  // ortam UYDURULMAZ. (Onfiltrelenmis sonda gelirse tek degisiklik: L'yi
  // yansima yonunde ornekle.)
  vec2 dfg = env_dfg(rough, NoV);
  vec3 pl_d, pl_s;
  point_lights_pbr(n, vdir, NoV, a, f0, pl_d, pl_s);
  // occlusion YALNIZ dolayli (ortam) terimi kisar — glTF spec: "indirect light".
  // Dogrudan gunes/nokta isik gomulmez. occ = 1.0 iken carpim IEEE754'te
  // birebir kimliktir, yani ORM'siz malzemenin sayisi degismez.
  vec3 amb = u.ambient.rgb * occ;
  vec3 c = diffuse_color * (amb + sun + pl_d);
  c += amb * (f0 * dfg.x + vec3(dfg.y));
  c += spec_sun + pl_s;
  return c; // isima main()'de eklenir (her iki golgeleme dali icin ORTAK)
}

// Isima (emissive) terimi. ONCEDEN yalniz shade_pbr icindeydi; Lambert
// malzemelerde isima SESSIZCE yok sayiliyordu -- glTF'ten gelen Lambert bir
// malzemeye isima rengi verilse bile ekranda hicbir sey degismiyordu.
// Artik iki dal da bunu kullanir. Isimasiz malzemede deger tam 0'dir ve
// `c + 0.0` bit-tam `c`'dir, yani eski goruntu korunur.
vec3 emissive_term() {
  vec3 emis = u_mat.emissive.rgb * u_mat.emissive.a;
  if (u_mat.tex.z > 0.5) emis *= texture(u_emissive, v_uv).rgb; // sRGB doku, ornekleme dogrusal dondurur
  return emis;
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
  // Golgeleme modeli MALZEME basina. Lambert dali eski ifadeyle ayni; ustune
  // ISIMA terimi eklenir. Isimasiz malzemede terim tam 0 oldugu ve `c + 0.0`
  // bit-tam `c` verdigi icin eski malzemeler yine bit bit eski goruntudedir.
  // Dal malzeme basina tekduze, yani dalga icinde ayrisma yok.
  vec3 c;
  if (u_mat.pbr.w < 0.5) c = albedo * (u.ambient.rgb + nl * vis * u.ambient.a + point_lights(n));
  else c = shade_pbr(n, albedo, nl, vis);
  c += emissive_term(); // ISIMA: iki dal icin de, tonemap/sRGB'den ONCE
  if (u.light_dir.w > 0.5) c = linear_to_srgb(c);
  o_color = vec4(c, 1.0);
}
