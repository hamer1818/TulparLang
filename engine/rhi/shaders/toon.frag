#version 450
// Cel/toon golgeleme — motorun KILITLI sanat yonu karariyla dogrudan uyumlu
// (docs/engine/CIHAZ-MATRISI.md: "stilize dusuk poligon... guclu siluet").
// mesh.vert / mesh_skin.vert ile DOGRUDAN eslesir (ayni varying duzeni,
// ayni Frame/push_constant on-eki) — yalniz bu FRAGMENT asamasi degisir,
// yeni bir vertex shader'a gerek yok. Referans: klasik N.L banding + Fresnel
// rim-light (bkz. GitHub topics "toon-shading"/"cel-shading" — cok sayida
// bagimsiz uygulama ayni iki teknigi kullaniyor, motor-spesifik degil).
//
// DERLEME/KABLOLA: bu YENI bir dosya, henuz derlenmis SPIR-V'si (_spv.h)
// YOK — bu makinede glslc olmadigi icin uretilemedi. Kablolamak icin:
// (1) `python3 engine/tools/compile_shaders.py` calistir (toon_frag_spv.h
//     otomatik uretilir, dosya adi listede zaten taranir),
// (2) renderer.hpp/cpp'ye mesh.vert/mesh_skin.vert'i BU fragment ile
//     eslestiren ikinci bir boru hatti seti ekle (make_pipeline_set'in
//     "skinned" parametresi gibi, "shading_model" parametresi eklenebilir),
// (3) Material'a bir "shading_model" alani (PBR/TOON) ekleyip draw()'da
//     dogru boru hattini sec.
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec4 v_light_pos;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec3 v_world;
layout(location = 5) in float v_viewz;
layout(location = 6) in float v_roughness; // kullanilmaz (toon'da PBR yok), arayuz uyumu icin var
layout(location = 7) in float v_metallic;  // kullanilmaz
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;  // x: 1/boyut, y: egilim, z: acik mi, w: normal kaydirma
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 fog_params; // x: yogunluk, y: yukseklik sonumu, z: pozlama, w: zaman
  vec4 fog_color;
} u;
layout(set = 0, binding = 1) uniform sampler2DShadow u_shadow;
layout(set = 1, binding = 0) uniform sampler2D u_albedo;
layout(location = 0) out vec4 o_color;

// N.L'yi ayrik bantlara boler. 3 bant stilize sanat icin standart baslangic
// (Lambert'ten 3 basamak: golge / orta / aydinlik) — bant sayisi arttikca
// fotogercekci Lambert'e yaklasir.
float toon_band(float nl, float bands) { return ceil(max(nl, 1e-4) * bands) / bands; }

// mesh.frag ile AYNI 3x3 PCF sozlesmesi (donanim karsilastirmali ornekleme).
float shadow_visibility(float nl) {
  if (u.shadow_params.z < 0.5) return 1.0;
  vec3 p = v_light_pos.xyz / v_light_pos.w;
  if (p.z <= 0.0 || p.z >= 1.0) return 1.0;
  vec2 uv = p.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
  float bias = u.shadow_params.y * clamp(1.0 - nl, 0.15, 1.0);
  float texel = u.shadow_params.x;
  float s = 0.0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
      s += texture(u_shadow, vec3(uv + vec2(float(x), float(y)) * texel, p.z - bias));
  return s / 9.0;
}

// mesh.frag'taki KANITLI teknik (Is 4 turetimi): view = R*T(-eye) => eye = -R^T*t.
vec3 camera_world_pos() { return -transpose(mat3(u.view)) * u.view[3].xyz; }

void main() {
  vec3 n = normalize(v_nrm);
  vec3 vdir = normalize(camera_world_pos() - v_world);
  float nl_raw = dot(n, normalize(u.light_dir.xyz));
  float nl = max(nl_raw, 0.0);
  float vis = shadow_visibility(nl);
  float banded = toon_band(nl, 3.0);
  // Golgedeyken bir bant asagi dus — YUMUSAK gecis DEGIL, KESKIN (stilize amac,
  // fiziksel golge yumusakligi degil cizgi-film gorunumu istiyoruz).
  if (vis < 0.5) banded = max(banded - (1.0 / 3.0), 0.0);
  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  vec3 c = albedo * (u.ambient.rgb + banded * u.ambient.a);
  // Fresnel rim-light: siluet vurgusu, N.V kucukken (yuzey kameraya neredeyse
  // dik) parlar — "guclu siluet" sanat yonu hedefine dogrudan hizmet eder.
  float rim = pow(1.0 - clamp(dot(n, vdir), 0.0, 1.0), 4.0);
  c += albedo * rim * 0.3;
  // Is 6: sis (dogrusal uzayda, tonemap'ten once — mesh.frag ile ayni sozlesme).
  if (u.fog_params.x > 0.0) {
    float dist = length(v_world - camera_world_pos());
    float fog_amt = clamp((1.0 - exp(-u.fog_params.x * dist)) * exp(-u.fog_params.y * v_world.y), 0.0, 1.0);
    c = mix(c, u.fog_color.rgb, fog_amt);
  }
  if (u.light_dir.w > 0.5) {
    c = clamp(c, 0.0, 1.0);
    c = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
  }
  o_color = vec4(c, 1.0);
}
