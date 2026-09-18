// L6 BRIDGE — teng_* C ABI'sinin uygulamasi. Tek global baglam; her cagri
// loglanir (seviye 3), durum degisiklikleri seviye 2, kurulum/rapor seviye 1,
// hata her zaman. Son 64 log satiri halkada tutulur ve shutdown'da hata varsa
// (ya da cokme isleyicisinden) dokulur: "nerede patladi" sorusunun cevabi
// halkanin son satiri + son API cagrisidir.
#include "bridge/engine_api.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include <sys/stat.h> // sahne sicak yeniden yukleme: dosya degisim zamani

#include "app/virtual_stick.hpp"
#include "audio/clip.hpp"
#include "audio/device.hpp"
#include "audio/mixer.hpp"
#include "bridge/bridge_host.hpp"
#include "content/font.hpp"
#include "content/gltf.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_runtime.hpp"
#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "platform/crash.hpp"
#include "platform/time.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "sim/physics.hpp"
#include "sim/schedule.hpp"

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "engine"
#endif

using namespace tulpar::engine;

namespace {
constexpr float kBridgePi = 3.14159265358979f;
constexpr uint32_t kMaxEntities = 4096;
constexpr uint32_t kMaxModels = 16;
constexpr uint32_t kMaxClips = 32;  // ses klibi (dosya + sentetik ton)
constexpr uint32_t kMaxVoices = 32; // audio::Mixer::kVoices ile ayni
constexpr uint32_t kMaxHud = 1024;
constexpr uint32_t kMaxOverlap = 64;   // kure sorgusu sonuc tavani (sabit dizi, 0 ayirma)
constexpr uint32_t kMaxNavPoints = 32; // navmesh duz yolunun kose sayisi tavani
constexpr uint32_t kRayRetries = 4;    // skip_id icin yeniden atma denemesi
constexpr uint32_t kHudTextBytes = 64 << 10;
constexpr uint32_t kLogRing = 64;
constexpr uint32_t kMaxUiIds = 64;     // kare basina widget (sabit dizi, kare ici ayirma yok)
constexpr uint32_t kMaxSaveKeys = 64;  // kalici kayit anahtari
constexpr uint32_t kSaveKeyLen = 48;
constexpr uint32_t kSaveValLen = 112;
constexpr uint32_t kWatchInterval = 6; // sahne dosyasi kac karede bir sorulur (headless: 0.1 s)
constexpr uint32_t kLogLine = 240;

// ---------------------------------------------------------------------------
// Log: seviye + halka. Her satir "[engine_bridge] k<kare> <seviye> <mesaj>".
// ---------------------------------------------------------------------------
struct Log {
  int level = 1;
  char ring[kLogRing][kLogLine];
  uint32_t ring_n = 0, ring_head = 0;
  uint32_t errors = 0, warnings = 0;
  const char *last_call = "-";
  uint32_t frame = 0;
};
Log g_log;

void blog(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void blog(int level, const char *fmt, ...) {
  char line[kLogLine];
  const char *tag = level == 0 ? "HATA" : level == 1 ? "bilgi" : level == 2 ? "ayrinti" : "iz";
  int n = std::snprintf(line, sizeof line, "[engine_bridge] k%u %s ", g_log.frame, tag);
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
  va_end(ap);
  // Halka: her seviyede kaydet (sessiz kipte bile cokme dokumu icin).
  std::memcpy(g_log.ring[g_log.ring_head], line, kLogLine);
  g_log.ring_head = (g_log.ring_head + 1) % kLogRing;
  if (g_log.ring_n < kLogRing) g_log.ring_n++;
  if (level == 0) g_log.errors++;
  if (level <= g_log.level || level == 0) { std::fputs(line, stdout); std::fputc('\n', stdout); std::fflush(stdout); }
}
#define BERR(...) blog(0, __VA_ARGS__)
#define BINFO(...) blog(1, __VA_ARGS__)
#define BDBG(...) blog(2, __VA_ARGS__)
#define BTRACE(...) do { if (g_log.level >= 3) blog(3, __VA_ARGS__); } while (0)
#define CALL(name) do { g_log.last_call = name; BTRACE("%s", name); } while (0)
#define CALLF(name, ...) do { g_log.last_call = name; BTRACE(name " " __VA_ARGS__); } while (0)

void dump_ring(const char *why) {
  std::printf("[engine_bridge] --- log halkasi (%s; son cagri %s; hata %u) ---\n", why, g_log.last_call, g_log.errors);
  for (uint32_t i = 0; i < g_log.ring_n; i++) {
    const uint32_t idx = (g_log.ring_head + kLogRing - g_log.ring_n + i) % kLogRing;
    std::printf("  %s\n", g_log.ring[idx]);
  }
  std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Kalici kayit (anahtar-deger). Bridge'in DISINDA yasar: ayarlar teng_init'ten
// ONCE okunur (parlama gibi seyler init oncesi kurulur) ve teng_shutdown'dan
// sonra da yazilabilir. Sabit dizi, ayirma yok.
// ---------------------------------------------------------------------------
struct SaveStore {
  bool loaded = false, dirty = false;
  char path[512] = {0};
  char keys[kMaxSaveKeys][kSaveKeyLen] = {};
  char vals[kMaxSaveKeys][kSaveValLen] = {};
  uint32_t n = 0, writes = 0, bad_lines = 0;
};
SaveStore g_save;

const char *save_path(void) {
  if (!g_save.path[0]) {
    const char *e = std::getenv("TULPAR_ENGINE_SAVE");
    // Varsayilan: calisma dizini. Android'de calisma dizini APK cikarma
    // kokudur (uygulamanin dahili dizini) — orasi yazilabilir.
    std::snprintf(g_save.path, sizeof g_save.path, "%s", e && *e ? e : "tulpar_kayit.txt");
  }
  return g_save.path;
}
int32_t save_find(const char *key) {
  if (!key) return -1;
  for (uint32_t i = 0; i < g_save.n; i++) if (std::strcmp(g_save.keys[i], key) == 0) return (int32_t)i;
  return -1;
}
bool save_key_ok(const char *key, const char *fn) {
  if (!key || !key[0]) { BERR("%s: anahtar bos", fn); return false; }
  if (std::strlen(key) >= kSaveKeyLen) { BERR("%s: anahtar cok uzun (%u bayt siniri): \"%s\"", fn, kSaveKeyLen - 1, key); return false; }
  for (const char *p = key; *p; p++)
    if (*p == '=' || *p == '\n' || *p == '\r') { BERR("%s: anahtar '=' ya da satir sonu iceremez: \"%s\"", fn, key); return false; }
  return true;
}
bool save_put(const char *key, const char *val, const char *fn) {
  if (!save_key_ok(key, fn)) return false;
  if (!val) val = "";
  if (std::strlen(val) >= kSaveValLen) { BERR("%s: \"%s\" degeri cok uzun (%u bayt siniri)", fn, key, kSaveValLen - 1); return false; }
  for (const char *p = val; *p; p++)
    if (*p == '\n' || *p == '\r') { BERR("%s: \"%s\" degeri satir sonu iceremez", fn, key); return false; }
  int32_t i = save_find(key);
  if (i < 0) {
    if (g_save.n >= kMaxSaveKeys) { BERR("%s: kayit kapasitesi dolu (%u anahtar), \"%s\" yazilmadi", fn, kMaxSaveKeys, key); return false; }
    i = (int32_t)g_save.n++;
    std::snprintf(g_save.keys[i], kSaveKeyLen, "%s", key);
  }
  std::snprintf(g_save.vals[i], kSaveValLen, "%s", val);
  g_save.dirty = true;
  return true;
}
// 1: dosya okundu, 0: dosya yok (yeni kayit). Bozuk SATIR sessizce gecilmez:
// hata loglanir (sayac artar) ve saglam satirlar yine yuklenir.
int save_read_file(const char *path) {
  g_save.n = 0;
  g_save.loaded = true;
  g_save.dirty = false;
  std::FILE *f = std::fopen(path, "rb");
  if (!f) { BINFO("kayit dosyasi yok, bos kayitla baslandi: %s", path); return 0; }
  char line[kSaveKeyLen + kSaveValLen + 8];
  uint32_t ln = 0, bad = 0, ok = 0;
  while (std::fgets(line, sizeof line, f)) {
    ln++;
    size_t L = std::strlen(line);
    while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
    if (L == 0 || line[0] == '#') continue;
    char *eq = std::strchr(line, '=');
    if (!eq || eq == line) { BERR("kayit dosyasi bozuk satir %u (\"anahtar=deger\" bekleniyordu): %s:%u \"%s\"", ln, path, ln, line); bad++; continue; }
    *eq = 0;
    if (std::strlen(line) >= kSaveKeyLen || std::strlen(eq + 1) >= kSaveValLen) {
      BERR("kayit dosyasi satir %u cok uzun (anahtar %u / deger %u bayt siniri): %s", ln, kSaveKeyLen - 1, kSaveValLen - 1, path);
      bad++;
      continue;
    }
    if (g_save.n >= kMaxSaveKeys) { BERR("kayit kapasitesi dolu (%u anahtar): %s satir %u ve sonrasi atlandi", kMaxSaveKeys, path, ln); bad++; break; }
    std::snprintf(g_save.keys[g_save.n], kSaveKeyLen, "%s", line);
    std::snprintf(g_save.vals[g_save.n], kSaveValLen, "%s", eq + 1);
    g_save.n++;
    ok++;
  }
  std::fclose(f);
  g_save.bad_lines += bad;
  g_save.dirty = false;
  BINFO("kayit okundu: %s — %u anahtar, %u bozuk satir", path, ok, bad);
  return 1;
}
void save_ensure(void) {
  if (!g_save.loaded) save_read_file(save_path());
}

// ---------------------------------------------------------------------------
// Varlik tablosu: nesil etiketli id ((nesil<<16)|yuva), 0 gecersiz.
// ---------------------------------------------------------------------------
enum class Kind : uint8_t { Empty = 0, Box, Sphere, Ground, Model, Light };
const char *kind_name(Kind k) {
  switch (k) {
  case Kind::Box: return "kutu";
  case Kind::Sphere: return "kure";
  case Kind::Ground: return "zemin";
  case Kind::Model: return "model";
  case Kind::Light: return "isik";
  default: return "bos";
  }
}
struct Ent {
  uint16_t gen = 1;
  bool alive = false;
  Kind kind = Kind::Empty;
  bool dynamic = false;
  Vec3 pos{0, 0, 0}, half{0.5f, 0.5f, 0.5f}, color{1, 1, 1};
  float radius = 0.5f, scale = 1.0f, yaw_deg = 0;
  float intensity = 1, light_radius = 5;
  int32_t asset = -1;
  sim::BodyId body;
  // Animasyon (yalniz Kind::Model): klip < 0 = statik cizim.
  int32_t anim_clip = -1;
  float anim_speed = 1.0f, anim_time = 0.0f;
  bool anim_loop = true;
};
struct HudCmd {
  uint8_t kind; // 0 rect, 1 text
  float x, y, w, h, scale;
  uint32_t rgba;
  uint32_t text_off;
};

// ---------------------------------------------------------------------------
// Anlik-kip arayuz durumu: isaretci + "hangi widget basili". Widget DEGERLERI
// burada DEGIL betikte yasar (checkbox/slider yeni degeri dondurur), o yuzden
// kare basina tutulan tek sey kimlik listesi (yinelenen etiket yakalanir).
// ---------------------------------------------------------------------------
struct Ui {
  bool begun = false;
  uint32_t begun_frame = 0xFFFFFFFFu;
  float px = -1000, py = -1000;
  bool down = false, prev_down = false, pressed = false, released = false;
  bool enabled = true, touch_pointer = false;
  uint32_t hot = 0, active = 0;
  uint32_t clicks = 0, widgets = 0, injects = 0;
  uint32_t ids[kMaxUiIds] = {};
  uint32_t id_n = 0;
  bool inject = false;
  float inj_x = 0, inj_y = 0;
  // Tema: paketlenmis 0xRRGGBBAA. Dugme/iz renkleri panelden turetilir (ui_shade).
  int64_t c_panel = ((int64_t)18 << 24) | ((int64_t)20 << 16) | ((int64_t)26 << 8) | 225;
  int64_t c_text = ((int64_t)236 << 24) | ((int64_t)239 << 16) | ((int64_t)243 << 8) | 255;
  int64_t c_accent = ((int64_t)64 << 24) | ((int64_t)200 << 16) | ((int64_t)128 << 8) | 255;
};

struct Bridge {
  bool inited = false, running = false, headless = false, have_window = false, in_frame = false;
  uint32_t headless_frames = 0;
  char out_ppm[512] = {0};
  char err[256] = {0};
  char title[128] = {0};
  uint32_t width = 1280, height = 720, fb_w = 0, fb_h = 0;
  Vec3 gravity{0, -9.81f, 0};
  bool bloom = false;
  float bloom_threshold = 1.0f, bloom_intensity = 0.6f;
  // motor
  SystemArena sys;
  FrameArena frame_arena;
  JobSystem jobs;
  Profiler prof;
  rhi::VkApi api;
  rhi::Device dev;
  rhi::Swapchain swap;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  renderer::Renderer ren;
  sim::Physics phys;
  sim::FixedStep fs;
  content::Font font;
  bool font_ok = false;
  bridge::BridgeHost host;
  bool host_open = false;
  // mesh'ler
  renderer::MeshHandle cube, sphere, plane;
  renderer::MaterialHandle ground_mat;
  content::Model models[kMaxModels];
  content::UploadedModel ups[kMaxModels];
  uint32_t model_count = 0;
  // sahne blob
  content::SceneRuntime srt;
  bool scene_ok = false;
  char scene_dir[1024] = {0};
  uint32_t scene_loads = 0; // bolum gecisi: her yukleme kaynak ayirir (arena + GPU)
  // animasyon: tek calisma alani (yaklasik 46 KB), kare icinde ayirma yok
  content::PoseScratch pose_scratch;
  uint32_t last_posed = 0; // son karede pozla cizilen model sayisi
  // ses (cihaz betigin istegiyle acilir; acilmazsa oyun sessiz devam eder)
  audio::Mixer mixer;
  audio::AudioDevice audio_dev;
  bool audio_ok = false;
  bool audio_null = false;
  uint32_t audio_off_reports = 0; // kapali cihazda cagri: ilk N tanesi HATA, sonrasi ayrinti (log selini onler)
  uint32_t audio_plays = 0;
  float audio_master = 1.0f;
  char audio_desc[192] = {0};
  struct ClipSlot {
    audio::Clip clip;
    bool tone = false;
    float hz = 0, secs = 0;
    char name[128] = {0};
  };
  ClipSlot clips[kMaxClips];
  uint32_t clip_count = 0;
  struct VoiceSlot { uint32_t id = 0; float gain = 1.0f; };
  VoiceSlot voices[kMaxVoices];
  // varliklar
  Ent ents[kMaxEntities];
  uint32_t ent_high = 0, ent_alive = 0;
  // kamera / dunya
  Vec3 cam_eye{0, 8, 14}, cam_target{0, 0, 0};
  Vec3 sun_dir{0.5f, 1.0f, 0.35f}, ambient{0.16f, 0.17f, 0.2f};
  float sun_diffuse = 0.85f;
  Vec3 shadow_center{0, 1, -1};
  float shadow_radius = 17, shadow_depth = 70;
  // zaman
  uint64_t t0_ns = 0, last_ns = 0;
  float dt = 1.0f / 60.0f;
  double time_s = 0;
  uint32_t frame = 0, tick = 0;
  float fps = 0;
  // girdi
  const platform::InputState *in = nullptr;
  bool prev_keys[512] = {};
  bool cur_keys[512] = {};
  const platform::TouchState *touch = nullptr;
  app::VirtualStick stick;
  // hud
  HudCmd hud[kMaxHud];
  uint32_t hud_n = 0;
  char hud_text[kHudTextBytes];
  uint32_t hud_text_n = 0;
  // sorgular: son isin testi + son kure sorgusu (sabit diziler, kare ici ayirma yok)
  float ray_dist = -1;
  Vec3 ray_point{0, 0, 0}, ray_normal{0, 0, 0};
  int ray_id = 0, ray_scene = -1;
  uint32_t collision_warn_frame = 0xFFFFFFFFu; // tasma uyarisi kare basina bir kez
  struct OverlapHit { int id; float dist; };
  OverlapHit ovl[kMaxOverlap];
  uint32_t ovl_n = 0;
  // navmesh: sahne blob'undaki bake'in runtime yuzu (bake YOK, yalniz sorgu)
  content::SceneNav nav;
  bool nav_ok = false;
  Vec3 nav_pts[kMaxNavPoints];
  uint32_t nav_n = 0;
  bool nav_partial = false;
  Vec3 nav_near{};        // son teng_nav_nearest sonucu
  bool nav_near_ok = false;
  float nav_ray_t = 1.0f; // son teng_nav_raycast parametresi (engel yoksa 1)
  // anlik-kip arayuz
  Ui ui;
  // sahne sicak yeniden yukleme (editorde "Derle" -> oyun kendini yeniler)
  bool scene_watch = false, reloading = false, scene_reloaded = false;
  char watch_path[1024] = {0};
  int64_t watch_mtime = 0, watch_size = 0;       // en son YUKLENEN halin damgasi
  int64_t pend_mtime = 0, pend_size = 0;         // degisim goruldu, yazim bitisi bekleniyor
  bool watch_pending = false;
  uint32_t watch_last_frame = 0, scene_reloads = 0;
  // istatistik
  uint32_t last_draws = 0, last_lights = 0;
};
Bridge *g = nullptr; // teng_init'te statik depodan kurulur (buyuk nesne; yigina sigmaz)
alignas(16) unsigned char g_storage[sizeof(Bridge)];

struct RecordCtx { renderer::Renderer *r; };
void record_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  c->r->record(cb);
  c->r->ui_record(cb);
}
void shadow_cb(VkCommandBuffer cb, void *user) { static_cast<RecordCtx *>(user)->r->record_shadow(cb); }

Vec3 color_of(int64_t c) { // 0xRRGGBBAA -> sRGB 0..1
  return {(float)((c >> 24) & 0xFF) / 255.0f, (float)((c >> 16) & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f};
}
uint32_t rgba_of(int64_t c) {
  return renderer::Renderer::rgba((uint8_t)((c >> 24) & 0xFF), (uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 8) & 0xFF), (uint8_t)(c & 0xFF));
}
bool ready(const char *fn) {
  if (g && g->inited) return true;
  BERR("%s: motor kurulmadan cagrildi (once eng_init)", fn);
  return false;
}
int32_t slot_of(int id, const char *fn) {
  if (!g || id <= 0) { if (id != 0) BERR("%s: gecersiz id %d", fn, id); return -1; }
  const uint32_t slot = (uint32_t)id & 0xFFFFu, gen = (uint32_t)id >> 16;
  if (slot >= kMaxEntities || !g->ents[slot].alive || g->ents[slot].gen != gen) {
    BERR("%s: id %d olu ya da yanlis nesil (yuva %u nesil %u, mevcut %s nesil %u)", fn, id, slot, gen,
         slot < kMaxEntities && g->ents[slot].alive ? "canli" : "olu", slot < kMaxEntities ? (unsigned)g->ents[slot].gen : 0u);
    return -1;
  }
  return (int32_t)slot;
}
int make_id(uint32_t slot) { return (int)(((uint32_t)g->ents[slot].gen << 16) | slot); }
int alloc_slot(Kind k) {
  for (uint32_t i = 0; i < kMaxEntities; i++) {
    Ent &e = g->ents[i];
    if (e.alive) continue;
    const uint16_t gen = e.gen;
    e = Ent{};
    e.gen = (uint16_t)(gen == 0 ? 1 : gen);
    e.alive = true;
    e.kind = k;
    if (i + 1 > g->ent_high) g->ent_high = i + 1;
    g->ent_alive++;
    return (int)i;
  }
  BERR("varlik kapasitesi dolu (%u)", kMaxEntities);
  return -1;
}
void free_slot(uint32_t slot) {
  Ent &e = g->ents[slot];
  if (e.body.valid()) g->phys.remove(e.body);
  e.alive = false;
  e.body = sim::BodyId{};
  e.gen = (uint16_t)(e.gen + 1 == 0 ? 1 : e.gen + 1);
  if (g->ent_alive) g->ent_alive--;
}
Quat yaw_quat(float yaw_deg) { return Quat::axis_angle({0, 1, 0}, yaw_deg * kBridgePi / 180.0f); }
bool make_body(Ent &e, const char *fn) {
  if (e.kind == Kind::Box || e.kind == Kind::Ground) e.body = g->phys.add_box(e.half, e.pos, yaw_quat(e.yaw_deg), e.dynamic);
  else if (e.kind == Kind::Sphere) e.body = g->phys.add_sphere(e.radius, e.pos, e.dynamic);
  else return true;
  if (!e.body.valid()) { BERR("%s: fizik govdesi kurulamadi (%s @ %.2f %.2f %.2f)", fn, kind_name(e.kind), e.pos.x, e.pos.y, e.pos.z); return false; }
  return true;
}
Vec3 ent_pos(const Ent &e) { return e.body.valid() && e.dynamic ? g->phys.position(e.body) : e.pos; }
// Ses cihazi kapaliyken gelen cagri: ILK 3 tanesi HATA, sonrasi ayrinti.
// Neden: oyun her atista ses calar; cihaz yoksa her kare HATA basmak hem logu
// hem hata sayacini doldurur ve gercek hatalari gizler. Kapali oldugu yine
// loglanir (ilk satirlar) ve kapanis raporunda gorunur.
bool audio_live(const char *fn) {
  if (!g || !g->inited) { BERR("%s: motor kurulmadan cagrildi (once eng_init)", fn); return false; }
  if (g->audio_ok) return true;
  g->audio_off_reports++;
  if (g->audio_off_reports <= 3) BERR("%s: ses cihazi kapali (eng_audio_open yok ya da acilamadi) — cagri yok sayildi", fn);
  else BDBG("%s: ses kapali, cagri yok sayildi (%u. kez)", fn, g->audio_off_reports);
  return false;
}
Mat4 ent_matrix(const Ent &e) {
  if (e.body.valid() && e.dynamic) return Mat4::translate(g->phys.position(e.body)) * to_mat4(g->phys.rotation(e.body));
  return Mat4::translate(e.pos) * to_mat4(yaw_quat(e.yaw_deg));
}

// UV kure: rings x segs, yaricap 0.5 (kup ile ayni olcek sozlesmesi).
uint32_t build_sphere(renderer::Vertex *v, uint32_t *idx, uint32_t rings, uint32_t segs) {
  uint32_t nv = 0;
  for (uint32_t r = 0; r <= rings; r++) {
    const float phi = kBridgePi * (float)r / (float)rings;
    for (uint32_t s = 0; s <= segs; s++) {
      const float th = 2.0f * kBridgePi * (float)s / (float)segs;
      const Vec3 n = {std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
      v[nv].pos = n * 0.5f;
      v[nv].nrm = n;
      v[nv].uv = {(float)s / (float)segs, (float)r / (float)rings};
      nv++;
    }
  }
  uint32_t ni = 0;
  for (uint32_t r = 0; r < rings; r++)
    for (uint32_t s = 0; s < segs; s++) {
      const uint32_t a = r * (segs + 1) + s, b = a + segs + 1;
      idx[ni++] = a; idx[ni++] = a + 1; idx[ni++] = b;
      idx[ni++] = a + 1; idx[ni++] = b + 1; idx[ni++] = b;
    }
  return ni;
}

int key_code(const char *name) {
  if (!name || !*name) return -1;
  char u[16];
  size_t n = 0;
  for (; name[n] && n < sizeof u - 1; n++) u[n] = (char)((name[n] >= 'a' && name[n] <= 'z') ? name[n] - 32 : name[n]);
  u[n] = 0;
  if (n == 1) {
    if (u[0] >= 'A' && u[0] <= 'Z') return u[0];
    if (u[0] >= '0' && u[0] <= '9') return u[0];
  }
  struct { const char *n; int c; } k[] = {{"SPACE", 32}, {"ESC", 256}, {"ESCAPE", 256}, {"ENTER", 257}, {"TAB", 258}, {"BACKSPACE", 259},
                                          {"RIGHT", 262}, {"LEFT", 263}, {"DOWN", 264}, {"UP", 265}, {"SHIFT", 340}, {"LSHIFT", 340},
                                          {"RSHIFT", 344}, {"CTRL", 341}, {"LCTRL", 341}, {"RCTRL", 345}, {"ALT", 342}, {"LALT", 342}};
  for (const auto &e : k) if (std::strcmp(u, e.n) == 0) return e.c;
  return -1;
}

void render_frame() {
  Bridge &b = *g;
  // En-boy orani GORUNEN yonden; Android on-dondurmede goruntu fiziksel olarak
  // 90 derece donuk gelir ve projeksiyon clip uzayinda dondurulmezse sahne yan
  // yatar (HUD dogru gorunur, cunku ui_begin donusu zaten aliyor) — Tuzaklar 8ab.
  const uint32_t vis_w = b.headless ? b.fb_w : b.swap.logical_extent().width;
  const uint32_t vis_h_px = b.headless ? b.fb_h : b.swap.logical_extent().height;
  const float aspect = (float)vis_w / (float)(vis_h_px ? vis_h_px : 1);
  Mat4 proj = Mat4::perspective(kBridgePi / 3.5f, aspect, 0.1f, 300.0f);
  if (!b.headless && b.swap.rotation_radians() != 0.0f)
    proj = Mat4::rotate({0, 0, 1}, b.swap.rotation_radians()) * proj;
  b.ren.set_camera(Mat4::look_at(b.cam_eye, b.cam_target, {0, 1, 0}), proj);
  b.ren.set_light(normalize(b.sun_dir), b.ambient, b.sun_diffuse);
  b.ren.set_shadow_volume(b.shadow_center, b.shadow_radius, b.shadow_depth);
  // Yakin golge kademeleri kameranin BAKTIGI yerde toplanir (oyuncu orada).
  // Golge hacmi (en dis kademe) yine sahnenin tamami.
  b.ren.set_shadow_focus(b.cam_target);
  b.ren.clear_point_lights();
  // Kare yuvasi swapchain'den (fence beklenmis yuva) — Tuzaklar 8l: once acquire, sonra begin_frame.
  rhi::FrameContext fc;
  if (!b.headless && !b.swap.acquire(&fc)) {
    // Yeniden kurma TEK YERDEN: teng_frame_begin'deki sync_size. OUT_OF_DATE
    // bayragi kalir ve sonraki karenin basinda oradan islenir — cizim ile
    // hedef olcusu ayni karede ayrismasin.
    BDBG("swapchain acquire basarisiz (kare %u), kare atlandi; sonraki kare basinda yeniden kurulacak", b.frame);
    b.hud_n = 0; b.hud_text_n = 0;
    return;
  }
  b.ren.begin_frame(b.headless ? 0 : fc.frame_index);
  if (b.scene_ok) b.srt.draw(b.ren, b.cam_eye, (float)b.time_s, &b.phys);
  uint32_t drawn = 0, lights = 0;
  b.last_posed = 0;
  for (uint32_t i = 0; i < b.ent_high; i++) {
    Ent &e = b.ents[i];
    if (!e.alive) continue;
    switch (e.kind) {
    case Kind::Box: b.ren.draw(b.cube, ent_matrix(e) * Mat4::scale(e.half * 2.0f * e.scale), e.color); drawn++; break;
    case Kind::Sphere: { const float d = e.radius * 2.0f * e.scale; b.ren.draw(b.sphere, ent_matrix(e) * Mat4::scale({d, d, d}), e.color); drawn++; break; }
    case Kind::Ground: b.ren.draw(b.plane, b.ground_mat, Mat4::translate({e.pos.x, e.pos.y + e.half.y, e.pos.z}) * Mat4::scale({e.half.x * 2, 1, e.half.z * 2}), e.color); drawn++; break;
    case Kind::Model:
      if (e.asset >= 0 && (uint32_t)e.asset < b.model_count) {
        const content::Model &mdl = b.models[e.asset];
        const Mat4 m = ent_matrix(e) * Mat4::scale({e.scale, e.scale, e.scale});
        bool posed = false;
        // Klip atanmissa poz her kare degerlendirilir (iskeletli cizim);
        // atanmamissa statik + LOD. Degerlendirme basarisizsa klip kapatilir
        // ki hata her karede tekrar etmesin.
        if (e.anim_clip >= 0 && (uint32_t)e.anim_clip < mdl.clip_count) {
          content::ModelPose pose;
          if (content::model_pose_evaluate(mdl, (uint32_t)e.anim_clip, e.anim_time, b.pose_scratch, &pose)) {
            content::draw_model(b.ren, mdl, b.ups[e.asset], m, e.color, nullptr, nullptr, &pose);
            posed = true;
            b.last_posed++;
          } else {
            BERR("model varligi yuva %u: klip %d degerlendirilemedi, animasyon kapatildi", i, e.anim_clip);
            e.anim_clip = -1;
          }
        }
        if (!posed) {
          content::ModelLod lod; lod.camera_pos = b.cam_eye;
          content::draw_model(b.ren, mdl, b.ups[e.asset], m, e.color, &lod);
        }
        drawn++;
      }
      break;
    case Kind::Light: {
      renderer::PointLight pl; pl.pos = e.pos; pl.color = e.color; pl.intensity = e.intensity; pl.radius = e.light_radius;
      if (b.ren.add_point_light(pl)) lights++;
      break;
    }
    default: break;
    }
  }
  // HUD: betigin bu karede kuyrukladigi komutlar.
  b.ren.ui_begin((float)vis_w, (float)vis_h_px, b.headless ? 0.0f : b.swap.rotation_radians());
  if (b.font_ok) b.ren.ui_set_atlas(b.font.atlas());
  for (uint32_t i = 0; i < b.hud_n; i++) {
    const HudCmd &c = b.hud[i];
    if (c.kind == 0) b.ren.ui_rect(c.x, c.y, c.w, c.h, c.rgba);
    else if (b.font_ok) b.font.draw(b.ren, c.x, c.y, b.hud_text + c.text_off, c.rgba, c.scale);
  }
  b.hud_n = 0; b.hud_text_n = 0;
  b.last_draws = drawn; b.last_lights = lights + b.srt.stats().lights;
  RecordCtx rc{&b.ren};
  if (b.headless) {
    if (!rhi::offscreen_render_custom(b.off, b.oc, record_cb, &rc, &b.ores, shadow_cb)) BERR("offscreen kare: %s", b.ores.error);
  } else {
    b.ren.record_shadow(fc.cmd); // kendi pass'i: ana pass BASLAMADAN once
    b.swap.begin_render_pass(fc);
    b.ren.record(fc.cmd);
    b.ren.ui_record(fc.cmd);
    b.swap.end_frame(fc);
    // needs_recreate bayragi burada TUKETILMEZ: sonraki karenin basindaki
    // sync_size tek karar noktasidir (yukariya bak).
  }
}
} // namespace

// ===========================================================================
extern "C" {

// Sahne dosyasi izleyicisi (sicak yeniden yukleme) ve dosya damgasi; govdeleri
// dosyanin sonunda (teng_scene_load/unload'i cagirdiklari icin).
static void scene_watch_tick(void);
static bool file_stamp_pub(const char *path, int64_t *mtime, int64_t *size);

void teng_log_level(int level) { g_log.level = level < 0 ? 0 : level > 3 ? 3 : level; BINFO("log seviyesi %d", g_log.level); }
void teng_log(const char *msg) { blog(1, "[tpr] %s", msg ? msg : ""); }
const char *teng_last_error(void) { return g ? g->err : ""; }
int teng_error_count(void) { return (int)g_log.errors; }
int teng_warning_count(void) { return (int)g_log.warnings; }
int teng_headless(void) { return g && g->headless ? 1 : 0; }
void teng_set_headless(int frames, const char *out_ppm) {
  if (!g) g = new (g_storage) Bridge();
  g->headless = frames > 0;
  g->headless_frames = frames > 0 ? (uint32_t)frames : 0;
  if (out_ppm) std::snprintf(g->out_ppm, sizeof g->out_ppm, "%s", out_ppm);
  BINFO("headless ayarlandi: %d kare, cikti %s", frames, out_ppm ? out_ppm : "-");
}
void teng_gravity(double gx, double gy, double gz) {
  if (!g) g = new (g_storage) Bridge();
  g->gravity = {(float)gx, (float)gy, (float)gz};
  BDBG("yercekimi (%.2f %.2f %.2f)%s", gx, gy, gz, g->inited ? " — init sonrasi: yeni govdelerde de eski degeri kullanir (Jolt dunyasi kuruldu)" : "");
}

void teng_bloom(int enable, double threshold, double intensity) {
  CALLF("teng_bloom", "%d esik %.2f yogunluk %.2f", enable, threshold, intensity);
  if (!g) g = new (g_storage) Bridge();
  g->bloom = enable != 0;
  g->bloom_threshold = (float)threshold;
  g->bloom_intensity = (float)intensity;
  if (g->inited) { // kare icinde: yalniz esik/yogunluk (boru hatti yeniden kurulmaz)
    g->ren.set_bloom(g->bloom_threshold, g->bloom ? g->bloom_intensity : 0.0f);
    BDBG("parlama guncellendi: esik %.2f yogunluk %.2f (acma/kapama init oncesi)", threshold, intensity);
  } else BDBG("parlama %s: esik %.2f yogunluk %.2f", enable ? "acik" : "kapali", threshold, intensity);
}
int teng_bloom_on(void) { return g && g->inited && g->ren.post().enabled ? 1 : 0; }

int teng_init(const char *title, int width, int height) {
  if (!g) g = new (g_storage) Bridge();
  Bridge &b = *g;
  CALLF("teng_init", "\"%s\" %dx%d", title ? title : "", width, height);
  if (b.inited) { BERR("teng_init iki kez cagrildi"); return 0; }
  if (const char *lv = std::getenv("TULPAR_ENGINE_LOG")) g_log.level = std::atoi(lv);
  if (const char *hf = std::getenv("TULPAR_ENGINE_HEADLESS"); hf && *hf && !b.headless) { b.headless = true; b.headless_frames = (uint32_t)std::atoi(hf); if (b.headless_frames == 0) b.headless_frames = 60; }
  if (const char *op = std::getenv("TULPAR_ENGINE_OUT"); op && *op && !b.out_ppm[0]) std::snprintf(b.out_ppm, sizeof b.out_ppm, "%s", op);
  std::snprintf(b.title, sizeof b.title, "%s", title ? title : "Tulpar");
  b.width = width > 0 ? (uint32_t)width : 1280;
  b.height = height > 0 ? (uint32_t)height : 720;
  BINFO("kurulum: \"%s\" %ux%u, log seviyesi %d, kip %s%s", b.title, b.width, b.height, g_log.level, b.headless ? "headless" : "pencere",
        b.headless ? "" : " (TULPAR_ENGINE_HEADLESS=N ile pencersiz)");
  platform::CrashConfig cc;
  cc.report_dir = ".";
  cc.build_id = "tulpar_engine_bridge";
  platform::crash_reporter_install(cc);

  if (!b.sys.reserve(512u << 20, "tulpar_engine")) { BERR("arena ayrilamadi (512 MB rezerv)"); return 0; }
  b.sys.carve(b.frame_arena, 8u << 20, "frame");
  if (!b.jobs.init(b.sys, JobSystemConfig{})) { BERR("job sistemi kurulamadi"); return 0; }
  ProfilerConfig pc;
  pc.frame_capacity = 600;
  pc.zone_capacity = 8192;
  b.prof.init(b.sys, pc);
  BDBG("arena 512 MB, job worker %u, profiler 600 kare", b.jobs.worker_count());

  if (!rhi::vk_api_load(b.api)) { BERR("Vulkan yukleyici yok (libvulkan bulunamadi)"); std::snprintf(b.err, sizeof b.err, "Vulkan loader yok"); return 0; }
  BDBG("Vulkan yukleyici acildi");
  // Pencere (host) — acilamazsa headless'a dus (loglayarak), betik yine calissin.
  if (!b.headless) {
    char herr[256] = {0};
    if (bridge::bridge_host_open(&b.host, b.title, b.width, b.height, herr, sizeof herr)) { b.host_open = true; BINFO("pencere acildi: %ux%u", b.width, b.height); }
    else {
      blog(1, "UYARI pencere acilamadi (%s) -> headless kipe dusuldu (60 kare). Gorsel istiyorsan DISPLAY/Wayland ortamini kontrol et", herr);
      g_log.warnings++;
      b.headless = true;
      b.headless_frames = 60;
    }
  }
  rhi::DeviceConfig dc;
  dc.validation = std::getenv("TULPAR_ENGINE_VK_VALIDATION") != nullptr;
  dc.best_practices = dc.validation;
  if (!b.headless) {
    uint32_t n = 0;
    dc.instance_extensions = b.host.instance_extensions(b.host.user, &n);
    dc.instance_extension_count = n;
    BDBG("instance uzantilari: %u", n);
  }
  if (!b.dev.init_instance(b.api, dc)) { BERR("Vulkan instance: %s", b.dev.last_error()); std::snprintf(b.err, sizeof b.err, "%s", b.dev.last_error()); return 0; }
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!b.headless && !b.host.create_surface(b.host.user, b.api, b.dev.instance(), &surface)) { BERR("Vulkan yuzeyi olusturulamadi"); return 0; }
  if (!b.dev.init_device(surface)) { BERR("Vulkan cihazi: %s", b.dev.last_error()); std::snprintf(b.err, sizeof b.err, "%s", b.dev.last_error()); return 0; }
  BINFO("GPU: %s (Vulkan %u.%u)%s", b.dev.caps().device_name, VK_API_VERSION_MAJOR(b.dev.caps().api_version), VK_API_VERSION_MINOR(b.dev.caps().api_version),
        dc.validation ? " dogrulama katmani ACIK" : "");
  VkRenderPass rp = VK_NULL_HANDLE;
  uint32_t image_count = 2;
  if (b.headless) {
    b.oc.width = b.width; b.oc.height = b.height; b.oc.srgb = true;
    b.off = rhi::offscreen_create(b.dev, b.sys, b.oc, &b.ores);
    if (!b.off) { BERR("offscreen hedef: %s", b.ores.error); return 0; }
    rp = rhi::offscreen_render_pass(b.off);
    b.fb_w = b.width; b.fb_h = b.height;
    BDBG("offscreen hedef %ux%u sRGB, %u kare", b.width, b.height, b.headless_frames);
  } else {
    uint32_t fw = 0, fh = 0;
    b.host.poll(b.host.user, &fw, &fh);
    if (!b.swap.init(b.dev, b.sys, surface, fw ? fw : b.width, fh ? fh : b.height)) { BERR("swapchain kurulamadi (%ux%u)", fw, fh); return 0; }
    rp = b.swap.render_pass();
    image_count = b.swap.image_count();
    b.fb_w = b.swap.extent().width; b.fb_h = b.swap.extent().height;
    BINFO("swapchain %ux%u, %u goruntu, gorunen %ux%u, on-dondurme %.0f derece", b.fb_w, b.fb_h, image_count, b.swap.logical_extent().width,
          b.swap.logical_extent().height, b.swap.rotation_radians() * 180.0f / kBridgePi);
  }
  renderer::RendererConfig rc;
  rc.srgb_target = b.headless ? true : b.swap.srgb_output();
  // Parlama: ic HDR hedefi kare olcusunde kurulur (yeniden boyutlandirma init
  // ister; pencere buyurse birlestirme gerilir — bugunku sinir, logda soylenir).
  if (const char *bl = std::getenv("TULPAR_ENGINE_BLOOM"); bl && *bl && bl[0] != '0') b.bloom = true;
  if (b.bloom) {
    rc.post = true;
    rc.post_width = b.fb_w;
    rc.post_height = b.fb_h;
    rc.bloom_threshold = b.bloom_threshold;
    rc.bloom_intensity = b.bloom_intensity;
    // Ic hedefin temizleme rengi, post KAPALIYKEN kullanilan hedef rengiyle
    // AYNI olmali; yoksa parlamayi acinca gokyuzu simsiyah oluyor (ilk
    // olcumde 112 bin piksel farkin buyuk kismi buydu, parlama degil).
    rc.post_clear = b.headless ? Vec3{10.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f} // OffscreenConfig::clear
                               : Vec3{0.05f, 0.06f, 0.09f};                            // Swapchain temizligi
  }
  if (!b.ren.init(b.dev, b.sys, rp, rc)) { BERR("renderer kurulamadi"); return 0; }
  if (b.bloom) {
    const renderer::PostInfo pi = b.ren.post();
    if (pi.enabled) {
      BINFO("parlama acik: %ux%u HDR (bicim %d), %u mip, %u gecis, %llu bayt ic hedef", pi.width, pi.height, (int)pi.hdr_format,
            pi.bloom_mips, pi.pass_count, (unsigned long long)pi.target_bytes);
    } else { BERR("parlama istendi ama acilmadi: %s", pi.disabled_reason); b.bloom = false; }
  }
  b.ren.set_render_size(b.fb_w, b.fb_h);
  {
    renderer::ShadowInfo sh = b.ren.shadow();
    BDBG("renderer hazir: golge %s %ux%u, hedef %s", sh.enabled ? "acik" : "KAPALI", sh.size, sh.size, rc.srgb_target ? "sRGB" : "UNORM");
  }
  // Yerlesik mesh'ler
  {
    renderer::Vertex v[24];
    uint32_t idx[36];
    uint32_t n = renderer::Renderer::cube(v, idx);
    b.cube = b.ren.create_mesh(v, 24, idx, n);
    n = renderer::Renderer::plane(v, idx, 10.0f);
    b.plane = b.ren.create_mesh(v, 4, idx, n);
    static renderer::Vertex sv[(16 + 1) * (24 + 1)];
    static uint32_t si[16 * 24 * 6];
    const uint32_t ni = build_sphere(sv, si, 16, 24);
    b.sphere = b.ren.create_mesh(sv, (16 + 1) * (24 + 1), si, ni);
    static uint8_t px[64 * 64 * 4];
    for (uint32_t y = 0; y < 64; y++)
      for (uint32_t x = 0; x < 64; x++) {
        const bool on = ((x / 32) + (y / 32)) % 2 == 0;
        const uint8_t gcol = on ? 205 : 150;
        uint8_t *p = px + (y * 64 + x) * 4;
        p[0] = gcol; p[1] = gcol; p[2] = (uint8_t)(gcol + 8); p[3] = 255;
      }
    b.ground_mat = b.ren.create_material(b.ren.create_texture(px, 64, 64, true), {1, 1, 1});
    BDBG("mesh: kup, duzlem, kure (%u ucgen)", ni / 3);
  }
  // Fizik
  {
    sim::PhysicsConfig pcfg;
    pcfg.jobs = &b.jobs;
    pcfg.gravity = b.gravity;
    if (!b.phys.init(b.sys, pcfg)) { BERR("fizik (Jolt) kurulamadi"); return 0; }
    BDBG("fizik hazir: yercekimi (%.2f %.2f %.2f), sabit adim %.4f s", b.gravity.x, b.gravity.y, b.gravity.z, b.fs.step_s);
  }
  // Font (HUD): aday listesi sirayla denenir, HER deneme loglanir — "metin
  // cikmiyor" sikayetinin cevabi logdadir. Android'de APK varligi olmayabilir,
  // sistem fontlari (Roboto/DroidSans) yedek.
  {
    const float vis_h = b.headless ? (float)b.fb_h : (float)b.swap.logical_extent().height;
    const float ui_px = vis_h / 1080.0f * 28.0f;
    const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
    const char *font_env = std::getenv("TULPAR_ENGINE_FONT");
    char cands[6][1024];
    uint32_t nc = 0;
    if (font_env && *font_env) std::snprintf(cands[nc++], sizeof cands[0], "%s", font_env);
    if (adir && *adir) {
      std::snprintf(cands[nc++], sizeof cands[0], "%s/DejaVuSans.ttf", adir);
      std::snprintf(cands[nc++], sizeof cands[0], "%s/fonts/DejaVuSans.ttf", adir);
    }
    std::snprintf(cands[nc++], sizeof cands[0], "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
    std::snprintf(cands[nc++], sizeof cands[0], "/system/fonts/Roboto-Regular.ttf"); // Android
    std::snprintf(cands[nc++], sizeof cands[0], "/system/fonts/DroidSans.ttf");      // eski Android
    for (uint32_t i = 0; i < nc && !b.font_ok; i++) {
      b.font_ok = b.font.load(b.sys, b.ren, cands[i], ui_px > 12 ? ui_px : 12);
      BDBG("font adayi %u/%u: %s -> %s", i + 1, nc, cands[i], b.font_ok ? "YUKLENDI" : "yok");
    }
    if (b.font_ok) BINFO("font: %.0f px (HUD metni acik)", b.font.height());
    else { blog(1, "UYARI font bulunamadi (%u aday denendi): eng_text cizmez, eng_rect calisir (TULPAR_ENGINE_FONT ile yol ver)", nc); g_log.warnings++; }
  }
  b.cam_eye = {0, 8, 14}; b.cam_target = {0, 0, 0};
  b.t0_ns = b.last_ns = platform::now_ns();
  b.inited = true;
  b.running = true;
  BINFO("motor hazir (%s, %s)", b.headless ? "headless" : "pencere", b.dev.caps().device_name);
  {
    // PSO onbellegi GORUNUR olmali: sessizce kapali kaldigi bir platformda
    // (Android'de XDG_CACHE_HOME/HOME/TMPDIR uculu tanimsizdir) hicbir sey
    // kizarmaz, yalnizca her acilista butun boru hatlari yeniden kurulur.
    // Yolun ve isabetin loglanmasi, cihazda tek bakista dogrulanmasini saglar.
    // KURULUM BITER BITMEZ DISKE YAZ — kapanisi bekleme.
    // Sebep olculdu (2026-09-16, emulator): Android'de uygulamalar TEMIZ
    // KAPANMAZ; sistem sureci oldurur, `Device::shutdown` cogu zaman hic
    // kosmaz. Yazma yalnizca kapanista olsaydi onbellek, en cok ihtiyac duyan
    // platformda ASLA olusmazdi — ve bunu hicbir sey kizartmazdi, cunku oyun
    // yine calisir, sadece her acilista butun boru hatlarini yeniden kurar.
    // Bu noktada on isinma bitmis, yani bilinen butun varyantlar zaten kurulu.
    if (!b.dev.pso().stats().loaded && b.dev.pso().stats().created > 0) b.dev.pso().save();
    const rhi::PsoCacheStats ps = b.dev.pso().stats();
    const char *yol = b.dev.pso_cache_path();
    BINFO("pso onbellegi: %s — %s (%u boru hatti %.1f ms'de kuruldu, diske yazilan %llu B)",
          (yol && *yol) ? yol : "KAPALI (yazilabilir dizin bulunamadi)",
          ps.loaded ? "diskten YUKLENDI" : rhi::pso_reject_str(ps.reject), ps.created, ps.create_ns / 1e6,
          (unsigned long long)ps.saved_bytes);
  }
  return 1;
}

int teng_running(void) {
  if (!g || !g->inited) return 0;
  return g->running ? 1 : 0;
}
void teng_close(void) { CALL("teng_close"); if (g) { g->running = false; BINFO("kapatma istendi (kare %u)", g->frame); } }

int teng_frame_begin(void) {
  if (!ready("teng_frame_begin")) return 0;
  Bridge &b = *g;
  g_log.last_call = "teng_frame_begin";
  if (b.in_frame) { BERR("teng_frame_begin: onceki kare teng_frame_end ile kapanmadi (kare %u)", b.frame); }
  b.in_frame = true;
  const uint64_t now = platform::now_ns();
  b.dt = (float)((now - b.last_ns) / 1e9);
  b.last_ns = now;
  if (b.dt > 0.25f) b.dt = 0.25f;
  if (b.headless) b.dt = 1.0f / 60.0f;
  b.time_s += b.dt;
  b.prof.begin_frame();
  b.frame_arena.begin_frame();
  b.have_window = b.headless;
  if (!b.headless) {
    uint32_t fw = 0, fh = 0;
    switch (b.host.poll(b.host.user, &fw, &fh)) {
    case bridge::HostPoll::Quit: b.running = false; BINFO("host kapatma istedi (kare %u)", b.frame); break;
    case bridge::HostPoll::NoWindow: b.have_window = false; break;
    case bridge::HostPoll::WindowChanged: {
      BINFO("pencere yeniden yaratildi: swapchain + yuzey yeniden (kare %u)", b.frame);
      b.dev.api().vkDeviceWaitIdle(b.dev.handle());
      b.swap.shutdown();
      VkSurfaceKHR ns = VK_NULL_HANDLE;
      if (!b.host.create_surface(b.host.user, b.api, b.dev.instance(), &ns)) { BERR("yuzey yeniden olusturulamadi"); b.running = false; break; }
      b.dev.replace_surface(ns);
      if (!b.swap.init(b.dev, b.sys, ns, fw, fh)) { BERR("swapchain yeniden kurulamadi"); b.running = false; break; }
      b.ren.set_render_size(b.swap.extent().width, b.swap.extent().height);
      b.fb_w = fw; b.fb_h = fh;
      b.have_window = true;
      break;
    }
    case bridge::HostPoll::Run: b.have_window = fw && fh; if (fw && fh) { b.fb_w = fw; b.fb_h = fh; } break;
    }
    // --- PENCERE OLCUSU -> SWAPCHAIN, KAYITTAN ONCE --------------------------
    // Tam ekran / yeniden boyutlandirma BURADA yakalanir. Yalniz OUT_OF_DATE
    // beklemek tasinabilir degil: Wayland'de yuzey olcusunu uygulama surer ve
    // OUT_OF_DATE HIC gelmez — swapchain eski olcude kalir, kompozitor gerer
    // ("tam ekran olmuyor, icerik ayni oranda buyuyor"). Karar ve X11/Wayland
    // ayrimi: rhi/swapchain.hpp ResizeAction.
    if (b.running && b.have_window && b.swap.sync_size(b.fb_w, b.fb_h)) {
      b.ren.set_render_size(b.swap.extent().width, b.swap.extent().height);
      BINFO("swapchain %ux%u (%s)", b.swap.extent().width, b.swap.extent().height, b.swap.last_resize_reason());
      // Parlama (post) ic HDR hedefi KURULUMDA olculendi ve kare icinde
      // degismez (renderer.hpp post_width/post_height). Pencere buyurse sahne
      // o cozunurlukten olceklenir — sessiz kalmasin diye bir kez yazilir.
      const renderer::PostInfo pi = b.ren.post();
      if (pi.enabled && (pi.width != b.swap.extent().width || pi.height != b.swap.extent().height))
        BINFO("parlama ic hedefi %ux%u sabit: sahne bu cozunurlukten olceklenir (kurulumda belirlenir)", pi.width, pi.height);
    }
    b.in = b.host.input ? b.host.input(b.host.user) : nullptr;
    b.touch = b.host.touch ? b.host.touch(b.host.user) : nullptr;
    if (b.in) { std::memcpy(b.prev_keys, b.cur_keys, sizeof b.cur_keys); std::memcpy(b.cur_keys, b.in->key_down, sizeof b.cur_keys); }
    if (b.touch) b.stick.update(*b.touch, now);
  }
  BTRACE("kare %u basladi dt=%.4f pencere=%d dokunus=%u", b.frame, b.dt, (int)b.have_window, b.touch ? b.touch->count : 0u);
  return b.have_window ? 1 : 0;
}

void teng_frame_end(void) {
  if (!ready("teng_frame_end")) return;
  Bridge &b = *g;
  g_log.last_call = "teng_frame_end";
  if (!b.in_frame) BERR("teng_frame_end: teng_frame_begin cagrilmadan (kare %u)", b.frame);
  b.in_frame = false;
  {
    ENGINE_ZONE("sim");
    const uint32_t ticks = b.fs.advance(b.dt);
    // Carpisma halkasi ADIMLARDAN HEMEN ONCE temizlenir, kare basinda DEGIL.
    // Sira onemli ve bir kez yanlis kuruldu: fizik `teng_frame_end` icinde
    // adimlaniyor, yani olaylar ONCEKI karenin sonunda olusuyor; kare basinda
    // temizlemek onlari oyun okumadan siliyordu (olculdu: Tulpar tarafi
    // "0 carpisma" goruyordu, C++ kapisi ise olaylari goruyordu — ayni kodun
    // iki ucu farkli cevap veriyordu).
    // Buradaki temizlik, olaylarin "son adimda olusanlar" olmasini saglar ve
    // oyun onlari SONRAKI kare boyunca istedigi anda okuyabilir.
    b.phys.clear_contacts();
    for (uint32_t t = 0; t < ticks; t++) { b.phys.step(b.fs.step_s, 1); b.tick++; }
    if (ticks == b.fs.max_ticks_per_frame) BDBG("kare %u: sim %u tick ile kirpildi (dt %.3f)", b.frame, ticks, b.dt);
  }
  {
    // Animasyon zamani kare basina BIR kez ilerler — cizim yapilmasa da (arka
    // planda gecen kareler animasyonu duraklatmasin, sim ile ayni kural).
    ENGINE_ZONE("anim");
    for (uint32_t i = 0; i < b.ent_high; i++) {
      Ent &e = b.ents[i];
      if (!e.alive || e.kind != Kind::Model || e.anim_clip < 0) continue;
      if (e.asset < 0 || (uint32_t)e.asset >= b.model_count) continue;
      const content::Model &mdl = b.models[e.asset];
      if ((uint32_t)e.anim_clip >= mdl.clip_count) continue;
      const float dur = mdl.clips[e.anim_clip].duration;
      e.anim_time += b.dt * e.anim_speed;
      if (dur <= 0) { e.anim_time = 0; continue; }
      if (e.anim_loop) {
        while (e.anim_time >= dur) e.anim_time -= dur;
        while (e.anim_time < 0) e.anim_time += dur;
      } else if (e.anim_time > dur) e.anim_time = dur;
      else if (e.anim_time < 0) e.anim_time = 0;
    }
  }
  if (b.have_window) {
    ENGINE_ZONE("render");
    render_frame();
  } else { b.hud_n = 0; b.hud_text_n = 0; }
  b.prof.end_frame();
  b.frame++;
  g_log.frame = b.frame;
  // Sahne dosyasi izleyicisi: kare CIZILDIKTEN sonra (sahne kaynaklari kare
  // ortasinda degismesin).
  scene_watch_tick();
  if (b.frame % 120 == 0) {
    static uint64_t scratch[1200]; // profiler sozlesmesi: kare kapasitesinin 2 KATI (ilk yari ornek, ikinci yari siralama)
    const FrameStats st = b.prof.frame_stats(Span<uint64_t>(scratch, 1200), 120);
    b.fps = st.p50_ns > 0 ? (float)(1e9 / (double)st.p50_ns) : 0.0f;
    BDBG("kare %u | p50 %.2f ms p99 %.2f ms | cizim %u | isik %u | varlik %u | govde %u | tick %u", b.frame, st.p50_ns / 1e6, st.p99_ns / 1e6,
         b.ren.stats().draws, b.last_lights, b.ent_alive, b.phys.stats().bodies, b.tick);
  }
  if (b.headless && b.frame >= b.headless_frames) {
    b.running = false;
    BINFO("headless: %u kare tamamlandi", b.frame);
    if (b.out_ppm[0]) {
      if (rhi::write_ppm(b.out_ppm, b.ores.pixels, b.oc.width, b.oc.height)) BINFO("goruntu yazildi: %s", b.out_ppm);
      else BERR("goruntu yazilamadi: %s", b.out_ppm);
    }
  }
}

void teng_shutdown(void) {
  CALL("teng_shutdown");
  if (!g) return;
  Bridge &b = *g;
  if (!b.inited) { BDBG("shutdown: kurulmamis motor, atlandi"); return; }
  static uint64_t scratch[1200]; // profiler sozlesmesi: kare kapasitesinin 2 KATI (ilk yari ornek, ikinci yari siralama)
  const FrameStats st = b.prof.frame_stats(Span<uint64_t>(scratch, 1200), 0);
  BINFO("kapanis: %u kare, %.1f s, p50 %.2f ms p99 %.2f ms, varlik %u (en yuksek yuva %u), govde %u, model %u, hata %u, uyari %u",
        b.frame, b.time_s, st.p50_ns / 1e6, st.p99_ns / 1e6, b.ent_alive, b.ent_high, b.phys.stats().bodies, b.model_count, g_log.errors, g_log.warnings);
  BINFO("kapanis (ek): sahne yukleme %u, ses %s (%u cal, %u klip, %u yok sayilan cagri)", b.scene_loads,
        b.audio_ok ? b.audio_desc : "KAPALI", b.audio_plays, b.clip_count, b.audio_off_reports);
  BINFO("kapanis (arayuz/kayit): %u ui etkinlestirme (%u enjekte), sicak yukleme %u, kayit %s (%u anahtar, %u yazma, %u bozuk satir)", b.ui.clicks,
        b.ui.injects, b.scene_reloads, g_save.loaded ? g_save.path : "acilmadi", g_save.n, g_save.writes, g_save.bad_lines);
  // Kirli ayar kaybolmasin: betik eng_save_write cagirmayi unutsa bile kapanista yazilir.
  if (g_save.dirty) { BINFO("kayit kirli: kapanista diske yaziliyor"); teng_save_write(); }
  // Ses ONCE kapanir: cihaz thread'i klip orneklerini arenadan okuyor.
  if (b.audio_ok) { b.mixer.stop_all(); b.audio_dev.shutdown(); b.audio_ok = false; BINFO("ses kapatildi (%u cal, %u klip)", b.audio_plays, b.clip_count); }
  b.dev.api().vkDeviceWaitIdle(b.dev.handle());
  // IS SISTEMI ONCE SUSTURULUR — alt sistemlerden ONCE.
  //
  // Eskiden en SONDA kapaniyordu: fizik, renderer ve cihaz yikilirken worker
  // thread'leri HALA CALISIYORDU. Jolt'un is uyarlayicisi (FiberJoltJobs)
  // bizim kuyruga CIPLAK Job* itiyor; `delete impl_->jobs` o havuzu yok
  // ediyor. Bir worker o sirada elinde eski bir girdi tutuyorsa cop bir
  // isaretciyi cagiriyor.
  //
  // Olculdu (CI macOS/arm64, 2026-09-16): `thread: tulpar-job`, SIGSEGV,
  // fault_addr 0x8bc94512aa864210 (null degil — COP). Yigin izi iki cerceve,
  // cunku fiber yigini cozucuyu kesiyor. Dort kosumun ikisinde dustu: yaris.
  //
  // `jobs.shutdown()` worker'lari JOIN eder ve hicbir fiber'in park halinde
  // kalmadigini ENGINE_ASSERT ile dogrular. Ondan sonrasi tek thread'lidir,
  // yani bu sinif tamamen kapanir. Kapanis yolunda is URETEN kimse yok
  // (yikim yalniz Vulkan/arena nesnesi serbest birakiyor).
  b.jobs.shutdown();
  b.nav.shutdown();
  b.nav_ok = false;
  if (b.scene_ok) b.srt.despawn(b.phys);
  for (uint32_t i = 0; i < b.ent_high; i++) if (b.ents[i].alive && b.ents[i].body.valid()) b.phys.remove(b.ents[i].body);
  b.phys.shutdown();
  b.ren.shutdown();
  if (b.off) rhi::offscreen_destroy(b.off);
  if (!b.headless) b.swap.shutdown();
  b.dev.shutdown();
  if (b.host_open) bridge::bridge_host_close(&b.host);
  b.inited = false;
  b.running = false;
  if (g_log.errors) dump_ring("kapanista hata vardi");
}

double teng_dt(void) { return g ? g->dt : 0.0; }
double teng_time(void) { return g ? g->time_s : 0.0; }
int teng_frame(void) { return g ? (int)g->frame : 0; }
double teng_fps(void) { return g ? g->fps : 0.0; }
int teng_width(void) { return g ? (int)(g->headless ? g->fb_w : g->swap.logical_extent().width) : 0; }
int teng_height(void) { return g ? (int)(g->headless ? g->fb_h : g->swap.logical_extent().height) : 0; }
const char *teng_gpu_name(void) { return g && g->inited ? g->dev.caps().device_name : ""; }
int teng_screenshot(const char *path) {
  CALLF("teng_screenshot", "%s", path ? path : "");
  if (!ready("teng_screenshot") || !path) return 0;
  if (!g->headless) { BERR("teng_screenshot yalniz headless kipte (offscreen piksel okunur)"); return 0; }
  if (g->frame == 0) { BERR("teng_screenshot: henuz kare cizilmedi"); return 0; }
  const bool ok = rhi::write_ppm(path, g->ores.pixels, g->oc.width, g->oc.height);
  if (ok) BINFO("ekran goruntusu: %s (%ux%u)", path, g->oc.width, g->oc.height); else BERR("ekran goruntusu yazilamadi: %s", path);
  return ok ? 1 : 0;
}

// --- dunya / kamera ---------------------------------------------------------
void teng_sun(double dx, double dy, double dz, double diffuse) {
  CALLF("teng_sun", "(%.2f %.2f %.2f) x%.2f", dx, dy, dz, diffuse);
  if (!g) g = new (g_storage) Bridge();
  g->sun_dir = {(float)dx, (float)dy, (float)dz}; g->sun_diffuse = (float)diffuse;
  BDBG("gunes (%.2f %.2f %.2f) x%.2f", dx, dy, dz, diffuse);
}
void teng_ambient(int64_t color) {
  CALLF("teng_ambient", "%08llx", (unsigned long long)color);
  if (!g) g = new (g_storage) Bridge();
  g->ambient = color_of(color);
  BDBG("ortam isigi %08llx", (unsigned long long)color);
}
void teng_shadow_volume(double cx, double cy, double cz, double radius, double depth) {
  CALLF("teng_shadow_volume", "(%.1f %.1f %.1f) r%.1f d%.1f", cx, cy, cz, radius, depth);
  if (!g) g = new (g_storage) Bridge();
  g->shadow_center = {(float)cx, (float)cy, (float)cz}; g->shadow_radius = (float)radius; g->shadow_depth = (float)depth;
  BDBG("golge hacmi (%.1f %.1f %.1f) r%.1f d%.1f", cx, cy, cz, radius, depth);
}
void teng_camera(double ex, double ey, double ez, double tx, double ty, double tz) {
  CALLF("teng_camera", "goz (%.2f %.2f %.2f) hedef (%.2f %.2f %.2f)", ex, ey, ez, tx, ty, tz);
  if (!g) g = new (g_storage) Bridge();
  g->cam_eye = {(float)ex, (float)ey, (float)ez}; g->cam_target = {(float)tx, (float)ty, (float)tz};
  if (length_sq(g->cam_eye - g->cam_target) < 1e-6f) { BERR("teng_camera: goz ve hedef ayni nokta"); g->cam_eye.z += 1.0f; }
}
void teng_camera_orbit(double tx, double ty, double tz, double yaw, double pitch, double radius) {
  CALLF("teng_camera_orbit", "hedef (%.2f %.2f %.2f) yaw %.2f pitch %.2f r %.2f", tx, ty, tz, yaw, pitch, radius);
  if (!g) g = new (g_storage) Bridge();
  if (radius < 0.1) { BERR("teng_camera_orbit: yaricap %.2f cok kucuk, 0.1 yapildi", radius); radius = 0.1; }
  g->cam_target = {(float)tx, (float)ty, (float)tz};
  g->cam_eye = {(float)(tx + std::cos(pitch) * std::sin(yaw) * radius), (float)(ty + std::sin(pitch) * radius), (float)(tz + std::cos(pitch) * std::cos(yaw) * radius)};
}
double teng_camera_x(void) { return g ? g->cam_eye.x : 0; }
double teng_camera_y(void) { return g ? g->cam_eye.y : 0; }
double teng_camera_z(void) { return g ? g->cam_eye.z : 0; }

// --- sahne blob ---------------------------------------------------------------
int teng_scene_load(const char *path) {
  CALLF("teng_scene_load", "%s", path ? path : "");
  if (!ready("teng_scene_load") || !path) return 0;
  Bridge &b = *g;
  if (b.scene_ok) { BERR("teng_scene_load: sahne zaten yuklu (%s); once eng_scene_unload cagir (bolum gecisi)", path); return 0; }
  content::SceneBlobView v;
  content::SceneError err{};
  if (!content::scene_blob_load(b.sys, path, &v, &err)) { BERR("sahne blob yuklenemedi %s: %s", path, err.msg); std::snprintf(b.err, sizeof b.err, "%s", err.msg); return 0; }
  content::scene_dir_of(path, b.scene_dir, sizeof b.scene_dir);
  if (!b.srt.init(b.sys, b.ren, v, b.scene_dir)) { BERR("sahne runtime kurulamadi (%s)", path); return 0; }
  b.srt.apply_world(b.ren);
  const content::SceneWorld w = v.world();
  b.sun_dir = w.sun_dir; b.ambient = w.ambient; b.sun_diffuse = w.sun_diffuse;
  b.shadow_center = w.shadow_center; b.shadow_radius = w.shadow_radius; b.shadow_depth = w.shadow_depth;
  const uint32_t nb = b.srt.spawn(b.phys);
  b.scene_ok = true;
  b.scene_loads++;
  if (b.scene_loads > 1)
    BDBG("bolum %u: her yukleme kaynak AYIRIR (arena + GPU doku/mesh); bolum sayisi cok artarsa kapasite dolar", b.scene_loads);
  BINFO("sahne yuklendi: %s — %u varlik, %u cizim, %u isik, %u govde (%u fizige), kaynak %u/%u, ozet %016llx", path, v.h->entity_count, v.h->draw_count,
        v.h->light_count, v.h->body_count, nb, b.srt.stats().assets_loaded, v.h->asset_count, (unsigned long long)v.hash());
  if (b.srt.stats().assets_failed) BERR("sahne: %u kaynak yuklenemedi (dizin %s)", b.srt.stats().assets_failed, b.scene_dir);
  // Navmesh: bake DERLEME aninda yapildi (engine_sahnec), burada yalniz sorgu
  // nesnesi kurulur. Bake yoksa hata degil: oyun duz yol + isin testine duser.
  b.nav_n = 0;
  b.nav_partial = false;
  // Sicak yeniden yukleme damgasi: bu dosyanin YUKLENEN hali.
  std::snprintf(b.watch_path, sizeof b.watch_path, "%s", path);
  file_stamp_pub(b.watch_path, &b.watch_mtime, &b.watch_size);
  b.watch_pending = false;
  b.watch_last_frame = b.frame;
  b.nav_ok = b.nav.init(b.sys, v, 2048);
  if (b.nav_ok) BINFO("navmesh hazir: %u poligon, ozet %016llx (sorgu yolunda ayirma yok)", b.nav.polys(), (unsigned long long)b.nav.data_hash());
  else if (v.has_nav()) BERR("sahnede navmesh verisi var ama sorgu kurulamadi (%s) — kovalama duz yola duser", path);
  else BDBG("sahnede navmesh yok (bake edilmedi: sabit kutu govdeli yurunebilir zemin gerekir) — kovalama duz yola duser");
  return 1;
}
int teng_scene_count(void) { return g && g->scene_ok ? (int)g->srt.view().h->entity_count : 0; }
int teng_scene_find(const char *name) {
  CALLF("teng_scene_find", "%s", name ? name : "");
  if (!g || !g->scene_ok || !name) return -1;
  const content::SceneBlobView &v = g->srt.view();
  for (uint32_t i = 0; i < v.h->entity_count; i++) if (std::strcmp(v.entity_name(i), name) == 0) return (int)i;
  BDBG("sahnede varlik yok: \"%s\"", name);
  return -1;
}
static bool scene_idx_ok(int i, const char *fn) {
  if (!g || !g->scene_ok) { BERR("%s: sahne yuklu degil", fn); return false; }
  if (i < 0 || (uint32_t)i >= g->srt.view().h->entity_count) { BERR("%s: sahne dizini %d sinir disi (%u)", fn, i, g->srt.view().h->entity_count); return false; }
  return true;
}
double teng_scene_x(int i) { return scene_idx_ok(i, "teng_scene_x") ? g->srt.entity_matrix((uint32_t)i, &g->phys).m[3][0] : 0; }
double teng_scene_y(int i) { return scene_idx_ok(i, "teng_scene_y") ? g->srt.entity_matrix((uint32_t)i, &g->phys).m[3][1] : 0; }
double teng_scene_z(int i) { return scene_idx_ok(i, "teng_scene_z") ? g->srt.entity_matrix((uint32_t)i, &g->phys).m[3][2] : 0; }
const char *teng_scene_name(int i) { return scene_idx_ok(i, "teng_scene_name") ? g->srt.view().entity_name((uint32_t)i) : ""; }

// Sahne varligina bagli govde. Okuma tarafi govdesiz varlikta SESSIZ 0 doner
// (kopru varliklarindaki teng_vx ile ayni kural); yazma tarafi hata loglar.
static sim::BodyId scene_body(int i, const char *fn) {
  if (!scene_idx_ok(i, fn)) return sim::BodyId{};
  return g->srt.entity_body((uint32_t)i);
}
static bool scene_body_writable(int i, const char *fn, sim::BodyId *out) {
  if (!scene_idx_ok(i, fn)) return false;
  const sim::BodyId b = g->srt.entity_body((uint32_t)i);
  if (!b.valid() || !g->srt.entity_dynamic((uint32_t)i)) {
    BERR("%s: sahne varligi %d (\"%s\") dinamik govde degil (%s) — cagri yok sayildi", fn, i, g->srt.view().entity_name((uint32_t)i),
         b.valid() ? "sabit govde" : "govdesiz");
    return false;
  }
  *out = b;
  return true;
}
double teng_scene_vx(int i) { const sim::BodyId b = scene_body(i, "teng_scene_vx"); return b.valid() ? g->phys.linear_velocity(b).x : 0.0; }
double teng_scene_vy(int i) { const sim::BodyId b = scene_body(i, "teng_scene_vy"); return b.valid() ? g->phys.linear_velocity(b).y : 0.0; }
double teng_scene_vz(int i) { const sim::BodyId b = scene_body(i, "teng_scene_vz"); return b.valid() ? g->phys.linear_velocity(b).z : 0.0; }
int teng_scene_is_dynamic(int i) { return scene_idx_ok(i, "teng_scene_is_dynamic") && g->srt.entity_dynamic((uint32_t)i) ? 1 : 0; }
void teng_scene_set_velocity(int i, double vx, double vy, double vz) {
  CALLF("teng_scene_set_velocity", "%d (%.2f %.2f %.2f)", i, vx, vy, vz);
  sim::BodyId b;
  if (!scene_body_writable(i, "teng_scene_set_velocity", &b)) return;
  g->phys.set_linear_velocity(b, {(float)vx, (float)vy, (float)vz});
  BTRACE("sahne %d hiz (%.2f %.2f %.2f)", i, vx, vy, vz);
}
void teng_scene_impulse(int i, double ix, double iy, double iz) {
  CALLF("teng_scene_impulse", "%d (%.2f %.2f %.2f)", i, ix, iy, iz);
  sim::BodyId b;
  if (!scene_body_writable(i, "teng_scene_impulse", &b)) return;
  const Vec3 v = g->phys.linear_velocity(b);
  g->phys.set_linear_velocity(b, {v.x + (float)ix, v.y + (float)iy, v.z + (float)iz});
  BDBG("sahne %d (\"%s\") durtu (%.2f %.2f %.2f): hiz (%.2f %.2f %.2f)", i, g->srt.view().entity_name((uint32_t)i), ix, iy, iz, v.x + ix, v.y + iy,
       v.z + iz);
}
int teng_scene_loaded(void) { return g && g->scene_ok ? 1 : 0; }
int teng_scene_unload(void) {
  CALL("teng_scene_unload");
  if (!ready("teng_scene_unload")) return 0;
  Bridge &b = *g;
  if (!b.scene_ok) { BERR("teng_scene_unload: yuklu sahne yok"); return 0; }
  const uint32_t ents = b.srt.view().h->entity_count, bodies = b.srt.stats().bodies;
  b.srt.despawn(b.phys);
  b.nav.shutdown(); // Detour nesneleri; veri arenada kalir (bir sonraki yukleme yeni kopya alir)
  b.nav_ok = false;
  b.nav_n = 0;
  b.nav_near_ok = false;
  b.nav_ray_t = 1.0f;
  b.scene_ok = false; // cizim + sorgular durur; teng_scene_load yeniden kabul eder
  // Betik bosalttiysa izleme hedefi de duser: bosaltilmis bir sahne, dosyasi
  // degisti diye kendiliginden GERI GELMEZ. Sicak yukleme kendi icinde bosaltir
  // (b.reloading), orada hedef korunur.
  if (!b.reloading) { b.watch_path[0] = 0; b.watch_pending = false; }
  BINFO("sahne bosaltildi: %u varlik, %u govde fizikten cikti (fizikte %u govde kaldi) — bolum gecisine hazir", ents, bodies, b.phys.stats().bodies);
  return 1;
}

// --- varliklar -----------------------------------------------------------------
int teng_spawn_box(double x, double y, double z, double hx, double hy, double hz, int dynamic, int64_t color) {
  CALLF("teng_spawn_box", "(%.2f %.2f %.2f) yarim (%.2f %.2f %.2f) %s %08llx", x, y, z, hx, hy, hz, dynamic ? "dinamik" : "sabit", (unsigned long long)color);
  if (!ready("teng_spawn_box")) return 0;
  if (hx <= 0 || hy <= 0 || hz <= 0) { BERR("teng_spawn_box: yarim kenar pozitif olmali (%.2f %.2f %.2f)", hx, hy, hz); return 0; }
  const int s = alloc_slot(Kind::Box);
  if (s < 0) return 0;
  Ent &e = g->ents[s];
  e.pos = {(float)x, (float)y, (float)z}; e.half = {(float)hx, (float)hy, (float)hz}; e.dynamic = dynamic != 0; e.color = color_of(color);
  if (!make_body(e, "teng_spawn_box")) { free_slot((uint32_t)s); return 0; }
  const int id = make_id((uint32_t)s);
  BDBG("kutu #%d yuva %d %s (%.2f %.2f %.2f)", id, s, e.dynamic ? "dinamik" : "sabit", x, y, z);
  return id;
}
int teng_spawn_sphere(double x, double y, double z, double radius, int dynamic, int64_t color) {
  CALLF("teng_spawn_sphere", "(%.2f %.2f %.2f) r%.2f %s %08llx", x, y, z, radius, dynamic ? "dinamik" : "sabit", (unsigned long long)color);
  if (!ready("teng_spawn_sphere")) return 0;
  if (radius <= 0) { BERR("teng_spawn_sphere: yaricap pozitif olmali (%.2f)", radius); return 0; }
  const int s = alloc_slot(Kind::Sphere);
  if (s < 0) return 0;
  Ent &e = g->ents[s];
  e.pos = {(float)x, (float)y, (float)z}; e.radius = (float)radius; e.dynamic = dynamic != 0; e.color = color_of(color);
  if (!make_body(e, "teng_spawn_sphere")) { free_slot((uint32_t)s); return 0; }
  const int id = make_id((uint32_t)s);
  BDBG("kure #%d yuva %d %s (%.2f %.2f %.2f) r%.2f", id, s, e.dynamic ? "dinamik" : "sabit", x, y, z, radius);
  return id;
}
int teng_spawn_ground(double half_size, int64_t color) {
  CALLF("teng_spawn_ground", "yarim %.1f %08llx", half_size, (unsigned long long)color);
  if (!ready("teng_spawn_ground")) return 0;
  if (half_size <= 0) { BERR("teng_spawn_ground: boyut pozitif olmali"); return 0; }
  const int s = alloc_slot(Kind::Ground);
  if (s < 0) return 0;
  Ent &e = g->ents[s];
  e.pos = {0, -0.5f, 0}; e.half = {(float)half_size, 0.5f, (float)half_size}; e.dynamic = false; e.color = color_of(color);
  if (!make_body(e, "teng_spawn_ground")) { free_slot((uint32_t)s); return 0; }
  const int id = make_id((uint32_t)s);
  BDBG("zemin #%d %.1fx%.1f (ust yuz y=0)", id, half_size * 2, half_size * 2);
  return id;
}
int teng_load_model(const char *path) {
  CALLF("teng_load_model", "%s", path ? path : "");
  if (!ready("teng_load_model") || !path) return -1;
  Bridge &b = *g;
  if (b.model_count >= kMaxModels) { BERR("teng_load_model: model kapasitesi dolu (%u)", kMaxModels); return -1; }
  const uint32_t i = b.model_count;
  b.models[i] = content::Model{};
  if (!content::gltf_load(b.sys, path, &b.models[i])) { BERR("model yuklenemedi %s: %s", path, b.models[i].error); return -1; }
  if (!content::upload_model(b.ren, b.sys, b.models[i], &b.ups[i])) { BERR("model GPU'ya yuklenemedi: %s", path); return -1; }
  b.model_count++;
  BINFO("model %u: %s (%u mesh, %u malzeme, %u doku, %u klip)", i, path, b.ups[i].mesh_count, b.ups[i].material_count, b.ups[i].texture_count, b.models[i].clip_count);
  return (int)i;
}
int teng_spawn_model(int asset, double x, double y, double z, double scale, int64_t tint) {
  CALLF("teng_spawn_model", "model %d (%.2f %.2f %.2f) x%.2f", asset, x, y, z, scale);
  if (!ready("teng_spawn_model")) return 0;
  if (asset < 0 || (uint32_t)asset >= g->model_count) { BERR("teng_spawn_model: model %d yok (%u yuklu)", asset, g->model_count); return 0; }
  const int s = alloc_slot(Kind::Model);
  if (s < 0) return 0;
  Ent &e = g->ents[s];
  e.pos = {(float)x, (float)y, (float)z}; e.asset = asset; e.scale = (float)(scale > 0 ? scale : 1.0); e.color = color_of(tint);
  const int id = make_id((uint32_t)s);
  BDBG("model varligi #%d (model %d) (%.2f %.2f %.2f)", id, asset, x, y, z);
  return id;
}
int teng_spawn_light(double x, double y, double z, int64_t color, double intensity, double radius) {
  CALLF("teng_spawn_light", "(%.2f %.2f %.2f) %08llx x%.2f r%.2f", x, y, z, (unsigned long long)color, intensity, radius);
  if (!ready("teng_spawn_light")) return 0;
  const int s = alloc_slot(Kind::Light);
  if (s < 0) return 0;
  Ent &e = g->ents[s];
  e.pos = {(float)x, (float)y, (float)z}; e.color = color_of(color); e.intensity = (float)intensity; e.light_radius = (float)(radius > 0 ? radius : 5.0);
  const int id = make_id((uint32_t)s);
  BDBG("isik #%d (%.2f %.2f %.2f) x%.2f r%.2f (renderer siniri %u)", id, x, y, z, intensity, radius, renderer::Renderer::kMaxPointLights);
  return id;
}
void teng_despawn(int id) {
  CALLF("teng_despawn", "#%d", id);
  const int32_t s = slot_of(id, "teng_despawn");
  if (s < 0) return;
  BDBG("sil #%d (%s)", id, kind_name(g->ents[s].kind));
  free_slot((uint32_t)s);
}
int teng_alive(int id) {
  if (!g || id <= 0) return 0;
  const uint32_t slot = (uint32_t)id & 0xFFFFu, gen = (uint32_t)id >> 16;
  return slot < kMaxEntities && g->ents[slot].alive && g->ents[slot].gen == gen ? 1 : 0;
}
int teng_count(void) { return g ? (int)g->ent_alive : 0; }
double teng_x(int id) { const int32_t s = slot_of(id, "teng_x"); return s < 0 ? 0 : ent_pos(g->ents[s]).x; }
double teng_y(int id) { const int32_t s = slot_of(id, "teng_y"); return s < 0 ? 0 : ent_pos(g->ents[s]).y; }
double teng_z(int id) { const int32_t s = slot_of(id, "teng_z"); return s < 0 ? 0 : ent_pos(g->ents[s]).z; }
void teng_set_pos(int id, double x, double y, double z) {
  CALLF("teng_set_pos", "#%d (%.2f %.2f %.2f)", id, x, y, z);
  const int32_t s = slot_of(id, "teng_set_pos");
  if (s < 0) return;
  Ent &e = g->ents[s];
  e.pos = {(float)x, (float)y, (float)z};
  if (e.body.valid()) { // Jolt'ta konum yazma yok: govde yeniden kurulur (hiz sifirlanir)
    g->phys.remove(e.body);
    e.body = sim::BodyId{};
    if (!make_body(e, "teng_set_pos")) BERR("teng_set_pos: govde yeniden kurulamadi #%d", id);
    else BTRACE("#%d govde yeniden kuruldu (isinlama)", id);
  }
}
void teng_set_color(int id, int64_t color) {
  CALLF("teng_set_color", "#%d %08llx", id, (unsigned long long)color);
  const int32_t s = slot_of(id, "teng_set_color");
  if (s >= 0) g->ents[s].color = color_of(color);
}
void teng_set_scale(int id, double sc) {
  CALLF("teng_set_scale", "#%d %.2f", id, sc);
  const int32_t s = slot_of(id, "teng_set_scale");
  if (s < 0) return;
  if (sc <= 0) { BERR("teng_set_scale: olcek pozitif olmali (%.2f)", sc); return; }
  g->ents[s].scale = (float)sc;
  if (g->ents[s].body.valid()) BDBG("teng_set_scale #%d: gorsel olcek degisti, fizik govdesi ayni kaldi", id);
}
void teng_set_yaw(int id, double yaw_deg) {
  CALLF("teng_set_yaw", "#%d %.1f", id, yaw_deg);
  const int32_t s = slot_of(id, "teng_set_yaw");
  if (s < 0) return;
  Ent &e = g->ents[s];
  e.yaw_deg = (float)yaw_deg;
  if (e.body.valid() && !e.dynamic) { g->phys.remove(e.body); e.body = sim::BodyId{}; make_body(e, "teng_set_yaw"); }
}
double teng_vx(int id) { const int32_t s = slot_of(id, "teng_vx"); return s < 0 || !g->ents[s].body.valid() ? 0 : g->phys.linear_velocity(g->ents[s].body).x; }
double teng_vy(int id) { const int32_t s = slot_of(id, "teng_vy"); return s < 0 || !g->ents[s].body.valid() ? 0 : g->phys.linear_velocity(g->ents[s].body).y; }
double teng_vz(int id) { const int32_t s = slot_of(id, "teng_vz"); return s < 0 || !g->ents[s].body.valid() ? 0 : g->phys.linear_velocity(g->ents[s].body).z; }
void teng_set_velocity(int id, double vx, double vy, double vz) {
  CALLF("teng_set_velocity", "#%d (%.2f %.2f %.2f)", id, vx, vy, vz);
  const int32_t s = slot_of(id, "teng_set_velocity");
  if (s < 0) return;
  Ent &e = g->ents[s];
  if (!e.body.valid() || !e.dynamic) { BERR("teng_set_velocity: #%d dinamik govde degil (%s)", id, kind_name(e.kind)); return; }
  g->phys.set_linear_velocity(e.body, {(float)vx, (float)vy, (float)vz});
}
void teng_impulse(int id, double ix, double iy, double iz) {
  CALLF("teng_impulse", "#%d (%.2f %.2f %.2f)", id, ix, iy, iz);
  const int32_t s = slot_of(id, "teng_impulse");
  if (s < 0) return;
  Ent &e = g->ents[s];
  if (!e.body.valid() || !e.dynamic) { BERR("teng_impulse: #%d dinamik govde degil (%s)", id, kind_name(e.kind)); return; }
  const Vec3 v = g->phys.linear_velocity(e.body);
  g->phys.set_linear_velocity(e.body, {v.x + (float)ix, v.y + (float)iy, v.z + (float)iz});
}
int teng_is_dynamic(int id) { const int32_t s = slot_of(id, "teng_is_dynamic"); return s >= 0 && g->ents[s].dynamic ? 1 : 0; }
int teng_awake(int id) { const int32_t s = slot_of(id, "teng_awake"); return s >= 0 && g->ents[s].body.valid() && g->phys.is_active(g->ents[s].body) ? 1 : 0; }

// --- model animasyonu -------------------------------------------------------
// Klip modele aittir (glTF animasyonu), varliga ATANIR. Atanmis klip her kare
// (render_frame) degerlendirilip draw_model'e verilir; klip yoksa model statik
// cizilir. Zaman ilerlemesi teng_frame_end'de, cizimden bagimsiz.
static const content::Model *model_of(int asset, const char *fn) {
  if (!ready(fn)) return nullptr;
  if (asset < 0 || (uint32_t)asset >= g->model_count) { BERR("%s: model %d yok (%u yuklu)", fn, asset, g->model_count); return nullptr; }
  return &g->models[asset];
}
static const content::ModelClip *clip_of(int asset, int clip, const char *fn) {
  const content::Model *m = model_of(asset, fn);
  if (!m) return nullptr;
  if (clip < 0 || (uint32_t)clip >= m->clip_count) { BERR("%s: model %d'de klip %d yok (%u klip)", fn, asset, clip, m->clip_count); return nullptr; }
  return &m->clips[clip];
}
int teng_model_clip_count(int asset) {
  CALLF("teng_model_clip_count", "%d", asset);
  const content::Model *m = model_of(asset, "teng_model_clip_count");
  return m ? (int)m->clip_count : 0;
}
double teng_model_clip_duration(int asset, int clip) {
  CALLF("teng_model_clip_duration", "%d/%d", asset, clip);
  const content::ModelClip *c = clip_of(asset, clip, "teng_model_clip_duration");
  return c ? c->duration : 0.0;
}
const char *teng_model_clip_name(int asset, int clip) {
  CALLF("teng_model_clip_name", "%d/%d", asset, clip);
  const content::ModelClip *c = clip_of(asset, clip, "teng_model_clip_name");
  return c ? c->name : "";
}
void teng_set_anim(int id, int clip, double speed, int loop) {
  CALLF("teng_set_anim", "#%d klip %d hiz %.2f %s", id, clip, speed, loop ? "dongu" : "tek");
  const int32_t s = slot_of(id, "teng_set_anim");
  if (s < 0) return;
  Ent &e = g->ents[s];
  if (e.kind != Kind::Model) { BERR("teng_set_anim: #%d model degil (%s); animasyon yalniz model varliklarinda", id, kind_name(e.kind)); return; }
  if (clip < 0) {
    if (e.anim_clip >= 0) BDBG("#%d animasyon kapatildi (statik cizime dondu)", id);
    e.anim_clip = -1;
    e.anim_time = 0;
    return;
  }
  const content::Model *m = model_of(e.asset, "teng_set_anim");
  if (!m) return;
  if ((uint32_t)clip >= m->clip_count) {
    BERR("teng_set_anim: #%d icin klip %d yok (model %d'de %u klip; glTF'te animasyon var mi?)", id, clip, e.asset, m->clip_count);
    return;
  }
  e.anim_clip = clip;
  e.anim_speed = (float)speed;
  e.anim_loop = loop != 0;
  e.anim_time = 0;
  BDBG("#%d animasyon: klip %d \"%s\" %.2f s, hiz %.2f, %s", id, clip, m->clips[clip].name, m->clips[clip].duration, speed, loop ? "dongulu" : "tek sefer");
  if (speed == 0.0) BDBG("#%d animasyon hizi 0: poz ilk karede donar (kasitli mi?)", id);
}
double teng_anim_time(int id) { const int32_t s = slot_of(id, "teng_anim_time"); return s < 0 ? 0.0 : g->ents[s].anim_time; }
int teng_anim_done(int id) {
  const int32_t s = slot_of(id, "teng_anim_done");
  if (s < 0) return 0;
  const Ent &e = g->ents[s];
  if (e.anim_clip < 0 || e.anim_loop) return 0; // dongulu klip bitmez
  const content::Model *m = model_of(e.asset, "teng_anim_done");
  if (!m || (uint32_t)e.anim_clip >= m->clip_count) return 0;
  return e.anim_time >= m->clips[e.anim_clip].duration ? 1 : 0;
}

// --- girdi ----------------------------------------------------------------------
// Tus adi ONCE dogrulanir: girdi cihazi olmadigi icin erken donersek (headless,
// Android) yanlis yazilmis bir ad sessizce hep false doner ve "tus calismiyor"
// diye saatler gider. Ad hatasi her kipte loglanir.
int teng_key_down(const char *name) {
  const int c = key_code(name);
  if (c < 0) { BERR("teng_key_down: bilinmeyen tus adi \"%s\" (W/A/S/D, SPACE, UP.., ESC, ENTER, SHIFT, 0-9)", name ? name : ""); return 0; }
  return g && g->in && g->cur_keys[c] ? 1 : 0;
}
int teng_key_pressed(const char *name) {
  const int c = key_code(name);
  if (c < 0) { BERR("teng_key_pressed: bilinmeyen tus adi \"%s\" (W/A/S/D, SPACE, UP.., ESC, ENTER, SHIFT, 0-9)", name ? name : ""); return 0; }
  return g && g->in && g->cur_keys[c] && !g->prev_keys[c] ? 1 : 0;
}
int teng_touch_count(void) { return g && g->touch ? (int)g->touch->count : 0; }
double teng_touch_x(int i) {
  if (!g || !g->touch) return 0;
  uint32_t k = 0;
  for (uint32_t j = 0; j < platform::TouchState::kMax; j++) if (g->touch->pts[j].down) { if ((int)k == i) return g->touch->pts[j].x; k++; }
  return 0;
}
double teng_touch_y(int i) {
  if (!g || !g->touch) return 0;
  uint32_t k = 0;
  for (uint32_t j = 0; j < platform::TouchState::kMax; j++) if (g->touch->pts[j].down) { if ((int)k == i) return g->touch->pts[j].y; k++; }
  return 0;
}
double teng_stick_x(void) { return g ? g->stick.move.x : 0; }
double teng_stick_y(void) { return g ? g->stick.move.y : 0; }
int teng_stick_action(void) { return g && g->stick.action ? 1 : 0; }
double teng_look_dx(void) { return g ? g->stick.look_delta.x : 0; }
double teng_mouse_x(void) { return g && g->in ? g->in->mouse_x : 0; }
double teng_mouse_y(void) { return g && g->in ? g->in->mouse_y : 0; }
int teng_mouse_down(int button) { return g && g->in && button >= 0 && button < 3 && g->in->mouse_down[button] ? 1 : 0; }

// --- HUD -------------------------------------------------------------------------
void teng_rect(double x, double y, double w, double h, int64_t color) {
  if (!ready("teng_rect")) return;
  Bridge &b = *g;
  if (!b.in_frame) { BERR("teng_rect: kare disinda (teng_frame_begin ile teng_frame_end arasinda cagir)"); return; }
  if (b.hud_n >= kMaxHud) { if (b.hud_n == kMaxHud) { BERR("HUD komut siniri (%u) asildi, fazlasi atlandi", kMaxHud); b.hud_n++; } return; }
  b.hud[b.hud_n++] = HudCmd{0, (float)x, (float)y, (float)w, (float)h, 1.0f, rgba_of(color), 0};
}
void teng_text(const char *s, double x, double y, double scale, int64_t color) {
  if (!ready("teng_text") || !s) return;
  Bridge &b = *g;
  if (!b.in_frame) { BERR("teng_text: kare disinda (teng_frame_begin ile teng_frame_end arasinda cagir)"); return; }
  if (b.hud_n >= kMaxHud) { if (b.hud_n == kMaxHud) { BERR("HUD komut siniri (%u) asildi", kMaxHud); b.hud_n++; } return; }
  const size_t n = std::strlen(s) + 1;
  if (b.hud_text_n + n > kHudTextBytes) { BERR("HUD metin tamponu doldu (%u bayt)", kHudTextBytes); return; }
  std::memcpy(b.hud_text + b.hud_text_n, s, n);
  b.hud[b.hud_n++] = HudCmd{1, (float)x, (float)y, 0, 0, (float)(scale > 0 ? scale : 1.0), rgba_of(color), b.hud_text_n};
  b.hud_text_n += (uint32_t)n;
}
double teng_text_width(const char *s, double scale) { return g && g->font_ok && s ? g->font.text_width(s, (float)(scale > 0 ? scale : 1.0)) : 0; }

// --- anlik-kip (immediate mode) arayuz -------------------------------------------
// Yeni cizim yolu YOK: her widget teng_rect + teng_text kuyruklar. Durum tek
// yapida (Ui): isaretci + "hangi widget basili". Deger BETIKTE yasar (Tulpar'da
// cikti parametresi yok), widget YENI degeri dondurur.
static int64_t ui_shade(int64_t c, float f) {
  const int64_t a = c & 0xFF;
  int r = (int)(((c >> 24) & 0xFF) * f), gq = (int)(((c >> 16) & 0xFF) * f), b = (int)(((c >> 8) & 0xFF) * f);
  if (r > 255) r = 255;
  if (gq > 255) gq = 255;
  if (b > 255) b = 255;
  return ((int64_t)r << 24) | ((int64_t)gq << 16) | ((int64_t)b << 8) | a;
}
static bool ui_hit(double x, double y, double w, double h) {
  const Ui &u = g->ui;
  return u.px >= (float)x && u.px <= (float)(x + w) && u.py >= (float)y && u.py <= (float)(y + h);
}
// Kimlik = etiket ozeti (FNV-1a) + widget turu: cagri SIRASI degisse de (menu
// bir satir buyudu) basili widget kaymaz. Ayni karede ayni kimlik iki kez =
// gercek hata (iki parca tek durumu paylasir), loglanir.
static uint32_t ui_hash(const char *s, uint8_t kind) {
  uint32_t h = 2166136261u ^ ((uint32_t)kind * 16777619u);
  for (const char *p = s; p && *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return h ? h : 1u;
}
static bool ui_frame(const char *fn) {
  if (!ready(fn)) return false;
  Bridge &b = *g;
  if (!b.in_frame) { BERR("%s: kare disinda (eng_frame_begin ile eng_frame_end arasinda cagir)", fn); return false; }
  if (!b.ui.begun || b.ui.begun_frame != b.frame) { BERR("%s: once eng_ui_begin cagrilmali (bu karede cagrilmadi)", fn); return false; }
  return true;
}
static uint32_t ui_use(const char *label, uint8_t kind, const char *fn) {
  Ui &u = g->ui;
  const uint32_t id = ui_hash(label ? label : "", kind);
  for (uint32_t i = 0; i < u.id_n && i < kMaxUiIds; i++)
    if (u.ids[i] == id) { BERR("%s: \"%s\" bu karede ikinci kez — ayni kimlik iki widget'a dusuyor (etiketi farklilastir)", fn, label ? label : ""); break; }
  if (u.id_n < kMaxUiIds) u.ids[u.id_n] = id;
  else if (u.id_n == kMaxUiIds) BERR("%s: kare basina widget siniri (%u) asildi, kimlik izlenmiyor", fn, kMaxUiIds);
  u.id_n++;
  u.widgets++;
  return id;
}
// Metin olcegi widget yuksekliginden: font cozunurluge gore yuklenir (1080p'de
// 28 px), o yuzden oran font yuksekligiyle kurulur. Font yoksa (HUD kapali)
// cizim atlanir ama TIKLAMA calisir — yerlesim testi sessizce bosa cikmasin.
static float ui_scale(double h) {
  const float fh = g->font_ok ? g->font.height() : 24.0f;
  float s = (float)h * 0.42f / (fh > 1.0f ? fh : 1.0f);
  if (s < 0.6f) s = 0.6f;
  if (s > 2.0f) s = 2.0f;
  return s;
}
// align: 0 ortali, 1 sola yasli, 2 saga yasli.
static void ui_text_box(const char *s, double x, double y, double w, double h, int64_t color, int align, float scale) {
  if (!s || !g->font_ok) return;
  const float tw = g->font.text_width(s, scale), th = g->font.height() * scale;
  float tx = (float)x + 10.0f;
  if (align == 0) tx = (float)x + ((float)w - tw) * 0.5f;
  else if (align == 2) tx = (float)(x + w) - tw - 10.0f;
  teng_text(s, tx, (float)y + ((float)h - th) * 0.5f, scale, color);
}

void teng_ui_begin(void) {
  CALL("teng_ui_begin");
  if (!ready("teng_ui_begin")) return;
  Bridge &b = *g;
  if (!b.in_frame) { BERR("teng_ui_begin: kare disinda (eng_frame_begin ile eng_frame_end arasinda cagir)"); return; }
  Ui &u = b.ui;
  if (u.begun && u.begun_frame == b.frame) BERR("teng_ui_begin: bu karede ikinci kez cagrildi (kare %u) — eng_ui_end unutuldu mu", b.frame);
  u.begun = true;
  u.begun_frame = b.frame;
  u.id_n = 0;
  u.widgets = 0;
  u.hot = 0;
  u.enabled = true;
  if (u.inject) { // pencersiz dogrulama: tek karede bas+birak
    u.px = u.inj_x;
    u.py = u.inj_y;
    u.pressed = true;
    u.released = true;
    u.down = false;
    u.prev_down = false;
    u.inject = false;
    u.injects++;
    BDBG("ui: enjekte tiklama (%.0f, %.0f) — tek karede bas+birak (%u.)", u.px, u.py, u.injects);
  } else {
    float x = u.px, y = u.py;
    bool d = false;
    u.touch_pointer = false;
    if (b.touch && b.touch->count > 0) { x = (float)teng_touch_x(0); y = (float)teng_touch_y(0); d = true; u.touch_pointer = true; }
    else if (b.in) { x = (float)b.in->mouse_x; y = (float)b.in->mouse_y; d = b.in->mouse_down[0]; }
    u.px = x;
    u.py = y;
    u.pressed = d && !u.prev_down;
    u.released = !d && u.prev_down;
    u.down = d;
    u.prev_down = d;
  }
}
void teng_ui_end(void) {
  CALL("teng_ui_end");
  if (!ready("teng_ui_end")) return;
  Ui &u = g->ui;
  if (!u.begun || u.begun_frame != g->frame) { BERR("teng_ui_end: eng_ui_begin cagrilmadan (kare %u)", g->frame); return; }
  if (!u.down) u.active = 0; // isaretci kalkti: surukleme bitti
  u.begun = false;
  BTRACE("ui: %u widget, hot %u, aktif %u, isaretci (%.0f %.0f) %s", u.widgets, u.hot, u.active, u.px, u.py,
         u.down ? "basili" : "serbest");
}
void teng_ui_theme(int64_t panel, int64_t text, int64_t accent) {
  CALLF("teng_ui_theme", "%08llx %08llx %08llx", (unsigned long long)panel, (unsigned long long)text, (unsigned long long)accent);
  if (!g) g = new (g_storage) Bridge();
  g->ui.c_panel = panel;
  g->ui.c_text = text;
  g->ui.c_accent = accent;
  BDBG("ui tema: panel %08llx yazi %08llx vurgu %08llx", (unsigned long long)panel, (unsigned long long)text, (unsigned long long)accent);
}
void teng_ui_enable(int on) {
  CALLF("teng_ui_enable", "%d", on);
  if (!ui_frame("teng_ui_enable")) return;
  g->ui.enabled = on != 0;
}
void teng_ui_panel(double x, double y, double w, double h, int64_t color) {
  CALLF("teng_ui_panel", "(%.0f %.0f %.0f %.0f) %08llx", x, y, w, h, (unsigned long long)color);
  if (!ready("teng_ui_panel")) return;
  if (!g->in_frame) { BERR("teng_ui_panel: kare disinda (eng_frame_begin ile eng_frame_end arasinda cagir)"); return; }
  const int64_t c = color ? color : g->ui.c_panel;
  teng_rect(x, y, w, h, c);
  teng_rect(x, y, w, 2, ui_shade(c, 2.2)); // ust kenar cizgisi: panel zeminden ayrilsin
}
void teng_ui_label(const char *s, double x, double y, double scale, int64_t color) {
  CALLF("teng_ui_label", "\"%s\" (%.0f %.0f) x%.2f", s ? s : "", x, y, scale);
  if (!ready("teng_ui_label")) return;
  if (!g->in_frame) { BERR("teng_ui_label: kare disinda (eng_frame_begin ile eng_frame_end arasinda cagir)"); return; }
  teng_text(s, x, y, scale, color ? color : g->ui.c_text);
}
int teng_ui_button(const char *label, double x, double y, double w, double h) {
  CALLF("teng_ui_button", "\"%s\" (%.0f %.0f %.0f %.0f)", label ? label : "", x, y, w, h);
  if (!ui_frame("teng_ui_button")) return 0;
  Ui &u = g->ui;
  if (w <= 0 || h <= 0) { BERR("teng_ui_button: \"%s\" olculeri pozitif olmali (%.1fx%.1f)", label ? label : "", w, h); return 0; }
  const uint32_t id = ui_use(label, 1, "teng_ui_button");
  const bool en = u.enabled, inside = ui_hit(x, y, w, h);
  bool clicked = false;
  if (en) {
    if (inside) u.hot = id;
    if (inside && u.pressed) u.active = id;
    if (u.active == id && u.released) {
      u.active = 0;
      if (inside) { clicked = true; u.clicks++; BDBG("ui: \"%s\" tiklandi (%u. etkinlestirme)", label ? label : "", u.clicks); }
    }
  }
  int64_t fill = ui_shade(u.c_panel, 2.4);
  if (!en) fill = ui_shade(u.c_panel, 1.5);
  else if (u.active == id) fill = u.c_accent;
  else if (inside) fill = ui_shade(u.c_panel, 3.4);
  teng_rect(x, y, w, h, fill);
  teng_rect(x, y + h - 3, w, 3, ui_shade(fill, 0.55));
  ui_text_box(label, x, y, w, h, en ? u.c_text : ui_shade(u.c_text, 0.55), 0, ui_scale(h));
  return clicked ? 1 : 0;
}
int teng_ui_checkbox(const char *label, double x, double y, double w, double h, int value) {
  CALLF("teng_ui_checkbox", "\"%s\" (%.0f %.0f %.0f %.0f) %d", label ? label : "", x, y, w, h, value);
  if (!ui_frame("teng_ui_checkbox")) return value ? 1 : 0;
  Ui &u = g->ui;
  if (w <= 0 || h <= 0) { BERR("teng_ui_checkbox: \"%s\" olculeri pozitif olmali (%.1fx%.1f)", label ? label : "", w, h); return value ? 1 : 0; }
  const uint32_t id = ui_use(label, 2, "teng_ui_checkbox");
  const bool en = u.enabled, inside = ui_hit(x, y, w, h);
  bool v = value != 0;
  if (en) {
    if (inside) u.hot = id;
    if (inside && u.pressed) u.active = id;
    if (u.active == id && u.released) {
      u.active = 0;
      if (inside) { v = !v; u.clicks++; BDBG("ui: \"%s\" onay kutusu -> %s (%u. etkinlestirme)", label ? label : "", v ? "acik" : "kapali", u.clicks); }
    }
  }
  const float bs = (float)(h > 16 ? h - 12 : h * 0.6);
  const int64_t frame_c = en ? ui_shade(u.c_panel, 3.2) : ui_shade(u.c_panel, 1.8);
  teng_rect(x, y, w, h, ui_shade(u.c_panel, inside && en ? 2.0f : 1.5f));
  teng_rect(x + 8, y + (h - bs) * 0.5, bs, bs, frame_c);
  if (v) teng_rect(x + 12, y + (h - bs) * 0.5 + 4, bs - 8, bs - 8, en ? u.c_accent : ui_shade(u.c_accent, 0.5));
  ui_text_box(label, x + bs + 14, y, w - bs - 22, h, en ? u.c_text : ui_shade(u.c_text, 0.55), 1, ui_scale(h));
  return v ? 1 : 0;
}
double teng_ui_slider(const char *label, double x, double y, double w, double h, double value, double min_v, double max_v) {
  CALLF("teng_ui_slider", "\"%s\" (%.0f %.0f %.0f %.0f) %.3f [%.3f %.3f]", label ? label : "", x, y, w, h, value, min_v, max_v);
  if (!ui_frame("teng_ui_slider")) return value;
  Ui &u = g->ui;
  if (max_v <= min_v) { BERR("teng_ui_slider: \"%s\" araligi gecersiz (en az %.3f >= en cok %.3f)", label ? label : "", min_v, max_v); return value; }
  if (w <= 24 || h <= 0) { BERR("teng_ui_slider: \"%s\" olculeri cok kucuk (%.1fx%.1f)", label ? label : "", w, h); return value; }
  const uint32_t id = ui_use(label, 3, "teng_ui_slider");
  const bool en = u.enabled, inside = ui_hit(x, y, w, h);
  double v = value < min_v ? min_v : value > max_v ? max_v : value;
  const float pad = 12.0f, tx0 = (float)x + pad, tw = (float)w - 2 * pad;
  if (en) {
    if (inside) u.hot = id;
    if (inside && u.pressed) u.active = id;
    if (u.active == id) {
      float t = (u.px - tx0) / (tw > 1.0f ? tw : 1.0f);
      if (t < 0) t = 0;
      if (t > 1) t = 1;
      v = min_v + (double)t * (max_v - min_v);
      if (u.released) { u.active = 0; BDBG("ui: \"%s\" kaydirici %.3f (birakildi)", label ? label : "", v); }
    }
  }
  const float t = (float)((v - min_v) / (max_v - min_v));
  const float ty = (float)(y + h) - 18.0f;
  teng_rect(x, y, w, h, ui_shade(u.c_panel, inside && en ? 2.0f : 1.5f));
  teng_rect(tx0, ty, tw, 7, ui_shade(u.c_panel, 3.0));
  teng_rect(tx0, ty, tw * t, 7, en ? u.c_accent : ui_shade(u.c_accent, 0.5));
  teng_rect(tx0 + tw * t - 6, ty - 6, 12, 19, en ? u.c_text : ui_shade(u.c_text, 0.55));
  char val[32];
  std::snprintf(val, sizeof val, (max_v - min_v) >= 10.0 ? "%.0f" : "%.2f", v);
  const float sc = ui_scale(h * 0.62);
  ui_text_box(label, x, y, w, h * 0.55, en ? u.c_text : ui_shade(u.c_text, 0.55), 1, sc);
  ui_text_box(val, x, y, w, h * 0.55, en ? u.c_accent : ui_shade(u.c_accent, 0.6), 2, sc);
  return v;
}
int teng_ui_active(void) { return g && g->ui.active ? 1 : 0; }
int teng_ui_clicks(void) { return g ? (int)g->ui.clicks : 0; }
void teng_ui_test_click(double x, double y) {
  CALLF("teng_ui_test_click", "(%.0f %.0f)", x, y);
  if (!ready("teng_ui_test_click")) return;
  Ui &u = g->ui;
  if (u.inject) BERR("teng_ui_test_click: onceki enjekte tiklama (%.0f %.0f) henuz tuketilmedi (arada eng_ui_begin yok) — ustune yazildi", u.inj_x, u.inj_y);
  u.inject = true;
  u.inj_x = (float)x;
  u.inj_y = (float)y;
  BDBG("ui: tiklama kuyruklandi (%.0f %.0f), sonraki eng_ui_begin tuketir", x, y);
}

// --- kalici kayit (anahtar-deger) -------------------------------------------------
int teng_save_open(const char *path) {
  CALLF("teng_save_open", "%s", path ? path : "");
  if (!path || !*path) { BERR("teng_save_open: yol bos"); return 0; }
  std::snprintf(g_save.path, sizeof g_save.path, "%s", path);
  return save_read_file(g_save.path);
}
const char *teng_save_path(void) { return save_path(); }
void teng_save_set(const char *key, double value) {
  CALLF("teng_save_set", "%s = %.6g", key ? key : "", value);
  save_ensure();
  char buf[kSaveValLen];
  std::snprintf(buf, sizeof buf, "%.10g", value);
  if (save_put(key, buf, "teng_save_set")) BDBG("kayit: %s = %s", key, buf);
}
double teng_save_get(const char *key, double def) {
  save_ensure();
  const int32_t i = save_find(key);
  if (i < 0) { BDBG("kayit: \"%s\" yok, varsayilan %.6g", key ? key : "", def); return def; }
  char *end = nullptr;
  const double v = std::strtod(g_save.vals[i], &end);
  if (!end || end == g_save.vals[i] || *end) {
    BERR("teng_save_get: \"%s\" degeri sayisal degil (\"%s\") — varsayilan %.6g dondu", key, g_save.vals[i], def);
    return def;
  }
  return v;
}
void teng_save_set_str(const char *key, const char *value) {
  CALLF("teng_save_set_str", "%s = %s", key ? key : "", value ? value : "");
  save_ensure();
  save_put(key, value, "teng_save_set_str");
}
const char *teng_save_get_str(const char *key, const char *def) {
  save_ensure();
  const int32_t i = save_find(key);
  if (i < 0) { BDBG("kayit: \"%s\" yok, varsayilan \"%s\"", key ? key : "", def ? def : ""); return def ? def : ""; }
  return g_save.vals[i];
}
int teng_save_has(const char *key) {
  save_ensure();
  return save_find(key) >= 0 ? 1 : 0;
}
int teng_save_count(void) {
  save_ensure();
  return (int)g_save.n;
}
void teng_save_clear(void) {
  CALL("teng_save_clear");
  save_ensure();
  BINFO("kayit temizlendi (%u anahtar dusuruldu; diske yazmak icin eng_save_write)", g_save.n);
  g_save.n = 0;
  g_save.dirty = true;
}
int teng_save_write(void) {
  CALL("teng_save_write");
  save_ensure();
  char tmp[sizeof g_save.path + 8];
  std::snprintf(tmp, sizeof tmp, "%s.tmp", g_save.path);
  std::FILE *f = std::fopen(tmp, "wb");
  if (!f) { BERR("kayit yazilamadi (acilamadi): %s", tmp); return 0; }
  std::fprintf(f, "# tulpar engine kayit v1\n");
  for (uint32_t i = 0; i < g_save.n; i++) std::fprintf(f, "%s=%s\n", g_save.keys[i], g_save.vals[i]);
  if (std::fclose(f) != 0) { BERR("kayit yazilamadi (kapanis): %s", tmp); return 0; }
  if (std::rename(tmp, g_save.path) != 0) { BERR("kayit yerine konamadi: %s -> %s", tmp, g_save.path); return 0; }
  g_save.dirty = false;
  g_save.writes++;
  BINFO("kayit yazildi: %s (%u anahtar, %u. yazma)", g_save.path, g_save.n, g_save.writes);
  return 1;
}

// --- sahne sicak yeniden yukleme ---------------------------------------------------
// Dosyanin (mtime, boyut) damgasi izlenir. Degisim GORULUNCE hemen yuklenmez:
// bir kontrol daha beklenir ve damga ayni cikarsa yuklenir — editor/derleyici
// dosyayi yazarken yakalanan YARIM blob'u yuklememek icin.
static bool file_stamp_pub(const char *path, int64_t *mtime, int64_t *size) {
  struct stat st;
  if (!path || !*path || stat(path, &st) != 0) return false;
#if defined(__APPLE__)
  *mtime = (int64_t)st.st_mtimespec.tv_sec * 1000000000ll + st.st_mtimespec.tv_nsec;
#else
  *mtime = (int64_t)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec;
#endif
  *size = (int64_t)st.st_size;
  return true;
}
static void scene_watch_tick(void) {
  Bridge &b = *g;
  if (!b.scene_watch || !b.watch_path[0]) return;
  if (b.frame - b.watch_last_frame < kWatchInterval) return;
  b.watch_last_frame = b.frame;
  int64_t mt = 0, sz = 0;
  if (!file_stamp_pub(b.watch_path, &mt, &sz)) { // dosya silindi ya da tasindi: yukleme yok, sessiz de degil
    if (!b.watch_pending) { b.watch_pending = true; BERR("sahne izleme: dosya okunamiyor (%s) — yeniden yukleme yok", b.watch_path); }
    return;
  }
  if (mt == b.watch_mtime && sz == b.watch_size) { b.watch_pending = false; return; }
  if (!b.watch_pending || mt != b.pend_mtime || sz != b.pend_size) {
    b.watch_pending = true;
    b.pend_mtime = mt;
    b.pend_size = sz;
    BINFO("sahne dosyasi degisti: %s (%lld bayt) — yazim bitisi bekleniyor", b.watch_path, (long long)sz);
    return;
  }
  char path[sizeof b.watch_path];
  std::snprintf(path, sizeof path, "%s", b.watch_path);
  b.watch_pending = false;
  b.reloading = true;
  if (b.scene_ok) teng_scene_unload();
  const bool ok = teng_scene_load(path) != 0;
  b.reloading = false;
  if (ok) {
    b.scene_reloads++;
    b.scene_reloaded = true;
    BINFO("SICAK YUKLEME: sahne yenilendi (%s), %u. kez — betik eng_scene_reloaded() ile haberdar olur", path, b.scene_reloads);
  } else {
    // Yukleme basarisiz: izleme SURER (dosya duzeltilince yeniden denenir).
    std::snprintf(b.watch_path, sizeof b.watch_path, "%s", path);
    b.watch_mtime = mt;
    b.watch_size = sz;
    BERR("SICAK YUKLEME BASARISIZ: %s (%s) — sahne bos kaldi, izleme suruyor", path, b.err);
  }
}
void teng_scene_watch(int on) {
  CALLF("teng_scene_watch", "%d", on);
  if (!ready("teng_scene_watch")) return;
  Bridge &b = *g;
  b.scene_watch = on != 0;
  b.watch_pending = false;
  b.watch_last_frame = b.frame;
  BINFO("sahne izleme %s (%s; her %u karede bir damga kontrolu)", b.scene_watch ? "ACIK" : "kapali",
        b.watch_path[0] ? b.watch_path : "izlenen dosya yok", kWatchInterval);
}
int teng_scene_reloaded(void) {
  if (!g) return 0;
  const bool r = g->scene_reloaded;
  g->scene_reloaded = false; // tuketilir: BIR kez true
  return r ? 1 : 0;
}
int teng_scene_reload_count(void) { return g ? (int)g->scene_reloads : 0; }
const char *teng_scene_path(void) { return g ? g->watch_path : ""; }

int teng_file_copy(const char *src, const char *dst) {
  CALLF("teng_file_copy", "%s -> %s", src ? src : "", dst ? dst : "");
  if (!src || !dst || !*src || !*dst) { BERR("teng_file_copy: kaynak ya da hedef yolu bos"); return 0; }
  std::FILE *in = std::fopen(src, "rb");
  if (!in) { BERR("teng_file_copy: kaynak acilamadi: %s", src); return 0; }
  std::FILE *out = std::fopen(dst, "wb");
  if (!out) { BERR("teng_file_copy: hedef acilamadi: %s", dst); std::fclose(in); return 0; }
  static unsigned char buf[64 << 10]; // sabit tampon: ayirma yok
  size_t total = 0, n = 0;
  bool ok = true;
  while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) {
    if (std::fwrite(buf, 1, n, out) != n) { BERR("teng_file_copy: yazma hatasi (%s)", dst); ok = false; break; }
    total += n;
  }
  std::fclose(in);
  if (std::fclose(out) != 0) { BERR("teng_file_copy: hedef kapatilamadi: %s", dst); ok = false; }
  if (ok) BINFO("dosya kopyalandi: %s -> %s (%zu bayt)", src, dst, total);
  return ok ? 1 : 0;
}
double teng_file_mtime(const char *path) {
  int64_t mt = 0, sz = 0;
  if (!file_stamp_pub(path, &mt, &sz)) { BDBG("teng_file_mtime: dosya yok (%s)", path ? path : ""); return 0.0; }
  return (double)mt / 1e9;
}

// --- ses --------------------------------------------------------------------
// Cihaz betigin istegiyle acilir. ACILAMAZSA oyun durmaz: hata loglanir, butun
// ses cagrilari yok sayilir (audio_live) ve kapanis raporu sessiz gectigini
// soyler. Klipler yukleme aninda arenada cozulur; cihaz thread'i o ornekleri
// okudugu icin teng_shutdown once sesi kapatir.
int teng_audio_open(int sample_rate, int channels) {
  CALLF("teng_audio_open", "%d Hz %d kanal", sample_rate, channels);
  if (!ready("teng_audio_open")) return 0;
  Bridge &b = *g;
  if (b.audio_ok) { BERR("teng_audio_open: ses zaten acik (%s)", b.audio_desc); return 0; }
  audio::DeviceConfig cfg;
  if (sample_rate > 0) cfg.sample_rate = (uint32_t)sample_rate;
  if (channels > 0) cfg.channels = (uint32_t)(channels > 2 ? 2 : channels);
  cfg.null_backend = std::getenv("TULPAR_ENGINE_AUDIO_NULL") != nullptr;
  if (!b.audio_dev.init(b.mixer, cfg)) {
    BERR("ses cihazi acilamadi (%s) — oyun SESSIZ devam eder", b.audio_dev.last_error());
    std::snprintf(b.err, sizeof b.err, "%s", b.audio_dev.last_error());
    return 0;
  }
  const audio::DeviceInfo &di = b.audio_dev.info();
  std::snprintf(b.audio_desc, sizeof b.audio_desc, "%s '%s' %u Hz %u kanal periyot %u", di.backend, di.name, di.sample_rate, di.channels, di.period_frames);
  b.audio_ok = true;
  b.audio_null = cfg.null_backend;
  b.audio_master = 1.0f;
  for (uint32_t i = 0; i < kMaxVoices; i++) b.voices[i] = Bridge::VoiceSlot{};
  BINFO("ses acildi: %s%s", b.audio_desc, cfg.null_backend ? " (NULL arka uc: TULPAR_ENGINE_AUDIO_NULL, hoparlore gitmez)" : "");
  return 1;
}
void teng_audio_close(void) {
  CALL("teng_audio_close");
  if (!g || !g->audio_ok) { BDBG("teng_audio_close: ses zaten kapali"); return; }
  Bridge &b = *g;
  b.mixer.stop_all();
  b.audio_dev.shutdown();
  b.audio_ok = false;
  for (uint32_t i = 0; i < kMaxVoices; i++) b.voices[i] = Bridge::VoiceSlot{};
  BINFO("ses kapatildi (%u cal cagrisi, %u klip)", b.audio_plays, b.clip_count);
  b.audio_desc[0] = 0;
}
int teng_audio_ok(void) { return g && g->audio_ok ? 1 : 0; }
const char *teng_audio_backend(void) { return g && g->audio_ok ? g->audio_desc : ""; }

int teng_audio_load(const char *path) {
  CALLF("teng_audio_load", "%s", path ? path : "");
  if (!audio_live("teng_audio_load") || !path) return -1;
  Bridge &b = *g;
  // Ayni dosya iki kez yuklenmez: arena buyumesin, tutamac kararli kalsin.
  for (uint32_t i = 0; i < b.clip_count; i++)
    if (!b.clips[i].tone && std::strcmp(b.clips[i].name, path) == 0) { BDBG("ses klibi %u yeniden kullanildi: %s", i, path); return (int)i; }
  if (b.clip_count >= kMaxClips) { BERR("teng_audio_load: klip kapasitesi dolu (%u): %s", kMaxClips, path); return -1; }
  const uint32_t i = b.clip_count;
  char cerr[160] = {0};
  if (!audio::clip_load(b.sys, path, b.mixer.rate(), b.mixer.channels(), &b.clips[i].clip, cerr, sizeof cerr)) {
    BERR("ses klibi yuklenemedi %s: %s", path, cerr);
    return -1;
  }
  b.clips[i].tone = false;
  std::snprintf(b.clips[i].name, sizeof b.clips[i].name, "%s", path);
  b.clip_count++;
  const audio::Clip &c = b.clips[i].clip;
  BINFO("ses klibi %u: %s (%u kare, %u kanal, %u Hz, %.2f s)", i, path, c.frames, c.channels, c.rate, (double)c.frames / (double)(c.rate ? c.rate : 1));
  return (int)i;
}
int teng_audio_tone(double hz, double seconds) {
  CALLF("teng_audio_tone", "%.1f Hz %.2f s", hz, seconds);
  if (!audio_live("teng_audio_tone")) return -1;
  Bridge &b = *g;
  if (hz <= 0.0 || seconds <= 0.0) { BERR("teng_audio_tone: frekans ve sure pozitif olmali (%.2f Hz, %.2f s)", hz, seconds); return -1; }
  if (seconds > 10.0) { BERR("teng_audio_tone: sure %.2f s cok uzun (en fazla 10 s), kirpildi", seconds); seconds = 10.0; }
  // Ayni (frekans, sure) ciftinde ayni tutamac: her bip'te arena yemesin.
  for (uint32_t i = 0; i < b.clip_count; i++)
    if (b.clips[i].tone && std::fabs(b.clips[i].hz - hz) < 0.01 && std::fabs(b.clips[i].secs - seconds) < 0.001) return (int)i;
  if (b.clip_count >= kMaxClips) { BERR("teng_audio_tone: klip kapasitesi dolu (%u)", kMaxClips); return -1; }
  const uint32_t i = b.clip_count;
  if (!audio::clip_sine(b.sys, (float)hz, (float)seconds, b.mixer.rate(), 0.5f, &b.clips[i].clip)) { BERR("teng_audio_tone: klip ayrilamadi (arena dolu)"); return -1; }
  b.clips[i].tone = true;
  b.clips[i].hz = (float)hz;
  b.clips[i].secs = (float)seconds;
  std::snprintf(b.clips[i].name, sizeof b.clips[i].name, "ton %.0f Hz", hz);
  b.clip_count++;
  BDBG("ton klibi %u: %.1f Hz %.2f s (%u kare)", i, hz, seconds, b.clips[i].clip.frames);
  return (int)i;
}
int teng_audio_play(int clip, double gain, int loop) {
  CALLF("teng_audio_play", "klip %d gain %.2f %s", clip, gain, loop ? "dongu" : "tek");
  if (!audio_live("teng_audio_play")) return 0;
  Bridge &b = *g;
  if (clip < 0 || (uint32_t)clip >= b.clip_count) { BERR("teng_audio_play: klip %d yok (%u yuklu)", clip, b.clip_count); return 0; }
  if (gain < 0.0) { BERR("teng_audio_play: ses seviyesi negatif (%.2f), 0 yapildi", gain); gain = 0.0; }
  const audio::VoiceHandle v = b.mixer.play(&b.clips[clip].clip, (float)gain * b.audio_master, loop != 0);
  if (!v.valid()) { BERR("teng_audio_play: komut kuyrugu dolu (klip %d) — ses calmadi", clip); return 0; }
  b.voices[v.id & 0xFFu] = Bridge::VoiceSlot{v.id, (float)gain};
  b.audio_plays++;
  BDBG("ses #%u: klip %d (%s) gain %.2f%s", v.id, clip, b.clips[clip].name, gain, loop ? " dongulu" : "");
  return (int)v.id;
}
int teng_audio_beep(double hz, double seconds, double gain) {
  CALLF("teng_audio_beep", "%.1f Hz %.2f s gain %.2f", hz, seconds, gain);
  const int c = teng_audio_tone(hz, seconds);
  if (c < 0) return 0;
  return teng_audio_play(c, gain, 0);
}
void teng_audio_stop(int voice) {
  CALLF("teng_audio_stop", "#%d", voice);
  if (!audio_live("teng_audio_stop")) return;
  if (voice <= 0) { BERR("teng_audio_stop: gecersiz ses id %d", voice); return; }
  Bridge &b = *g;
  const audio::VoiceHandle h{(uint32_t)voice};
  b.mixer.stop(h);
  const uint32_t slot = (uint32_t)voice & 0xFFu;
  if (slot < kMaxVoices && b.voices[slot].id == (uint32_t)voice) b.voices[slot] = Bridge::VoiceSlot{};
  BDBG("ses #%d durduruldu", voice);
}
void teng_audio_stop_all(void) {
  CALL("teng_audio_stop_all");
  if (!audio_live("teng_audio_stop_all")) return;
  Bridge &b = *g;
  b.mixer.stop_all();
  for (uint32_t i = 0; i < kMaxVoices; i++) b.voices[i] = Bridge::VoiceSlot{};
  BDBG("butun sesler durduruldu");
}
void teng_audio_master(double gain) {
  CALLF("teng_audio_master", "%.2f", gain);
  if (!audio_live("teng_audio_master")) return;
  Bridge &b = *g;
  if (gain < 0.0) { BERR("teng_audio_master: seviye negatif (%.2f), 0 yapildi", gain); gain = 0.0; }
  if (gain > 4.0) { BERR("teng_audio_master: seviye %.2f cok yuksek (kirpma), 4 yapildi", gain); gain = 4.0; }
  b.audio_master = (float)gain;
  uint32_t n = 0;
  for (uint32_t i = 0; i < kMaxVoices; i++) {
    if (!b.voices[i].id) continue;
    b.mixer.set_gain(audio::VoiceHandle{b.voices[i].id}, b.voices[i].gain * b.audio_master);
    n++;
  }
  BDBG("ana ses seviyesi %.2f (%u calan ses guncellendi)", gain, n);
}
int teng_audio_playing(void) { return g && g->audio_ok ? (int)g->mixer.stats().voices_active : 0; }
double teng_audio_peak(void) { return g && g->audio_ok ? (double)g->mixer.stats().peak : 0.0; }

// --- sorgular: isin testi ve yakinlik -------------------------------------------
// Neden kopruye girdi: oyun mantigi Tulpar'da yaziliyor ve savas/yapay zeka icin
// "onumde ne var" ile "yakinimda kim var" sorularinin cevabi lazim. Callback FFI
// olmadigi icin carpisma OLAYI veremiyoruz; bu iki SORGU onun yerini tutuyor
// (dusman gorus hatti, kilic menzili, zemin kontrolu).

// Jolt govde id'sinden kopru varlik id'si (0 = kopru varligi degil).
static int ent_id_of_body(sim::BodyId b) {
  if (!g || !b.valid()) return 0;
  for (uint32_t i = 0; i < g->ent_high; i++) {
    const Ent &e = g->ents[i];
    if (e.alive && e.body.valid() && e.body.v == b.v) return make_id(i);
  }
  return 0;
}
// Jolt govde id'sinden sahne varlik dizini (-1 = sahne govdesi degil).
static int scene_idx_of_body(sim::BodyId b) {
  if (!g || !g->scene_ok || !b.valid()) return -1;
  const uint32_t n = g->srt.view().h->entity_count;
  for (uint32_t i = 0; i < n; i++) {
    const sim::BodyId sb = g->srt.entity_body(i);
    if (sb.valid() && sb.v == b.v) return (int)i;
  }
  return -1;
}

// Varligin sinir yaricapi (kure sorgusu icin kaba kusatma).
static float ent_bound_radius(const Ent &e) {
  switch (e.kind) {
  case Kind::Sphere: return e.radius * e.scale;
  case Kind::Box: return length(e.half) * e.scale;
  case Kind::Model: return 0.5f * e.scale; // govdesiz: nominal kutu olcusu
  default: return 0.0f;
  }
}
double teng_raycast(double ox, double oy, double oz, double dx, double dy, double dz, double max_dist, int skip_id) {
  CALLF("teng_raycast", "(%.2f %.2f %.2f) yon (%.2f %.2f %.2f) mesafe %.2f atla #%d", ox, oy, oz, dx, dy, dz, max_dist, skip_id);
  if (!ready("teng_raycast")) return -1.0;
  Bridge &b = *g;
  b.ray_dist = -1.0f;
  b.ray_point = Vec3{0, 0, 0};
  b.ray_normal = Vec3{0, 0, 0};
  b.ray_id = 0;
  b.ray_scene = -1;
  if (max_dist <= 0.0) { BERR("teng_raycast: mesafe pozitif olmali (%.2f)", max_dist); return -1.0; }
  Vec3 dir{(float)dx, (float)dy, (float)dz};
  const float dl = length(dir);
  if (dl <= 1e-6f) { BERR("teng_raycast: yon vektoru sifir uzunlukta"); return -1.0; }
  dir = dir * (1.0f / dl);
  // skip_id: Jolt sorgusunda filtre yok, bu yuzden atlanacak govdeye carpinca
  // carpma noktasinin bir tik otesinden yeniden atariz.
  sim::BodyId skip{};
  float skip_span = 0.02f;
  if (skip_id != 0) {
    const int32_t s = slot_of(skip_id, "teng_raycast");
    if (s >= 0) { skip = b.ents[s].body; skip_span = 2.0f * ent_bound_radius(b.ents[s]) + 0.02f; }
  }
  Vec3 org{(float)ox, (float)oy, (float)oz};
  float remaining = (float)max_dist, traveled = 0.0f;
  for (uint32_t attempt = 0; attempt < kRayRetries && remaining > 0.0f; attempt++) {
    sim::RayHit h{};
    if (!b.phys.raycast(org, dir, remaining, &h)) break;
    if (skip.valid() && h.body.v == skip.v) {
      // Isin atlanacak govdenin ICINDEN basliyorsa Jolt mesafe 0 verir; 0.01
      // ilerlemek her denemede ayni govdeyi bulur, bu yuzden govdeyi kusatan
      // kureyi TAMAMEN geceriz (yaklasiklik: o kurenin icindeki baska bir
      // carpma atlanir; menzilli savas icin kabul edilebilir).
      const float adv = h.distance > 1e-4f ? h.distance + 0.01f : skip_span;
      org = org + dir * adv;
      traveled += adv;
      remaining -= adv;
      continue;
    }
    b.ray_dist = traveled + h.distance;
    b.ray_point = h.point;
    b.ray_normal = h.normal;
    b.ray_id = ent_id_of_body(h.body);
    b.ray_scene = scene_idx_of_body(h.body);
    BTRACE("isin carpti: mesafe %.3f nokta (%.2f %.2f %.2f) varlik #%d sahne %d", b.ray_dist, b.ray_point.x, b.ray_point.y, b.ray_point.z,
           b.ray_id, b.ray_scene);
    return b.ray_dist;
  }
  BTRACE("isin iskaladi (mesafe %.2f)", max_dist);
  return -1.0;
}
double teng_ray_x(void) { return g ? g->ray_point.x : 0.0; }
double teng_ray_y(void) { return g ? g->ray_point.y : 0.0; }
double teng_ray_z(void) { return g ? g->ray_point.z : 0.0; }
double teng_ray_nx(void) { return g ? g->ray_normal.x : 0.0; }
double teng_ray_ny(void) { return g ? g->ray_normal.y : 0.0; }
double teng_ray_nz(void) { return g ? g->ray_normal.z : 0.0; }
int teng_ray_id(void) { return g ? g->ray_id : 0; }
int teng_ray_scene(void) { return g ? g->ray_scene : -1; }

// Kure sorgusunun govdesi (teng_overlap ve teng_nearest ayni kodu kullanir ki
// "en yakin" ile "listenin ilki" ayrisamasin).
static uint32_t overlap_query(const char *fn, double x, double y, double z, double radius, int skip_id) {
  Bridge &b = *g;
  b.ovl_n = 0;
  if (radius <= 0.0) { BERR("%s: yaricap pozitif olmali (%.2f)", fn, radius); return 0; }
  int32_t skip_slot = -1;
  if (skip_id != 0) skip_slot = slot_of(skip_id, fn);
  const Vec3 c{(float)x, (float)y, (float)z};
  bool full = false;
  for (uint32_t i = 0; i < b.ent_high; i++) {
    const Ent &e = b.ents[i];
    if (!e.alive || (int32_t)i == skip_slot) continue;
    // Zemin her sorguyu doldururdu (tek dev govde), isigin govdesi yok.
    if (e.kind == Kind::Ground || e.kind == Kind::Light) continue;
    const float d = length(ent_pos(e) - c);
    if (d > (float)radius + ent_bound_radius(e)) continue;
    if (b.ovl_n >= kMaxOverlap) { full = true; break; }
    uint32_t k = b.ovl_n++;
    while (k > 0 && b.ovl[k - 1].dist > d) { b.ovl[k] = b.ovl[k - 1]; k--; } // yakindan uzaga (n <= 64)
    b.ovl[k].id = make_id(i);
    b.ovl[k].dist = d;
  }
  if (full) BDBG("%s: sonuc tavani %u doldu, kalan varliklar atlandi", fn, kMaxOverlap);
  BTRACE("%s: (%.2f %.2f %.2f) r%.2f -> %u sonuc", fn, x, y, z, radius, b.ovl_n);
  return b.ovl_n;
}
int teng_overlap(double x, double y, double z, double radius, int skip_id) {
  CALLF("teng_overlap", "(%.2f %.2f %.2f) r%.2f atla #%d", x, y, z, radius, skip_id);
  if (!ready("teng_overlap")) return 0;
  return (int)overlap_query("teng_overlap", x, y, z, radius, skip_id);
}
int teng_overlap_id(int i) {
  if (!g) return 0;
  if (i < 0 || (uint32_t)i >= g->ovl_n) { BERR("teng_overlap_id: dizin %d sinir disi (%u sonuc)", i, g->ovl_n); return 0; }
  return g->ovl[i].id;
}
double teng_overlap_dist(int i) {
  if (!g) return 0.0;
  if (i < 0 || (uint32_t)i >= g->ovl_n) { BERR("teng_overlap_dist: dizin %d sinir disi (%u sonuc)", i, g->ovl_n); return 0.0; }
  return g->ovl[i].dist;
}
int teng_nearest(double x, double y, double z, double radius, int skip_id) {
  CALLF("teng_nearest", "(%.2f %.2f %.2f) r%.2f atla #%d", x, y, z, radius, skip_id);
  if (!ready("teng_nearest")) return 0;
  return overlap_query("teng_nearest", x, y, z, radius, skip_id) ? g->ovl[0].id : 0;
}

// --- navmesh (sahne blob'undaki bake; runtime yalniz sorgular) -------------------
int teng_nav_ok(void) { return g && g->nav_ok ? 1 : 0; }
int teng_nav_polys(void) { return g && g->nav_ok ? (int)g->nav.polys() : 0; }
// --- Carpisma olaylari -------------------------------------------------------
// Kuyruk okumasi: gecersiz indis SESSIZCE 0 donmez, hata loglar. Bir oyun
// donguyu yanlis sinirlarsa bunu gormeli; sessiz 0 "carpma yok" gibi okunur.
static const sim::ContactEvent *contact_at(const char *who, int i) {
  if (!ready(who)) return nullptr;
  const uint32_t n = g->phys.contact_count();
  if (i < 0 || (uint32_t)i >= n) {
    BERR("%s: carpisma dizini %d sinir disi (%u olay)", who, i, n);
    return nullptr;
  }
  static sim::ContactEvent tmp;
  tmp = g->phys.contact((uint32_t)i);
  return &tmp;
}
int teng_collision_count(void) {
  if (!ready("teng_collision_count")) return 0;
  const uint32_t n = g->phys.contact_count();
  const uint32_t d = g->phys.contact_overflow();
  // Tasma BIR KEZ degil, oldugu her karede loglanir: sessiz kirpilma bu
  // koprude en pahali hata sinifi olurdu (oyun "carpma gelmedi" sanir).
  if (d && g->frame != g->collision_warn_frame) {
    g->collision_warn_frame = g->frame;
    BERR("teng_collision_count: %u carpisma olayi DUSTU (halka %u yuva) — kapasiteyi buyut ya da daha erken tuket", d, n);
  }
  return (int)n;
}
int teng_collision_dropped(void) { return g ? (int)g->phys.contact_overflow() : 0; }
int teng_collision_a(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_a", i);
  return e ? ent_id_of_body(e->a) : 0;
}
int teng_collision_b(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_b", i);
  return e ? ent_id_of_body(e->b) : 0;
}
int teng_collision_scene_a(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_scene_a", i);
  return e ? scene_idx_of_body(e->a) : -1;
}
int teng_collision_scene_b(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_scene_b", i);
  return e ? scene_idx_of_body(e->b) : -1;
}
double teng_collision_x(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_x", i);
  return e ? e->point.x : 0.0;
}
double teng_collision_y(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_y", i);
  return e ? e->point.y : 0.0;
}
double teng_collision_z(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_z", i);
  return e ? e->point.z : 0.0;
}
double teng_collision_nx(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_nx", i);
  return e ? e->normal.x : 0.0;
}
double teng_collision_ny(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_ny", i);
  return e ? e->normal.y : 0.0;
}
double teng_collision_nz(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_nz", i);
  return e ? e->normal.z : 0.0;
}
double teng_collision_speed(int i) {
  const sim::ContactEvent *e = contact_at("teng_collision_speed", i);
  return e ? e->speed : 0.0;
}

int teng_nav_partial(void) { return g && g->nav_partial ? 1 : 0; }
int teng_nav_path(double fx, double fy, double fz, double tx, double ty, double tz) {
  CALLF("teng_nav_path", "(%.2f %.2f %.2f) -> (%.2f %.2f %.2f)", fx, fy, fz, tx, ty, tz);
  if (!ready("teng_nav_path")) return 0;
  Bridge &b = *g;
  b.nav_n = 0;
  b.nav_partial = false;
  if (!b.nav_ok) { BERR("teng_nav_path: yuklu sahnede navmesh yok (engine_sahnec bake etmedi)"); return 0; }
  bool partial = false;
  const int n = b.nav.find_path(Vec3{(float)fx, (float)fy, (float)fz}, Vec3{(float)tx, (float)ty, (float)tz}, b.nav_pts, (int)kMaxNavPoints,
                                &partial);
  b.nav_n = n > 0 ? (uint32_t)n : 0;
  b.nav_partial = partial;
  BTRACE("navmesh yolu: %u nokta%s", b.nav_n, partial ? " (kismi)" : "");
  return (int)b.nav_n;
}
static bool nav_idx_ok(int i, const char *fn) {
  if (!g) return false;
  if (i < 0 || (uint32_t)i >= g->nav_n) { BERR("%s: dizin %d sinir disi (%u nokta)", fn, i, g ? g->nav_n : 0u); return false; }
  return true;
}
double teng_nav_x(int i) { return nav_idx_ok(i, "teng_nav_x") ? g->nav_pts[i].x : 0.0; }
double teng_nav_y(int i) { return nav_idx_ok(i, "teng_nav_y") ? g->nav_pts[i].y : 0.0; }
double teng_nav_z(int i) { return nav_idx_ok(i, "teng_nav_z") ? g->nav_pts[i].z : 0.0; }

int teng_nav_nearest(double x, double y, double z) {
  CALLF("teng_nav_nearest", "(%.2f %.2f %.2f)", x, y, z);
  if (!ready("teng_nav_nearest")) return 0;
  Bridge &b = *g;
  b.nav_near_ok = false;
  b.nav_near = Vec3{(float)x, (float)y, (float)z};
  if (!b.nav_ok) { BERR("teng_nav_nearest: yuklu sahnede navmesh yok (engine_sahnec bake etmedi)"); return 0; }
  Vec3 out{};
  if (!b.nav.nearest_point(Vec3{(float)x, (float)y, (float)z}, &out)) {
    // Sessiz 0 degil: cagiran "nokta mesh'in cok uzaginda" ile "navmesh yok"u
    // ayirt edebilmeli, ikisi de 0 donuyor.
    BTRACE("navmesh en yakin nokta: arama kutusunda poligon yok");
    return 0;
  }
  b.nav_near = out;
  b.nav_near_ok = true;
  BTRACE("navmesh en yakin nokta: (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)", x, y, z, (double)out.x, (double)out.y, (double)out.z);
  return 1;
}
double teng_nav_near_x(void) { return g ? (double)g->nav_near.x : 0.0; }
double teng_nav_near_y(void) { return g ? (double)g->nav_near.y : 0.0; }
double teng_nav_near_z(void) { return g ? (double)g->nav_near.z : 0.0; }

int teng_nav_raycast(double fx, double fy, double fz, double tx, double ty, double tz) {
  CALLF("teng_nav_raycast", "(%.2f %.2f %.2f) -> (%.2f %.2f %.2f)", fx, fy, fz, tx, ty, tz);
  if (!ready("teng_nav_raycast")) return 0;
  Bridge &b = *g;
  b.nav_ray_t = 1.0f;
  if (!b.nav_ok) { BERR("teng_nav_raycast: yuklu sahnede navmesh yok (engine_sahnec bake etmedi)"); return 0; }
  float t = 1.0f;
  const bool blocked = b.nav.raycast(Vec3{(float)fx, (float)fy, (float)fz}, Vec3{(float)tx, (float)ty, (float)tz}, &t);
  b.nav_ray_t = t;
  BTRACE("navmesh gorus: %s (t=%.3f)", blocked ? "ENGEL" : "temiz", (double)t);
  return blocked ? 1 : 0;
}
double teng_nav_ray_t(void) { return g ? (double)g->nav_ray_t : 1.0; }

// --- olcum ----------------------------------------------------------------------
int teng_draw_count(void) { return g ? (int)g->ren.stats().draws : 0; }
int teng_body_count(void) { return g && g->inited ? (int)g->phys.stats().bodies : 0; }
int teng_light_count(void) { return g ? (int)g->last_lights : 0; }
double teng_frame_ms(void) {
  if (!g || !g->inited) return 0;
  static uint64_t scratch[1200]; // profiler sozlesmesi: kare kapasitesinin 2 KATI (ilk yari ornek, ikinci yari siralama)
  const FrameStats st = g->prof.frame_stats(Span<uint64_t>(scratch, 1200), 120);
  return st.p50_ns / 1e6;
}

} // extern "C"
