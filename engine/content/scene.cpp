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
  // Ebeveyn/bayrak yalniz VARSA yazilir: duz sahnelerin (kok varliklar,
  // bayraksiz) baytlari Faz E2 oncesiyle AYNI kalsin — editor.sahne kanonik
  // kapisi bunu olcuyor.
  if (e.parent >= 0) { o.puts("  ebeveyn "); o.num((float)e.parent); o.ch('\n'); }
  if (e.flags) { o.puts("  bayrak "); o.num((float)e.flags); o.ch('\n'); }
  if (e.components & kSceneModel) {
    o.puts("  model "); o.num((float)e.asset); o.ch(' '); o.vec(e.tint); o.ch('\n');
  }
  if (e.components & kSceneAnim) {
    o.puts("  animasyon "); o.num((float)e.clip); o.ch(' '); o.num(e.phase); o.ch(' '); o.num(e.speed); o.ch('\n');
  }
  if (e.components & kSceneLight) {
    o.puts("  isik "); o.vec(e.light_color); o.ch(' '); o.num(e.light_intensity); o.ch(' '); o.num(e.light_radius);
    o.puts(e.light_type == SceneLightType::Directional ? " yonlu\n" : " nokta\n");
  }
  if (e.components & kSceneBody) {
    o.puts("  govde ");
    if (e.shape == SceneShape::Box) { o.puts("kutu "); o.vec(e.half); }
    else { o.puts("kure "); o.num(e.radius); }
    o.puts(e.dynamic ? " dinamik" : " sabit");
    o.ch('\n');
  }
  if (e.components & kSceneCamera) {
    o.puts("  kamera "); o.num(e.cam_fov); o.ch(' '); o.num(e.cam_near); o.ch(' '); o.num(e.cam_far); o.ch('\n');
  }
  if (e.components & kSceneAudio) {
    o.puts("  ses \""); o.puts(e.audio_clip); o.puts("\" "); o.num(e.audio_volume); o.ch(' '); o.num(e.audio_pitch);
    o.puts(e.audio_loop ? " dongu" : " tek");
    o.puts(e.audio_spatial ? " uzamsal" : " 2b");
    o.ch('\n');
  }
  if (e.components & kSceneScript) {
    o.puts("  betik \""); o.puts(e.script_file); o.puts(e.script_enabled ? "\" etkin\n" : "\" kapali\n");
  }
  o.puts("son\n");
}
} // namespace

