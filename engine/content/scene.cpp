#include "content/scene.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tulpar::engine::content {

namespace {
constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;

uint32_t bits_of(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
bool feq(float a, float b) { return bits_of(a) == bits_of(b); }
bool veq(Vec3 a, Vec3 b) { return feq(a.x, b.x) && feq(a.y, b.y) && feq(a.z, b.z); }

// --- yazici: bayt sayar, kapasite asilsa da uzunlugu dogru dondurur ---
struct Out {
  char *buf;
  size_t cap, len = 0;
  void put(const char *s, size_t n) {
    if (buf && len < cap) {
      size_t room = cap - len, k = n < room ? n : room;
      std::memcpy(buf + len, s, k);
    }
    len += n;
  }
  void puts(const char *s) { put(s, std::strlen(s)); }
  void ch(char c) { put(&c, 1); }
  // En kisa, bit-tam geri okunan ondalik: %.6g'den %.9g'ye ilk tutan. strtof/
  // snprintf dogru yuvarlar (glibc, musl, bionic, Apple) -> deterministik.
  void num(float v) {
    char tmp[40];
    for (int p = 6; p <= 9; p++) {
      std::snprintf(tmp, sizeof tmp, "%.*g", p, (double)v);
      if (feq(std::strtof(tmp, nullptr), v)) break;
    }
    puts(tmp);
  }
  void vec(Vec3 v) { num(v.x); ch(' '); num(v.y); ch(' '); num(v.z); }
  void str(const char *s) { ch('"'); puts(s); ch('"'); }
  void finish() {
    if (buf && cap) buf[len < cap ? len : cap - 1] = 0;
  }
};

// --- ayristirici ---
struct Tok {
  const char *s;
  size_t n;
  bool quoted;
};
constexpr size_t kMaxTok = 8;

// Satiri bosluklardan boler; "..." tek jeton (kacis yok, cift tirnak ad icinde olamaz).
// Donus: jeton sayisi; *bad = kapanmamis tirnak / fazla jeton.
size_t split(const char *line, size_t len, Tok *toks, bool *bad) {
  size_t n = 0, i = 0;
  *bad = false;
  while (i < len) {
    while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i >= len) break;
    if (line[i] == '#') break; // yorum
    if (n == kMaxTok) { *bad = true; return n; }
    if (line[i] == '"') {
      size_t j = i + 1;
      while (j < len && line[j] != '"') j++;
      if (j >= len) { *bad = true; return n; }
      toks[n++] = Tok{line + i + 1, j - i - 1, true};
      i = j + 1;
    } else {
      size_t j = i;
      while (j < len && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') j++;
      toks[n++] = Tok{line + i, j - i, false};
      i = j;
    }
  }
  return n;
}
bool tok_is(const Tok &t, const char *kw) { return !t.quoted && std::strlen(kw) == t.n && std::memcmp(t.s, kw, t.n) == 0; }

struct Parser {
  SceneError *err;
  uint32_t line = 0;
  bool fail(const char *what) {
    if (err) {
      err->line = line;
      std::snprintf(err->msg, sizeof err->msg, "satir %u: %s", line, what);
    }
    return false;
  }
  bool num(const Tok &t, float *out) {
    char tmp[64];
    if (t.quoted || t.n == 0 || t.n >= sizeof tmp) return fail("sayi bekleniyor");
    std::memcpy(tmp, t.s, t.n);
    tmp[t.n] = 0;
    char *end = nullptr;
    float v = std::strtof(tmp, &end);
    if (end != tmp + t.n || !std::isfinite(v)) return fail("gecersiz sayi");
    *out = v;
    return true;
  }
  bool uint(const Tok &t, uint32_t *out) {
    char tmp[32];
    if (t.quoted || t.n == 0 || t.n >= sizeof tmp) return fail("tamsayi bekleniyor");
    std::memcpy(tmp, t.s, t.n);
    tmp[t.n] = 0;
    char *end = nullptr;
    unsigned long v = std::strtoul(tmp, &end, 10);
    if (end != tmp + t.n || tmp[0] == '-' || v > 0xFFFFFFFFul) return fail("gecersiz tamsayi");
    *out = (uint32_t)v;
    return true;
  }
  bool vec(const Tok *t, Vec3 *out) { return num(t[0], &out->x) && num(t[1], &out->y) && num(t[2], &out->z); }
  bool str(const Tok &t, char *out, size_t cap) {
    if (!t.quoted) return fail("tirnakli metin bekleniyor");
    if (t.n >= cap) return fail("metin cok uzun");
    std::memcpy(out, t.s, t.n);
    out[t.n] = 0;
    return true;
  }
};

void write_entity(Out &o, const SceneEntity &e) {
  o.puts("nesne "); o.str(e.name); o.ch('\n');
  o.puts("  konum "); o.vec(e.pos); o.ch('\n');
  o.puts("  donus "); o.vec(e.rot_deg); o.ch('\n');
  o.puts("  olcek "); o.vec(e.scale); o.ch('\n');
  if (e.components & kSceneModel) {
    o.puts("  model "); o.num((float)e.asset); o.ch(' '); o.vec(e.tint); o.ch('\n');
  }
  if (e.components & kSceneAnim) {
    o.puts("  animasyon "); o.num((float)e.clip); o.ch(' '); o.num(e.phase); o.ch(' '); o.num(e.speed); o.ch('\n');
  }
  if (e.components & kSceneLight) {
    o.puts("  isik "); o.vec(e.light_color); o.ch(' '); o.num(e.light_intensity); o.ch(' '); o.num(e.light_radius); o.ch('\n');
  }
  if (e.components & kSceneBody) {
    o.puts("  govde ");
    if (e.shape == SceneShape::Box) { o.puts("kutu "); o.vec(e.half); }
    else { o.puts("kure "); o.num(e.radius); }
    o.puts(e.dynamic ? " dinamik" : " sabit");
    o.ch('\n');
  }
  o.puts("son\n");
}
} // namespace

// Esitlik veri modeli anlaminda: olmayan bilesenin alanlari veri DEGILDIR
// (dosyaya yazilmaz), karsilastirilmaz. Gunluk no-op tespiti de bunu kullanir.
bool scene_entity_equal(const SceneEntity &a, const SceneEntity &b) {
  if (std::strcmp(a.name, b.name) != 0 || !veq(a.pos, b.pos) || !veq(a.rot_deg, b.rot_deg) || !veq(a.scale, b.scale) ||
      a.components != b.components)
    return false;
  const uint32_t c = a.components;
  if ((c & kSceneModel) && (a.asset != b.asset || !veq(a.tint, b.tint))) return false;
  if ((c & kSceneAnim) && (a.clip != b.clip || !feq(a.phase, b.phase) || !feq(a.speed, b.speed))) return false;
  if ((c & kSceneLight) && (!veq(a.light_color, b.light_color) || !feq(a.light_intensity, b.light_intensity) || !feq(a.light_radius, b.light_radius)))
    return false;
  if (c & kSceneBody) {
    if (a.shape != b.shape || a.dynamic != b.dynamic) return false;
    if (a.shape == SceneShape::Box ? !veq(a.half, b.half) : !feq(a.radius, b.radius)) return false;
  }
  return true;
}

bool scene_world_equal(const SceneWorld &a, const SceneWorld &b) {
  return veq(a.sun_dir, b.sun_dir) && veq(a.ambient, b.ambient) && feq(a.sun_diffuse, b.sun_diffuse) &&
         veq(a.shadow_center, b.shadow_center) && feq(a.shadow_radius, b.shadow_radius) && feq(a.shadow_depth, b.shadow_depth) &&
         veq(a.cam_target, b.cam_target) && feq(a.cam_yaw, b.cam_yaw) && feq(a.cam_pitch, b.cam_pitch) && feq(a.cam_radius, b.cam_radius);
}

int32_t SceneDesc::add_asset(const char *path) {
  for (uint32_t i = 0; i < asset_count; i++)
    if (std::strcmp(assets[i], path) == 0) return (int32_t)i;
  if (asset_count >= kSceneMaxAssets || std::strlen(path) >= kScenePathLen) return -1;
  std::strcpy(assets[asset_count], path);
  return (int32_t)asset_count++;
}
int32_t SceneDesc::find_entity(const char *name) const {
  for (uint32_t i = 0; i < entity_count; i++)
    if (std::strcmp(entities[i].name, name) == 0) return (int32_t)i;
  return -1;
}
bool SceneDesc::insert_entity(uint32_t at, const SceneEntity &e) {
  if (entity_count >= kSceneMaxEntities || at > entity_count) return false;
  for (uint32_t i = entity_count; i > at; i--) entities[i] = entities[i - 1];
  entities[at] = e;
  entity_count++;
  return true;
}
bool SceneDesc::remove_entity(uint32_t at) {
  if (at >= entity_count) return false;
  for (uint32_t i = at + 1; i < entity_count; i++) entities[i - 1] = entities[i];
  entity_count--;
  return true;
}

size_t scene_write(const SceneDesc &d, char *buf, size_t cap) {
  Out o{buf, cap};
  o.puts("tulpar-sahne "); o.num((float)kSceneVersion); o.ch('\n');
  o.puts("isik-yon "); o.vec(d.sun_dir); o.ch('\n');
  o.puts("isik-ortam "); o.vec(d.ambient); o.ch('\n');
  o.puts("isik-gunes "); o.num(d.sun_diffuse); o.ch('\n');
  o.puts("golge "); o.vec(d.shadow_center); o.ch(' '); o.num(d.shadow_radius); o.ch(' '); o.num(d.shadow_depth); o.ch('\n');
  o.puts("kamera "); o.vec(d.cam_target); o.ch(' '); o.num(d.cam_yaw); o.ch(' '); o.num(d.cam_pitch); o.ch(' '); o.num(d.cam_radius); o.ch('\n');
  for (uint32_t i = 0; i < d.asset_count; i++) { o.puts("kaynak "); o.str(d.assets[i]); o.ch('\n'); }
  for (uint32_t i = 0; i < d.entity_count; i++) write_entity(o, d.entities[i]);
  o.finish();
  return o.len;
}

bool scene_parse(const char *text, size_t len, SceneDesc *out, SceneError *err) {
  *out = SceneDesc{};
  Parser p{err};
  bool in_entity = false, header = false;
  SceneEntity cur{};
  uint32_t seen = 0; // varlik icinde gorulen anahtarlar (yineleme yasak)
  enum { kKonum = 1u << 8, kDonus = 1u << 9, kOlcek = 1u << 10 };
  size_t i = 0;
  while (i <= len) {
    size_t j = i;
    while (j < len && text[j] != '\n') j++;
    p.line++;
    Tok t[kMaxTok];
    bool bad = false;
    size_t n = split(text + i, j - i, t, &bad);
    i = j + 1;
    if (bad) return p.fail("tirnak kapanmadi ya da fazla jeton");
    if (n == 0) { if (j >= len) break; continue; }
    if (!header) {
      if (n != 2 || !tok_is(t[0], "tulpar-sahne")) return p.fail("baslik 'tulpar-sahne 1' bekleniyor");
      uint32_t v = 0;
      if (!p.uint(t[1], &v)) return false;
      if (v != kSceneVersion) return p.fail("desteklenmeyen sahne surumu");
      header = true;
      continue;
    }
    if (in_entity) {
      if (tok_is(t[0], "son")) {
        if (n != 1) return p.fail("'son' tek basina olmali");
        if (!out->insert_entity(out->entity_count, cur)) return p.fail("cok fazla varlik");
        in_entity = false;
        continue;
      }
      if (tok_is(t[0], "konum")) {
        if (n != 4 || (seen & kKonum)) return p.fail("konum x y z (bir kez)");
        seen |= kKonum;
        if (!p.vec(t + 1, &cur.pos)) return false;
      } else if (tok_is(t[0], "donus")) {
        if (n != 4 || (seen & kDonus)) return p.fail("donus x y z (bir kez)");
        seen |= kDonus;
        if (!p.vec(t + 1, &cur.rot_deg)) return false;
      } else if (tok_is(t[0], "olcek")) {
        if (n != 4 || (seen & kOlcek)) return p.fail("olcek x y z (bir kez)");
        seen |= kOlcek;
        if (!p.vec(t + 1, &cur.scale)) return false;
      } else if (tok_is(t[0], "model")) {
        if (n != 5 || (seen & kSceneModel)) return p.fail("model kaynak r g b (bir kez)");
        uint32_t a = 0;
        if (!p.uint(t[1], &a)) return false;
        if (a >= out->asset_count) return p.fail("model kaynak indeksi tanimsiz (kaynak satiri once gelmeli)");
        cur.asset = (int32_t)a;
        if (!p.vec(t + 2, &cur.tint)) return false;
        seen |= kSceneModel;
      } else if (tok_is(t[0], "animasyon")) {
        if (n != 4 || (seen & kSceneAnim)) return p.fail("animasyon klip faz hiz (bir kez)");
        if (!p.uint(t[1], &cur.clip) || !p.num(t[2], &cur.phase) || !p.num(t[3], &cur.speed)) return false;
        seen |= kSceneAnim;
      } else if (tok_is(t[0], "isik")) {
        if (n != 6 || (seen & kSceneLight)) return p.fail("isik r g b siddet yaricap (bir kez)");
        if (!p.vec(t + 1, &cur.light_color) || !p.num(t[4], &cur.light_intensity) || !p.num(t[5], &cur.light_radius)) return false;
        seen |= kSceneLight;
      } else if (tok_is(t[0], "govde")) {
        if (seen & kSceneBody) return p.fail("govde bir kez");
        const Tok *last = nullptr;
        if (n >= 2 && tok_is(t[1], "kutu")) {
          if (n != 6) return p.fail("govde kutu hx hy hz dinamik|sabit");
          cur.shape = SceneShape::Box;
          if (!p.vec(t + 2, &cur.half)) return false;
          last = &t[5];
        } else if (n >= 2 && tok_is(t[1], "kure")) {
          if (n != 4) return p.fail("govde kure yaricap dinamik|sabit");
          cur.shape = SceneShape::Sphere;
          if (!p.num(t[2], &cur.radius)) return false;
          last = &t[3];
        } else return p.fail("govde kutu|kure ...");
        if (tok_is(*last, "dinamik")) cur.dynamic = true;
        else if (tok_is(*last, "sabit")) cur.dynamic = false;
        else return p.fail("govde sonu dinamik|sabit");
        seen |= kSceneBody;
      } else return p.fail("varlik icinde bilinmeyen anahtar");
      cur.components = seen & 0xFFu;
      continue;
    }
    // ust duzey
    if (tok_is(t[0], "nesne")) {
      if (n != 2) return p.fail("nesne \"ad\"");
      cur = SceneEntity{};
      seen = 0;
      if (!p.str(t[1], cur.name, sizeof cur.name)) return false;
      if (cur.name[0] == 0) return p.fail("varlik adi bos");
      in_entity = true;
    } else if (tok_is(t[0], "kaynak")) {
      if (n != 2) return p.fail("kaynak \"yol\"");
      char path[kScenePathLen];
      if (!p.str(t[1], path, sizeof path)) return false;
      if (path[0] == 0 || out->asset_count >= kSceneMaxAssets) return p.fail("kaynak bos ya da cok fazla kaynak");
      for (uint32_t k = 0; k < out->asset_count; k++)
        if (std::strcmp(out->assets[k], path) == 0) return p.fail("yinelenen kaynak");
      out->add_asset(path);
    } else if (tok_is(t[0], "isik-yon")) {
      if (n != 4 || !p.vec(t + 1, &out->sun_dir)) return n != 4 ? p.fail("isik-yon x y z") : false;
    } else if (tok_is(t[0], "isik-ortam")) {
      if (n != 4 || !p.vec(t + 1, &out->ambient)) return n != 4 ? p.fail("isik-ortam r g b") : false;
    } else if (tok_is(t[0], "isik-gunes")) {
      if (n != 2 || !p.num(t[1], &out->sun_diffuse)) return n != 2 ? p.fail("isik-gunes f") : false;
    } else if (tok_is(t[0], "golge")) {
      if (n != 6) return p.fail("golge cx cy cz yaricap derinlik");
      if (!p.vec(t + 1, &out->shadow_center) || !p.num(t[4], &out->shadow_radius) || !p.num(t[5], &out->shadow_depth)) return false;
    } else if (tok_is(t[0], "kamera")) {
      if (n != 7) return p.fail("kamera tx ty tz yaw pitch yaricap");
      if (!p.vec(t + 1, &out->cam_target) || !p.num(t[4], &out->cam_yaw) || !p.num(t[5], &out->cam_pitch) || !p.num(t[6], &out->cam_radius))
        return false;
    } else if (tok_is(t[0], "son")) return p.fail("'son' varlik disinda");
    else return p.fail("bilinmeyen anahtar");
  }
  if (!header) return p.fail("bos dosya: baslik yok");
  if (in_entity) return p.fail("varlik 'son' ile kapanmadi");
  return true;
}

void scene_dir_of(const char *path, char *out, size_t cap) {
  const char *slash = std::strrchr(path, '/');
  if (!slash) { std::snprintf(out, cap, "."); return; }
  size_t n = (size_t)(slash - path);
  if (n == 0) n = 1; // "/x" -> "/"
  if (n >= cap) n = cap - 1;
  std::memcpy(out, path, n);
  out[n] = 0;
}

bool scene_load(Arena &scratch, const char *path, SceneDesc *out, SceneError *err) {
  FILE *f = std::fopen(path, "rb");
  if (!f) {
    if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "dosya acilamadi: %s", path); }
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz < 0 || sz > (16 << 20)) { std::fclose(f); if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "dosya boyutu gecersiz"); } return false; }
  char *buf = scratch.alloc_array<char>((size_t)sz + 1);
  if (!buf) { std::fclose(f); if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "arena dolu"); } return false; }
  size_t got = std::fread(buf, 1, (size_t)sz, f);
  std::fclose(f);
  buf[got] = 0;
  return scene_parse(buf, got, out, err);
}

