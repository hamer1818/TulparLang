#include "content/gltf.hpp"
#include "content/cluster_dag.hpp"
#include "content/meshopt.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cgltf.h>    // govdeler: content/vendored_impl.c
#define STBI_NO_STDIO
#include <stb_image.h>

namespace tulpar::engine::content {

namespace {
void set_error(Model *m, const char *what, const char *detail) {
  std::snprintf(m->error, sizeof m->error, "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

// Dosyayi tumuyle oku (dis .bin ve dis goruntu icin). malloc: yukleme ani.
unsigned char *read_file(const char *path, size_t *size) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); return nullptr; }
  unsigned char *buf = static_cast<unsigned char *>(std::malloc((size_t)n));
  if (!buf) { std::fclose(f); return nullptr; }
  size_t got = std::fread(buf, 1, (size_t)n, f);
  std::fclose(f);
  if (got != (size_t)n) { std::free(buf); return nullptr; }
  *size = got;
  return buf;
}

// "data:...;base64,XXXX" -> cozulmus bayt (cgltf'nin base64'u)
unsigned char *decode_data_uri(const char *uri, size_t *size) {
  const char *comma = std::strchr(uri, ',');
  if (!comma || std::strncmp(uri, "data:", 5) != 0) return nullptr;
  const char *b64 = comma + 1;
  size_t len = std::strlen(b64);
  size_t decoded = len / 4 * 3;
  if (len >= 1 && b64[len - 1] == '=') decoded--;
  if (len >= 2 && b64[len - 2] == '=') decoded--;
  cgltf_options opt{};
  void *out = nullptr;
  if (cgltf_load_buffer_base64(&opt, decoded, b64, &out) != cgltf_result_success) return nullptr;
  *size = decoded;
  return static_cast<unsigned char *>(out);
}

// Goruntuyu coz: data URI, dis dosya (gltf dizinine gore) ya da bufferView (GLB).
bool load_image(const cgltf_image *img, const char *gltf_dir, Arena &arena, ModelImage *out) {
  unsigned char *encoded = nullptr;
  size_t enc_size = 0;
  bool owned = false;
  if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
    encoded = static_cast<unsigned char *>(img->buffer_view->buffer->data) + img->buffer_view->offset;
    enc_size = img->buffer_view->size;
  } else if (img->uri && std::strncmp(img->uri, "data:", 5) == 0) {
    encoded = decode_data_uri(img->uri, &enc_size);
    owned = encoded != nullptr;
  } else if (img->uri) {
    char path[1024];
    std::snprintf(path, sizeof path, "%s%s", gltf_dir, img->uri);
    encoded = read_file(path, &enc_size);
    owned = encoded != nullptr;
  }
  if (!encoded) return false;
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load_from_memory(encoded, (int)enc_size, &w, &h, &comp, 4);
  if (owned) std::free(encoded);
  if (!px || w <= 0 || h <= 0) return false;
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->rgba = arena.alloc_array<uint8_t>((size_t)w * h * 4);
  if (out->rgba) std::memcpy(out->rgba, px, (size_t)w * h * 4);
  stbi_image_free(px);
  return out->rgba != nullptr;
}

const cgltf_accessor *find_attr(const cgltf_primitive &p, cgltf_attribute_type t) {
  for (cgltf_size i = 0; i < p.attributes_count; i++)
    if (p.attributes[i].type == t) return p.attributes[i].data;
  return nullptr;
}