// Esitlik veri modeli anlaminda: olmayan bilesenin alanlari veri DEGILDIR
// (dosyaya yazilmaz), karsilastirilmaz. Gunluk no-op tespiti de bunu kullanir.
bool scene_entity_equal(const SceneEntity &a, const SceneEntity &b) {
  if (std::strcmp(a.name, b.name) != 0 || !veq(a.pos, b.pos) || !veq(a.rot_deg, b.rot_deg) || !veq(a.scale, b.scale) ||
      a.parent != b.parent || a.flags != b.flags || a.components != b.components)
    return false;
  const uint32_t c = a.components;
  if ((c & kSceneModel) && (a.asset != b.asset || !veq(a.tint, b.tint))) return false;
  if ((c & kSceneAnim) && (a.clip != b.clip || !feq(a.phase, b.phase) || !feq(a.speed, b.speed))) return false;
  if ((c & kSceneLight) && (!veq(a.light_color, b.light_color) || !feq(a.light_intensity, b.light_intensity) || !feq(a.light_radius, b.light_radius) ||
                            a.light_type != b.light_type))
    return false;
  if (c & kSceneBody) {
    if (a.shape != b.shape || a.dynamic != b.dynamic) return false;
    if (a.shape == SceneShape::Box ? !veq(a.half, b.half) : !feq(a.radius, b.radius)) return false;
  }
  if (c & kSceneCamera) {
    if (!feq(a.cam_fov, b.cam_fov) || !feq(a.cam_near, b.cam_near) || !feq(a.cam_far, b.cam_far)) return false;
  }
  if (c & kSceneAudio) {
    if (std::strcmp(a.audio_clip, b.audio_clip) != 0 || !feq(a.audio_volume, b.audio_volume) ||
        !feq(a.audio_pitch, b.audio_pitch) || a.audio_loop != b.audio_loop || a.audio_spatial != b.audio_spatial) return false;
  }
  if (c & kSceneScript) {
    if (std::strcmp(a.script_file, b.script_file) != 0 || a.script_enabled != b.script_enabled) return false;
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
  // Ortaya ekleme: mevcut ebeveyn indeksleri kayar. SONA ekleme HICBIR SEYI
  // kaydirmaz — ayristirma sirasinda henuz olusmamis varliga bakan ileri
  // referanslar (parent >= entity_count) bozulmasin diye (bkz. baslik).
  if (at < entity_count)
    for (uint32_t i = 0; i < entity_count; i++)
      if (entities[i].parent >= (int32_t)at) entities[i].parent++;
  for (uint32_t i = entity_count; i > at; i--) entities[i] = entities[i - 1];
  entities[at] = e; // e.parent cagirandan geldigi gibi: yeni indeks uzayinda
  entity_count++;
  return true;
}
bool SceneDesc::remove_entity(uint32_t at) {
  if (at >= entity_count) return false;
  // 1. Cocuklar buyukbabaya (alt agac sessizce yok olmasin).
  const int32_t gp = entities[at].parent;
  for (uint32_t i = 0; i < entity_count; i++)
    if (entities[i].parent == (int32_t)at) entities[i].parent = gp;
  // 2. Kaydirma. gp > at ise 1. adimda verilen deger de burada duzelir; bu
  //    yuzden iki adim AYRI ve bu sirada.
  for (uint32_t i = 0; i < entity_count; i++)
    if (entities[i].parent > (int32_t)at) entities[i].parent--;
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
  enum { kKonum = 1u << 8, kDonus = 1u << 9, kOlcek = 1u << 10, kEbeveyn = 1u << 11, kBayrak = 1u << 12 };
  // Ebeveyn satirlari: ILERI referans serbest oldugu icin gecerlilik ancak
  // dosya bitince olculebilir; hata yine de DOGRU satiri gostersin diye her
  // varligin "ebeveyn" satiri saklanir (0 = satir yok).
  static_assert(kSceneMaxEntities <= 4096, "parent_line yigin uzerinde");
  uint32_t parent_line[kSceneMaxEntities] = {0};
  uint32_t cur_parent_line = 0;
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
        const uint32_t at = out->entity_count;
        if (!out->insert_entity(at, cur)) return p.fail("cok fazla varlik");
        parent_line[at] = cur_parent_line;
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
      } else if (tok_is(t[0], "ebeveyn")) {
        if (n != 2 || (seen & kEbeveyn)) return p.fail("ebeveyn <indeks> (bir kez)");
        seen |= kEbeveyn;
        uint32_t pi = 0;
        if (!p.uint(t[1], &pi)) return false;
        if (pi >= kSceneMaxEntities) return p.fail("ebeveyn indeksi sinir disi");
        cur.parent = (int32_t)pi;
        cur_parent_line = p.line; // gecerlilik dosya bitince olculur
      } else if (tok_is(t[0], "bayrak")) {
        if (n != 2 || (seen & kBayrak)) return p.fail("bayrak <maske> (bir kez)");
        seen |= kBayrak;
        uint32_t fl = 0;
        if (!p.uint(t[1], &fl)) return false;
        if (fl & ~(uint32_t)(kSceneHidden | kSceneLocked)) return p.fail("bilinmeyen bayrak biti");
        cur.flags = fl;
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
        // n==6: eski dosya (turu yok, varsayilan Nokta -- geriye donuk okunur).
        // n>=7: 7. token tur anahtar sozcugu ("nokta"/"yonlu").
        if (n < 6 || (seen & kSceneLight)) return p.fail("isik r g b siddet yaricap [nokta|yonlu] (bir kez)");
        if (!p.vec(t + 1, &cur.light_color) || !p.num(t[4], &cur.light_intensity) || !p.num(t[5], &cur.light_radius)) return false;
        cur.light_type = SceneLightType::Point;
        if (n >= 7) {
          if (tok_is(t[6], "yonlu")) cur.light_type = SceneLightType::Directional;
          else if (tok_is(t[6], "nokta")) cur.light_type = SceneLightType::Point;
          else return p.fail("isik turu nokta|yonlu olmali");
        }
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
      } else if (tok_is(t[0], "kamera")) {
        if (n != 4 || (seen & kSceneCamera)) return p.fail("kamera fov yakin uzak (bir kez)");
        if (!p.num(t[1], &cur.cam_fov) || !p.num(t[2], &cur.cam_near) || !p.num(t[3], &cur.cam_far)) return false;
        seen |= kSceneCamera;
      } else if (tok_is(t[0], "ses")) {
        if (n < 4 || (seen & kSceneAudio)) return p.fail("ses \"klip\" ses_duzeyi perde [dongu|tek] [uzamsal|2b] (bir kez)");
        if (!p.str(t[1], cur.audio_clip, sizeof cur.audio_clip) || !p.num(t[2], &cur.audio_volume) || !p.num(t[3], &cur.audio_pitch)) return false;
        if (n >= 5) cur.audio_loop = tok_is(t[4], "dongu");
        if (n >= 6) cur.audio_spatial = tok_is(t[5], "uzamsal");
        seen |= kSceneAudio;
      } else if (tok_is(t[0], "betik")) {
        if (n < 2 || (seen & kSceneScript)) return p.fail("betik \"dosya\" [etkin|kapali] (bir kez)");
        if (!p.str(t[1], cur.script_file, sizeof cur.script_file)) return false;
        if (n >= 3) cur.script_enabled = tok_is(t[2], "etkin");
        seen |= kSceneScript;
      } else return p.fail("varlik icinde bilinmeyen anahtar");
      cur.components = seen & 0xFFu;
      continue;
    }
    // ust duzey
    if (tok_is(t[0], "nesne")) {
      if (n != 2) return p.fail("nesne \"ad\"");
      cur = SceneEntity{};
      seen = 0;
      cur_parent_line = 0;
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
  // Agac gecerliligi: SESSIZ degil. Bozuk bir ebeveyn zinciri kabul edilirse
  // dunya matrisi hesabi ya yanlis olur ya donguye girer; ikisi de burada
  // durur ve kullaniciya SATIR NUMARASIYLA soylenir.
  uint32_t bad = 0;
  if (!scene_tree_validate(*out, &bad)) {
    p.line = parent_line[bad] ? parent_line[bad] : 0;
    const int32_t pp = out->entities[bad].parent;
    char what[120];
    if (pp == (int32_t)bad) std::snprintf(what, sizeof what, "varlik kendi ebeveyni olamaz (\"%s\")", out->entities[bad].name);
    else if (pp < 0 || (uint32_t)pp >= out->entity_count)
      std::snprintf(what, sizeof what, "ebeveyn indeksi tanimsiz: %d (\"%s\")", pp, out->entities[bad].name);
    else
      std::snprintf(what, sizeof what, "ebeveyn dongusu ya da %u'dan derin zincir (\"%s\")", kSceneMaxDepth, out->entities[bad].name);
    return p.fail(what);
  }
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

// --- sahne agaci (Faz E2) ----------------------------------------------------
namespace {
// Zinciri kok'e dogru yurur: out[0] = i, out[n-1] = kok. Donus adim sayisi
// (kok icin 1), 0 = BOZUK (sinir disi indeks, dongu ya da tavan asimi).
// Ozyineleme yok; tavan sayesinde dongude bile SONLU.
uint32_t chain_up(const SceneDesc &d, uint32_t i, uint32_t *out) {
  uint32_t n = 0;
  int32_t cur = (int32_t)i;
  while (cur >= 0) {
    if ((uint32_t)cur >= d.entity_count) return 0;
    if (n > kSceneMaxDepth) return 0; // derinlik kSceneMaxDepth -> n = tavan+1
    out[n++] = (uint32_t)cur;
    cur = d.entities[cur].parent;
  }
  return n;
}
// b (ve ustleri) a'ya ulasir mi — yani a, b'nin atasi mi / a == b mi.
// Yeniden ebeveynleme dongu testi: yeni ebeveyn cocugun altindaysa yasak.
// Bozuk zincir "evet" sayilir: supheli durumda REDDET.
bool reaches(const SceneDesc &d, uint32_t a, int32_t b) {
  uint32_t guard = 0;
  while (b >= 0) {
    if ((uint32_t)b >= d.entity_count) return true;
    if ((uint32_t)b == a) return true;
    if (++guard > kSceneMaxDepth + 1) return true;
    b = d.entities[b].parent;
  }
  return false;
}
// T*Rz*Ry*Rx*S ayristirmasi — scene_entity_matrix'in tersi, ImGuizmo'nun
// DecomposeMatrixToComponents'iyle ayni sozlesme (olcek = sutun boylari,
// Euler ZYX). Ayna (det < 0) TEK eksene, X'e yuklenir: isaret bir yere
// gitmek zorunda ve secimin belirlenimli olmasi kaynaktakiyle ayni eksen
// olmasindan onemli. Gimbal kilidinde (|sin b| ~ 1) Rx ile Rz ayrisamaz;
// X sifirlanip tum aci Z'ye verilir (tek cozum secilmis olur).
void decompose_trs(const Mat4 &m, Vec3 *pos, Vec3 *rot_deg, Vec3 *scale) {
  const Vec3 c0{m.m[0][0], m.m[0][1], m.m[0][2]};
  const Vec3 c1{m.m[1][0], m.m[1][1], m.m[1][2]};
  const Vec3 c2{m.m[2][0], m.m[2][1], m.m[2][2]};
  float s0 = length(c0);
  const float s1 = length(c1), s2 = length(c2);
  if (dot(c0, cross(c1, c2)) < 0) s0 = -s0;
  const float i0 = s0 != 0 ? 1.0f / s0 : 0.0f, i1 = s1 != 0 ? 1.0f / s1 : 0.0f, i2 = s2 != 0 ? 1.0f / s2 : 0.0f;
  const Vec3 r0 = c0 * i0, r1 = c1 * i1, r2 = c2 * i2;
  // Sutun-major: satir-sutun gosterimiyle R[sat][sut] = m[sut][sat].
  float sy = -r0.z; // -R[2][0]
  if (sy > 1.0f) sy = 1.0f;
  else if (sy < -1.0f) sy = -1.0f;
  const float ay = std::asin(sy);
  float ax, az;
  if (std::fabs(sy) > 0.999999f) {
    ax = 0.0f;
    az = std::atan2(-r1.x, r1.y); // b = ±90: yalniz (a ∓ c) olculebilir
  } else {
    ax = std::atan2(r1.z, r2.z);  // R[2][1], R[2][2]
    az = std::atan2(r0.y, r0.x);  // R[1][0], R[0][0]
  }
  const float kRad2Deg = 180.0f / 3.14159265358979f;
  *pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
  *rot_deg = {ax * kRad2Deg, ay * kRad2Deg, az * kRad2Deg};
  *scale = {s0, s1, s2};
}
} // namespace

bool scene_tree_validate(const SceneDesc &d, uint32_t *bad_index) {
  uint32_t chain[kSceneMaxDepth + 2];
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const int32_t p = d.entities[i].parent;
    const bool range_bad = p < -1 || (p >= 0 && (uint32_t)p >= d.entity_count);
    if (range_bad || p == (int32_t)i || chain_up(d, i, chain) == 0) {
      if (bad_index) *bad_index = i;
      return false;
    }
  }
  if (bad_index) *bad_index = 0;
  return true;
}