bool scene_save(Arena &scratch, const SceneDesc &d, const char *path, SceneError *err) {
  size_t need = scene_write(d, nullptr, 0) + 1;
  char *buf = scratch.alloc_array<char>(need);
  if (!buf) { if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "arena dolu"); } return false; }
  scene_write(d, buf, need);
  FILE *f = std::fopen(path, "wb");
  if (!f) { if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "dosya yazilamadi: %s", path); } return false; }
  const bool ok = std::fwrite(buf, 1, need - 1, f) == need - 1;
  std::fclose(f);
  if (!ok && err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "yazma eksik: %s", path); }
  return ok;
}

Quat scene_entity_rotation(const SceneEntity &e) {
  const Quat qx = Quat::axis_angle({1, 0, 0}, e.rot_deg.x * kDeg2Rad);
  const Quat qy = Quat::axis_angle({0, 1, 0}, e.rot_deg.y * kDeg2Rad);
  const Quat qz = Quat::axis_angle({0, 0, 1}, e.rot_deg.z * kDeg2Rad);
  return normalize(qz * qy * qx);
}
Mat4 scene_entity_matrix(const SceneEntity &e) {
  return Mat4::translate(e.pos) * Mat4::rotate({0, 0, 1}, e.rot_deg.z * kDeg2Rad) * Mat4::rotate({0, 1, 0}, e.rot_deg.y * kDeg2Rad) *
         Mat4::rotate({1, 0, 0}, e.rot_deg.x * kDeg2Rad) * Mat4::scale(e.scale);
}