// Dugumun yerel TRS'si (matrix verilmisse ayristirilir: olcek sutun boyu,
// donus normalize 3x3'ten quaternion).
Quat quat_from_cols(const float c0[3], const float c1[3], const float c2[3]) {
  const float m00 = c0[0], m01 = c1[0], m02 = c2[0];
  const float m10 = c0[1], m11 = c1[1], m12 = c2[1];
  const float m20 = c0[2], m21 = c1[2], m22 = c2[2];
  const float tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0.0f) {
    const float sq = std::sqrt(tr + 1.0f) * 2.0f;
    q.w = 0.25f * sq; q.x = (m21 - m12) / sq; q.y = (m02 - m20) / sq; q.z = (m10 - m01) / sq;
  } else if (m00 > m11 && m00 > m22) {
    const float sq = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q.w = (m21 - m12) / sq; q.x = 0.25f * sq; q.y = (m01 + m10) / sq; q.z = (m02 + m20) / sq;
  } else if (m11 > m22) {
    const float sq = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q.w = (m02 - m20) / sq; q.x = (m01 + m10) / sq; q.y = 0.25f * sq; q.z = (m12 + m21) / sq;
  } else {
    const float sq = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q.w = (m10 - m01) / sq; q.x = (m02 + m20) / sq; q.y = (m12 + m21) / sq; q.z = 0.25f * sq;
  }
  return normalize(q);
}
void node_local_trs(const cgltf_node &n, Vec3 *t, Quat *r, Vec3 *sc) {
  *t = {0, 0, 0}; *r = Quat{}; *sc = {1, 1, 1};
  if (n.has_matrix) {
    const float *m = n.matrix;
    *t = {m[12], m[13], m[14]};
    float c0[3] = {m[0], m[1], m[2]}, c1[3] = {m[4], m[5], m[6]}, c2[3] = {m[8], m[9], m[10]};
    const float l0 = std::sqrt(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]);
    const float l1 = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
    const float l2 = std::sqrt(c2[0] * c2[0] + c2[1] * c2[1] + c2[2] * c2[2]);
    *sc = {l0, l1, l2};
    for (int i = 0; i < 3; i++) { if (l0 > 0) c0[i] /= l0; if (l1 > 0) c1[i] /= l1; if (l2 > 0) c2[i] /= l2; }
    *r = quat_from_cols(c0, c1, c2);
    return;
  }
  if (n.has_translation) *t = {n.translation[0], n.translation[1], n.translation[2]};
  if (n.has_rotation) *r = normalize(Quat{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]});
  if (n.has_scale) *sc = {n.scale[0], n.scale[1], n.scale[2]};
}
// Animasyon kanalini t aninda degerlendirir (out[comps]). LINEAR/STEP; CUBICSPLINE
// icin anahtar degeri alinip dogrusal karistirilir (teget yok sayilir; bilgi).
void eval_channel(const cgltf_animation_sampler &sm, float t, int comps, float *out) {
  const cgltf_accessor *in = sm.input, *ov = sm.output;
  const uint32_t n = (uint32_t)in->count;
  if (n == 0) return;
  const int stride = sm.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
  const int mid = stride == 3 ? 1 : 0;
  auto read = [&](uint32_t k, float *v) { cgltf_accessor_read_float(ov, (cgltf_size)(k * stride + mid), v, (cgltf_size)comps); };
  float t0 = 0, t1 = 0;
  cgltf_accessor_read_float(in, 0, &t0, 1);
  if (t <= t0 || n == 1) { read(0, out); return; }
  cgltf_accessor_read_float(in, n - 1, &t1, 1);
  if (t >= t1) { read(n - 1, out); return; }
  uint32_t lo = 0, hi = n - 1; // t0 <= t < t1: [lo, lo+1] araligi
  while (hi - lo > 1) {
    const uint32_t mi = (lo + hi) / 2;
    float tm = 0;
    cgltf_accessor_read_float(in, mi, &tm, 1);
    if (tm <= t) lo = mi; else hi = mi;
  }
  float ta = 0, tb = 0;
  cgltf_accessor_read_float(in, lo, &ta, 1);
  cgltf_accessor_read_float(in, lo + 1, &tb, 1);
  float a[4] = {0, 0, 0, 1}, b[4] = {0, 0, 0, 1};
  read(lo, a);
  if (sm.interpolation == cgltf_interpolation_type_step) { for (int i = 0; i < comps; i++) out[i] = a[i]; return; }
  read(lo + 1, b);
  const float u = tb > ta ? (t - ta) / (tb - ta) : 0.0f;
  if (comps == 4) {
    const Quat q = slerp(Quat{a[0], a[1], a[2], a[3]}, Quat{b[0], b[1], b[2], b[3]}, u);
    out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
    return;
  }
  for (int i = 0; i < comps; i++) out[i] = a[i] + (b[i] - a[i]) * u;
}
int32_t skin_of_node(const cgltf_data *d, const cgltf_node *node, int32_t *joint_index) {
  for (cgltf_size s = 0; s < d->skins_count; s++)
    for (cgltf_size j = 0; j < d->skins[s].joints_count; j++)
      if (d->skins[s].joints[j] == node) { if (joint_index) *joint_index = (int32_t)j; return (int32_t)s; }
  return -1;
}

uint32_t count_mesh_primitives(const cgltf_data *d) {
  uint32_t n = 0;
  for (cgltf_size m = 0; m < d->meshes_count; m++)
    for (cgltf_size p = 0; p < d->meshes[m].primitives_count; p++)
      if (d->meshes[m].primitives[p].type == cgltf_primitive_type_triangles) n++;
  return n;
}

void extend_bounds(Model *out, const Mat4 &world, const renderer::Vertex *v, uint32_t n, bool *first) {
  for (uint32_t i = 0; i < n; i++) {
    Vec4 p = world * Vec4{v[i].pos.x, v[i].pos.y, v[i].pos.z, 1.0f};
    Vec3 q{p.x, p.y, p.z};
    if (*first) { out->bounds_min = out->bounds_max = q; *first = false; continue; }
    out->bounds_min = {q.x < out->bounds_min.x ? q.x : out->bounds_min.x, q.y < out->bounds_min.y ? q.y : out->bounds_min.y,
                       q.z < out->bounds_min.z ? q.z : out->bounds_min.z};
    out->bounds_max = {q.x > out->bounds_max.x ? q.x : out->bounds_max.x, q.y > out->bounds_max.y ? q.y : out->bounds_max.y,
                       q.z > out->bounds_max.z ? q.z : out->bounds_max.z};
  }
}
} // namespace