uint32_t scene_tree_depth(const SceneDesc &d, uint32_t i) {
  if (i >= d.entity_count) return 0;
  uint32_t chain[kSceneMaxDepth + 2];
  const uint32_t n = chain_up(d, i, chain);
  return n ? n - 1 : kSceneMaxDepth; // bozuk zincir: tavan (sessiz 0 degil)
}

Mat4 scene_entity_world_matrix(const SceneDesc &d, uint32_t i) {
  if (i >= d.entity_count) return Mat4::identity();
  const SceneEntity &e = d.entities[i];
  if (e.parent < 0) return scene_entity_matrix(e); // kokte BIT-TAM erken donus
  uint32_t chain[kSceneMaxDepth + 2];
  const uint32_t n = chain_up(d, i, chain);
  if (n == 0) return scene_entity_matrix(e); // bozuk zincir: yerelde kal
  Mat4 m = scene_entity_matrix(d.entities[chain[n - 1]]);
  for (uint32_t k = n - 1; k > 0; k--) m = m * scene_entity_matrix(d.entities[chain[k - 1]]);
  return m;
}

Quat scene_entity_world_rotation(const SceneDesc &d, uint32_t i) {
  if (i >= d.entity_count) return Quat::identity();
  const SceneEntity &e = d.entities[i];
  if (e.parent < 0) return scene_entity_rotation(e); // kokte BIT-TAM
  uint32_t chain[kSceneMaxDepth + 2];
  const uint32_t n = chain_up(d, i, chain);
  if (n == 0) return scene_entity_rotation(e);
  Quat q = scene_entity_rotation(d.entities[chain[n - 1]]);
  for (uint32_t k = n - 1; k > 0; k--) q = q * scene_entity_rotation(d.entities[chain[k - 1]]);
  return normalize(q);
}

