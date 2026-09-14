#version 450
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec4 v_light_pos;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec3 v_world;
layout(location = 5) in float v_viewz;
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

void main() {
  vec3 n = normalize(v_nrm);
  float nl = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  float vis = shadow_visibility(nl);
  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  vec3 c = albedo * (u.ambient.rgb + nl * vis * u.ambient.a + point_lights(n));
  if (u.light_dir.w > 0.5) c = linear_to_srgb(c);
  o_color = vec4(c, 1.0);
}