bool gltf_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim) {
  *out = Model{};
  cgltf_options opt{};
  cgltf_data *data = nullptr;
  cgltf_result r = cgltf_parse_file(&opt, path, &data);
  if (r != cgltf_result_success) { set_error(out, "glTF ayristirma", path); return false; }
  r = cgltf_load_buffers(&opt, data, path);
  if (r != cgltf_result_success) { cgltf_free(data); set_error(out, "glTF tampon yukleme", path); return false; }

  // gltf dizini (dis goruntu yollari icin)
  char dir[1024];
  std::snprintf(dir, sizeof dir, "%s", path);
  char *slash = std::strrchr(dir, '/');
  if (slash) slash[1] = 0; else dir[0] = 0;

  bool ok = true;
  // Goruntuler
  uint32_t nimg = (uint32_t)data->images_count;
  if (nimg > lim.max_images) { set_error(out, "goruntu siniri asildi", nullptr); ok = false; }
  out->images = arena.alloc_array_zeroed<ModelImage>(nimg ? nimg : 1);
  for (uint32_t i = 0; ok && i < nimg; i++) {
    if (!load_image(&data->images[i], dir, arena, &out->images[i])) {
      set_error(out, "goruntu cozulemedi", data->images[i].uri && std::strncmp(data->images[i].uri, "data:", 5) ? data->images[i].uri : "(gomulu)");
      ok = false;
    }
  }
  out->image_count = ok ? nimg : 0;
  // Malzemeler
  uint32_t nmat = (uint32_t)data->materials_count;
  if (ok && nmat > lim.max_materials) { set_error(out, "malzeme siniri asildi", nullptr); ok = false; }
  out->materials = arena.alloc_array_zeroed<ModelMaterial>(nmat ? nmat : 1);
  for (uint32_t i = 0; ok && i < nmat; i++) {
    const cgltf_material &cm = data->materials[i];
    ModelMaterial &mm = out->materials[i];
    mm.base_color = {1, 1, 1};
    mm.image = -1;
    // TUZAK 8u: `alloc_array_zeroed` YAPICI CALISTIRMAZ — ModelMaterial'daki
    // `roughness = 1.0f` varsayilani uygulanmaz, alan 0 kalir. roughness 0 =
    // ayna; yani pbr blogu olmayan her malzeme sessizce AYNAYA donerdi.
    // Sifirdan farkli her varsayilan ELLE atanmali (base_color/image zaten oyle).
    mm.metallic = 0.0f;
    mm.roughness = 1.0f;
    mm.has_pbr = false;
    mm.orm_image = mm.normal_image = mm.emissive_image = -1;
    mm.normal_scale = 1.0f;
    mm.occlusion_strength = 0.0f;
    mm.occlusion_separate = false;
    if (cm.has_pbr_metallic_roughness) {
      const cgltf_pbr_metallic_roughness &pbr = cm.pbr_metallic_roughness;
      mm.base_color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
      if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
        mm.image = (int32_t)(pbr.base_color_texture.texture->image - data->images);
      // metallic/roughness CARPANLARI. Doku varsa shader bunlarla CARPAR
      // (glTF 2.0: "multiplied with the texture values").
      mm.metallic = pbr.metallic_factor;
      mm.roughness = pbr.roughness_factor;
      // metallicRoughness DOKUSU: G = roughness, B = metallic (glTF 2.0 spec).
      // cgltf bunu SOYLEMEZ — saf ayristiricidir, yalnizca doku isaretcisini ve
      // `scale`i tasir; esleme spec'ten gelir ve `content_gltf_orm_channel_mapping`
      // kapisi kanallari takas ederek olcer.
      if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image)
        mm.orm_image = (int32_t)(pbr.metallic_roughness_texture.texture->image - data->images);
      mm.has_pbr = true;
    }
    mm.emissive = {cm.emissive_factor[0], cm.emissive_factor[1], cm.emissive_factor[2]};
    // TUZAK (Tuzaklar 8u'nun ayni sinifi, bu kez SATICI AYRISTIRICISINDA):
    // cgltf `texture_view.scale`i YALNIZ o JSON nesnesi VARSA 1'e kurar
    // (cgltf_parse_json_texture_view); malzeme duzeyinde varsayilan YOK. Yani
    // normalTexture yazilmamissa `normal_texture.scale` SIFIR kalir — kosulsuz
    // okumak butun normal haritalarini duzlestirirdi. Bu yuzden scale yalniz
    // doku VARKEN okunur, yoksa 1 kalir.
    if (cm.normal_texture.texture && cm.normal_texture.texture->image) {
      mm.normal_image = (int32_t)(cm.normal_texture.texture->image - data->images);
      mm.normal_scale = cm.normal_texture.scale;
    }
    if (cm.emissive_texture.texture && cm.emissive_texture.texture->image)
      mm.emissive_image = (int32_t)(cm.emissive_texture.texture->image - data->images);
    // occlusionTexture: R kanali. Varliklarin ezici cogunlugunda metallicRoughness
    // ILE AYNI goruntudur ("ORM" paketlemesi) — o zaman bedava, ayni sampler'dan
    // okunur. FARKLI bir goruntuyse bugun okunmuyor: besinci bir doku baglamasi
    // TBDR'da bu kazanca degmez. Sessizce yutulmuyor, `occlusion_separate` ile
    // isaretleniyor.
    if (cm.occlusion_texture.texture && cm.occlusion_texture.texture->image) {
      const int32_t occ = (int32_t)(cm.occlusion_texture.texture->image - data->images);
      if (occ == mm.orm_image) mm.occlusion_strength = cm.occlusion_texture.scale; // scale == strength (cgltf)
      else mm.occlusion_separate = true;
    }
  }
  out->material_count = ok ? nmat : 0;
  // --- Goruntu renk uzayi: KULLANIMDAN turetilir --------------------------
  // glTF goruntusu renk uzayi tasimaz; onu MALZEMEDEKI YERI belirler (spec):
  // baseColor + emissive = sRGB, metallicRoughness + normal + occlusion =
  // DOGRUSAL veri. Bir goruntu yalniz VERI olarak kullanildiysa dogrusal olur;
  // her iki rolde de geciyorsa (glTF'te gecersiz) renk kazanir ve sayac artar.
  // TUZAK 8u: `alloc_array_zeroed` YAPICI CALISTIRMAZ, yani ModelImage'in
  // `srgb = true` varsayilani uygulanmaz ve alan SIFIR (= dogrusal) gelir.
  // Guvenli varsayilan ESKI davranistir (sRGB); dogrusal olan ACIKCA isaretlenir.
  for (uint32_t i = 0; ok && i < out->image_count; i++) out->images[i].srgb = true;
  {
    bool *as_color = arena.alloc_array_zeroed<bool>(out->image_count ? out->image_count : 1);
    bool *as_data = arena.alloc_array_zeroed<bool>(out->image_count ? out->image_count : 1);
    // Normal haritalari AYRI izleniyor: renk uzayi acisindan ORM ile ayni
    // (dogrusal veri) ama mip uretimi acisindan degil — bkz. ModelImage.
    bool *as_normal = arena.alloc_array_zeroed<bool>(out->image_count ? out->image_count : 1);
    if (as_color && as_data && as_normal) {
      for (uint32_t i = 0; ok && i < out->material_count; i++) {
        const ModelMaterial &mm = out->materials[i];
        if (mm.image >= 0 && (uint32_t)mm.image < out->image_count) as_color[mm.image] = true;
        if (mm.emissive_image >= 0 && (uint32_t)mm.emissive_image < out->image_count) as_color[mm.emissive_image] = true;
        if (mm.orm_image >= 0 && (uint32_t)mm.orm_image < out->image_count) as_data[mm.orm_image] = true;
        if (mm.normal_image >= 0 && (uint32_t)mm.normal_image < out->image_count) {
          as_data[mm.normal_image] = true;
          as_normal[mm.normal_image] = true;
        }
      }
      for (uint32_t i = 0; ok && i < out->image_count; i++) {
        out->images[i].srgb = as_color[i] || !as_data[i]; // hicbir yerde kullanilmayan: sRGB (eski davranis)
        // Bir goruntu hem renk hem normal olarak kullaniliyorsa (catisma)
        // normal mip yolu KAPALI kalir: sRGB texel'i normal sanip
        // normallestirmek catismayi buyutur, kucultmez.
        out->images[i].normal_map = as_normal[i] && !as_color[i];
        if (as_color[i] && as_data[i]) out->colorspace_conflicts++;
      }
    }
  }

  // Iskeletler (skin): eklemler ebeveyn-once yeniden siralanir (sim::to_model
  // kurali); glTF eklem indeksi -> bizim indeks (skin_remap) vertex'lere uygulanir.
  uint32_t nskin = (uint32_t)data->skins_count;
  if (nskin > lim.max_skins) nskin = lim.max_skins;
  out->skins = arena.alloc_array_zeroed<ModelSkin>(nskin ? nskin : 1);
  uint8_t **skin_remap = arena.alloc_array_zeroed<uint8_t *>(nskin ? nskin : 1);
  uint32_t **skin_order = arena.alloc_array_zeroed<uint32_t *>(nskin ? nskin : 1);
  if (!out->skins || !skin_remap || !skin_order) { set_error(out, "arena dolu (skin)", nullptr); ok = false; }
  for (uint32_t si = 0; ok && si < nskin; si++) {
    const cgltf_skin &sk = data->skins[si];
    ModelSkin &ms = out->skins[si];
    uint32_t jc = (uint32_t)sk.joints_count;
    if (jc > kModelMaxJoints) { set_error(out, "eklem siniri asildi (128)", sk.name); ok = false; break; }
    if (sk.name) std::snprintf(ms.name, sizeof ms.name, "%s", sk.name);
    ms.joints = arena.alloc_array_zeroed<sim::Joint>(jc ? jc : 1);
    ms.inverse_bind = arena.alloc_array_zeroed<Mat4>(jc ? jc : 1);
    skin_remap[si] = arena.alloc_array_zeroed<uint8_t>(jc ? jc : 1);
    skin_order[si] = arena.alloc_array_zeroed<uint32_t>(jc ? jc : 1);
    int32_t *parent_in = arena.alloc_array_zeroed<int32_t>(jc ? jc : 1);
    bool *placed = arena.alloc_array_zeroed<bool>(jc ? jc : 1);
    if (!ms.joints || !ms.inverse_bind || !skin_remap[si] || !skin_order[si] || !parent_in || !placed) {
      set_error(out, "arena dolu (skin)", nullptr); ok = false; break;
    }
    for (uint32_t j = 0; j < jc; j++) {
      parent_in[j] = -1;
      const cgltf_node *p = sk.joints[j]->parent;
      for (uint32_t k = 0; p && k < jc; k++) if (sk.joints[k] == p) { parent_in[j] = (int32_t)k; break; }
    }
    uint32_t n = 0;
    while (n < jc) { // ebeveyn-once siralama (dongu yoksa her turda en az bir eklem yerlesir)
      uint32_t before = n;
      for (uint32_t j = 0; j < jc; j++)
        if (!placed[j] && (parent_in[j] < 0 || placed[parent_in[j]])) { skin_order[si][n] = j; skin_remap[si][j] = (uint8_t)n; placed[j] = true; n++; }
      if (n == before) { set_error(out, "eklem hiyerarsisinde dongu", sk.name); ok = false; break; }
    }
    if (!ok) break;
    for (uint32_t k = 0; k < jc; k++) {
      const uint32_t j = skin_order[si][k];
      sim::Joint &jt = ms.joints[k];
      jt.parent = parent_in[j] < 0 ? (int16_t)-1 : (int16_t)skin_remap[si][parent_in[j]];
      node_local_trs(*sk.joints[j], &jt.bind_t, &jt.bind_r, &jt.bind_s);
      Mat4 ib;
      if (sk.inverse_bind_matrices) {
        float f[16];
        cgltf_accessor_read_float(sk.inverse_bind_matrices, (cgltf_size)j, f, 16);
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) ib.m[c][rr] = f[c * 4 + rr];
      }
      ms.inverse_bind[k] = ib;
    }
    ms.joint_count = jc;
  }
  out->skin_count = ok ? nskin : 0;

  // Klipler: glTF animasyonu sabit hizda orneklenir (kanal LINEAR/STEP/CUBIC
  // degerleri), ClipBuilder sikistirir (sabit iz eleme + niceleme; bilgi stats).
  uint32_t nclip = (uint32_t)data->animations_count;
  if (nclip > lim.max_clips) nclip = lim.max_clips;
  out->clips = arena.alloc_array_zeroed<ModelClip>(nclip ? nclip : 1);
  if (!out->clips) { set_error(out, "arena dolu (klip)", nullptr); ok = false; }
  uint32_t cn = 0;
  for (uint32_t ai = 0; ok && ai < nclip; ai++) {
    const cgltf_animation &an = data->animations[ai];
    int32_t skin_idx = -1;
    float duration = 0;
    for (cgltf_size c = 0; c < an.channels_count; c++) {
      const cgltf_animation_channel &ch = an.channels[c];
      if (!ch.sampler || !ch.target_node || !ch.sampler->input || !ch.sampler->output) continue;
      if (skin_idx < 0) skin_idx = skin_of_node(data, ch.target_node, nullptr);
      const cgltf_size nk = ch.sampler->input->count;
      if (nk) { float tl = 0; cgltf_accessor_read_float(ch.sampler->input, nk - 1, &tl, 1); if (tl > duration) duration = tl; }
    }
    if (skin_idx < 0 || (uint32_t)skin_idx >= nskin || duration <= 0) continue; // iskelete bagli degil: atla (bilgi)
    const ModelSkin &ms = out->skins[skin_idx];
    const cgltf_skin &sk = data->skins[skin_idx];
    const uint32_t jc = ms.joint_count;
    const float rate = lim.clip_sample_rate > 0 ? lim.clip_sample_rate : 30.0f;
    uint32_t samples = (uint32_t)(duration * rate) + 1;
    if (samples < 2) samples = 2;
    sim::RawTrack *tracks = arena.alloc_array_zeroed<sim::RawTrack>(jc);
    Vec3 *tt = arena.alloc_array<Vec3>(jc * samples);
    Quat *rr = arena.alloc_array<Quat>(jc * samples);
    Vec3 *ss = arena.alloc_array<Vec3>(jc * samples);
    if (!tracks || !tt || !rr || !ss) { set_error(out, "arena dolu (klip)", nullptr); ok = false; break; }
    for (uint32_t k = 0; k < jc; k++) {
      const cgltf_node *node = sk.joints[skin_order[skin_idx][k]];
      Vec3 bt, bs; Quat br;
      node_local_trs(*node, &bt, &br, &bs);
      const cgltf_animation_channel *ct = nullptr, *cr = nullptr, *cs = nullptr;
      for (cgltf_size c = 0; c < an.channels_count; c++) {
        const cgltf_animation_channel &ch = an.channels[c];
        if (ch.target_node != node || !ch.sampler) continue;
        if (ch.target_path == cgltf_animation_path_type_translation) ct = &ch;
        else if (ch.target_path == cgltf_animation_path_type_rotation) cr = &ch;
        else if (ch.target_path == cgltf_animation_path_type_scale) cs = &ch;
      }
      for (uint32_t i = 0; i < samples; i++) {
        const float t = (float)i / rate;
        float v[4];
        Vec3 p = bt, scl = bs; Quat q = br;
        if (ct) { eval_channel(*ct->sampler, t, 3, v); p = {v[0], v[1], v[2]}; }
        if (cr) { eval_channel(*cr->sampler, t, 4, v); q = normalize(Quat{v[0], v[1], v[2], v[3]}); }
        if (cs) { eval_channel(*cs->sampler, t, 3, v); scl = {v[0], v[1], v[2]}; }
        tt[k * samples + i] = p; rr[k * samples + i] = q; ss[k * samples + i] = scl;
      }
      tracks[k].t = tt + k * samples; tracks[k].r = rr + k * samples; tracks[k].s = ss + k * samples;
    }
    sim::RawClip raw;
    raw.tracks = tracks; raw.joints = jc; raw.samples = samples; raw.sample_rate = rate;
    ModelClip &mc = out->clips[cn];
    if (an.name) std::snprintf(mc.name, sizeof mc.name, "%s", an.name);
    mc.skin = skin_idx;
    mc.duration = (float)(samples - 1) / rate;
    mc.clip = sim::ClipBuilder::build(arena, raw, 1e-5f, &mc.stats);
    if (!mc.clip) { set_error(out, "klip sikistirma", an.name); ok = false; break; }
    cn++;
  }
  out->clip_count = ok ? cn : 0;
  // Mesh'ler (ucgen primitifleri; her primitif ayri ModelMesh)
  uint32_t nprim = count_mesh_primitives(data);
  if (ok && nprim > lim.max_meshes) { set_error(out, "mesh siniri asildi", nullptr); ok = false; }
  out->meshes = arena.alloc_array_zeroed<ModelMesh>(nprim ? nprim : 1);
  // cgltf mesh -> ilk ModelMesh indeksi ve sayisi (instance kurarken)
  uint32_t *mesh_first = arena.alloc_array_zeroed<uint32_t>(data->meshes_count ? data->meshes_count : 1);
  uint32_t *mesh_n = arena.alloc_array_zeroed<uint32_t>(data->meshes_count ? data->meshes_count : 1);
  uint32_t mi = 0;
  for (cgltf_size m = 0; ok && m < data->meshes_count; m++) {
    mesh_first[m] = mi;
    for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
      const cgltf_primitive &prim = data->meshes[m].primitives[p];
      if (prim.type != cgltf_primitive_type_triangles) continue;
      const cgltf_accessor *pos = find_attr(prim, cgltf_attribute_type_position);
      const cgltf_accessor *nrm = find_attr(prim, cgltf_attribute_type_normal);
      const cgltf_accessor *uv = find_attr(prim, cgltf_attribute_type_texcoord);
      if (!pos) { set_error(out, "POSITION yok", data->meshes[m].name); ok = false; break; }
      ModelMesh &mm = out->meshes[mi];
      mm.skin = -1; // alloc_array_zeroed: varsayilan kurucu CALISMAZ (0 = "skin 0" olurdu; cokme goruldu)
      mm.vertex_count = (uint32_t)pos->count;
      mm.verts = arena.alloc_array<renderer::Vertex>(mm.vertex_count);
      if (!mm.verts) { set_error(out, "arena dolu (vertex)", nullptr); ok = false; break; }
      for (uint32_t i = 0; i < mm.vertex_count; i++) {
        float f[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos, i, f, 3);
        mm.verts[i].pos = {f[0], f[1], f[2]};
        f[0] = 0; f[1] = 1; f[2] = 0;
        if (nrm) cgltf_accessor_read_float(nrm, i, f, 3);
        mm.verts[i].nrm = {f[0], f[1], f[2]};
        f[0] = f[1] = 0;
        if (uv) cgltf_accessor_read_float(uv, i, f, 2);
        mm.verts[i].uv = {f[0], f[1]};
      }
      if (prim.indices) {
        mm.index_count = (uint32_t)prim.indices->count;
        mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
        if (!mm.indices) { set_error(out, "arena dolu (indeks)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < mm.index_count; i++) mm.indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
      } else {
        mm.index_count = mm.vertex_count;
        mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
        if (!mm.indices) { set_error(out, "arena dolu (indeks)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < mm.index_count; i++) mm.indices[i] = i;
      }
      mm.material = prim.material ? (int32_t)(prim.material - data->materials) : -1;
      // Iskeletli primitif: JOINTS_0 + WEIGHTS_0 ve bu mesh'i kullanan dugumun skin'i.
      const cgltf_accessor *jacc = find_attr(prim, cgltf_attribute_type_joints);
      const cgltf_accessor *wacc = find_attr(prim, cgltf_attribute_type_weights);
      if (jacc && wacc && nskin) {
        for (cgltf_size n = 0; n < data->nodes_count && mm.skin < 0; n++)
          if (data->nodes[n].mesh == &data->meshes[m] && data->nodes[n].skin) {
            const int32_t si = (int32_t)(data->nodes[n].skin - data->skins);
            if (si >= 0 && (uint32_t)si < nskin) mm.skin = si;
          }
      }
      if (mm.skin >= 0) {
        mm.skin_verts = arena.alloc_array_zeroed<renderer::SkinnedVertex>(mm.vertex_count);
        if (!mm.skin_verts) { set_error(out, "arena dolu (skin vertex)", nullptr); ok = false; break; }
        const uint32_t jc = out->skins[mm.skin].joint_count;
        for (uint32_t i = 0; i < mm.vertex_count; i++) {
          renderer::SkinnedVertex &sv = mm.skin_verts[i];
          sv.pos = mm.verts[i].pos; sv.nrm = mm.verts[i].nrm; sv.uv = mm.verts[i].uv;
          cgltf_uint ju[4] = {0, 0, 0, 0};
          float w[4] = {1, 0, 0, 0};
          cgltf_accessor_read_uint(jacc, i, ju, 4);
          cgltf_accessor_read_float(wacc, i, w, 4);
          float sum = w[0] + w[1] + w[2] + w[3];
          if (sum <= 0) { w[0] = 1; sum = 1; }
          for (int c = 0; c < 4; c++) {
            const uint32_t j = ju[c] < jc ? skin_remap[mm.skin][ju[c]] : 0;
            sv.joints[c] = (uint8_t)j;
            float wn = w[c] / sum;
            if (wn < 0) wn = 0;
            sv.weights[c] = (uint16_t)(wn * 65535.0f + 0.5f);
          }
        }
      }
      if (mm.skin < 0 && lim.optimize && !mesh_optimize(arena, mm, &out->opt)) { set_error(out, "arena dolu (meshopt)", nullptr); ok = false; break; }
      if (mm.skin < 0 && lim.lods) {
        const float ratios[kModelMaxLods] = {0.5f, 0.25f};
        if (!mesh_build_lods(arena, mm, ratios, kModelMaxLods, lim.lod_error)) { set_error(out, "arena dolu (lod)", nullptr); ok = false; break; }
      }
      if (mm.skin < 0 && lim.cluster_dag) {
        const DeviceClass dc = lim.cluster_class == 0 ? DeviceClass::Low : (lim.cluster_class >= 2 ? DeviceClass::High : DeviceClass::Mid);
        ClusterDag *dag = arena.alloc_array_zeroed<ClusterDag>(1);
        if (!dag) { set_error(out, "arena dolu (kume DAG)", nullptr); ok = false; break; }
        *dag = ClusterDag{};
        if (cluster_dag_build(arena, mm, cluster_dag_preset(dc), dag)) mm.dag = dag;
      }
      mi++;
      mesh_n[m]++;
    }
  }
  out->mesh_count = ok ? mi : 0;
  // Instance'lar: sahnedeki (ya da tum) dugumler, dunya matrisiyle
  uint32_t ninst = 0;
  out->instances = arena.alloc_array_zeroed<ModelInstance>(lim.max_instances);
  bool first_bound = true;
  for (cgltf_size n = 0; ok && n < data->nodes_count; n++) {
    const cgltf_node &node = data->nodes[n];
    if (!node.mesh) continue;
    cgltf_size m = (cgltf_size)(node.mesh - data->meshes);
    float wm[16];
    cgltf_node_transform_world(&node, wm);
    Mat4 world;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) world.m[c][rr] = wm[c * 4 + rr]; // glTF sutun-major
    for (uint32_t k = 0; k < mesh_n[m]; k++) {
      if (ninst >= lim.max_instances) { set_error(out, "instance siniri asildi", nullptr); ok = false; break; }
      out->instances[ninst].mesh = mesh_first[m] + k;
      out->instances[ninst].world = world;
      extend_bounds(out, world, out->meshes[mesh_first[m] + k].verts, out->meshes[mesh_first[m] + k].vertex_count, &first_bound);
      ninst++;
    }
  }
  out->instance_count = ok ? ninst : 0;
  cgltf_free(data);
  return ok;
}

// NORMAL HARITASI MIP ZINCIRI — CPU'da, her seviyeden sonra YENIDEN
// NORMALLESTIREREK.
//
// Neden blit yetmiyor: donanim blit'i dogrusal suzuyor, yani dort komsu
// normalin BILESENLERINI ortaliyor. Iki komsu normal birbirine ters egimliyse
// ortalama vektorun boyu 1 degil, ~0 olur; encode edilince (0.5,0.5,~1) yani
// "duz yuzey" cikar. Sonuc: uzaktaki yuzey sessizce duzlesir ve isik yassilar.
// Bu, hicbir seyin kizarmadigi, yalnizca goruntunun yanlis oldugu sinif.
//
// Cozum kucultmeden SONRA normallestirmek. Filtre kutu (2x2) — ilgi cekici bir
// secim degil ama dogru olan: normal haritasinda yuksek frekansi korumaya
// calismak (Kaiser vb.) aliasing'i artirir.
//
// Alfa kanali ortalanip birakiliyor: normal haritasinda kullanilmiyor, ama
// sifirlamak da bilgiyi ATMAK olurdu.
bool build_normal_mips(Arena &arena, const ModelImage &img, uint32_t levels,
                              uint8_t **data, uint32_t *sizes) {
  uint32_t w = img.width, h = img.height;
  data[0] = img.rgba;
  sizes[0] = w * h * 4;
  for (uint32_t l = 1; l < levels; l++) {
    const uint32_t pw = w, ph = h;
    w = w > 1 ? w >> 1 : 1;
    h = h > 1 ? h >> 1 : 1;
    uint8_t *dst = arena.alloc_array<uint8_t>(w * h * 4);
    if (!dst) return false;
    const uint8_t *src = data[l - 1];
    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        // Kaynak 2x2 (tek boyutta kenarda ayni piksel iki kez okunur).
        const uint32_t x0 = x * 2, x1 = (x * 2 + 1 < pw) ? x * 2 + 1 : x * 2;
        const uint32_t y0 = y * 2, y1 = (y * 2 + 1 < ph) ? y * 2 + 1 : y * 2;
        const uint32_t idx[4] = {(y0 * pw + x0) * 4, (y0 * pw + x1) * 4,
                                 (y1 * pw + x0) * 4, (y1 * pw + x1) * 4};
        float nx = 0, ny = 0, nz = 0, na = 0;
        for (int k = 0; k < 4; k++) {
          // UNORM [0,1] -> [-1,1]
          nx += (float)src[idx[k] + 0] / 255.0f * 2.0f - 1.0f;
          ny += (float)src[idx[k] + 1] / 255.0f * 2.0f - 1.0f;
          nz += (float)src[idx[k] + 2] / 255.0f * 2.0f - 1.0f;
          na += (float)src[idx[k] + 3];
        }
        float len = nx * nx + ny * ny + nz * nz;
        if (len > 1e-12f) {
          len = 1.0f / sqrtf(len);
          nx *= len; ny *= len; nz *= len;
        } else {
          // Dort normal birbirini tam goturdu: duz yuzey en az yanlis cevap.
          nx = 0; ny = 0; nz = 1;
        }
        auto enc = [](float v) -> uint8_t {
          float u = (v * 0.5f + 0.5f) * 255.0f + 0.5f;
          if (u < 0) u = 0;
          if (u > 255) u = 255;
          return (uint8_t)u;
        };
        const uint32_t o = (y * w + x) * 4;
        dst[o + 0] = enc(nx);
        dst[o + 1] = enc(ny);
        dst[o + 2] = enc(nz);
        dst[o + 3] = (uint8_t)(na * 0.25f + 0.5f);
      }
    }
    data[l] = dst;
    sizes[l] = w * h * 4;
  }
  return true;
}