Vec3 scene_entity_world_scale(const SceneDesc &d, uint32_t i) {
  if (i >= d.entity_count) return Vec3{1, 1, 1};
  const SceneEntity &e = d.entities[i];
  if (e.parent < 0) return e.scale; // kokte BIT-TAM
  uint32_t chain[kSceneMaxDepth + 2];
  const uint32_t n = chain_up(d, i, chain);
  if (n == 0) return e.scale;
  Vec3 s = d.entities[chain[n - 1]].scale;
  for (uint32_t k = n - 1; k > 0; k--) s = s * d.entities[chain[k - 1]].scale;
  return s;
}

uint32_t scene_tree_order(const SceneDesc &d, int32_t *out, uint32_t cap) {
  // Acik yigin: her seviyede "su ana kadar taranan varlik indeksi". Sira
  // BELIRLENIMLI: kokler indeks sirasinda, cocuklar indeks sirasinda.
  int32_t st_parent[kSceneMaxDepth + 2];
  uint32_t st_scan[kSceneMaxDepth + 2];
  uint32_t n = 0, sp = 1;
  st_parent[0] = -1;
  st_scan[0] = 0;
  while (sp > 0) {
    const uint32_t top = sp - 1;
    bool descended = false;
    while (st_scan[top] < d.entity_count) {
      const uint32_t i = st_scan[top]++;
      if (d.entities[i].parent != st_parent[top]) continue;
      if (out && n < cap) out[n] = (int32_t)i;
      n++;
      // Tavan dolduysa bu dugumun cocuklarina INILMEZ ama kardes taramasi
      // SURER (break atilirsa geri kalan kardesler sessizce duserdi).
      if (sp <= kSceneMaxDepth) {
        st_parent[sp] = (int32_t)i;
        st_scan[sp] = 0;
        sp++;
        descended = true;
        break;
      }
    }
    if (!descended) sp--; // bu seviyede baska cocuk yok
  }
  return n;
}

