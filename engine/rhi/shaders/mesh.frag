#version 450
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec4 v_light_pos;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec3 v_world;
layout(location = 5) in float v_viewz;
layout(location = 6) in float v_roughness;
layout(location = 7) in float v_metallic;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;  // x: 1/boyut, y: sabit egilim, z: golge acik mi, w: normal kaydirma (dunya)
  vec4 cluster_params; // x: dilim olcegi, y: dilim sapmasi, z: tile genisligi px, w: tile yuksekligi px
  uvec4 cluster_grid;  // x, y, z, isik sayisi
} u;
layout(set = 0, binding = 1) uniform sampler2DShadow u_shadow;
layout(set = 1, binding = 0) uniform sampler2D u_albedo; // malzeme (klasik set, bindless yok)
struct PointLight { vec4 pos_radius; vec4 color_intensity; };
layout(set = 0, binding = 2) uniform Lights { PointLight l[32]; } u_lights;
layout(std430, set = 0, binding = 3) readonly buffer Clusters { uint mask[]; } u_clusters;

const float PI = 3.14159265359;

// Is 4: Cook-Torrance GGX specular (Filament mobil kisaltmasi). alpha =
// roughness^2 (algisal -> dogrusal), cagiran zaten karesini almis olmali.
// D: Trowbridge-Reitz normal dagilimi.
float D_GGX(float NoH, float alpha) {
  float k = alpha / (1.0 - NoH * NoH + alpha * alpha * NoH * NoH);
  return k * k * (1.0 / PI);
}
// V: Smith yukseklik-ilintili gorunurluk (Heitz 2014) — 1/(4*NoV*NoL) zaten icinde.
float V_SmithGGXCorrelated(float NoV, float NoL, float alpha) {
  float a2 = alpha * alpha;
  float ggxv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
  float ggxl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(ggxv + ggxl, 1e-5);
}
vec3 F_Schlick(float voh, vec3 f0) {
  float f = pow(clamp(1.0 - voh, 0.0, 1.0), 5.0);
  return f0 + (vec3(1.0) - f0) * f;
}
// Tek bir isik yonu icin GGX specular katkisi. NoL disaridan verilir (nl clamp'i
// cagiran zaten yapmis olmali) — burada yalniz D*Vis*F hesaplanir.
vec3 specular_ggx(vec3 n, vec3 vdir, vec3 ldir, float NoL, float NoV, float alpha, vec3 f0) {
  vec3 h = normalize(vdir + ldir);
  float NoH = clamp(dot(n, h), 0.0, 1.0);
  float VoH = clamp(dot(vdir, h), 0.0, 1.0);
  return (D_GGX(NoH, alpha) * V_SmithGGXCorrelated(NoV, NoL, alpha)) * F_Schlick(VoH, f0);
}

// Kumelenmis nokta isiklar: bu pikselin kumesinin 32-bit maskesi, set bitleri
// icin Lambert (metalik olmayan pay) + GGX specular + pencereli ters-kare sonum.
vec3 point_lights(vec3 n, vec3 vdir, float NoV, vec3 albedo, float alpha, float metallic, vec3 f0) {
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
    // Is 3: fiziksel ters-kare sonum. eps KUCUK (0.01) olmali — yalniz d=0
    // tekilligini onler. Eskiden +1.0 idi: yaricapa yakin mesafelerde bile
    // dist2'yle kiyaslanabilir buyuklukte oldugu icin sonum egrisini
    // duzlestirip fiziksel olmayan genis/duz "boya lekesi" isik havuzlarina
    // yol aciyordu (TULPAR_TAM_DOKUMAN_KONTROL.md Is 3). NOT: bu degisiklik
    // isik siddetini etkiler — mevcut sahnelerdeki intensity degerleri
    // gercek bir ekranda GORSEL olarak yeniden ayarlanmali (burada yapilamadi).
    float att = win * win / (dist2 + 0.01);
    vec3 ldir = d * inversesqrt(max(dist2, 1e-8));
    float NoL = max(dot(n, ldir), 0.0);
    vec3 radiance = L.color_intensity.rgb * (L.color_intensity.w * att);
    vec3 diff = albedo * (1.0 - metallic) * NoL;
    vec3 spec = specular_ggx(n, vdir, ldir, NoL, NoV, alpha, f0) * NoL;
    sum += radiance * (diff + spec);
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

// 3x3 PCF; donanim karsilastirmali ornekleme (compareOp LESS_OR_EQUAL):
// texture() 1.0 = isikli. Harita disi = isikli (golge kutusu sahneyi kapsamali).
float shadow_visibility(float nl) {
  if (u.shadow_params.z < 0.5) return 1.0;
  vec3 p = v_light_pos.xyz / v_light_pos.w;
  if (p.z <= 0.0 || p.z >= 1.0) return 1.0;
  vec2 uv = p.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
  // Egik yuzeyde akne buyur: egilim normal-isik acisiyla olceklenir.
  float bias = u.shadow_params.y * clamp(1.0 - nl, 0.15, 1.0);
  float texel = u.shadow_params.x;
  float s = 0.0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
      s += texture(u_shadow, vec3(uv + vec2(float(x), float(y)) * texel, p.z - bias));
  return s / 9.0;
}

// Goz (kamera) dunya konumunu view matrisinden cikar (standart teknik):
// view = R * T(-eye) => eye = -transpose(R) * translation_sutunu.
vec3 camera_world_pos() {
  return -transpose(mat3(u.view)) * u.view[3].xyz;
}

void main() {
  vec3 n = normalize(v_nrm);
  vec3 vdir = normalize(camera_world_pos() - v_world);
  float NoV = max(dot(n, vdir), 1e-4);
  float nl = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  float vis = shadow_visibility(nl);
  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  // Is 4: PBR. roughness alt siniri 0.045 — alpha=0'a yakinken D_GGX'in
  // paydasi cokup gereksiz keskin/patlak highlight uretmesin.
  float roughness = clamp(v_roughness, 0.045, 1.0);
  float metallic = clamp(v_metallic, 0.0, 1.0);
  float alpha = roughness * roughness; // algisal -> dogrusal
  vec3 f0 = mix(vec3(0.04), albedo, metallic);

  vec3 ldir_sun = normalize(u.light_dir.xyz);
  vec3 diff_sun = albedo * (1.0 - metallic) * nl;
  vec3 spec_sun = specular_ggx(n, vdir, ldir_sun, nl, NoV, alpha, f0) * nl;
  vec3 sun = (diff_sun + spec_sun) * vis * u.ambient.a;

  // ARA-DONEM (Is 5 gelene kadar): gercek bir yansima probe'u yok, sabit
  // ambiyansi kaba bir "cevre" gibi kullaniyoruz ki metal TAMAMEN siyah
  // cikmasin. Bu fiziksel olarak dogru degil, yalniz "PBR bozuk" izlenimini
  // onlemek icin — Is 5 (yansima probe) bunu degistirecek.
  vec3 ambient_diffuse = albedo * (1.0 - metallic) * u.ambient.rgb;
  vec3 ambient_specular = f0 * u.ambient.rgb;

  vec3 c = ambient_diffuse + ambient_specular + sun + point_lights(n, vdir, NoV, albedo, alpha, metallic, f0);
  if (u.light_dir.w > 0.5) c = linear_to_srgb(c);
  o_color = vec4(c, 1.0);
}