bool upload_model(renderer::Renderer &r, Arena &arena, const Model &m, UploadedModel *out) {
  *out = UploadedModel{};
  out->textures = arena.alloc_array_zeroed<renderer::TextureHandle>(m.image_count ? m.image_count : 1);
  out->materials = arena.alloc_array_zeroed<renderer::MaterialHandle>(m.material_count ? m.material_count : 1);
  out->meshes = arena.alloc_array_zeroed<renderer::MeshHandle>(m.mesh_count ? m.mesh_count : 1);
  out->lod_meshes = arena.alloc_array_zeroed<renderer::MeshHandle>((m.mesh_count ? m.mesh_count : 1) * kModelMaxLods);
  if (!out->textures || !out->materials || !out->meshes || !out->lod_meshes) return false;
  for (uint32_t i = 0; i < m.image_count; i++) {
    // RENK UZAYI: sRGB doku donanimda dogrusala cozer; ORM/normal UNORM kalir
    // ve texel oldugu gibi gelir. Bu, "goruntu biraz yanlis ama hicbir sey
    // kizarmaz" sinifinin ta kendisidir — bu yuzden kapiyla olculuyor
    // (content_gltf_texture_colorspace).
    if (m.images[i].normal_map) {
      // Seviye sayisi blit yolundakiyle AYNI hesap (en buyuk kenar).
      uint32_t levels = 1, mm = m.images[i].width > m.images[i].height ? m.images[i].width : m.images[i].height;
      while (mm > 1) { mm >>= 1; levels++; }
      const size_t mark = arena.mark();
      uint8_t **lv = arena.alloc_array_zeroed<uint8_t *>(levels);
      uint32_t *sz = arena.alloc_array_zeroed<uint32_t>(levels);
      if (lv && sz && build_normal_mips(arena, m.images[i], levels, lv, sz)) {
        out->textures[i] = r.create_texture_levels(VK_FORMAT_R8G8B8A8_UNORM, m.images[i].width, m.images[i].height, levels,
                                                   (const uint8_t *const *)lv, sz);
      }
      if (out->textures[i].valid()) out->normal_mip_textures++;
      arena.reset_to(mark); // seviyeler GPU'ya kopyalandi
      // Sessiz dusme yok: CPU yolu basarisizsa blit yoluna donuluyor, ama
      // goruntu o zaman ESKI (normallestirilmemis) davranisi gosterir.
      if (!out->textures[i].valid())
        out->textures[i] = r.create_texture(m.images[i].rgba, m.images[i].width, m.images[i].height, true, false);
    } else {
      out->textures[i] = r.create_texture(m.images[i].rgba, m.images[i].width, m.images[i].height, true, m.images[i].srgb);
    }
    if (!out->textures[i].valid()) return false;
  }
  out->texture_count = m.image_count;
  for (uint32_t i = 0; i < m.material_count; i++) {
    const ModelMaterial &mm = m.materials[i];
    renderer::TextureHandle t = mm.image >= 0 ? out->textures[mm.image] : r.default_texture();
    // Dosyada pbr_metallic_roughness YOKSA eski Lambert yolu — boylece PBR'siz
    // bir varlik bugunku goruntusunu KORUR (gate: pbr_default_material_stays_near_lambert).
    if (mm.has_pbr) {
      renderer::PbrParams pbr;
      pbr.metallic = mm.metallic;
      pbr.roughness = mm.roughness;
      pbr.emissive = mm.emissive;
      // Doku basina kanallar (set 1 binding 2/3/4). Tutamac gecersizse shader
      // dali kapali kalir: dokusuz malzemenin goruntusu de maliyeti de ayni.
      renderer::PbrTextures tex;
      const uint32_t nt = m.image_count;
      if (mm.orm_image >= 0 && (uint32_t)mm.orm_image < nt) tex.orm = out->textures[mm.orm_image];
      if (mm.normal_image >= 0 && (uint32_t)mm.normal_image < nt) tex.normal = out->textures[mm.normal_image];
      if (mm.emissive_image >= 0 && (uint32_t)mm.emissive_image < nt) tex.emissive = out->textures[mm.emissive_image];
      tex.normal_scale = mm.normal_scale;
      tex.occlusion_strength = mm.occlusion_strength;
      out->materials[i] = r.create_material(t, mm.base_color, pbr, tex);
    } else {
      out->materials[i] = r.create_material(t, mm.base_color);
    }
    if (!out->materials[i].valid()) return false;
  }
  out->material_count = m.material_count;
  for (uint32_t i = 0; i < m.mesh_count; i++) {
    if (m.meshes[i].skin >= 0 && m.meshes[i].skin_verts)
      out->meshes[i] = r.create_skinned_mesh(m.meshes[i].skin_verts, m.meshes[i].vertex_count, m.meshes[i].indices, m.meshes[i].index_count);
    else
      out->meshes[i] = r.create_mesh(m.meshes[i].verts, m.meshes[i].vertex_count, m.meshes[i].indices, m.meshes[i].index_count);
    if (!out->meshes[i].valid()) return false;
    for (uint32_t k = 0; k < kModelMaxLods; k++) {
      if (!m.meshes[i].lod_index_count[k]) continue;
      out->lod_meshes[i * kModelMaxLods + k] =
          r.create_mesh(m.meshes[i].verts, m.meshes[i].vertex_count, m.meshes[i].lod_indices[k], m.meshes[i].lod_index_count[k]);
      if (!out->lod_meshes[i * kModelMaxLods + k].valid()) return false;
    }
  }
  out->mesh_count = m.mesh_count;
  return true;
}