bool scene_reparent_entity(const SceneDesc &d, uint32_t child, int32_t new_parent, SceneEntity *out) {
  if (!out || child >= d.entity_count) return false;
  if (new_parent < -1 || (new_parent >= 0 && (uint32_t)new_parent >= d.entity_count)) return false;
  if (new_parent == (int32_t)child) return false;
  if (new_parent >= 0 && reaches(d, child, new_parent)) return false; // dongu
  if (new_parent == d.entities[child].parent) {                      // istek yok: dokunma
    *out = d.entities[child];                                        // (ayristir/yeniden kur sapmasi olmasin)
    return true;
  }
  // Derinlik tavani: yeni derinlik + alt agacin yuksekligi tavani asamaz.
  const uint32_t base = new_parent < 0 ? 0u : scene_tree_depth(d, (uint32_t)new_parent) + 1u;
  const uint32_t cd = scene_tree_depth(d, child);
  uint32_t height = 0;
  for (uint32_t i = 0; i < d.entity_count; i++) {
    if (i == child || !reaches(d, child, (int32_t)i)) continue;
    const uint32_t dd = scene_tree_depth(d, i);
    if (dd > cd && dd - cd > height) height = dd - cd;
  }
  if (base + height > kSceneMaxDepth) return false;
  // Dunya donusumu korunur: yeni yerel = ters(dunya(yeni ebeveyn)) * dunya(cocuk).
  const Mat4 w = scene_entity_world_matrix(d, child);
  const Mat4 local = new_parent < 0 ? w : inverse(scene_entity_world_matrix(d, (uint32_t)new_parent)) * w;
  *out = d.entities[child];
  out->parent = new_parent;
  decompose_trs(local, &out->pos, &out->rot_deg, &out->scale);
  return true;
}