SceneBounds scene_entity_local_bounds(const SceneEntity &e, const SceneBounds *model) {
  SceneBounds b{{-0.15f, -0.15f, -0.15f}, {0.15f, 0.15f, 0.15f}}; // isaret kutusu (bos / isik)
  bool any = false;
  auto grow = [&](Vec3 lo, Vec3 hi) {
    if (!any) { b.lo = lo; b.hi = hi; any = true; return; }
    b.lo = {b.lo.x < lo.x ? b.lo.x : lo.x, b.lo.y < lo.y ? b.lo.y : lo.y, b.lo.z < lo.z ? b.lo.z : lo.z};
    b.hi = {b.hi.x > hi.x ? b.hi.x : hi.x, b.hi.y > hi.y ? b.hi.y : hi.y, b.hi.z > hi.z ? b.hi.z : hi.z};
  };
  if ((e.components & kSceneModel) && model) grow(model->lo, model->hi);
  if (e.components & kSceneBody) {
    if (e.shape == SceneShape::Box) grow(e.half * -1.0f, e.half);
    else grow({-e.radius, -e.radius, -e.radius}, {e.radius, e.radius, e.radius});
  }
  return b;
}
SceneBounds scene_world_bounds(const SceneBounds &local, const Mat4 &m) {
  SceneBounds w{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
  for (int i = 0; i < 8; i++) {
    const Vec4 c = m * Vec4{i & 1 ? local.hi.x : local.lo.x, i & 2 ? local.hi.y : local.lo.y, i & 4 ? local.hi.z : local.lo.z, 1.0f};
    w.lo = {c.x < w.lo.x ? c.x : w.lo.x, c.y < w.lo.y ? c.y : w.lo.y, c.z < w.lo.z ? c.z : w.lo.z};
    w.hi = {c.x > w.hi.x ? c.x : w.hi.x, c.y > w.hi.y ? c.y : w.hi.y, c.z > w.hi.z ? c.z : w.hi.z};
  }
  return w;
}
bool scene_ray_aabb(Vec3 o, Vec3 d, const SceneBounds &b, float *t) {
  float tmin = 0.0f, tmax = 1e30f;
  const float os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
  const float lo[3] = {b.lo.x, b.lo.y, b.lo.z}, hi[3] = {b.hi.x, b.hi.y, b.hi.z};
  for (int i = 0; i < 3; i++) {
    if (std::fabs(ds[i]) < 1e-12f) {
      if (os[i] < lo[i] || os[i] > hi[i]) return false; // eksene paralel, dilim disinda
      continue;
    }
    const float inv = 1.0f / ds[i];
    float t0 = (lo[i] - os[i]) * inv, t1 = (hi[i] - os[i]) * inv;
    if (t0 > t1) { const float tmp = t0; t0 = t1; t1 = tmp; }
    if (t0 > tmin) tmin = t0;
    if (t1 < tmax) tmax = t1;
    if (tmin > tmax) return false;
  }
  if (t) *t = tmin;
  return true;
}
int32_t scene_pick(const SceneBounds *bounds, uint32_t n, Vec3 origin, Vec3 dir, float *t_out) {
  int32_t best = -1;
  float best_t = 1e30f;
  for (uint32_t i = 0; i < n; i++) {
    float t = 0;
    if (scene_ray_aabb(origin, dir, bounds[i], &t) && t < best_t) { best_t = t; best = (int32_t)i; }
  }
  if (t_out) *t_out = best_t;
  return best;
}

uint32_t scene_spawn_bodies(const SceneDesc &d, sim::Physics &ph, sim::BodyId *ids) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const SceneEntity &e = d.entities[i];
    ids[i] = sim::BodyId{};
    if (!(e.components & kSceneBody)) continue;
    if (e.shape == SceneShape::Box) ids[i] = ph.add_box(e.half * e.scale, e.pos, scene_entity_rotation(e), e.dynamic);
    else ids[i] = ph.add_sphere(e.radius * e.scale.x, e.pos, e.dynamic);
    if (ids[i].valid()) n++;
  }
  return n;
}
void scene_remove_bodies(sim::Physics &ph, sim::BodyId *ids, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    if (ids[i].valid()) ph.remove(ids[i]);
    ids[i] = sim::BodyId{};
  }
}
Mat4 scene_body_matrix(const SceneEntity &e, const sim::Physics &ph, sim::BodyId id) {
  return Mat4::translate(ph.position(id)) * to_mat4(ph.rotation(id)) * Mat4::scale(e.scale);
}