bool model_pose_evaluate(const Model &m, uint32_t clip, float time_s, PoseScratch &scratch, ModelPose *pose) {
  if (clip >= m.clip_count || !m.clips[clip].clip || m.clips[clip].skin < 0) return false;
  const ModelClip &mc = m.clips[clip];
  const ModelSkin &sk = m.skins[mc.skin];
  sim::LocalPose lp{scratch.t, scratch.r, scratch.s, sk.joint_count};
  sim::ClipSampler::sample(mc.clip, time_s, lp);
  sim::Skeleton skel{sk.joints, sk.joint_count};
  sim::ClipSampler::to_model(skel, lp, scratch.model);
  sim::ClipSampler::skin_matrices(scratch.model, sk.inverse_bind, sk.joint_count, scratch.skin[mc.skin]);
  pose->skin_mats[mc.skin] = scratch.skin[mc.skin];
  pose->joint_count[mc.skin] = sk.joint_count;
  return true;
}

void draw_model(renderer::Renderer &r, const Model &m, const UploadedModel &u, const Mat4 &model, Vec3 tint,
                const ModelLod *lod, uint32_t lod_counts[kModelMaxLods + 1], const ModelPose *pose) {
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const ModelInstance &inst = m.instances[i];
    if (inst.mesh >= u.mesh_count) continue;
    int32_t mat = m.meshes[inst.mesh].material;
    renderer::MaterialHandle mh = (mat >= 0 && (uint32_t)mat < u.material_count) ? u.materials[mat] : r.default_material();
    const Mat4 world = model * inst.world;
    renderer::MeshHandle mesh = u.meshes[inst.mesh];
    const int32_t skin = m.meshes[inst.mesh].skin;
    if (skin >= 0) { // iskeletli: poz sart (yoksa cizilmez; sayilir)
      if (pose && (uint32_t)skin < kModelMaxSkins && pose->skin_mats[skin])
        r.draw_skinned(mesh, mh, world, tint, pose->skin_mats[skin], pose->joint_count[skin]);
      if (lod_counts) lod_counts[0]++;
      continue;
    }
    uint32_t level = 0;
    if (lod && u.lod_meshes) {
      const Vec3 p{world.m[3][0], world.m[3][1], world.m[3][2]};
      const Vec3 d{p.x - lod->camera_pos.x, p.y - lod->camera_pos.y, p.z - lod->camera_pos.z};
      const float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
      if (dist2 >= lod->distance2 * lod->distance2) level = 2;
      else if (dist2 >= lod->distance1 * lod->distance1) level = 1;
      // Istenen seviye yoksa bir alta dus (LOD2 yoksa LOD1, o da yoksa LOD0).
      while (level > 0 && !u.lod_meshes[inst.mesh * kModelMaxLods + (level - 1)].valid()) level--;
      if (level > 0) mesh = u.lod_meshes[inst.mesh * kModelMaxLods + (level - 1)];
    }
    if (lod_counts) lod_counts[level]++;
    r.draw(mesh, mh, world, tint);
  }
}

} // namespace tulpar::engine::content