Mat4 scene_world_to_local_matrix(const SceneDesc &d, uint32_t i, const Mat4 &world) {
  if (i >= d.entity_count) return world;
  const int32_t p = d.entities[i].parent;
  if (p < 0 || (uint32_t)p >= d.entity_count) return world; // kok: dunya = yerel
  return inverse(scene_entity_world_matrix(d, (uint32_t)p)) * world;
}

bool scene_reparent(SceneDesc &d, uint32_t child, int32_t new_parent) {
  SceneEntity e{};
  if (!scene_reparent_entity(d, child, new_parent, &e)) return false;
  d.entities[child] = e;
  return true;
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
    // DUNYA donusumu: cocuk govde gorundugu yerde dogar. Kok varlikta bu
    // degerler yerel olanlarla bit-tam ayni (erken donus), yani duz sahnelerde
    // sim davranisi degismez.
    const Mat4 wm = scene_entity_world_matrix(d, i);
    const Vec3 wp{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
    const Vec3 ws = scene_entity_world_scale(d, i);
    if (e.shape == SceneShape::Box) ids[i] = ph.add_box(e.half * ws, wp, scene_entity_world_rotation(d, i), e.dynamic);
    else ids[i] = ph.add_sphere(e.radius * ws.x, wp, e.dynamic);
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
  // Cocuklar SILMEDEN ONCE isaretlenir: silme onlari buyukbabaya bagladigi
  // icin sonradan gercek buyukbaba cocuklarindan ayirt edilemezler.
  for (uint32_t k = 0; k < d.entity_count; k++)
    if (d.entities[k].parent == (int32_t)i) op.child_mask[k >> 5] |= 1u << (k & 31);
  d.remove_entity(i);
  return push(op);
}
bool SceneHistory::reparent(SceneDesc &d, uint32_t child, int32_t new_parent) {
  SceneEntity after{};
  if (!scene_reparent_entity(d, child, new_parent, &after)) return false;
  return set_entity(d, child, after); // tek Set islemi: geri alma bayt-tam
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
  case SceneOp::Remove:
    if (!d.insert_entity(op.index, op.before)) return false;
    // insert_entity indeksleri eski uzaya geri tasidi; silmede buyukbabaya
    // kaydirilan cocuklar simdi yeniden bu dugume baglanir (child_mask).
    for (uint32_t k = 0; k < d.entity_count; k++)
      if (op.child_mask[k >> 5] & (1u << (k & 31))) d.entities[k].parent = (int32_t)op.index;
    break;
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