// --- islem gunlugu ---
bool SceneHistory::init(Arena &arena, uint32_t capacity) {
  ops_ = arena.alloc_array<SceneOp>(capacity);
  cap_ = ops_ ? capacity : 0;
  count_ = cursor_ = 0;
  return ops_ != nullptr;
}
bool SceneHistory::push(const SceneOp &op) {
  if (cap_ == 0) return false;
  count_ = cursor_; // yinele kuyrugu silinir
  if (count_ == cap_) {
    for (uint32_t i = 1; i < count_; i++) ops_[i - 1] = ops_[i];
    count_--;
  }
  ops_[count_++] = op;
  cursor_ = count_;
  return true;
}
bool SceneHistory::set_entity(SceneDesc &d, uint32_t i, const SceneEntity &after) {
  if (i >= d.entity_count || scene_entity_equal(d.entities[i], after)) return false;
  SceneOp op{};
  op.kind = SceneOp::Set; op.index = i; op.before = d.entities[i]; op.after = after;
  d.entities[i] = after;
  return push(op);
}
bool SceneHistory::add_entity(SceneDesc &d, const SceneEntity &e) {
  const uint32_t at = d.entity_count;
  if (!d.insert_entity(at, e)) return false;
  SceneOp op{};
  op.kind = SceneOp::Add; op.index = at; op.after = e;
  return push(op);
}
bool SceneHistory::remove_entity(SceneDesc &d, uint32_t i) {
  if (i >= d.entity_count) return false;
  SceneOp op{};
  op.kind = SceneOp::Remove; op.index = i; op.before = d.entities[i];
  d.remove_entity(i);
  return push(op);
}
bool SceneHistory::set_world(SceneDesc &d, const SceneWorld &after) {
  if (scene_world_equal(d.world(), after)) return false;
  SceneOp op{};
  op.kind = SceneOp::World;
  op.world_before = d.world();
  op.world_after = after;
  d.set_world(after);
  return push(op);
}
bool SceneHistory::undo(SceneDesc &d) {
  if (cursor_ == 0) return false;
  const SceneOp &op = ops_[cursor_ - 1];
  switch (op.kind) {
  case SceneOp::Set: if (op.index >= d.entity_count) return false; d.entities[op.index] = op.before; break;
  case SceneOp::Add: if (!d.remove_entity(op.index)) return false; break;
  case SceneOp::Remove: if (!d.insert_entity(op.index, op.before)) return false; break;
  case SceneOp::World: d.set_world(op.world_before); break;
  }
  cursor_--;
  return true;
}
bool SceneHistory::redo(SceneDesc &d) {
  if (cursor_ >= count_) return false;
  const SceneOp &op = ops_[cursor_];
  switch (op.kind) {
  case SceneOp::Set: if (op.index >= d.entity_count) return false; d.entities[op.index] = op.after; break;
  case SceneOp::Add: if (!d.insert_entity(op.index, op.after)) return false; break;
  case SceneOp::Remove: if (!d.remove_entity(op.index)) return false; break;
  case SceneOp::World: d.set_world(op.world_after); break;
  }
  cursor_++;
  return true;
}

} // namespace tulpar::engine::content
