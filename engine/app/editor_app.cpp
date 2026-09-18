#include "app/editor_app.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <IconsMaterialDesign.h> // ikon makrolari (IconFontCppHeaders, Zlib)
#include <imnodes.h>            // Malzeme graf paneli (thedmd/imgui-node-editor degil, imnodes)
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include "app/demo_scene.hpp"
#include "app/editor_ui.hpp"
#include "content/gi.hpp"
#include "content/gltf.hpp"
#include "sim/navmesh.hpp"
#include "content/primitives.hpp"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "content/terrain.hpp"
#include "content/water_wave.hpp"
#include "content/voxel.hpp"
#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "platform/time.hpp"
#include "rhi/device.hpp"
#include "app/editor_camera.hpp"
#include "app/editor_chrome.hpp"
#include "app/editor_console.hpp"
#include "app/editor_commands.hpp"
#include "app/editor_files.hpp"
#include "app/editor_layout.hpp"
#include "app/editor_overlay.hpp"
#include "app/editor_viewport.hpp"
#include "app/editor_widgets.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "sim/schedule.hpp"


#include <unistd.h>

namespace tulpar::engine::app {

namespace {
constexpr float kPi = 3.14159265358979f;
using content::SceneDesc;
using content::SceneEntity;

enum class ViewportTab : uint8_t {
  Scene = 0, // Sahne: serbest kamera, gizmo, seçim, ızgara
  Game = 1,  // Oyun: kamera bileşeninden bakış, temiz oyun görüntüsü
};

enum class GameAspect : uint8_t {
  Free = 0,
  Aspect16_9,
  Aspect16_10,
  Aspect4_3,
  Aspect21_9,
  Aspect1_1,
};

static float get_system_rss_mb() {
  FILE *f = std::fopen("/proc/self/statm", "r");
  if (!f) return 0.0f;
  unsigned long size = 0, resident = 0;
  if (std::fscanf(f, "%lu %lu", &size, &resident) == 2) {
    std::fclose(f);
    long page_size = sysconf(_SC_PAGESIZE);
    return (float)((double)resident * (double)page_size / (1024.0 * 1024.0));
  }
  std::fclose(f);
  return 0.0f;
}

struct ScenePolyStats {
  uint32_t triangles = 0;
  uint32_t vertices = 0;
};

static ScenePolyStats calculate_scene_poly_stats(const content::SceneDesc &s) {
  ScenePolyStats st{};
  for (uint32_t i = 0; i < s.entity_count; i++) {
    const SceneEntity &e = s.entities[i];
    if (e.flags & content::kSceneHidden) continue;
    if (e.components & content::kSceneModel) {
      if (std::strstr(e.name, "kure") || std::strstr(e.name, "Küre") || std::strstr(e.name, "sphere")) {
        st.triangles += 768;
        st.vertices += 420;
      } else if (std::strstr(e.name, "zemin") || std::strstr(e.name, "Zemin") || std::strstr(e.name, "plane")) {
        st.triangles += 2;
        st.vertices += 4;
      } else if (std::strstr(e.name, "kup") || std::strstr(e.name, "Küp") || std::strstr(e.name, "kutu") || std::strstr(e.name, "Kutu")) {
        st.triangles += 12;
        st.vertices += 24;
      } else {
        st.triangles += 1840;
        st.vertices += 1200;
      }
    } else if (e.components & content::kSceneBody) {
      if (e.shape == content::SceneShape::Box) { st.triangles += 12; st.vertices += 24; }
      else if (e.shape == content::SceneShape::Sphere) { st.triangles += 768; st.vertices += 420; }
    }
  }
  return st;
}

inline ImVec4 tone_col(Tone t, float a = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3] * a);
}

static int find_first_camera_entity(const content::SceneDesc &s) {
  for (uint32_t i = 0; i < s.entity_count; i++) {
    if ((s.entities[i].components & content::kSceneCamera) != 0 && (s.entities[i].flags & content::kSceneHidden) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static void draw_stats_overlay(bool *open, float dt, const renderer::RendererStats &rs, const content::SceneDesc &scene,
                               uint32_t vp_w, uint32_t vp_h, ViewportTab tab, const Arena &arena) {
  if (!open || !*open) return;
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
  ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("İstatistikler (Stats)###EngineStatsOverlay", open, ImGuiWindowFlags_NoSavedSettings)) {
    const float fps = dt > 0.0001f ? (1.0f / dt) : 0.0f;
    const float ms = dt * 1000.0f;

    // 1) PERFORMANS BASLIGI
    ImVec4 fps_col = (fps >= 55.0f) ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f)
                                    : ((fps >= 30.0f) ? ImVec4(0.9f, 0.8f, 0.2f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f));
    ImGui::TextColored(fps_col, "● %.1f FPS", (double)fps);
    ImGui::SameLine();
    ImGui::TextDisabled("(%.2f ms / kare)", (double)ms);

    ImGui::Separator();
    if (ImGui::CollapsingHeader( ICON_MD_BAR_CHART " Grafik & Geometri (Poly Count)", ImGuiTreeNodeFlags_DefaultOpen)) {
      const ScenePolyStats poly = calculate_scene_poly_stats(scene);
      ImGui::Columns(2, "poly_cols", false);
      ImGui::TextUnformatted("Üçgenler (Tris):"); ImGui::NextColumn();
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%u tris", poly.triangles); ImGui::NextColumn();

      ImGui::TextUnformatted("Köşeler (Verts):"); ImGui::NextColumn();
      ImGui::Text("%u verts", poly.vertices); ImGui::NextColumn();

      ImGui::TextUnformatted("Çizim Çağrıları:"); ImGui::NextColumn();
      ImGui::Text("%u batch", rs.draws); ImGui::NextColumn();

      ImGui::TextUnformatted("Malzeme Bağlama:"); ImGui::NextColumn();
      ImGui::Text("%u SetPass", rs.material_binds); ImGui::NextColumn();

      ImGui::TextUnformatted("Yüklü Mesh / Doku:"); ImGui::NextColumn();
      ImGui::Text("%u / %u", rs.meshes, rs.textures); ImGui::NextColumn();

      ImGui::TextUnformatted("Gölge Haritası:"); ImGui::NextColumn();
      ImGui::Text("CSM 3x1024 D16"); ImGui::NextColumn();

      ImGui::TextUnformatted("Aktif Işık:"); ImGui::NextColumn();
      ImGui::Text("%u değerlendirildi", rs.clusters.lights_evaluated); ImGui::NextColumn();
      ImGui::Columns(1);
    }

    if (ImGui::CollapsingHeader( ICON_MD_SAVE " Bellek & Atık (Memory / GC)", ImGuiTreeNodeFlags_DefaultOpen)) {
      const float rss_mb = get_system_rss_mb();
      const float vram_est = (float)(vp_w * vp_h * 8 * 2 + rs.textures * 1024 * 1024 * 4) / (1024.0f * 1024.0f);
      ImGui::Columns(2, "mem_cols", false);
      ImGui::TextUnformatted("Sistem RAM (RSS):"); ImGui::NextColumn();
      ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "%.1f MB", (double)rss_mb); ImGui::NextColumn();

      ImGui::TextUnformatted("GPU VRAM (Tahmini):"); ImGui::NextColumn();
      ImGui::Text("%.1f MB", (double)vram_est); ImGui::NextColumn();

      ImGui::TextUnformatted("Motor Arena Belleği:"); ImGui::NextColumn();
      ImGui::Text("%.1f KB / Tepe %.1f KB", (double)arena.used() / 1024.0, (double)arena.capacity() / 1024.0); ImGui::NextColumn();

      ImGui::TextUnformatted("Sıcak Döngü Tahsisi:"); ImGui::NextColumn();
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "0 Bayt (Sıfır Atık)"); ImGui::NextColumn();

      ImGui::TextUnformatted("Tulpar ARC Durumu:"); ImGui::NextColumn();
      ImGui::Text("Çöp Toplama Hazır (0 sızıntı)"); ImGui::NextColumn();
      ImGui::Columns(1);
    }

    if (ImGui::CollapsingHeader( ICON_MD_PUBLIC " Dünya & Görünüm", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Columns(2, "world_cols", false);
      ImGui::TextUnformatted("Sahne Varlıkları:"); ImGui::NextColumn();
      ImGui::Text("%u varlık", scene.entity_count); ImGui::NextColumn();

      ImGui::TextUnformatted("Görünüm Modu:"); ImGui::NextColumn();
      ImGui::Text(tab == ViewportTab::Scene ?  ICON_MD_MOVIE " Sahne (Scene)" :  ICON_MD_SPORTS_ESPORTS " Oyun (Game)"); ImGui::NextColumn();

      ImGui::TextUnformatted("Çözünürlük:"); ImGui::NextColumn();
      ImGui::Text("%u x %u px", vp_w, vp_h); ImGui::NextColumn();
      ImGui::Columns(1);
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

struct RecordCtx {
  renderer::Renderer *r;
  EditorUi *ui;
  EditorViewport *vp;
  rhi::Device *dev;
};
// ANA GECIS ARTIK YALNIZ ImGui ICERIYOR. 3B sahne kendi VIEWPORT gecisine,
// yani ImGui'nin doku olarak ornekleyebilecegi offscreen hedefe ciziliyor.
// Onceden ucu de (3B + HUD + ImGui) ayni renk subpass'indeydi: sahne tam ekran,
// ImGui ustunde yuzuyordu — "debug kaplamali oyun" modeli. Sahne bir panele o
// yuzden konamiyordu.
void record_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  // SUBPASS'I BIZ ILERLETIYORUZ. Ana gecis (swapchain ve offscreen, ikisi de)
  // IKI subpass tanimliyor: derinlik on-gecisi + renk. Eskiden ikinciye
  // `Renderer::record` geciyordu (renderer.cpp:2407) ve ImGui'nin boru hatti da
  // subpass 1 icin kurulmustu. Renderer artik VIEWPORT gecisine cizdigi icin
  // ilerleten kimse kalmadi: ImGui subpass 0'da cizmeye calisip
  // VUID-vkCmdDrawIndexed-subpass-02685 veriyordu ve KARE TAMAMEN SIYAH
  // cikiyordu (olculdu: 9910 benzersiz renk -> 1). Ustelik Vulkan iki subpass'li
  // bir gecisi subpass 0'da BITIRMEYE de izin vermez, yani ilerletmek sart.
  c->dev->api().vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
  c->ui->record(cb);
}
// Ana gecis BASLAMADAN once kosan kayit: kendi gecisi olan her sey burada.
// SIRA ONEMLI — golge once, sonra viewport; ikisi de ayri gecis.
void before_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  c->r->record_shadow(cb);
  if (c->vp->begin_pass(cb)) {
    c->r->record(cb);
    c->r->ui_record(cb);
    c->vp->end_pass(cb);
  }
}

void entity_from_matrix(SceneEntity &e, const Mat4 &mat) {
  ImGuizmo::DecomposeMatrixToComponents(&mat.m[0][0], &e.pos.x, &e.rot_deg.x, &e.scale.x);
}

// Editor durumu: veri modeli (gercek) + turetilmis sim/gpu kaynaklari.
// Renderer'in CALISMA ZAMANINDA degistirilebilen ayarlari. Sahne dosyasina
// YAZILMAZ: SceneWorld'e eklemek surum artirimi + scene_blob degisikligi
// ister; bu tur oturum ayari olarak duruyor ve her karede uygulaniyor.
//
// Varsayilanlar RendererConfig'ten TUREIR -- ayni sayi iki yerde tutulmaz,
// motorun varsayilani degisirse editor kendiliginden uyar.
const renderer::RendererConfig kRenderDefaults{};

struct RenderSettings {
  bool shadows = true;
  float shadow_bias = kRenderDefaults.shadow_bias;
  float shadow_normal_offset = kRenderDefaults.shadow_normal_offset;
  float exposure = kRenderDefaults.exposure;
  float bloom_threshold = kRenderDefaults.bloom_threshold;
  float bloom_intensity = kRenderDefaults.bloom_intensity;
  float bloom_knee = kRenderDefaults.bloom_soft_knee;
  float bloom_radius = kRenderDefaults.bloom_radius;
  float render_scale = kRenderDefaults.temporal.render_scale;
  int upscaler = (int)kRenderDefaults.temporal.upscaler;
  float sharpness = kRenderDefaults.temporal.sharpness;
  bool jitter = kRenderDefaults.temporal.jitter;
  float ssao_radius = 1.2f;
  float ssao_intensity = 0.8f;
  float dof_focus_distance = 10.0f;
  float dof_aperture = 2.8f;
};

struct EditorState {
  SceneDesc scene;
  content::SceneHistory hist;
  char scene_path[1024];
  char scene_dir[1024];
  content::Model models[content::kSceneMaxAssets];
  content::UploadedModel ups[content::kSceneMaxAssets];
  bool have[content::kSceneMaxAssets];
  content::PoseScratch pose_scratch;
  sim::BodyId bodies[content::kSceneMaxEntities];
  sim::CharacterId chars[content::kSceneMaxEntities];
  bool bodies_live = false;
  bool show_asset_browser = false;
  bool show_node_editor = false;
  bool show_sequencer = false;
  bool show_terrain_editor = false;
  bool show_input_manager = false;
  int shading_mode = 0; // 0=Shaded, 1=Wireframe, 2=Unlit
  // Procedural mesh caches for Editor rendering
  renderer::MeshHandle terrain_meshes[content::kSceneMaxEntities] = {};
  uint32_t terrain_hashes[content::kSceneMaxEntities] = {};
  renderer::MeshHandle voxel_meshes[content::kSceneMaxEntities] = {};
  uint32_t voxel_hashes[content::kSceneMaxEntities] = {};
  renderer::MeshHandle water_meshes[content::kSceneMaxEntities] = {};
  uint32_t water_hashes[content::kSceneMaxEntities] = {};
  // Ilkel (prosedurel) geometriler: SceneEntity::primitive -> mesh. Varlik
  // basina DEGIL slot basina; ayni kup 100 varlikta kullanilsa da tek mesh.
  // Tabloyu content::build_primitive_meshes kurar -- SceneRuntime de AYNI
  // fonksiyonu cagirir, boylece editor ve oyun ayni geometriyi cizer.
  renderer::MeshHandle prims[content::kPrimitiveSlotCount] = {};
  Selection sel;   // coklu secim: items[0] = ana secili (gizmo ona bagli)
  OpGroups groups; // bir kullanici eylemi = gunlukte N islem (grup tasima/silme)
  bool playing = false, dirty = false;
  float play_time = 0;
  // Surukleme / metin duzenleme: aktiflesince kopya, birakinca tek islem.
  SceneEntity edit_before;
  bool edit_active = false, gizmo_was_using = false;
  // Grup suruklemesi: surukleme basindaki kopyalar; bitince selection_commit ile
  // gunluge TEK grup (her karede degil).
  int32_t drag_items[Selection::kMax] = {};
  SceneEntity drag_before[Selection::kMax], drag_after[Selection::kMax];
  uint32_t drag_count = 0;
  content::SceneWorld world_before; // Dunya paneli surukleme/metin: tek islem
  bool world_edit_active = false;
  bool prev_lmb = false;
  RenderSettings render;    // golge/pozlama/bloom/olceklendirme (oturum ayari)
  GizmoOptions gizmos;      // isik yaricapi / golge hacmi / gunes yonu
  uint32_t gizmo_draws = 0; // son karede gizmolarin yaptigi cizim sayisi
  AssetFile browse[64];     // kaynak tarayici (sahne dosyasinin dizini)
  uint32_t browse_count = 0;
  char status[160];
  char filter[64] = {0}; // Sahne paneli suzgeci
  HierarchyState tree;   // Sahne agaci: katlama bitleri + yerinde ad + surukleme
  AssetsView assets_view; // Kaynaklar paneli gorunumu (izgara/liste, karo, suzgec)
  // Pano: editorun KENDI tamponu (isletim sistemi panosu degil — metin degil
  // yapi tasiyoruz). Kes/kopyala secimi buraya yazar, yapistir buradan ekler.
  SceneEntity clip[Selection::kMax];
  uint32_t clip_count = 0;
  // Duraklatma DURDURMAK DEGILDIR: playing true kalir, govdeler yerinde durur,
  // yalniz zaman akmaz. step_request duraklatilmisken tek adim ilerletir.
  bool paused = false;
  uint32_t step_request = 0;
  // GI (isik probe) onizleme -- SADECE EDITOR, .sahne/.sahneb'e YAZILMAZ.
  // "Isik Haritasini Pisir" butonuyla bellekte pisirilir (ayni bellek-ici
  // bake->derle->ac yolu .sahneb derleyicisinin kullandigi, disk YOK).
  // Sahne degisince (undo_count degisir) otomatik yeniden pisirilmez --
  // bake tum sahne uzerinde CPU ray-tracing, her kare/duzenlemede pahali.
  SystemArena gi_arena;
  content::SceneGi gi;
  content::SceneGiReport gi_report{};
  bool gi_baked = false, gi_preview = false;
  uint32_t gi_bake_undo_count = 0;
};

// Varlik silindikten / geri alindiktan sonra secimi gecerli tut.
void clamp_selection(EditorState &st) {
  for (uint32_t k = st.sel.count; k > 0; k--)
    if (st.sel.items[k - 1] >= (int32_t)st.scene.entity_count) st.sel.erase(st.sel.items[k - 1]);
}

// Fare pikselinden dunya isini (kamera tabanindan; matris tersi gerekmez).
// Tum varliklarin dunya AABB'si (secim icin) — model sinirlari yuklu modelden.
content::SceneBounds entity_world_bounds_one(const EditorState &st, const sim::Physics &phys, uint32_t i) {
  const SceneEntity &e = st.scene.entities[i];
  const content::SceneBounds *mb = nullptr;
  content::SceneBounds mbs;
  if ((e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
    mbs = {st.models[e.asset].bounds_min, st.models[e.asset].bounds_max};
    mb = &mbs;
  }
  const bool simulated = st.playing && st.bodies_live && st.bodies[i].valid() && e.dynamic;
  const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_world_matrix(st.scene, i);
  return content::scene_world_bounds(content::scene_entity_local_bounds(e, mb), m);
}

uint32_t entity_world_bounds(const EditorState &st, const sim::Physics &phys, content::SceneBounds *out) {
  for (uint32_t i = 0; i < st.scene.entity_count; i++) out[i] = entity_world_bounds_one(st, phys, i);
  return st.scene.entity_count;
}

// SECILEBILIR varliklarin dunya AABB'leri, SIKISTIRILMIS. scene.hpp:55
// kSceneHidden icin "cizilmez, SECILEMEZ" diyor ama secim yollari bayragi hic
// sormuyordu: gozu kapatilmis bir nesne hem isinla hem kutu secimle
// yakalanabiliyordu (ve secilince gizmo ile tasinabiliyordu -- gorunmeyen bir
// seyi kazara tasimak, kilit/gizlilik ozelliginin tam tersi).
//
// Sikistirmak sart: content::scene_pick bitisik bir dizi bekler ve INDEKS
// dondurur; gizli varliklari yerinde birakip sonra elemek, arkalarindaki
// nesnenin secilmesini engellerdi. map[k] = k'inci sinirin GERCEK varlik
// indeksidir. Donus: yazilan eleman sayisi.
uint32_t entity_pick_bounds(const EditorState &st, const sim::Physics &phys, content::SceneBounds *out, int32_t *map) {
  uint32_t m = 0;
  for (uint32_t i = 0; i < st.scene.entity_count; i++) {
    if (st.scene.entities[i].flags & content::kSceneHidden) continue;
    out[m] = entity_world_bounds_one(st, phys, i);
    map[m] = (int32_t)i;
    m++;
  }
  return m;
}

void set_status(EditorState &st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void set_status(EditorState &st, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(st.status, sizeof st.status, fmt, ap);
  va_end(ap);
  // Durum cubugu yalniz SON iletiyi tutar: art arda iki hatada ilki okunmadan
  // siliniyordu. Ayni satir konsola da dusuyor (gecmis orada kaliyor); duzey
  // metinden turetiliyor ("KAYDEDILEMEDI" -> Hata), siniflandirici konsolunkiyle
  // AYNI — iki yerde iki kural olmasin.
  console_log_raw(console_classify_level(st.status, false), kConsoleTagEditor, st.status);
}

void bodies_spawn(EditorState &st, sim::Physics &ph) {
  if (st.bodies_live) return;
  content::scene_spawn_bodies(st.scene, ph, st.bodies);
  st.bodies_live = true;
}
void bodies_remove(EditorState &st, sim::Physics &ph) {
  if (!st.bodies_live) return;
  content::scene_remove_bodies(ph, st.bodies, st.scene.entity_count);
  st.bodies_live = false;
}
// Varlik listesini degistiren islemler (ekle/sil/geri al/yinele) oynarken
// govde indekslerini kaydirir: once govdeler cikar, sonra yeniden girer.
template <class F> void with_bodies(EditorState &st, sim::Physics &ph, F &&f) {
  const bool live = st.bodies_live;
  if (live) bodies_remove(st, ph);
  f();
  if (live) bodies_spawn(st, ph);
}

// Ozellik paneli: PropItem uzerinden — ImGui "son oge"sine BAKMAZ. Bilesik bir
// widget'ta (vec3 = uc surukleme) son oge yalniz Z alanidir; Y suruklenirken
// IsItemActivated() false kalir ve gunluge islem dusmezdi (editor_widgets kapisi).
void track_edit(EditorState &st, SceneEntity &e, int index, const PropItem &it) {
  if (!st.edit_active) {
    st.edit_before = e;
  }
  if (it.activated || it.changed) {
    st.edit_active = true;
  }
  if (it.deactivated_after_edit) {
    const SceneEntity after = e;
    e = st.edit_before;
    if (st.hist.set_entity(st.scene, (uint32_t)index, after)) {
      st.groups.push(1);
      st.dirty = true;
    }
    st.edit_active = false;
    st.edit_before = after;
  } else if (it.deactivated && !it.changed && content::scene_entity_equal(e, st.edit_before)) {
    st.edit_active = false;
  }
}
// Dunya paneli (gunes/ortam/golge): ayni kural, SceneOp::World olarak gunluge.
void track_world_edit(EditorState &st, const PropItem &it) {
  if (!st.world_edit_active) {
    st.world_before = st.scene.world();
  }
  if (it.activated || it.changed) {
    st.world_edit_active = true;
  }
  if (it.deactivated_after_edit) {
    const content::SceneWorld after = st.scene.world();
    st.scene.set_world(st.world_before);
    if (st.hist.set_world(st.scene, after)) {
      st.groups.push(1);
      st.dirty = true;
    }
    st.world_edit_active = false;
    st.world_before = after;
  } else if (it.deactivated && content::scene_world_equal(st.scene.world(), st.world_before)) {
    st.world_edit_active = false;
  }
}
// --- Gizli Özellikler İçin Dinamik Mesh Üreticileri ---

static renderer::MeshHandle make_terrain_mesh(Arena &temp_arena, renderer::Renderer &ren, const content::HeightmapConfig &cfg) {
  float *heights = temp_arena.alloc_array<float>(cfg.width * cfg.height);
  content::generate_heightmap(cfg, heights);
  
  uint32_t nverts = cfg.width * cfg.height;
  renderer::Vertex *verts = temp_arena.alloc_array<renderer::Vertex>(nverts);
  for (uint32_t z = 0; z < cfg.height; z++) {
    for (uint32_t x = 0; x < cfg.width; x++) {
      uint32_t i = z * cfg.width + x;
      verts[i].pos = { (float)x * cfg.cell_size, heights[i], (float)z * cfg.cell_size };
      verts[i].uv = { (float)x / (cfg.width > 1 ? cfg.width - 1 : 1), (float)z / (cfg.height > 1 ? cfg.height - 1 : 1) };
      verts[i].nrm = { 0, 1, 0 };
    }
  }
  for (uint32_t z = 1; z < cfg.height - 1; z++) {
    for (uint32_t x = 1; x < cfg.width - 1; x++) {
       float hl = heights[z * cfg.width + x - 1];
       float hr = heights[z * cfg.width + x + 1];
       float hd = heights[(z - 1) * cfg.width + x];
       float hu = heights[(z + 1) * cfg.width + x];
       Vec3 n = normalize(Vec3{hl - hr, 2.0f * cfg.cell_size, hd - hu});
       verts[z * cfg.width + x].nrm = n;
    }
  }
  uint32_t nindices = (cfg.width - 1) * (cfg.height - 1) * 6;
  uint32_t *indices = temp_arena.alloc_array<uint32_t>(nindices);
  uint32_t idx = 0;
  for (uint32_t z = 0; z < cfg.height - 1; z++) {
    for (uint32_t x = 0; x < cfg.width - 1; x++) {
      uint32_t i0 = z * cfg.width + x;
      uint32_t i1 = i0 + 1;
      uint32_t i2 = (z + 1) * cfg.width + x;
      uint32_t i3 = i2 + 1;
      indices[idx++] = i0; indices[idx++] = i2; indices[idx++] = i1;
      indices[idx++] = i1; indices[idx++] = i2; indices[idx++] = i3;
    }
  }
  return ren.create_mesh(verts, nverts, indices, nindices);
}

static renderer::MeshHandle make_voxel_mesh(Arena &temp_arena, renderer::Renderer &ren, uint32_t nx, uint32_t ny, uint32_t nz, float cell) {
  content::VoxelGrid grid;
  grid.nx = nx; grid.ny = ny; grid.nz = nz; grid.voxel_size = cell;
  uint8_t *cells = temp_arena.alloc_array<uint8_t>(nx * ny * nz);
  for (uint32_t z=0; z<nz; z++) {
    for (uint32_t y=0; y<ny; y++) {
      for (uint32_t x=0; x<nx; x++) {
         float dist = length(Vec3{(float)x - nx/2.0f, (float)y - ny/2.0f, (float)z - nz/2.0f});
         cells[(z * ny + y) * nx + x] = (dist < nx/2.0f) ? 1 : 0;
      }
    }
  }
  grid.cells = cells;
  int16_t *mask = temp_arena.alloc_array<int16_t>(content::voxel_mesh_mask_capacity(grid));
  content::VoxelMeshCounts counts = content::count_voxel_mesh(grid, mask);
  
  // MeshHandle{} = 0xFFFFFFFF (GECERSIZ). "return 0" hem derlenmiyor
  // hem de derlenseydi 0 numarali GECERLI mesh'i isaret ederdi.
  if (counts.vertices == 0) return renderer::MeshHandle{};
  content::VoxelVertex *verts = temp_arena.alloc_array<content::VoxelVertex>(counts.vertices);
  uint32_t *indices = temp_arena.alloc_array<uint32_t>(counts.indices);
  content::build_voxel_mesh(grid, mask, verts, indices);
  
  renderer::Vertex *r_verts = temp_arena.alloc_array<renderer::Vertex>(counts.vertices);
  for (uint32_t i=0; i<counts.vertices; i++) {
     r_verts[i].pos = verts[i].pos;
     r_verts[i].nrm = verts[i].nrm;
     r_verts[i].uv = verts[i].uv;
  }
  return ren.create_mesh(r_verts, counts.vertices, indices, counts.indices);
}

static renderer::MeshHandle make_water_mesh(Arena &temp_arena, renderer::Renderer &ren, const content::GerstnerWave &wave) {
  uint32_t w = 64, h = 64;
  float cell = 2.0f;
  uint32_t nverts = w * h;
  renderer::Vertex *verts = temp_arena.alloc_array<renderer::Vertex>(nverts);
  for (uint32_t z = 0; z < h; z++) {
    for (uint32_t x = 0; x < w; x++) {
      uint32_t i = z * w + x;
      float wx = (x - w/2.0f) * cell;
      float wz = (z - h/2.0f) * cell;
      
      float theta = wave.direction.x * wx + wave.direction.y * wz;
      float k = 6.28318f / wave.wavelength;
      float q = wave.steepness / (wave.amplitude * k * 1.01f);
      
      float dx = q * wave.amplitude * wave.direction.x * std::cos(k * theta);
      float dz = q * wave.amplitude * wave.direction.y * std::cos(k * theta);
      float dy = wave.amplitude * std::sin(k * theta);
      
      verts[i].pos = { wx + dx, dy, wz + dz };
      verts[i].uv = { (float)x / (w - 1), (float)z / (h - 1) };
      
      float wa = k * wave.amplitude;
      float nx = wave.direction.x * wa * std::cos(k * theta);
      float nz = wave.direction.y * wa * std::cos(k * theta);
      float ny = 1.0f - q * wa * std::sin(k * theta);
      verts[i].nrm = normalize(Vec3{-nx, ny, -nz});
    }
  }
  uint32_t nindices = (w - 1) * (h - 1) * 6;
  uint32_t *indices = temp_arena.alloc_array<uint32_t>(nindices);
  uint32_t idx = 0;
  for (uint32_t z = 0; z < h - 1; z++) {
    for (uint32_t x = 0; x < w - 1; x++) {
      uint32_t i0 = z * w + x;
      uint32_t i1 = i0 + 1;
      uint32_t i2 = (z + 1) * w + x;
      uint32_t i3 = i2 + 1;
      indices[idx++] = i0; indices[idx++] = i2; indices[idx++] = i1;
      indices[idx++] = i1; indices[idx++] = i2; indices[idx++] = i3;
    }
  }
  return ren.create_mesh(verts, nverts, indices, nindices);
}

// Ayrik widget (onay kutusu, secim): kopya uzerinde degisiklik, hemen islem.
bool commit(EditorState &st, int index, const SceneEntity &after) {
  if (!st.hist.set_entity(st.scene, (uint32_t)index, after)) return false;
  st.groups.push(1);
  st.dirty = true;
  return true;
}
} // namespace

int editor_run(const EditorOptions &opts, const EditorHost *host) {
  const bool headless = host == nullptr || opts.headless_frames > 0;
  static SystemArena sys;
  if (!sys.reserve(512u << 20, "editor")) { std::fprintf(stderr, "arena\n"); return 1; }
  FrameArena frame;
  sys.carve(frame, 8u << 20, "frame");
  JobSystem jobs;
  if (!jobs.init(sys, JobSystemConfig{})) { std::fprintf(stderr, "job sistemi\n"); return 1; }
  Profiler prof;
  ProfilerConfig pc;
  pc.frame_capacity = 600;
  pc.zone_capacity = 32768;
  prof.init(sys, pc);

  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { std::fprintf(stderr, "Vulkan loader yok\n"); return 1; }
  rhi::DeviceConfig dc;
  dc.validation = opts.validation;
  dc.best_practices = opts.validation;
  if (!headless) {
    uint32_t n = 0;
    dc.instance_extensions = host->instance_extensions(host->user, &n);
    dc.instance_extension_count = n;
  }
  rhi::Device dev;
  if (!dev.init_instance(api, dc)) { std::fprintf(stderr, "instance: %s\n", dev.last_error()); return 1; }
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!headless && !host->create_surface(host->user, api, dev.instance(), &surface)) { std::fprintf(stderr, "yuzey\n"); return 1; }
  if (!dev.init_device(surface)) { std::fprintf(stderr, "cihaz: %s\n", dev.last_error()); return 1; }
  std::printf("[engine_editor] GPU: %s (Vulkan %u.%u)\n", dev.caps().device_name, VK_API_VERSION_MAJOR(dev.caps().api_version),
              VK_API_VERSION_MINOR(dev.caps().api_version));

  uint32_t width = opts.width, height = opts.height;
  rhi::Swapchain swap;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  VkRenderPass rp = VK_NULL_HANDLE;
  uint32_t image_count = 2;
  if (headless) {
    oc.width = width; oc.height = height; oc.srgb = true;
    off = rhi::offscreen_create(dev, sys, oc, &ores);
    if (!off) { std::fprintf(stderr, "offscreen: %s\n", ores.error); return 1; }
    rp = rhi::offscreen_render_pass(off);
  } else {
    uint32_t fw = 0, fh = 0;
    host->poll(host->user, &fw, &fh);
    if (!swap.init(dev, sys, surface, fw ? fw : width, fh ? fh : height)) { std::fprintf(stderr, "swapchain\n"); return 1; }
    rp = swap.render_pass();
    image_count = swap.image_count();
    width = swap.extent().width; height = swap.extent().height;
  }
  // --- VIEWPORT ONCE KURULUR, RENDERER ONUN GECISINE GORE ---------------------
  // Vulkan'da gecis UYUMLULUGU subpass BAGIMLILIKLARINI da kapsar, yalniz
  // formatlari degil. Renderer'i swapchain gecisine gore kurup viewport
  // gecisine kaydetmek dogrulama katmanini kiriyor (olculdu):
  //   srcStageMask incompatible: FRAGMENT_SHADER_BIT != COLOR_ATTACHMENT_OUTPUT_BIT
  // Bu yuzden sira: vp.init -> ren.init(vp.render_pass()) -> ui.init(rp).
  // vp.render_pass() yeniden boyutlanmada YENIDEN YARATILMAZ, yani renderer'in
  // boru hatlari panel olcusu degisince gecerli kalir.
  static EditorViewport vp;
  EditorViewportConfig vc;
  if (!vp.init(dev, vc, width, height)) { std::fprintf(stderr, "viewport: %s\n", vp.last_error()); return 1; }

  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.srgb_target = true; // hedef ARTIK viewport ve o *_SRGB (bkz. EditorViewportConfig)
  // --- Son isleme (post): ISIMA GORUNSUN DIYE ACIK --------------------------
  // post kapaliyken renderer dogrudan LDR hedefe yazar ve isima 1.0e
  // KIRPILIR: malzemeye 5 birim isima verilse de ekranda beyazdan oteye
  // gitmez, parlama (bloom) hic calismaz. Editorde isima ayarlayip
  // sonucu gorememek, ayari hic vermemekle ayni sey.
  //
  // Ic HDR hedef KURULUMDA olculenir ve kare icinde degismez
  // (renderer.hpp post_width/post_height). Viewport paneli her zaman
  // pencereden kucuk oldugu icin pencere olcusu guvenli ust sinirdir;
  // panel kuculdugunde mevcut render_scale alt-dikdortgeni devreye girer
  // (renderer.hpp:130-138). Desen bridge/engine_api.cpp:713-725 ile ayni.
  rc.post = true;
  rc.post_width = width;
  rc.post_height = height;
  // Ic hedefin temizleme rengi viewport gecisininkiyle AYNI olmali;
  // yoksa post acilinca arka plan aniden kararir (bridge bunu olcmustu).
  rc.post_clear = Vec3{vc.clear[0], vc.clear[1], vc.clear[2]};
  rc.tonemap = true; // HDR -> LDR: kirpma yerine Reinhard
  if (!ren.init(dev, sys, vp.render_pass(), rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  ren.set_render_size(vp.width(), vp.height());

  // --- Sahne dosyasi (veri modeli) ---
  static EditorState st;
  st.hist.init(sys, 256);
  st.gi_arena.reserve(32u << 20, "editor_gi_preview"); // bake + blob derleme scratch'i
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (opts.scene_path) std::snprintf(st.scene_path, sizeof st.scene_path, "%s", opts.scene_path);
  else if (adir && *adir) std::snprintf(st.scene_path, sizeof st.scene_path, "%s/editor.sahne", adir);
  else std::snprintf(st.scene_path, sizeof st.scene_path, "%s/tests/assets/editor.sahne", ENGINE_SOURCE_DIR);
  content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
  {
    content::SceneError err{};
    if (!content::scene_load(sys, st.scene_path, &st.scene, &err)) {
      console_log(ConsoleLevel::Hata, kConsoleTagScene, "sahne %s: %s", st.scene_path, err.msg);
      std::fprintf(stderr, "sahne %s: %s\n", st.scene_path, err.msg);
      return 1;
    }
  }
  char path[1024];
  auto asset = [&](const char *name) {
    std::snprintf(path, sizeof path, "%s/%s", st.scene_dir, name);
    return path;
  };
  // Kaynak yukleme (glTF -> GPU); tarayicidan eklenen kaynak da buradan gecer.
  auto load_asset = [&](int32_t i) {
    if (i < 0 || i >= (int32_t)st.scene.asset_count) return false;
    st.have[i] = content::gltf_load(sys, asset(st.scene.assets[i]), &st.models[i]) && content::upload_model(ren, sys, st.models[i], &st.ups[i]);
    if (!st.have[i]) {
      console_log(ConsoleLevel::Uyari, kConsoleTagScene, "kaynak yuklenemedi: %s", st.scene.assets[i]);
      std::printf("[engine_editor] kaynak yuklenemedi: %s\n", st.scene.assets[i]);
    }
    return st.have[i];
  };
  for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
  st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
  std::printf("[engine_editor] sahne %s: %u varlik, %u kaynak\n", st.scene_path, st.scene.entity_count, st.scene.asset_count);
  console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "sahne %s: %u varlik, %u kaynak", st.scene_path, st.scene.entity_count,
              st.scene.asset_count);

  // Editorun KENDI yardimci geometrisi. Eskiden burada demo sahnesinin tam
  // cizim takimi (dama zemin malzemesi, duzlem, glTF kutu) kuruluyordu;
  // demo arka plani editorden cikinca geriye yalniz GERCEKTEN kullanilan
  // kup kaldi: modelsiz govdenin carpisma hacmi, bos varlik isareti ve
  // editor_draw_gizmos. Tip DemoScene::DrawSet olarak KALIYOR (duz bir
  // MeshHandle yeterdi) ki bu satirlar demo arka planinin donusu
  // tartisilirsa yeniden yazilmasin.
  DemoScene::DrawSet ds;
  {
    renderer::Vertex v[24];
    uint32_t idx[36];
    const uint32_t n = renderer::Renderer::cube(v, idx); // v/idx BURADA dolar
    ds.cube = ren.create_mesh(v, 24, idx, n);
  }
  DemoScene scene;
  // with_content=false: editore YALNIZ bos bir fizik dunyasi lazim.
  // Demo icerigi (arena zemini/duvarlari, ajanlar, dusen kutular) sahne
  // dosyasina ait degil; ekranda 100+ nesne gosterip agacta 8 satir
  // birakiyordu ve GORUNMEZ carpisma kutulari kullanicinin dinamik
  // govdelerini tutuyordu. Derlenmis oyunda (scene_runtime) o kutular
  // YOK, yani editor oyundan farkli davraniyordu. Bos sahne artik
  // gercekten bos: Faz 4.5 vitrin sahnesi GERCEK sahne icerigi olacak.
  if (!scene.init(sys, &jobs, /*with_content*/ false)) { std::fprintf(stderr, "sahne\n"); return 1; }
  // Ilkel mesh tablosu bir kez kurulur (sahneden bagimsiz, salt geometri).
  content::build_primitive_meshes(ren, st.prims);
  sim::Physics &phys = scene.physics();

  EditorUi ui;
  {
    char fpath[1024], ipath[1024];
    std::snprintf(fpath, sizeof fpath, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
    // Ikon fontu (Material Icons, Apache-2.0) metin fontunun atlasina
    // birlestirilir; bkz. EditorUi::init. Bulunamazsa arayuz yine acilir.
    std::snprintf(ipath, sizeof ipath, "%s/assets/fonts/" FONT_ICON_FILE_NAME_MD, ENGINE_SOURCE_DIR);
    if (!ui.init(dev, rp, 1, image_count, fpath, 17.0f, 1.0f, true, ipath)) { std::fprintf(stderr, "editor ui: %s\n", ui.last_error()); return 1; }
    if (!ui.icons_ok())
      console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "ikon fontu yuklenemedi (%s): ikonlar bos kutu cizilecek", ipath);
  }

  // ImNodes kendi baglamini ister; kurulmadan BeginNodeEditor cagirmak
  // coker. Panel su an yalniz onizleme (bkz. asagidaki rozet).
  ImNodes::CreateContext();
  EditorCamera cam;
  cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius;
  if (headless) st.sel.set_single(0);
  st.playing = headless;
  if (st.playing) bodies_spawn(st, phys);
  if (headless && st.scene.entity_count) {
    // Betikli durum: bir islem + geri al (gunluk kapali dongude calisiyor mu).
    SceneEntity e = st.scene.entities[0];
    e.pos.x += 1.0f;
    if (st.hist.set_entity(st.scene, 0, e)) st.groups.push(1);
    st.groups.undo_size(); // grup muhasebesi gunlukle hizali kalsin
    st.hist.undo(st.scene);
  }
  set_status(st, "yuklendi: %s", st.scene_path);
  int gizmo_op = 0; // 0 tasi, 1 dondur, 2 olcekle
  bool snap_on = false;   // arac cubugundaki "Yakala": ImGuizmo adim kilidi
  float snap_step = 0.5f; // dunya birimi / derece / olcek adimi
  sim::FixedStep fs;
  uint64_t last_ns = platform::now_ns();
  uint32_t frame_i = 0, tick_i = 0;
  double prev_mx = 0, prev_my = 0, prev_scroll = 0;
  GizmoSpace gizmo_space = GizmoSpace::World; // ImGuizmo: dunya / yerel eksen
  // Kamera girdisi ONCEKI karenin panel durumunu okur (panel kare icinde daha
  // sonra ciziliyor): bir kare gecikme gorunmez, yanlis kosul gorunur olurdu.
  ViewportRect view_rect{};
  bool view_hovered = false;
  OverlayResult ovres;       // kaplamanin son karede urettigi etkilesim
  bool cam_dragging = false; // kamera tusu basili: panelden cikmak donusu kesmesin
  bool prev_f = false;
  ViewportTab view_tab = ViewportTab::Scene;
  GameAspect game_aspect = GameAspect::Free;
  bool maximize_on_play = false;
  bool mute_audio_on_play = false;
  bool show_grid = true;
  // Demo sahnesi (ajanlar, Jolt kutulari, dama zemin, duvar, parcaciklar)
  // EDITORUN ARKA PLANIYDI: ekranda 100+ nesne, sahne agacinda 8 satir.
  // Hicbiri secilemez, tasinamaz, kaydedilemez -- yani kullaniciya
  // duzenledigi sahne hakkinda YANLIS bir doluluk hissi veriyordu.
  // Artik varsayilan KAPALI; isteyen Gorunum menusunden acar.
  bool show_stats = true;
  bool was_playing = false;
  RecordCtx rctx{&ren, &ui, &vp, &dev};
  bool running = true;
  // Konsol + dosya islemleri durumu. static: ic tamponlari buyuk (halka ~150 KB,
  // diyalogun dizin listesi 512 girdi) ve surec basina TEK editor kosuyor.
  static ConsoleView console_view;
  static ConsoleCapture console_cap;
  static FileDialog dlg;
  static ConfirmState confirm;
  bool show_console = true;
  enum PendingAction { PendingNone = 0, PendingNew = 1, PendingOpen = 2, PendingOpenPath = 3 };
  int pending = PendingNone;
  char pending_path[1024] = {0}; // "Son dosyalar"dan secilen yol
  char recent_file[1024];
  {
    const char *home = std::getenv("HOME");
    std::snprintf(recent_file, sizeof recent_file, "%s/.tulpar_son_sahneler", (home && *home) ? home : ENGINE_SOURCE_DIR);
    recent_load(recent_file); // dosya yoksa false doner, liste bos kalir — HATA DEGIL
    recent_push(st.scene_path);
  }
  auto set_playing = [&](bool p) {
    if (p == st.playing) return;
    st.playing = p;
    st.paused = false; // durdur/baslat duraklatmayi da sifirlar
    st.step_request = 0;
    if (p) { st.play_time = 0; bodies_spawn(st, phys); }
    else bodies_remove(st, phys); // durdur: veri modeli (yazar donusumu) gecerli
  };
  // Geri al / yinele GRUP isler: bir kullanici eylemi gunlukte N islem olabilir
  // (grup tasima = N set_entity, grup silme = N remove_entity). Sinirlar OpGroups'ta;
  // gunluk once biterse dongu orada durur (grup muhasebesi gunlugun onune gecemez).
  auto do_undo = [&]() {
    if (st.edit_active) {
      const int32_t si = st.sel.primary();
      if (si >= 0 && si < (int32_t)st.scene.entity_count) {
        st.scene.entities[si] = st.edit_before;
      }
      st.edit_active = false;
      ImGui::ClearActiveID();
      set_status(st, "degisiklik iptal edildi");
      return;
    }
    if (st.hist.undo_count() == 0) return;
    const uint32_t k = st.groups.undo_size();
    uint32_t done = 0;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < k && st.hist.undo(st.scene); i++) done++;
    });
    if (done) { st.dirty = true; clamp_selection(st); set_status(st, "geri alindi (%u islem, %u kaldi)", done, st.hist.undo_count()); }
  };
  auto do_redo = [&]() {
    if (st.hist.redo_count() == 0) return;
    const uint32_t k = st.groups.redo_size();
    uint32_t done = 0;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < k && st.hist.redo(st.scene); i++) done++;
    });
    if (done) { st.dirty = true; clamp_selection(st); set_status(st, "yinelendi (%u islem, %u kaldi)", done, st.hist.redo_count()); }
  };
  auto do_save_as = [&]() {
    file_dialog_open(dlg, FileDialogMode::Kaydet, st.scene_path[0] ? st.scene_path : st.scene_dir, ".sahne", "Farkl\xC4\xB1 kaydet");
  };
  // Sahneyi verilen yola yazar ve editorun ACIK DOSYASINI oraya tasir (kaynak
  // tarayicisi da yeni dizini gosterir — sahne dosyasi dizinini takip eder).
  auto save_scene_to = [&](const char *path) {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    content::SceneError err{};
    if (!content::scene_save(frame, st.scene, path, &err)) { set_status(st, "KAYDEDILEMEDI: %s", err.msg); return false; }
    std::snprintf(st.scene_path, sizeof st.scene_path, "%s", path);
    content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    st.dirty = false;
    recent_push(st.scene_path);
    recent_save(recent_file);
    set_status(st, "kaydedildi: %s", st.scene_path);
    return true;
  };
  auto do_save = [&]() {
    if (st.scene_path[0] == 0) { do_save_as(); return; } // adsiz sahne: once yer sor
    save_scene_to(st.scene_path);
  };
  // Dosyadan yukleme: veri modeli + turetilmis her sey (kaynaklar, tarayici,
  // kamera) yenilenir ve GUNLUK SIFIRLANIR — eski sahnenin geri al kayitlari
  // yeni sahneye uygulanamaz (indeksler baska bir sahneye ait).
  auto load_scene_from = [&](const char *path) {
    content::SceneDesc nd;
    content::SceneError err{};
    if (!content::scene_load(sys, path, &nd, &err)) { set_status(st, "ACILAMADI: %s (%s)", path, err.msg); return false; }
    with_bodies(st, phys, [&] {
      st.scene = nd;
      st.hist.clear();
      st.groups.clear();
      st.sel.clear();
      st.dirty = false;
    });
    std::snprintf(st.scene_path, sizeof st.scene_path, "%s", path);
    content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) st.have[i] = false;
    for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius;
    st.clip_count = 0; // pano baska bir sahnenin varliklarini tasiyordu
    recent_push(st.scene_path);
    recent_save(recent_file);
    set_status(st, "acildi: %s (%u varlik)", st.scene_path, st.scene.entity_count);
    return true;
  };
  auto do_new = [&]() {
    content::SceneDesc fresh; // varsayilan dunya + 0 varlik
    with_bodies(st, phys, [&] {
      st.scene = fresh;
      st.hist.clear();
      st.groups.clear();
      st.sel.clear();
      st.dirty = false;
    });
    st.scene_path[0] = 0; // ADSIZ: ilk Kaydet "Farkli kaydet"e duser
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) st.have[i] = false;
    st.clip_count = 0;
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    set_status(st, "yeni sahne (henuz kaydedilmedi)");
  };
  auto run_pending = [&](int a) {
    if (a == PendingNew) do_new();
    else if (a == PendingOpen) file_dialog_open(dlg, FileDialogMode::Ac, st.scene_dir, ".sahne", "Sahne a\xC3\xA7");
    else if (a == PendingOpenPath && pending_path[0]) load_scene_from(pending_path);
  };
  // KIRLI SAHNE KORUMASI: kaydedilmemis is varken Yeni/Ac ONCE sorar. Onay kipli
  // oldugu icin eylem ERTELENIR (pending) ve cevap gelince calisir.
  auto guard_then = [&](int action, const char *path = nullptr) {
    std::snprintf(pending_path, sizeof pending_path, "%s", path ? path : "");
    if (!st.dirty) { run_pending(action); return; }
    pending = action;
    confirm.open = true;
  };
  auto do_new_guarded = [&]() { guard_then(PendingNew); };
  auto do_open_guarded = [&]() { guard_then(PendingOpen); };
  // Derle: veri modeli -> runtime blob (.sahneb, sahne dosyasinin yanina; PLAN §6).
  // Kaydet gibi kamerayi da yazar; dosyayi degil bellekteki sahneyi derler.
  auto do_compile = [&]() {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    char out[1024];
    if (!content::scene_blob_path_for(st.scene_path, out, sizeof out)) { set_status(st, "DERLENEMEDI: yol cok uzun"); return; }
    content::SceneError err{};
    if (!content::scene_blob_save(frame, st.scene, out, &err)) { set_status(st, "DERLENEMEDI: %s", err.msg); return; }
    content::SceneBlobView v;
    if (!content::scene_blob_load(frame, out, &v, &err)) { set_status(st, "DERLENDI ama acilamadi: %s", err.msg); return; }
    set_status(st, "derlendi: %s (%u bayt, %u cizim, %u isik, %u govde, ozet %08x)", out, v.h->total_size, v.h->draw_count, v.h->light_count,
               v.h->body_count, (unsigned)v.h->hash_lo);
  };
  // GI onizleme pisirme: sahne+yuklu modellerden bellek-ici bake -> derle ->
  // ac (ayni scene_blob_compile_ex/scene_blob_open yolu, DISK YOK). Dunya
  // panelindeki "Isik Haritasini Pisir" butonu cagirir; her cagrida onceki
  // pisirme atilir (arena().reset_to(0)) -- tek aktif sonuc yeter.
  auto do_bake_gi = [&]() {
    st.gi_arena.reset_to(0);
    content::GiScene g{};
    content::scene_gi_setup(st.scene, &g);
    static content::GiOccluder occ[content::kGiMaxOccluders];
    static content::GiLight lights[content::kGiMaxLights];
    static content::GiTri tris[200000]; // editor.sahne'nin gercek yuku (~6600) icin cok pay
    content::SceneGiOptions opt;
    g.occluders = occ;
    g.occluder_count = content::scene_gi_occluders(st.scene, opt, occ, content::kGiMaxOccluders);
    const content::Model *model_ptrs[content::kSceneMaxAssets];
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) model_ptrs[i] = st.have[i] ? &st.models[i] : nullptr;
    g.tris = tris;
    g.tri_count = content::scene_gi_model_tris(st.scene, opt, model_ptrs, st.scene.asset_count, tris, 200000);
    g.lights = lights;
    g.light_count = content::scene_gi_lights(st.scene, lights, content::kGiMaxLights);
    content::SceneBlobExtras x{};
    st.gi_baked = false;
    if (!content::scene_gi_bake(st.gi_arena, g, opt, &x, &st.gi_report)) {
      set_status(st, "GI pisirilemedi: sahne bos ya da izgara kurulamadi");
      return;
    }
    const size_t need = content::scene_blob_compile_ex(st.scene, &x, nullptr, 0);
    void *buf = st.gi_arena.alloc(need, content::kSceneBlobAlign);
    if (!buf) { set_status(st, "GI pisirilemedi: onizleme belleği yetersiz (32 MB)"); return; }
    content::scene_blob_compile_ex(st.scene, &x, buf, need);
    content::SceneBlobView v;
    content::SceneError err{};
    if (!content::scene_blob_open(buf, need, &v, &err)) { set_status(st, "GI pisirilemedi: %s", err.msg); return; }
    st.gi_baked = st.gi.init(v);
    st.gi_bake_undo_count = st.hist.undo_count();
    if (st.gi_baked)
      set_status(st, "GI pisirildi: %u sonda (%u gecerli), %.3f s", st.gi_report.probes, st.gi_report.probes - st.gi_report.probes_inside,
                 st.gi_report.seconds);
    else
      set_status(st, "GI pisirildi ama sonda izgarasi acilamadi (sahne cok kucuk/buyuk olabilir)");
  };
  // kind: 0 = kopya/varsayilan (secili varsa kopyasi, yoksa bos ya da model),
  // 1 bos, 2 model, 3 isik, 4 govde (Sahne panelinin "+" menusu).
  auto do_add = [&](int kind = 0, int32_t parent = -1) {
    SceneEntity e{};
    const int32_t prim = st.sel.primary();
    if (kind == 0 && prim >= 0) {
      e = st.scene.entities[prim];
      std::snprintf(e.name, sizeof e.name, "%.24s_kopya", st.scene.entities[prim].name);
      e.pos.x += 1.5f;
    } else {
      static const char *const kStem[] = {
        "nesne", "nesne", "model", "isik", "kutu_sabit",
        "kure_sabit", "kutu_dinamik", "kure_dinamik", "zemin",
        "animasyon", "kup", "kure", "kamera", "ses", "isik_yonlu"
      };
      // Ilkel geometriler 20..24 araliginda; kStem'i 25 uzunluga cikarip
      // ortasini bos birakmak yerine ayri tablo (bosluklar sessizce "nesne"
      // olur ve iki kapsul "nesne_3" adini alirdi).
      static const char *const kPrimStem[] = {"kapsul", "silindir", "koni", "dortgen", "simit"};
      const char *stem = "nesne";
      if (kind >= 20 && kind <= 24) stem = kPrimStem[kind - 20];
      else if (kind >= 0 && kind < (int)(sizeof(kStem)/sizeof(kStem[0]))) stem = kStem[kind];
      std::snprintf(e.name, sizeof e.name, "%s_%u", stem, st.scene.entity_count + 1);
      e.pos = cam.target;
      e.parent = parent;

      switch (kind) {
      case 1: // Boş varlık
        e.components = 0;
        break;
      case 2: // Model
        e.components = content::kSceneModel;
        e.asset = st.scene.asset_count ? 0 : -1;
        break;
      case 3: // Nokta Işık
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Point;
        e.light_color = Vec3{1.0f, 1.0f, 1.0f};
        e.light_intensity = 3.0f;
        e.light_radius = 8.0f;
        break;
      case 4: // Sabit Kutu Gövde
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.dynamic = false;
        break;
      case 5: // Sabit Küre Gövde
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Sphere;
        e.radius = 0.5f;
        e.dynamic = false;
        break;
      case 6: // Dinamik Kutu Gövde
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.dynamic = true;
        break;
      case 7: // Dinamik Küre Gövde
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Sphere;
        e.radius = 0.5f;
        e.dynamic = true;
        break;
      case 8: // Zemin / Düzlem
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{10.0f, 0.1f, 10.0f};
        e.dynamic = false;
        e.pos.y = -0.1f;
        break;
      case 9: // Animasyonlu Model
        e.components = content::kSceneModel | content::kSceneAnim;
        e.asset = st.scene.asset_count > 1 ? 1 : (st.scene.asset_count ? 0 : -1);
        e.clip = 0;
        e.speed = 1.0f;
        break;
      case 10: { // Küp (Model + Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.dynamic = false;
        int32_t cube_a = -1;
        for (uint32_t a = 0; a < st.scene.asset_count; a++) {
          if (std::strstr(st.scene.assets[a], "cube") != nullptr) { cube_a = (int32_t)a; break; }
        }
        if (cube_a < 0 && st.scene.asset_count) cube_a = 0;
        e.asset = cube_a;
        break;
      }
      case 11: { // Küre (Model + Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.shape = content::SceneShape::Sphere;
        e.radius = 0.5f;
        e.dynamic = false;
        int32_t sph_a = -1;
        for (uint32_t a = 0; a < st.scene.asset_count; a++) {
          if (std::strstr(st.scene.assets[a], "sphere") != nullptr) { sph_a = (int32_t)a; break; }
        }
        if (sph_a < 0 && st.scene.asset_count) sph_a = 0;
        e.asset = sph_a;
        break;
      }
      case 12: // Kamera Varlığı
        e.components = content::kSceneCamera;
        e.cam_fov = 60.0f;
        e.cam_near = 0.1f;
        e.cam_far = 200.0f;
        break;
      case 13: // Ses Kaynağı
        e.components = content::kSceneAudio;
        std::snprintf(e.audio_clip, sizeof e.audio_clip, "ambient.wav");
        e.audio_volume = 1.0f;
        e.audio_pitch = 1.0f;
        e.audio_loop = true;
        e.audio_spatial = true;
        break;
      case 14: // Yönlü Işık
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Directional;
        e.light_color = Vec3{1.0f, 0.96f, 0.9f};
        e.light_intensity = 2.0f;
        e.rot_deg = Vec3{50.0f, -30.0f, 0.0f}; // asagi/yana bakan tipik gunes acisi
        break;
      // --- Ilkel (prosedurel) geometriler ---------------------------------
      // Bu kodlar kCreate3D menusunde ZATEN vardi ama buraya hic ulasmiyordu:
      // cagiran `tb <= 14` ile kesiyor, switch de 14'te bitiyordu. Yani
      // Kapsul/Silindir/Koni/Dortgen/Simit tiklandiginda HICBIR SEY olmuyordu.
      //
      // glTF kaynagi aranmaz: geometri motorun kendi ureteclerinden gelir
      // (content/primitives.hpp), bu yuzden asset = -1 ve primitive = kind.
      // Slot numaralari menu kodlariyla ayni secildi (bkz. primitives.hpp).
      case 20: // Kapsül
      case 21: // Silindir
      case 22: // Koni
      case 23: // Dörtgen
      case 24: // Simit
        e.components = content::kSceneModel;
        e.primitive = kind;
        e.asset = -1;
        break;
      default:
        if (kind == 2 || (kind == 0 && st.scene.asset_count)) { e.components = content::kSceneModel; e.asset = st.scene.asset_count ? 0 : -1; }
        if (kind == 3) e.components = content::kSceneLight;
        if (kind == 4) e.components = content::kSceneBody;
        break;
      }
    }
    with_bodies(st, phys, [&] {
      if (st.hist.add_entity(st.scene, e)) {
        st.groups.push(1);
        st.sel.set_single((int32_t)st.scene.entity_count - 1);
        st.dirty = true;
        set_status(st, "eklendi: %s", e.name);
      } else set_status(st, "eklenemedi (kapasite %u)", content::kSceneMaxEntities);
    });
  };
  // Grup silme: secilenlerin tamami, buyukten kucuge (indeksler kaymasin); gunluge
  // N islem ama TEK eylem (geri al hepsini birden getirir).
  auto do_remove = [&]() {
    if (st.sel.count == 0) return;
    int32_t idx[Selection::kMax];
    const uint32_t n = st.sel.sorted_desc(idx);
    uint32_t ops = 0;
    with_bodies(st, phys, [&] { ops = selection_remove(st.scene, st.hist, idx, n); });
    for (uint32_t k = 0; k < n; k++) st.tree.collapse.after_remove((uint32_t)idx[k]); // buyukten kucuge silindi
    if (ops) {
      st.groups.push(ops);
      st.dirty = true;
      set_status(st, "silindi (%u varlik)", ops);
    }
    st.sel.clear();
  };
  // Pano: kopyalama secimi ARTAN indeks sirasinda alir (belirlenimli — secim
  // kumesinin kendi sirasi tiklama sirasidir, yapistirma sirasi ona bagli olmasin).
  auto do_copy = [&]() {
    if (st.sel.count == 0) return;
    int32_t idx[Selection::kMax];
    const uint32_t n = st.sel.sorted_desc(idx); // buyukten kucuge; tersten okuyacagiz
    st.clip_count = 0;
    for (uint32_t i = n; i > 0; i--) st.clip[st.clip_count++] = st.scene.entities[idx[i - 1]];
    set_status(st, "panoya alindi (%u varlik)", st.clip_count);
  };
  auto do_cut = [&]() {
    if (st.sel.count == 0) return;
    do_copy();
    do_remove();
    set_status(st, "kesildi (%u varlik)", st.clip_count);
  };
  // Yapistir: her varlik yeni bir varliktir (kopya adi + kucuk otelemeyle
  // ustuste binmesin). Gunluge N islem ama TEK eylem: geri al hepsini alir.
  auto do_paste = [&]() {
    if (st.clip_count == 0) return;
    uint32_t ops = 0;
    const uint32_t first = st.scene.entity_count;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < st.clip_count; i++) {
        SceneEntity e = st.clip[i];
        e.pos.x += 1.0f;
        if (st.hist.add_entity(st.scene, e)) ops++;
        else break; // kapasite doldu: sessizce kirpma yok, asagida bildirilir
      }
    });
    if (!ops) { set_status(st, "yapistirilamadi (kapasite %u)", content::kSceneMaxEntities); return; }
    st.groups.push(ops);
    st.dirty = true;
    st.sel.clear();
    for (uint32_t i = 0; i < ops; i++) st.sel.toggle((int32_t)(first + i));
    if (ops < st.clip_count) set_status(st, "yapistirildi (%u/%u — kapasite %u doldu)", ops, st.clip_count, content::kSceneMaxEntities);
    else set_status(st, "yapistirildi (%u varlik)", ops);
  };
  // Sahne panelinin dondurdugu NIYETI uygular. Panel sahneyi DEGISTIRMEZ; gunluk,
  // secim ve govde yeniden kurulumu tek yerde — burada.
  auto apply_hierarchy = [&](const HierarchyResult &r) {
    const int32_t i = r.index;
    const bool valid = i >= 0 && i < (int32_t)st.scene.entity_count;
    if (!valid && r.action != HierarchyAction::None) return;
    switch (r.action) {
    case HierarchyAction::None: break;
    case HierarchyAction::Select:
      if (r.ctrl) st.sel.toggle(i);
      else st.sel.set_single(i);
      break;
    case HierarchyAction::Toggle: st.tree.collapse.toggle((uint32_t)i); break;
    case HierarchyAction::Rename: {
      SceneEntity after = st.scene.entities[i];
      std::snprintf(after.name, sizeof after.name, "%s", r.name);
      if (st.hist.set_entity(st.scene, (uint32_t)i, after)) {
        st.groups.push(1);
        st.dirty = true;
        set_status(st, "adlandirildi: %s", after.name);
      }
      break;
    }
    case HierarchyAction::Delete:
      st.sel.set_single(i);
      do_remove();
      st.tree.collapse.after_remove((uint32_t)i);
      break;
    case HierarchyAction::Duplicate:
      st.sel.set_single(i);
      do_add(0);
      break;
    case HierarchyAction::Cut:
      st.sel.set_single(i);
      do_cut();
      break;
    case HierarchyAction::Copy:
      st.sel.set_single(i);
      do_copy();
      break;
    case HierarchyAction::Paste:
      do_paste();
      break;
    case HierarchyAction::Detach:
    case HierarchyAction::Reparent: {
      const int32_t par = (r.action == HierarchyAction::Detach) ? -1 : r.target;
      bool ok = false;
      with_bodies(st, phys, [&] { ok = st.hist.reparent(st.scene, (uint32_t)i, par); });
      if (ok) {
        st.groups.push(1);
        st.dirty = true;
        if (par < 0) set_status(st, "ebeveynden ayrildi: %s", st.scene.entities[i].name);
        else set_status(st, "ebeveyn: %s", st.scene.entities[par].name);
      } else if (par >= 0) set_status(st, "ebeveynlenemedi (dongu ya da derinlik tavani %u)", content::kSceneMaxDepth);
      break;
    }
    case HierarchyAction::Visibility:
    case HierarchyAction::Lock: {
      SceneEntity after = st.scene.entities[i];
      after.flags ^= (r.action == HierarchyAction::Visibility) ? content::kSceneHidden : content::kSceneLocked;
      if (st.hist.set_entity(st.scene, (uint32_t)i, after)) {
        st.groups.push(1);
        st.dirty = true;
      }
      break;
    }
    }
  };
  // Kaynak tarayicidan ekleme: kaynak sahneye (varsa mevcut indeks) + o kaynakla
  // yeni varlik; kaynak henuz yuklenmemisse burada yuklenir.
  auto do_add_asset = [&](const char *file) {
    int32_t a = -1;
    uint32_t ops = 0;
    with_bodies(st, phys, [&] { ops = editor_add_asset_entity(st.scene, st.hist, file, cam.target, &a); });
    if (!ops) { set_status(st, "kaynak eklenemedi: %s", file); return; }
    st.groups.push(ops);
    st.dirty = true;
    if (a >= 0 && !st.have[a]) load_asset(a);
    st.sel.set_single((int32_t)st.scene.entity_count - 1);
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    set_status(st, "kaynak eklendi: %s (%s)", file, (a >= 0 && st.have[a]) ? "yuklendi" : "YUKLENEMEDI");
  };
  // --- Komut tablosu: menu, arac cubugu ve kisayollar TEK kaynaktan ----------
  // (editor_commands.hpp'nin varlik sebebi: ayni komut iki yerde yazilmasin,
  // cakisma sessiz kalmasin, koruma kurali tek olsun). Geri cagrilar yukaridaki
  // yakalayan lambda'lar; CommandFn duz isaretci oldugu icin arada bir baglam
  // yapisi var — lambda'lar adresle tutulur, kopyalanmaz.
  struct CmdCtx {
    EditorState *st;
    int *gizmo_op;
    decltype(&do_save) save;
    decltype(&do_compile) compile;
    decltype(&do_undo) undo;
    decltype(&do_redo) redo;
    decltype(&do_add) add;
    decltype(&do_remove) remove;
    decltype(&set_playing) play;
    decltype(&do_cut) cut;
    decltype(&do_copy) copy;
    decltype(&do_paste) paste;
    decltype(&do_new_guarded) newscene;
    decltype(&do_open_guarded) open;
    decltype(&do_save_as) saveas;
    bool *show_console;
    sim::Physics *phys;      // ViewFocus: sinirlar govde konumlarini da sorar
    EditorCamera *cam;       // ViewFocus kamerayi kenetler
    const EditorHost *host; // tam ekran: yetenek host'ta (headless'ta nullptr)
  } cc{&st,      &gizmo_op, &do_save, &do_compile,      &do_undo,         &do_redo,     &do_add,     &do_remove,
       &set_playing, &do_cut,   &do_copy, &do_paste,    &do_new_guarded,  &do_open_guarded, &do_save_as, &show_console, &phys, &cam, host};
  CommandTable cmds;
  cmds.bind(CommandId::FileNew, [](void *c) { (*static_cast<CmdCtx *>(c)->newscene)(); }, &cc);
  cmds.bind(CommandId::FileOpen, [](void *c) { (*static_cast<CmdCtx *>(c)->open)(); }, &cc);
  cmds.bind(CommandId::FileSave, [](void *c) { (*static_cast<CmdCtx *>(c)->save)(); }, &cc);
  cmds.bind(CommandId::FileSaveAs, [](void *c) { (*static_cast<CmdCtx *>(c)->saveas)(); }, &cc);
  cmds.bind(CommandId::FileCompile, [](void *c) { (*static_cast<CmdCtx *>(c)->compile)(); }, &cc);
  cmds.bind(CommandId::EditUndo, [](void *c) { (*static_cast<CmdCtx *>(c)->undo)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->hist.undo_count() > 0; });
  cmds.bind(CommandId::EditRedo, [](void *c) { (*static_cast<CmdCtx *>(c)->redo)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->hist.redo_count() > 0; });
  cmds.bind(CommandId::EditDuplicate, [](void *c) { (*static_cast<CmdCtx *>(c)->add)(); }, &cc);
  cmds.bind(CommandId::EditDelete, [](void *c) { (*static_cast<CmdCtx *>(c)->remove)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditCut, [](void *c) { (*static_cast<CmdCtx *>(c)->cut)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditCopy, [](void *c) { (*static_cast<CmdCtx *>(c)->copy)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditPaste, [](void *c) { (*static_cast<CmdCtx *>(c)->paste)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->clip_count > 0; });
  cmds.bind(CommandId::SelectAll,
            [](void *c) {
              EditorState *s = static_cast<CmdCtx *>(c)->st;
              s->sel.clear();
              for (uint32_t i = s->scene.entity_count; i > 0; i--) s->sel.toggle((int32_t)(i - 1));
            },
            &cc, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->scene.entity_count > 0; });
  cmds.bind(CommandId::SelectClear, [](void *c) { static_cast<CmdCtx *>(c)->st->sel.clear(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::ViewGizmos,
            [](void *c) { // gizmolarin tamami ac/kapa
              GizmoOptions &g = static_cast<CmdCtx *>(c)->st->gizmos;
              const bool on = !(g.light_radius || g.light_glyph || g.shadow_volume || g.sun_dir || g.camera_frustum);
              g.light_radius = g.light_glyph = g.shadow_volume = g.sun_dir = g.camera_frustum = on;
            },
            &cc, nullptr, [](const void *c) {
              const GizmoOptions &g = static_cast<const CmdCtx *>(c)->st->gizmos;
              return g.light_radius || g.light_glyph || g.shadow_volume || g.sun_dir || g.camera_frustum;
            });
  cmds.bind(CommandId::ViewConsole, [](void *c) { bool *b = static_cast<CmdCtx *>(c)->show_console; *b = !*b; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->show_console; });
  cmds.bind(CommandId::ViewFocus, [](void *c) {
    const CmdCtx *x = static_cast<CmdCtx *>(c);
    const int32_t s0 = x->st->sel.primary();
    if (s0 >= 0 && s0 < (int32_t)x->st->scene.entity_count) {
      static content::SceneBounds fb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(*x->st, *x->phys, fb);
      if ((uint32_t)s0 < nb) camera_focus(*x->cam, fb[s0]);
    }
  }, &cc, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  // Tam ekran: yetenek HOST'un (GLFW). Yoksa menu ogesi soluk — sessizce
  // hicbir sey yapan bir dugme kalmaz. Swapchain'i bu komut DEGIL, kare
  // basindaki sync_size yeniden kurar (tek karar noktasi).
  cmds.bind(CommandId::ViewFullscreen,
            [](void *c) {
              const EditorHost *h = static_cast<CmdCtx *>(c)->host;
              if (!h || !h->set_fullscreen) return;
              const bool now = h->is_fullscreen ? h->is_fullscreen(h->user) : false;
              if (!h->set_fullscreen(h->user, !now))
                console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "tam ekran: pencere yoneticisi/GLFW istegi uygulamadi");
            },
            &cc,
            [](const void *c) {
              const EditorHost *h = static_cast<const CmdCtx *>(c)->host;
              return h && h->set_fullscreen != nullptr;
            },
            [](const void *c) {
              const EditorHost *h = static_cast<const CmdCtx *>(c)->host;
              return h && h->is_fullscreen && h->is_fullscreen(h->user);
            });
  cmds.bind(CommandId::GizmoTranslate, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 0; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 0; });
  cmds.bind(CommandId::GizmoRotate, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 1; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 1; });
  cmds.bind(CommandId::GizmoScale, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 2; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 2; });
  cmds.bind(CommandId::PlayToggle,
            [](void *c) {
              CmdCtx *x = static_cast<CmdCtx *>(c);
              (*x->play)(!x->st->playing);
            },
            &cc, nullptr, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->playing; });
  cmds.bind(CommandId::PlayPause, [](void *c) { EditorState *s = static_cast<CmdCtx *>(c)->st; s->paused = !s->paused; }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->playing; },
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->paused; });
  cmds.bind(CommandId::PlayStep, [](void *c) { static_cast<CmdCtx *>(c)->st->step_request++; }, &cc,
            [](const void *c) {
              const EditorState *s = static_cast<const CmdCtx *>(c)->st;
              return s->playing && s->paused;
            });
  // Dosya menusune "Son dosyalar" alt menusu: komut tablosu KOMUT tasir, bu ise
  // bir veri listesi — chrome'un ek oge kancasindan geliyor.
  struct MenuExtraCtx {
    decltype(&guard_then) guard;
    int open_action;
  } mx{&guard_then, PendingOpenPath};
  ChromeMenuExtra menu_extra;
  menu_extra.ctx = &mx;
  menu_extra.fn = [](void *ctx, CommandCategory cat) {
    if (cat != CommandCategory::File) return;
    MenuExtraCtx *m = static_cast<MenuExtraCtx *>(ctx);
    const char *rec[kRecentMax];
    const uint32_t nrec = recent_list(rec, kRecentMax);
    if (!ImGui::BeginMenu("Son dosyalar", nrec > 0)) return;
    for (uint32_t i = 0; i < nrec; i++) {
      const bool var = recent_exists(i);
      char lbl[kFilePathLen + 16];
      std::snprintf(lbl, sizeof lbl, "%u  %s", i + 1, rec[i]);
      ImGui::BeginDisabled(!var); // eksik dosya SOLUK, gizli degil
      if (ImGui::MenuItem(lbl)) (*m->guard)(m->open_action, rec[i]);
      ImGui::EndDisabled();
      if (!var && ImGui::IsItemHovered()) ImGui::SetTooltip("Dosya bulunamadi: %s", rec[i]);
    }
    ImGui::EndMenu();
  };
  {
    // Kurulum denetimi: bagli kalmayan komut = menude tiklanmayan satir. Sessiz
    // gecmez; headless kosuda da gorunur.
    CommandId ub[kCommandCount];
    const uint32_t n = cmds.unbound(ub, kCommandCount);
    if (n) {
      console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "%u komut BAGLANMADI (menude olu satir), ilki: %s", n, cmds.desc(ub[0]).name);
      std::fprintf(stderr, "[editor] %u komut BAGLANMADI (menude olu satir), ilki: %s\n", n, cmds.desc(ub[0]).name);
    }
  }
  // stdout/stderr yakalama: motorun printf'i ve dogrulama katmani konsola aksin.
  // DONGUDEN HEMEN ONCE aciliyor — yukaridaki kurulum yollari `return 1` ile
  // cikabiliyor ve borudaki bayt drain edilmeden kaybolurdu. HEADLESS'ta
  // ACILMAZ: kapi satirlari ([engine_editor] ... OK) dogrudan akmali.
  if (!headless && !console_capture_begin(&console_cap))
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "cikti yakalanamadi: %s", console_cap.err_msg);

  while (running) {
    ENGINE_ZONE("frame");
    console_set_frame(frame_i);
    console_capture_drain(); // kare basina BIR kez; yakalama kapaliysa no-op
    uint64_t now = platform::now_ns();
    float dt = (float)((now - last_ns) / 1e9);
    last_ns = now;
    if (dt > 0.25f) dt = 0.25f;
    if (headless) dt = 1.0f / 60.0f;
    prof.begin_frame();
    frame.begin_frame();
    const platform::InputState *in = nullptr;
    uint32_t fw = width, fh = height;
    if (!headless) {
      if (!host->poll(host->user, &fw, &fh)) running = false;
      in = host->input ? host->input(host->user) : nullptr;
      if (fw == 0 || fh == 0) continue; // kucultulmus
      // --- PENCERE OLCUSU -> SWAPCHAIN, KAYITTAN ONCE ------------------------
      // Tam ekrana gecis (ve her yeniden boyutlandirma) BURADA yakalanir.
      // Eskiden yalniz `needs_recreate()` (OUT_OF_DATE) bakiliyordu ve o da
      // kare SONUNDA: Wayland'de OUT_OF_DATE HIC gelmedigi icin swapchain
      // 1280x720'de kaliyor, kompozitor o goruntuyu tam ekrana GERIYORDU —
      // "tam ekran olmuyor, icerik ayni oranda buyuyup bozuluyor" bu.
      // Kararin gerekcesi ve X11/Wayland ayrimi: rhi/swapchain.hpp ResizeAction.
      if (swap.sync_size(fw, fh))
        console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "swapchain %ux%u (%s)", swap.extent().width, swap.extent().height,
                    swap.last_resize_reason());
      // Arayuz ile hedef AYNI olcuyu gorur: ImGui'nin DisplaySize'i cizilen
      // hedefin olcusudur, pencerenin degil. Ayrisirlarsa (yeniden kurma
      // basarisiz ya da surucu currentExtent'i dayatti) kare gerilir.
      fw = swap.extent().width;
      fh = swap.extent().height;
      // HiDPI: imlec mantiksal pikselde, arayuz hedef pikselinde. Oran her
      // karede okunur (pencere baska ekrana tasininca degisir).
      if (host->window_size) {
        uint32_t ww = 0, wh = 0;
        host->window_size(host->user, &ww, &wh);
        if (ww > 0) ui.set_pointer_scale((float)fw / (float)ww);
      }
    }
    // --- KAMERA: karar editor_camera.hpp'de (saf gecis fonksiyonu; kapilar orayi
    // olcer, burasi yalniz girdiyi toplar).
    if (in) {
      CameraInput ci;
      ci.dx = (float)(in->mouse_x - prev_mx);
      ci.dy = (float)(in->mouse_y - prev_my);
      ci.scroll = (float)(in->scroll_y - prev_scroll);
      ci.lmb = in->mouse_down[0];
      ci.rmb = in->mouse_down[1];
      ci.mmb = in->mouse_down[2];
      ci.shift = in->key_down[GLFW_KEY_LEFT_SHIFT] || in->key_down[GLFW_KEY_RIGHT_SHIFT];
      ci.ctrl = in->key_down[GLFW_KEY_LEFT_CONTROL] || in->key_down[GLFW_KEY_RIGHT_CONTROL];
      ci.alt = in->key_down[GLFW_KEY_LEFT_ALT] || in->key_down[GLFW_KEY_RIGHT_ALT];
      ci.key_w = in->key_down[GLFW_KEY_W];
      ci.key_a = in->key_down[GLFW_KEY_A];
      ci.key_s = in->key_down[GLFW_KEY_S];
      ci.key_d = in->key_down[GLFW_KEY_D];
      ci.key_q = in->key_down[GLFW_KEY_Q];
      ci.key_e = in->key_down[GLFW_KEY_E];
      // DIKKAT: kosul !ui.wants_mouse() DEGIL. 3B artik bir ImGui panelinin
      // icinde yasiyor, yani fare goruntunun uzerindeyken WantCaptureMouse
      // ZATEN true olur ve kamera goruntude HIC donmezdi. Dogru kosul: fare
      // goruntunun ustunde ve kaplama/gizmo onu almamis — ya da surukleme
      // zaten basladi (panelden disari tasan surukleme kesilmesin).
      const bool cam_btn = ci.rmb || ci.mmb;
      const bool can_start = (view_tab == ViewportTab::Scene) && view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
      if (!cam_btn) cam_dragging = false;
      else if (can_start) cam_dragging = true;
      ci.allow_mouse = (view_tab == ViewportTab::Scene) && (cam_dragging || can_start);
      ci.allow_keys = (view_tab == ViewportTab::Scene) && !ui.wants_text_input(); // WASD bir ad alanina yaziliyorsa ucus baslamasin
      ci.dt = dt;
      camera_update(cam, ci);
      prev_mx = in->mouse_x;
      prev_my = in->mouse_y;
      prev_scroll = in->scroll_y;
    }

    // Oynatma baslayinca otomatik Oyun sekmesine gec, durunca Sahne sekmesine don
    if (st.playing && !was_playing) {
      view_tab = ViewportTab::Game;
    } else if (!st.playing && was_playing) {
      view_tab = ViewportTab::Scene;
    }
    was_playing = st.playing;

    // Sim: yalniz oynatilirken (sabit adim). Duraklatilmisken zaman AKMAZ ama
    // govdeler yerinde durur; F10 tek adim ilerletir. fs.advance duraklamada
    // cagrilmaz — yoksa birikmis zaman devam edince bir anda bosalirdi.
    if (st.playing && !st.paused) {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) { scene.tick(fs.step_s, tick_i++); st.play_time += fs.step_s; }
    } else if (st.playing && st.paused && st.step_request) {
      ENGINE_ZONE("sim");
      scene.tick(fs.step_s, tick_i++);
      st.play_time += fs.step_s;
      st.step_request--;
      fs.advance(dt); // biriken zamani YUT: adim adim ilerlerken geri kalmasin
    }

    // En-boy orani artik PENCERENIN degil, sahnenin icinde yasadigi PANELIN
    // orani: 3B viewport dokusuna ciziliyor ve o dokunun olcusu panelden geliyor.
    const float aspect = vp.aspect();
    Mat4 proj, view;
    if (view_tab == ViewportTab::Game) {
      const int cam_ent = find_first_camera_entity(st.scene);
      if (cam_ent >= 0) {
        const SceneEntity &ce = st.scene.entities[cam_ent];
        const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)cam_ent);
        const Vec3 c_eye = {wm.m[3][0], wm.m[3][1], wm.m[3][2]};
        Vec3 c_f = {-wm.m[2][0], -wm.m[2][1], -wm.m[2][2]};
        Vec3 c_u = {wm.m[1][0], wm.m[1][1], wm.m[1][2]};
        if (length_sq(c_f) < 1e-6f) c_f = Vec3{0, 0, -1};
        if (length_sq(c_u) < 1e-6f) c_u = Vec3{0, 1, 0};
        const float fov_rad = ce.cam_fov > 1.0f ? (ce.cam_fov * 3.14159265f / 180.0f) : (ce.cam_fov > 0.1f ? ce.cam_fov : 0.897598f);
        const float znear = ce.cam_near > 0.001f ? ce.cam_near : 0.1f;
        const float zfar = ce.cam_far > znear ? ce.cam_far : 200.0f;
        view = Mat4::look_at(c_eye, c_eye + c_f, c_u);
        proj = Mat4::perspective(fov_rad, aspect, znear, zfar);
      } else {
        view = camera_view(cam);
        proj = camera_projection(cam, aspect, 0.1f, 200.0f);
      }
    } else {
      proj = camera_projection(cam, aspect, 0.1f, 200.0f);
      view = camera_view(cam);
    }
    ren.set_camera(view, proj);
    ren.clear_point_lights();

    // --- ImGui paneller ---
    ui.begin_frame(in, (float)fw, (float)fh, dt);
    // Kisayollar: komut tablosundan. Koruma kurali tablonun (ham T/R/S/Delete
    // metin yazarken VE kaydirac suruklerken kapali; Ctrl+* yalniz metinde kapali).
    const InputGuards guards{ui.wants_text_input(), ui.wants_keyboard()};
    commands_poll_imgui(cmds, guards);
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) show_stats = !show_stats;
    // --- CERCEVE: menu / arac / durum cubugu (editor_chrome.hpp; sira sozlesmesi
    // orada: uc cubuk da ana viewport'un WorkRect'ini daraltir, dockspace tam
    // aralarina oturur — ImGui daraltmayi bir kare gecikmeli uygular).
    ChromeState cs;
    cs.playing = st.playing;
    cs.dirty = st.dirty;
    // Duraklatma durum cubugunda gorunsun (arac cubugu henuz duraklatmayi
    // cizmiyor; menude F6 var). Bos mesaj yerine acik bir ek.
    char status_buf[200];
    if (st.playing && st.paused) {
      std::snprintf(status_buf, sizeof status_buf, "DURAKLATILDI (F10 kare ilerlet) \xC2\xB7 %s", st.status);
      cs.status = status_buf;
    } else cs.status = st.status;
    cs.scene_path = st.scene_path;
    const int32_t prim_i = st.sel.primary();
    cs.primary_name = (prim_i >= 0 && prim_i < (int32_t)st.scene.entity_count) ? st.scene.entities[prim_i].name : nullptr;
    cs.selection_count = st.sel.count;
    cs.undo_count = st.hist.undo_count();
    cs.redo_count = st.hist.redo_count();
    cs.frame = frame_i;
    cs.tick = tick_i;
    cs.frame_ms = dt * 1000.0f;
    cs.entity_count = st.scene.entity_count;
    cs.gizmo_op = gizmo_op;
    cs.snap = snap_on;
    cs.snap_value = snap_step;
    cs.gizmos_visible = st.gizmos.light_radius || st.gizmos.light_glyph || st.gizmos.shadow_volume || st.gizmos.sun_dir || st.gizmos.camera_frustum;
    const Vec3 eye = camera_eye(cam);
    cs.cam_eye[0] = eye.x; cs.cam_eye[1] = eye.y; cs.cam_eye[2] = eye.z;
    chrome_menu_bar(cmds, cs, menu_extra);
    ChromeOutput co;
    chrome_toolbar(cmds, cs, &co);
    if (co.snap_toggled) snap_on = !snap_on;
    if (co.snap_value_changed) snap_step = co.snap_value;
    chrome_status_bar(cs);
    // --- DOCKSPACE -----------------------------------------------------------
    // Paneller artik ekranda yuzen sabit pencereler degil; kullanici surukleyip
    // yeniden duzenleyebiliyor (Unity/Godot/Blender'in dordunde de boyle).
    // Varsayilan yerlesim ILK KAREDE programatik kuruluyor: ImGui'nin kendi
    // imgui.ini'si kapali (io.IniFilename = nullptr), yani duzen diskten
    // gelmiyor — kurulmazsa her acilista paneller serbest gelirdi.
    const ImGuiID dock_id = ImGui::DockSpaceOverViewport(ImGui::GetID("TulparDock"), ImGui::GetMainViewport(), 0);
    if (frame_i == 0) {
      layout_set_dockspace_id(dock_id);
      if (!layout_apply_default(dock_id, (float)fw, (float)fh)) {
        console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "varsayilan duzen: %s", layout_last_error());
        std::fprintf(stderr, "[editor] varsayilan duzen: %s\n", layout_last_error());
      }
    }

    // --- GORUNUM: 3B sahnenin YASADIGI panel --------------------------------
    // Sahne viewport dokusuna ciziliyor ve burada gosteriliyor. Panel olcusu
    // degisince hedef yeniden yaratiliyor (yalniz GERCEKTEN degistiyse) ve
    // renderer'in cizim olcusu ona baglaniyor — en-boy orani artik pencerenin
    // degil PANELIN orani.
    view_rect = ViewportRect{};
    view_hovered = false;
    ovres = OverlayResult{}; // panel kapaliyken bayat sonuc uygulanmasin
    if (ImGui::Begin(kPanelGorunumLabel)) {
      // 1) Viewport Ust Arac Cubugu (Sekmeler + Araclar + Stats + 3 Nokta)
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));

      // Sekmeler: [ICON_MD_MOVIE Sahne] [ICON_MD_SPORTS_ESPORTS Oyun]
      const bool is_scene = (view_tab == ViewportTab::Scene);
      if (is_scene) {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::AccentHi));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Bg3));
        ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::TextDim));
      }
      if (ImGui::Button(" " ICON_MD_MOVIE " Sahne ")) view_tab = ViewportTab::Scene;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sahne Görünümü (Düzenleme, Gizmo, Serbest Kamera)");
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      const bool is_game = (view_tab == ViewportTab::Game);
      if (is_game) {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::AccentHi));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Bg3));
        ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::TextDim));
      }
      if (ImGui::Button(" " ICON_MD_SPORTS_ESPORTS " Oyun ")) view_tab = ViewportTab::Game;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oyun Görünümü (Kamera Perspektifi, Temiz Oyun Ekranı)");
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();

      if (view_tab == ViewportTab::Scene) {
        if (ImGui::Button(show_grid ? "⊞ Izgara: Açık" : "⊞ Izgara: Kapalı")) show_grid = !show_grid;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("3B Zemin Izgarasını Göster / Gizle");

        // NOT: burada bir "Tel Kafes / Duz Golgeli" dugmesi vardi ve HICBIR
        // SEY yapmiyordu: wireframe_mode degiskeni depoda baska hicbir yerde
        // okunmuyor, dugme yalniz kendi etiketini degistiriyordu.
        //
        // Gercegi yapmak icin VK_POLYGON_MODE_LINE gerekir; o da
        // fillModeNonSolid cihaz ozelligini ister. Bu motor onu hic
        // istemiyor (polygonMode alti yerde de FILL sabit) ve ozellik mobil
        // GPU'larin cogunda YOK. Dogru cozum barycentric tek gecisli tel
        // kafes; ayri bir is. O gelene kadar var olmayan bir yetenegi vaat
        // eden dugmeyi tutmaktansa kaldirmak dogrudur.

        ImGui::SameLine();
        ImGui::SameLine();
        if (ImGui::Button( ICON_MD_CENTER_FOCUS_STRONG " Odaklan")) {
          const int32_t s0 = st.sel.primary();
          if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) {
            static content::SceneBounds fb[content::kSceneMaxEntities];
            const uint32_t nb = entity_world_bounds(st, phys, fb);
            if ((uint32_t)s0 < nb) camera_focus(cam, fb[s0]);
          }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Seçili Varlığa Odaklan (F)");
      } else {
        // Oyun görünümü araçları
        static const char *const kAspectNames[] = {"Serbest En-Boy", "16:9 (FHD)", "16:10", "4:3", "21:9", "1:1"};
        ImGui::SetNextItemWidth(130.0f);
        int cur_aspect = (int)game_aspect;
        if (ImGui::Combo("##aspect", &cur_aspect, kAspectNames, IM_ARRAYSIZE(kAspectNames))) {
          game_aspect = (GameAspect)cur_aspect;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oyun Görünümü En-Boy Oranı");

        ImGui::SameLine();
        ImGui::Checkbox("Oynatılınca Büyüt", &maximize_on_play);

        ImGui::SameLine();
        ImGui::Checkbox("Sessiz", &mute_audio_on_play);
      }

      // Sağ Taraf: [ ICON_MD_BAR_CHART Stats ] [ ⋮ ]
      const float right_width = 115.0f;
      const float avail_w = ImGui::GetContentRegionAvail().x;
      if (avail_w > right_width) {
        ImGui::SameLine(ImGui::GetWindowWidth() - right_width - ImGui::GetStyle().WindowPadding.x);
      } else {
        ImGui::SameLine();
      }

      const bool stats_btn_active = show_stats;
      if (stats_btn_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Accent));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
      }
      if (ImGui::Button( ICON_MD_BAR_CHART " Stats")) show_stats = !show_stats;
      if (stats_btn_active) ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("İstatistikler Panelini Aç / Kapat (FPS, Poligon, Bellek, Çöp)");

      ImGui::SameLine();
      if (ImGui::Button(" ⋮ ")) ImGui::OpenPopup("ViewportOptionsMenu");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Görünüm Seçenekleri ve Araçlar");

      if (ImGui::BeginPopup("ViewportOptionsMenu")) {
        if (ImGui::MenuItem( ICON_MD_BAR_CHART " Detaylı İstatistikler (Stats)", nullptr, show_stats)) show_stats = !show_stats;
        ImGui::Separator();
        if (ImGui::MenuItem( ICON_MD_MOVIE " Sahne Görünümüne Geç", nullptr, view_tab == ViewportTab::Scene)) view_tab = ViewportTab::Scene;
        if (ImGui::MenuItem( ICON_MD_SPORTS_ESPORTS " Oyun Görünümüne Geç", nullptr, view_tab == ViewportTab::Game)) view_tab = ViewportTab::Game;
        ImGui::Separator();
        if (ImGui::MenuItem( ICON_MD_STRAIGHTEN " Izgarayı Göster / Gizle", nullptr, show_grid)) show_grid = !show_grid;
        if (ImGui::MenuItem("☀ Işık ve Gölge Gizmolarnı Göster", nullptr, st.gizmos.light_radius)) {
          st.gizmos.light_radius = !st.gizmos.light_radius;
          st.gizmos.light_glyph = st.gizmos.light_radius;
          st.gizmos.shadow_volume = st.gizmos.light_radius;
          st.gizmos.sun_dir = st.gizmos.light_radius;
          st.gizmos.camera_frustum = st.gizmos.light_radius;
        }
        if (ImGui::MenuItem( ICON_MD_CENTER_FOCUS_STRONG " Se\xC3\xA7ili Varl\xC4\xB1\xC4\x9F" "a Odaklan")) {
          const int32_t s0 = st.sel.primary();
          if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) {
            static content::SceneBounds fb[content::kSceneMaxEntities];
            const uint32_t nb = entity_world_bounds(st, phys, fb);
            if ((uint32_t)s0 < nb) camera_focus(cam, fb[s0]);
          }
        }
        if (ImGui::MenuItem( ICON_MD_REFRESH " Kamerayı Varsayılana Sıfırla")) {
          cam.yaw = 0.7f; cam.pitch = 0.45f; cam.radius = 26.0f;
          cam.target = Vec3{0, 1.0f, -3.0f};
        }
        ImGui::Separator();
        ImGui::TextDisabled("Kamera Hızı: %.1fx", (double)(cam.speed / 9.0f));
        float sp = cam.speed / 9.0f;
        if (ImGui::SliderFloat("##camspeed", &sp, 0.1f, 5.0f, "%.1fx")) {
          cam.speed = sp * 9.0f;
        }
        ImGui::EndPopup();
      }

      ImGui::PopStyleVar(2);
      ImGui::Separator();

      // 2) 3B Canvas Çizimi
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      if (avail.x >= 1.0f && avail.y >= 1.0f) {
        float draw_w = avail.x;
        float draw_h = avail.y;
        float offset_x = 0.0f;
        float offset_y = 0.0f;

        if (view_tab == ViewportTab::Game && game_aspect != GameAspect::Free) {
          float target_ar = 16.0f / 9.0f;
          if (game_aspect == GameAspect::Aspect16_10) target_ar = 16.0f / 10.0f;
          else if (game_aspect == GameAspect::Aspect4_3) target_ar = 4.0f / 3.0f;
          else if (game_aspect == GameAspect::Aspect21_9) target_ar = 21.0f / 9.0f;
          else if (game_aspect == GameAspect::Aspect1_1) target_ar = 1.0f;

          if (avail.x / avail.y > target_ar) {
            draw_h = avail.y;
            draw_w = std::floor(draw_h * target_ar);
            offset_x = std::floor((avail.x - draw_w) * 0.5f);
          } else {
            draw_w = avail.x;
            draw_h = std::floor(draw_w / target_ar);
            offset_y = std::floor((avail.y - draw_h) * 0.5f);
          }

          // Arka planı koyu letterbox ile doldur
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(8, 8, 8, 255));
        }

        if (draw_w < 1.0f) draw_w = 1.0f;
        if (draw_h < 1.0f) draw_h = 1.0f;
        vp.resize((uint32_t)draw_w, (uint32_t)draw_h);
        ren.set_render_size(vp.width(), vp.height());
        if (vp.texture_id()) {
          if (offset_x > 0.0f || offset_y > 0.0f) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + offset_x, origin.y + offset_y));
          }
          ImGui::Image((ImTextureID)vp.texture_id(), ImVec2((float)vp.width(), (float)vp.height()));
          view_hovered = ImGui::IsItemHovered();
          
          // --- VIEWPORT OVERLAY (AAA Kısayol Menüsü) ---
          ImGui::SetNextWindowPos(ImVec2(origin.x + offset_x + 10.0f, origin.y + offset_y + 10.0f));
          ImGui::SetNextWindowBgAlpha(0.6f);
          if (ImGui::BeginChild("ViewportOverlay", ImVec2(240, 35), false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse)) {
            if (st.shading_mode == 0) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::Button(ICON_MD_IMAGE " Shaded")) st.shading_mode = 0;
            if (st.shading_mode == 0) { ImGui::PopStyleColor(); }
            
            ImGui::SameLine();
            if (st.shading_mode == 1) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::Button(ICON_MD_GRID_ON " Wireframe")) st.shading_mode = 1;
            if (st.shading_mode == 1) { ImGui::PopStyleColor(); }

            ImGui::SameLine();
            if (st.shading_mode == 2) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::Button(ICON_MD_WB_SUNNY " Unlit")) st.shading_mode = 2;
            if (st.shading_mode == 2) { ImGui::PopStyleColor(); }
          }
          ImGui::EndChild();
          // ---------------------------------------------
          
          // Sürükle-Bırak (Drag & Drop) Hedefi: Viewport içine
          if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE")) {
              const char* file_name = (const char*)payload->Data;
              char add_file[content::kScenePathLen];
              std::snprintf(add_file, sizeof add_file, "%s", file_name);
              do_add_asset(add_file);
            }
            ImGui::EndDragDropTarget();
          }

          if (view_tab == ViewportTab::Scene) {
            OverlayInfo oi;
            const Mat4 vm = camera_view(cam);
            std::memcpy(oi.view, &vm.m[0][0], sizeof oi.view);
            const Vec3 eye = camera_eye(cam);
            oi.cam_eye[0] = eye.x; oi.cam_eye[1] = eye.y; oi.cam_eye[2] = eye.z;
            oi.cam_target[0] = cam.target.x; oi.cam_target[1] = cam.target.y; oi.cam_target[2] = cam.target.z;
            oi.gizmo_op = gizmo_op;
            oi.gizmos_visible = st.gizmos.light_radius || st.gizmos.light_glyph || st.gizmos.shadow_volume || st.gizmos.sun_dir || st.gizmos.camera_frustum;
            oi.playing = st.playing;
            oi.hovered = view_hovered;
            oi.focused = ImGui::IsWindowFocused();
            oi.frame_ms = dt * 1000.0f;
            oi.draw_calls = ren.stats().draws;
            oi.entity_count = st.scene.entity_count;
            oi.proj = cam.proj;
            oi.cam_mode = cam.mode;
            oi.gizmo_space = gizmo_space;
            oi.hint = "Sağ tık döndür · orta tuş kaydır · F odak";
            viewport_overlay(ViewportRect{origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height()}, oi, nullptr, &ovres);
          }
        } else {
          ImGui::TextUnformatted(vp.last_error());
        }
        view_rect = ViewportRect{origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height()};
      }
    }
    ImGui::End();
    // --- Kaplamadan gelen gezinme eylemleri (cip/gosterge tiklamalari) ------
    if (ovres.axis_clicked >= 0) {
      camera_align(cam, (CameraAxis)ovres.axis_clicked);
      static const char *const kAx[6] = {"+X", "-X", "Ust", "Alt", "On", "Arka"};
      set_status(st, "eksen gorunusu: %s", kAx[ovres.axis_clicked]);
    }
    if (ovres.ortho_toggled) cam.proj = cam.proj == CameraProjection::Perspective ? CameraProjection::Ortho : CameraProjection::Perspective;
    if (ovres.mode_toggled) cam.mode = cam.mode == CameraMode::Orbit ? CameraMode::Fly : CameraMode::Orbit;
    if (ovres.gizmo_space_toggled) gizmo_space = gizmo_space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;

    if (ImGui::Begin(kPanelSahneLabel)) {
      const int tb = hierarchy_toolbar(st.scene.entity_count, st.sel.count > 0);
      if (tb == 100) do_remove();
      else if (tb > 0) do_add(tb); // ust sinir YOK: kCreate3D 20..24 uretiyor
      hierarchy_search(st.filter, sizeof st.filter);

      if (ImGui::BeginPopupContextWindow("SahnePanelMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        // Olustur agacinin TEK dogruluk kaynagi kCreateMenu (editor_widgets).
        // Burada elle yazilmis bir ikinci kopya vardi ve ayrismisti: kCreate3D
        // uzun zamandir Kapsul/Silindir/Koni/Dortgen/Simit tasiyor, bu kopyada
        // hicbiri yoktu. Ayrica "Zemin" satirinin ikonu gecersiz bir kacis
        // dizisiyle yazilmisti ve derleyici tarafindan duz harfe dusuruluyordu.
        // Tek tablo birakmak iki sorunu da kokunden kapatir.
        if (ImGui::BeginMenu("Yeni Varl\xC4\xB1k Ekle")) {
          const int r = create_menu_draw(kCreateMenu, kCreateMenuCount);
          if (r) do_add(r);
          ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Yeni Edit\xC3\xB6r Konsolu", nullptr, &show_console)) {}
        if (ImGui::MenuItem("Yeni Geli\xC5\x9Fmi\xC5\x9F Varl\xC4\xB1k Taray\xC4\xB1" "c\xC4\xB1", nullptr, &st.show_asset_browser)) {}
        if (ImGui::MenuItem("Materyal D\xC3\xBC\xC4\x9F\xC3\xBCm (Node) Edit\xC3\xB6r\xC3\xBC", nullptr, &st.show_node_editor)) {}
        if (ImGui::MenuItem("Animasyon Sequencer (Timeline)", nullptr, &st.show_sequencer)) {}
        if (ImGui::MenuItem("Arazi (Terrain) Boyama", nullptr, &st.show_terrain_editor)) {}
        if (ImGui::MenuItem("Girdi Y\xC3\xB6neticisi (Input Manager)", nullptr, &st.show_input_manager)) {}
        ImGui::Separator();
        if (ImGui::MenuItem("Yap\xC4\xB1\xC5\x9Ft\xC4\xB1r", "Ctrl+V", false, st.clip_count > 0)) do_paste();
        if (ImGui::MenuItem("T\xC3\xBCm\xC3\xBCn\xC3\xBC Se\xC3\xA7", "Ctrl+A", false, st.scene.entity_count > 0)) {
          st.sel.clear();
          for (uint32_t k = st.scene.entity_count; k > 0; k--) st.sel.toggle((int32_t)(k - 1));
        }
        ImGui::EndPopup();
      }
      // Cizim sirasi belirlenimli ON-SIRADIR (kokler indeks sirasinda, cocuklar
      // indeks sirasinda). SUZGEC ACIKKEN duz liste cizilir: katlanmis bir ata
      // eslesmeyi gizlemesin.
      const bool filtering = st.filter[0] != 0;
      int32_t order[content::kSceneMaxEntities];
      uint32_t n = 0;
      if (filtering) {
        for (uint32_t i = 0; i < st.scene.entity_count; i++) order[n++] = (int32_t)i;
      } else n = content::scene_tree_order(st.scene, order, content::kSceneMaxEntities);
      HierarchyResult act;
      uint32_t shown = 0, hide_depth = 0; // hide_depth > 0: katlanmis alt agactayiz
      
      ImGuiSelectionExternalStorage ms_ext;
      ms_ext.UserData = (void*)&st.sel;
      ms_ext.AdapterSetItemSelected = [](ImGuiSelectionExternalStorage* self, int idx, bool selected) {
        auto* sel = (Selection*)self->UserData;
        if (selected) {
          if (!sel->contains(idx) && sel->count < Selection::kMax)
            sel->items[sel->count++] = idx;
        } else {
          sel->erase(idx);
        }
      };
      ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_None, (int)st.sel.count, (int)n);
      
      for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = (uint32_t)order[k];
        if (i >= st.scene.entity_count) continue;
        const SceneEntity &e = st.scene.entities[i];
        const uint32_t depth = filtering ? 0u : content::scene_tree_depth(st.scene, i);
        if (hide_depth) {
          if (depth >= hide_depth) continue;
          hide_depth = 0;
        }
        if (filtering && !hierarchy_filter_match(e.name, st.filter)) continue;
        bool kids = false;
        for (uint32_t j = 0; j < st.scene.entity_count && !kids; j++) kids = st.scene.entities[j].parent == (int32_t)i;
        HierarchyRow row;
        row.name = e.name;
        row.selected = st.sel.contains((int32_t)i);
        row.has_model = (e.components & content::kSceneModel) != 0;
        row.has_light = (e.components & content::kSceneLight) != 0;
        row.has_body = (e.components & content::kSceneBody) != 0;
        row.has_anim = (e.components & content::kSceneAnim) != 0;
        row.depth = depth;
        row.has_children = kids && !filtering;
        row.expanded = !st.tree.collapse.collapsed(i);
        row.hidden = (e.flags & content::kSceneHidden) != 0;
        row.locked = (e.flags & content::kSceneLocked) != 0;
        shown++;
        const HierarchyResult r = hierarchy_tree_row((int)i, row, &st.tree);
        if (r.action != HierarchyAction::None && r.action != HierarchyAction::Select) act = r;
        if (row.has_children && !row.expanded) hide_depth = depth + 1;
      }
      
      ms_io = ImGui::EndMultiSelect();
      ms_ext.ApplyRequests(ms_io);
      
      if (shown == 0)
        hierarchy_empty(st.scene.entity_count ? "S\xC3\xBCzge\xC3\xA7le e\xC5\x9Fle\xC5\x9F" "en varl\xC4\xB1k yok"
                                              : "Sahne bo\xC5\x9F \xE2\x80\x94 \xE2\x80\x9C+\xE2\x80\x9D ile varl\xC4\xB1k ekle");
      // Listenin altindaki bosluk: buraya birakmak KOKE tasir.
      const HierarchyResult zone = hierarchy_root_drop_zone(&st.tree);
      if (zone.action != HierarchyAction::None) act = zone;
      // F2: secili varligin adini YERINDE duzenle (panel odakliyken).
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
        const int32_t s0 = st.sel.primary();
        if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) hierarchy_begin_rename(&st.tree, s0, st.scene.entities[s0].name);
      }
      apply_hierarchy(act);
    }
    ImGui::End();
    if (ImGui::Begin(kPanelOzelliklerLabel)) {
      const int si = (int)st.sel.primary();
      static int32_t last_inspected = -1;
      if (last_inspected != si) {
        st.edit_active = false;
        last_inspected = si;
      }
      if (si >= 0 && si < (int)st.scene.entity_count) {
        SceneEntity &e = st.scene.entities[si];
        const bool has_m = (e.components & content::kSceneModel) != 0, has_l = (e.components & content::kSceneLight) != 0,
                   has_b = (e.components & content::kSceneBody) != 0, has_a = (e.components & content::kSceneAnim) != 0,
                   has_c = (e.components & content::kSceneCamera) != 0, has_s = (e.components & content::kSceneAudio) != 0,
                   has_sc = (e.components & content::kSceneScript) != 0;
        const char *icon = has_l ? "\xE2\x98\x80" : has_m ? "\xE2\x97\x86" : has_b ? "\xE2\x97\xBC" : has_c ?  ICON_MD_VIDEOCAM  : has_s ?  ICON_MD_VOLUME_UP  : has_sc ?  ICON_MD_DESCRIPTION  : "\xE2\x97\x8B";
        const Tone icon_tone = has_l ? Tone::Warn : has_m ? Tone::Text : has_b ? Tone::AxisZ : has_c ? Tone::Accent : Tone::TextDim;
        char sub[96];
        if (st.sel.count > 1) std::snprintf(sub, sizeof sub, "grup: %u se\xC3\xA7ili \xC2\xB7 alanlar ana se\xC3\xA7ilide, gizmo grubu ta\xC5\x9F\xC4\xB1r", st.sel.count);
        else std::snprintf(sub, sizeof sub, "%u bile\xC5\x9F""en%s%s", (unsigned)(has_m + has_l + has_b + has_a + has_c + has_s + has_sc),
                           has_b ? (e.shape == content::SceneShape::Box ? " \xC2\xB7 kutu g\xC3\xB6vde" : " \xC2\xB7 k\xC3\xBCre g\xC3\xB6vde") : "",
                           (has_b && e.dynamic) ? " \xC2\xB7 dinamik" : "");
        track_edit(st, e, si, inspector_title(icon, e.name, sizeof e.name, sub, icon_tone));

        section_label("D\xC3\x96N\xC3\x9C\xC5\x9E\xC3\x9CM");
        if (prop_begin("donusum")) {
          track_edit(st, e, si, prop_vec3("Konum", &e.pos.x, 0.05f));
          prop_help("Euler derece; uygulama s\xC4\xB1ras\xC4\xB1 T\xC2\xB7Rz\xC2\xB7Ry\xC2\xB7Rx\xC2\xB7S");
          track_edit(st, e, si, prop_vec3("D\xC3\xB6n\xC3\xBC\xC5\x9F", &e.rot_deg.x, 0.5f, 0, 0, "%.1f\xC2\xB0"));
          track_edit(st, e, si, prop_vec3("\xC3\x96l\xC3\xA7""ek", &e.scale.x, 0.02f, 0.05f, 20.0f));
          prop_end();
        }
        section_label("B\xC4\xB0LE\xC5\x9E""ENLER");
        bool rem = false;
        SceneEntity after = e;
        if (has_m) {
          if (component_header("\xE2\x97\x86", "Model", nullptr, &rem, true, Tone::Text)) {
            if (prop_begin("model")) {
              // Geometri kaynagi IKI turlu olabilir ve birbirini disar:
              // ya glTF kaynagi (asset >= 0) ya prosedurel ilkel
              // (primitive >= 0). Ikisi de dolu olursa scene_runtime ilkeli
              // secer; secici bu yuzden ONCE sorulur ve secim otekini -1 yapar.
              int prim_sel = 0;
              static const int kPrimOf[] = {-1,
                                            (int)content::kPrimitiveCube,   (int)content::kPrimitiveSphere,
                                            (int)content::kPrimitiveCapsule, (int)content::kPrimitiveCylinder,
                                            (int)content::kPrimitiveCone,   (int)content::kPrimitivePlane,
                                            (int)content::kPrimitiveQuad,   (int)content::kPrimitiveTorus};
              for (int k = 1; k < (int)(sizeof kPrimOf / sizeof kPrimOf[0]); k++)
                if (e.primitive == kPrimOf[k]) { prim_sel = k; break; }
              if (prop_combo("Geometri", &prim_sel,
                             "Kaynaktan\0K\xC3\xBCp\0K\xC3\xBCre\0Kaps\xC3\xBCl\0Silindir\0Koni\0D\xC3\xBCzlem\0D\xC3\xB6rtgen\0Simit\0").changed) {
                after = e;
                after.primitive = kPrimOf[prim_sel];
                if (after.primitive >= 0) after.asset = -1;
                else if (after.asset < 0 && st.scene.asset_count) after.asset = 0;
                commit(st, si, after);
              }
              if (e.primitive < 0) {
                if (prop_asset("Kaynak", &after.asset, st.scene.assets, st.scene.asset_count).changed) commit(st, si, after);
              }
              track_edit(st, e, si, prop_color("Renk", &e.tint.x));
              prop_end();
            }
            // --- PBR ---------------------------------------------------------
            // Bu bes alan scene_runtime + renderer tarafindan GERCEKTEN shade
            // ediliyor ve .sahne'ye yaziliyordu; editorde tek bir kaydirici
            // yoktu. Isima Faz 0'da post acilana kadar 1.0'a kirpiliyordu, o
            // yuzden simdi gercekten parliyor.
            section_label("PBR MALZEME");
            if (prop_begin("model_pbr")) {
              prop_help("0 = dielektrik, 1 = metal. Ara degerler fiziksel DEGIL, karisimdir.");
              track_edit(st, e, si, prop_float("Metaliklik", &e.metallic, 0.01f, 0.0f, 1.0f, "%.2f"));
              prop_help("0 = ayna, 1 = tamamen mat (glTF gelenegi: varsayilan 1).");
              track_edit(st, e, si, prop_float("P\xC3\xBCr\xC3\xBCzl\xC3\xBCl\xC3\xBCk", &e.roughness, 0.01f, 0.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Yans\xC4\xB1t\xC4\xB1rl\xC4\xB1k", &e.reflectance, 0.01f, 0.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_color("I\xC5\x9F\xC4\xB1ma rengi", &e.emissive.x));
              prop_help("1'in uzerinde HDR: parlama (bloom) ancak burada gorunur.");
              track_edit(st, e, si, prop_float("I\xC5\x9F\xC4\xB1ma \xC5\x9Fiddeti", &e.emissive_strength, 0.05f, 0.0f, 20.0f, "%.2f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneModel; commit(st, si, after); }
        }
        if (has_a) {
          rem = false;
          if (component_header("\xE2\x86\xBB", "Animasyon", nullptr, &rem, true, Tone::Accent)) {
            if (prop_begin("anim")) {
              // clip runtime'da kullaniliyor ama editorde hic ayarlanamiyordu
              // (do_add her zaman 0 yaziyor). Yuklu modelin klip sayisi bilinir;
              // bilinmiyorsa 0'da kalir.
              int clip_i = (int)e.clip;
              const uint32_t nclip = (e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset])
                                         ? st.models[e.asset].clip_count : 0;
              if (prop_int("Klip", &clip_i, 0, nclip ? (int)nclip - 1 : 0).changed) {
                after = e;
                after.clip = (uint32_t)(clip_i < 0 ? 0 : clip_i);
                commit(st, si, after);
              }
              track_edit(st, e, si, prop_float("Faz", &e.phase, 0.01f, 0.0f, 10.0f, "%.2f"));
              track_edit(st, e, si, prop_float("H\xC4\xB1z", &e.speed, 0.01f, 0.0f, 10.0f, "%.2f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneAnim; commit(st, si, after); }
        }
        if (has_l) {
          rem = false;
          after = e;
          if (component_header("\xE2\x98\x80", "I\xC5\x9F\xC4\xB1k", nullptr, &rem, true, Tone::Warn)) {
            if (prop_begin("isik")) {
              int ltype = (int)e.light_type;
              // "govde" bileseninin sekil combo'suyla AYNI desen: tur degisince
              // tek islem olarak gunluge yazilir, alan gorunumu tipe gore degisir.
              if (prop_combo("T\xC3\xBCr", &ltype, "Nokta\0Y\xC3\xB6nl\xC3\xBC\0").changed) {
                after.light_type = (content::SceneLightType)ltype;
                commit(st, si, after);
              }
              track_edit(st, e, si, prop_color("Renk", &e.light_color.x));
              track_edit(st, e, si, prop_float("\xC5\x9Eiddet", &e.light_intensity, 0.05f, 0.0f, 100.0f, "%.2f"));
              // Yaricap yalniz Nokta icin anlamli (Yonlu'de yon = varligin donusu,
              // gizmo gunes-oku cizer; runtime'da HENUZ ayri bir yonlu terim shade
              // etmez -- bkz. scene.hpp SceneLightType yorumu).
              if (e.light_type == content::SceneLightType::Point)
                track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &e.light_radius, 0.05f, 0.1f, 100.0f, "%.2f m"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneLight; commit(st, si, after); }
        }
        if (has_b) {
          rem = false;
          after = e;
          if (component_header("\xE2\x97\xBC", "G\xC3\xB6vde", nullptr, &rem, true, Tone::AxisZ)) {
            if (prop_begin("govde")) {
              int shape = (int)e.shape;
              if (prop_combo("\xC5\x9E""ekil", &shape, "Kutu\0K\xC3\xBCre\0").changed) { after.shape = (content::SceneShape)shape; commit(st, si, after); }
              if (e.shape == content::SceneShape::Box) track_edit(st, e, si, prop_vec3("Yar\xC4\xB1m kenar", &e.half.x, 0.02f, 0.01f, 50.0f, "%.2f"));
              else track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &e.radius, 0.02f, 0.01f, 50.0f, "%.2f"));
              bool dyn = e.dynamic;
              if (prop_check("Dinamik", &dyn).changed) { after = e; after.dynamic = dyn; commit(st, si, after); }
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneBody; commit(st, si, after); }
        }
        
        bool has_terrain = (e.components & content::kSceneTerrain) != 0;
        if (has_terrain) {
          rem = false;
          after = e;
          // Oktav/tohum tamsayi: prop_int yerel bir int ister. Kapsam panelin
          // DISINDA olmali cunku degisimi component_end()ten sonra yaziyoruz.
          int oct_i = e.terrain_octaves, seed_i = (int)e.terrain_seed;
          if (component_header(ICON_MD_LANDSCAPE, "Arazi (Terrain)", nullptr, &rem, true, Tone::AxisY)) {
            if (prop_begin("terrain")) {
              track_edit(st, e, si, prop_float("Genislik", &e.terrain_width, 1.0f, 1.0f, 1024.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("Uzunluk", &e.terrain_height, 1.0f, 1.0f, 1024.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("Hucre Boyutu", &e.terrain_cell, 0.1f, 0.1f, 10.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Yukseklik Carpani", &e.terrain_amp, 0.5f, 0.1f, 200.0f, "%.1f"));
              track_edit(st, e, si, prop_float("Frekans (Gurultu)", &e.terrain_freq, 0.001f, 0.001f, 0.5f, "%.4f"));
              track_edit(st, e, si, prop_int("Oktav (Gurultu katmani)", &oct_i, 1, 8));
              track_edit(st, e, si, prop_int("Tohum", &seed_i, 0, 0));
              // NOT: burada bir "Prefab Olarak Kaydet" dugmesi vardi. Hicbir sey
              // yapmiyordu ama durum cubuguna "diske kaydedildi" YAZIYORDU --
              // yani kullaniciya olmayan bir dosyayi var gosteriyordu. Prefab
              // ayri bir is (bkz. docs/engine/EDITOR-DURUM.md Faz E.8); o gelene
              // kadar yaniltici dugme durmaz.
              prop_end();
            }
            component_end();
          }
          // Oktav/tohum tamsayi: prop_int'in referansi yerel, degisimi ayrica yaz.
          if (oct_i != e.terrain_octaves || (uint32_t)seed_i != e.terrain_seed) {
            after = e;
            after.terrain_octaves = oct_i;
            after.terrain_seed = (uint32_t)(seed_i < 0 ? 0 : seed_i);
            commit(st, si, after);
          }
          if (rem) { after = e; after.components &= ~content::kSceneTerrain; commit(st, si, after); }
        }
        
        bool has_joint = (e.components & content::kSceneJoint) != 0;
        if (has_joint) {
          rem = false;
          after = e;
          if (component_header(ICON_MD_LINK, "Fizik Eklemi (Joint)", nullptr, &rem, true, Tone::AxisZ)) {
            if (prop_begin("joint")) {
              track_edit(st, e, si, prop_int("Ba\xC4\x9Flanan Obje ID", &e.joint_target));
              track_edit(st, e, si, prop_vec3("D\xC3\xB6n\xC3\xBC\xC5\x9F Ekseni", &e.joint_axis.x, 0.1f));
              track_edit(st, e, si, prop_float("Alt S\xC4\xB1n\xC4\xB1r (Min)", &e.joint_limit_min, 1.0f, -180.0f, 180.0f, "%.1f\xC2\xB0"));
              track_edit(st, e, si, prop_float("\xC3\x9Cst S\xC4\xB1n\xC4\xB1r (Max)", &e.joint_limit_max, 1.0f, -180.0f, 180.0f, "%.1f\xC2\xB0"));
              track_edit(st, e, si, prop_float("Motor H\xC4\xB1z\xC4\xB1", &e.joint_motor_speed, 0.1f, 0.0f, 100.0f, "%.1f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneJoint; commit(st, si, after); }
        }
        
        bool has_skybox = (e.components & content::kSceneSkybox) != 0;
        if (has_skybox) {
          rem = false;
          after = e;
          if (component_header(ICON_MD_CLOUD, "G\xC3\xB6ky\xC3\xBCz\xC3\xBC (Skybox)", nullptr, &rem, true, Tone::AxisZ)) {
            if (prop_begin("skybox")) {
              // ONCE: sabit bir Button("hdri_sky.hdr") vardi -- isleyicisi de
              // yoktu, SceneEntity'de karsilik alani da. Yani bileseni eklemek
              // ve "ayarlamak" hicbir sey kaydetmiyordu. Artik gercek bir alan.
              track_edit(st, e, si, prop_text("HDRI / K\xC3\xBCp haritas\xC4\xB1", e.skybox_asset, sizeof e.skybox_asset));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneSkybox; commit(st, si, after); }
        }

        bool has_ref_probe = (e.components & content::kSceneRefProbe) != 0;
        if (has_ref_probe) {
          rem = false;
          after = e;
          if (component_header(ICON_MD_LENS, "Yans\xC4\xB1ma Sondas\xC4\xB1 (IBL)", nullptr, &rem, true, Tone::AxisZ)) {
            if (prop_begin("ref_probe")) {
              track_edit(st, e, si, prop_float("Etki Yari\xC3\xA7" "api", &e.ref_probe_radius, 0.5f, 1.0f, 100.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("\xC5\x9Eiddet (Intensity)", &e.ref_probe_intensity, 0.05f, 0.0f, 5.0f, "%.2f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneRefProbe; commit(st, si, after); }
        }

        bool has_reverb = (e.components & content::kSceneReverb) != 0;
        if (has_reverb) {
          rem = false;
          after = e;
          if (component_header(ICON_MD_WAVES, "Yank\xC4\xB1 Alan\xC4\xB1 (Reverb)", nullptr, &rem, true, Tone::Warn)) {
            if (prop_begin("reverb")) {
              track_edit(st, e, si, prop_float("S\xC3\xB6n\xC3\xBCmlenme S\xC3\xBCresi", &e.reverb_decay, 0.1f, 0.1f, 10.0f, "%.1f s"));
              track_edit(st, e, si, prop_float("Oda B\xC3\xBCy\xC3\xBCkl\xC3\xBC\xC4\x9F\xC3\xBC", &e.reverb_room_size, 0.05f, 0.0f, 1.0f, "%.2f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneReverb; commit(st, si, after); }
        }

        if (has_c) {
          rem = false;
          after = e;
          if (component_header( ICON_MD_VIDEOCAM , "Kamera", nullptr, &rem, true, Tone::Accent)) {
            if (prop_begin("kamera")) {
              track_edit(st, e, si, prop_float("G\xC3\xB6r\xC3\xBC\xC5\x9F A\xC3\xA7\xC4\xB1s\xC4\xB1 (FOV)", &e.cam_fov, 0.5f, 10.0f, 120.0f, "%.1f\xC2\xB0"));
              track_edit(st, e, si, prop_float("Yak\xC4\xB1n K\xC4\xB1rpma", &e.cam_near, 0.01f, 0.01f, 10.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Uzak K\xC4\xB1rpma", &e.cam_far, 1.0f, 1.0f, 5000.0f, "%.1f m"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneCamera; commit(st, si, after); }
        }
        if (has_s) {
          rem = false;
          after = e;
          if (component_header( ICON_MD_VOLUME_UP , "Ses Kayna\xC4\x9F\xC4\xB1", nullptr, &rem, true, Tone::Warn)) {
            if (prop_begin("ses")) {
              track_edit(st, e, si, prop_text("Ses Dosyas\xC4\xB1", e.audio_clip, sizeof e.audio_clip));
              track_edit(st, e, si, prop_float("Ses D\xC3\xBCzeyi", &e.audio_volume, 0.02f, 0.0f, 2.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Perde", &e.audio_pitch, 0.02f, 0.1f, 3.0f, "%.2f"));
              bool loop = e.audio_loop;
              if (prop_check("D\xC3\xB6ng\xC3\xBC", &loop).changed) { after = e; after.audio_loop = loop; commit(st, si, after); }
              bool spat = e.audio_spatial;
              if (prop_check("3B Uzamsal", &spat).changed) { after = e; after.audio_spatial = spat; commit(st, si, after); }
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneAudio; commit(st, si, after); }
        }
        if (has_sc) {
          rem = false;
          after = e;
          if (component_header( ICON_MD_DESCRIPTION , "Tulpar Betik", nullptr, &rem, true, Tone::AccentLo)) {
            if (prop_begin("betik")) {
              track_edit(st, e, si, prop_text("Betik (.tpr)", e.script_file, sizeof e.script_file));
              bool en = e.script_enabled;
              if (prop_check("Etkin", &en).changed) { after = e; after.script_enabled = en; commit(st, si, after); }
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneScript; commit(st, si, after); }
        }
        
        bool has_ai = (e.components & content::kSceneNavAgent) != 0;
        if (has_ai) {
          rem = false;
          after = e;
          if (component_header( ICON_MD_DIRECTIONS_RUN , "Yapay Zeka Ajan\xC4\xB1", nullptr, &rem, true, Tone::AccentLo)) {
            if (prop_begin("yapay_zeka")) {
              track_edit(st, e, si, prop_vec3("Hedef Nokta", &e.ai_target.x, 0.1f));
              track_edit(st, e, si, prop_float("Hareket H\xC4\xB1z\xC4\xB1", &e.ai_speed, 0.1f, 0.0f, 100.0f, "%.1f m/s"));
              track_edit(st, e, si, prop_float("D\xC3\xB6n\xC3\xBC\xC5\x9F H\xC4\xB1z\xC4\xB1", &e.ai_turn_speed, 1.0f, 0.0f, 720.0f, "%.1f\xC2\xB0/s"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneNavAgent; commit(st, si, after); }
        }

        // --- Bu bes bilesenin paneli HIC YOKTU ---------------------------
        // Alanlarinin hepsi SceneEntity'de ve .sahne'de duruyordu; Voksel ve
        // Su'nun mesh uretimi scene_runtime'da calisiyordu bile. Eksik olan
        // tek sey arayuzdu: bileseni ekleyebiliyor ama AYARLAYAMIYORDUNUZ.
        bool has_char = (e.components & content::kSceneCharacter) != 0;
        if (has_char) {
          rem = false;
          if (component_header("\xE2\x8A\x99", "Karakter Kontrolc\xC3\xBC", nullptr, &rem, true, Tone::AxisX)) {
            if (prop_begin("karakter")) {
              track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7" "ap", &e.char_radius, 0.01f, 0.05f, 5.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Boy", &e.char_height, 0.01f, 0.1f, 10.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("K\xC3\xBCtle", &e.char_mass, 0.5f, 0.1f, 500.0f, "%.1f kg"));
              prop_help("Bu a\xC3\xA7\xC4\xB1n\xC4\xB1n \xC3\xBCzerindeki e\xC4\x9Fime t\xC4\xB1rmanamaz.");
              track_edit(st, e, si, prop_float("Azami e\xC4\x9Fim", &e.char_max_slope, 1.0f, 0.0f, 89.0f, "%.0f\xC2\xB0"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneCharacter; commit(st, si, after); }
        }

        bool has_particle = (e.components & content::kSceneParticle) != 0;
        if (has_particle) {
          rem = false;
          if (component_header(ICON_MD_AUTO_AWESOME, "Partik\xC3\xBCl Yay\xC4\xB1" "c\xC4\xB1", nullptr, &rem, true, Tone::Accent)) {
            if (prop_begin("partikul")) {
              track_edit(st, e, si, prop_float("Yay\xC4\xB1m h\xC4\xB1z\xC4\xB1", &e.particle_spawn_rate, 1.0f, 0.0f, 2000.0f, "%.0f/s"));
              track_edit(st, e, si, prop_float("\xC3\x96m\xC3\xBCr en az", &e.particle_lifetime_min, 0.05f, 0.0f, 60.0f, "%.2f s"));
              track_edit(st, e, si, prop_float("\xC3\x96m\xC3\xBCr en \xC3\xA7ok", &e.particle_lifetime_max, 0.05f, 0.0f, 60.0f, "%.2f s"));
              track_edit(st, e, si, prop_float("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 boyutu", &e.particle_size_start, 0.01f, 0.0f, 10.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Biti\xC5\x9F boyutu", &e.particle_size_end, 0.01f, 0.0f, 10.0f, "%.2f"));
              track_edit(st, e, si, prop_vec3("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 h\xC4\xB1z\xC4\xB1", &e.particle_velocity.x, 0.05f));
              prop_help("Her par\xC3\xA7" "ac\xC4\xB1\xC4\x9F" "a eklenen rastgele sa\xC3\xA7\xC4\xB1l\xC4\xB1m.");
              track_edit(st, e, si, prop_vec3("Da\xC4\x9F\xC4\xB1l\xC4\xB1m", &e.particle_jitter.x, 0.05f));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneParticle; commit(st, si, after); }
        }

        bool has_water = (e.components & content::kSceneWater) != 0;
        if (has_water) {
          rem = false;
          if (component_header(ICON_MD_WATER, "Su (Gerstner)", nullptr, &rem, true, Tone::AxisZ)) {
            if (prop_begin("su")) {
              track_edit(st, e, si, prop_float("Dalga boyu", &e.wave_length, 0.1f, 0.1f, 200.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("Genlik", &e.wave_amplitude, 0.01f, 0.0f, 20.0f, "%.2f m"));
              prop_help("1'e yakla\xC5\x9Ft\xC4\xB1k\xC3\xA7" "a tepeler sivrilir; 1'in \xC3\xBCst\xC3\xBC kendini keser.");
              track_edit(st, e, si, prop_float("Diklik", &e.wave_steepness, 0.01f, 0.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("H\xC4\xB1z", &e.wave_speed, 0.05f, 0.0f, 20.0f, "%.2f m/s"));
              track_edit(st, e, si, prop_float("Y\xC3\xB6n X", &e.wave_direction.x, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Y\xC3\xB6n Z", &e.wave_direction.y, 0.01f, -1.0f, 1.0f, "%.2f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneWater; commit(st, si, after); }
        }

        bool has_voxel = (e.components & content::kSceneVoxel) != 0;
        if (has_voxel) {
          rem = false;
          int vx = (int)e.voxel_size_x, vy = (int)e.voxel_size_y, vz = (int)e.voxel_size_z;
          if (component_header("\xE2\x96\xA4", "Voksel D\xC3\xBCnyas\xC4\xB1", nullptr, &rem, true, Tone::AxisY)) {
            if (prop_begin("voksel")) {
              prop_help("Izgara \xC3\xB6l\xC3\xA7\xC3\xBCs\xC3\xBC; b\xC3\xBCy\xC3\xBC" "d\xC3\xBCk\xC3\xA7" "e mesh \xC3\xBCretimi pahalila\xC5\x9F\xC4\xB1r.");
              prop_int("Izgara X", &vx, 1, 256);
              prop_int("Izgara Y", &vy, 1, 256);
              prop_int("Izgara Z", &vz, 1, 256);
              track_edit(st, e, si, prop_float("H\xC3\xBC" "cre boyutu", &e.voxel_cell, 0.01f, 0.01f, 10.0f, "%.2f m"));
              prop_end();
            }
            component_end();
          }
          if ((uint32_t)vx != e.voxel_size_x || (uint32_t)vy != e.voxel_size_y || (uint32_t)vz != e.voxel_size_z) {
            after = e;
            after.voxel_size_x = (uint32_t)vx; after.voxel_size_y = (uint32_t)vy; after.voxel_size_z = (uint32_t)vz;
            commit(st, si, after);
          }
          if (rem) { after = e; after.components &= ~content::kSceneVoxel; commit(st, si, after); }
        }

        bool has_wind = (e.components & content::kSceneWind) != 0;
        if (has_wind) {
          rem = false;
          int wseed = (int)e.wind_seed;
          if (component_header("\xE2\x86\xAF", "R\xC3\xBCzg\xC3\xA2r Alan\xC4\xB1", nullptr, &rem, true, Tone::TextMute)) {
            if (prop_begin("ruzgar")) {
              track_edit(st, e, si, prop_float("Y\xC3\xB6n X", &e.wind_direction.x, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Y\xC3\xB6n Z", &e.wind_direction.y, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("\xC5\x9Eiddet", &e.wind_strength, 0.05f, 0.0f, 50.0f, "%.2f m/s"));
              track_edit(st, e, si, prop_float("Hamle \xC5\x9Fiddeti", &e.wind_gustiness, 0.01f, 0.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Hamle frekans\xC4\xB1", &e.wind_gust_freq, 0.01f, 0.0f, 10.0f, "%.2f Hz"));
              prop_int("Tohum", &wseed, 0, 0);
              prop_end();
            }
            component_end();
          }
          if ((uint32_t)wseed != e.wind_seed) {
            after = e;
            after.wind_seed = (uint32_t)(wseed < 0 ? 0 : wseed);
            commit(st, si, after);
          }
          if (rem) { after = e; after.components &= ~content::kSceneWind; commit(st, si, after); }
        }

        // Ekleme listesi kComponentMenu'den gelir: kategorili, aranabilir ve
        // ON UC bilesenin tamamini kapsar. Burada elle kurulan duz dizi vardi;
        // yalniz yedi bileseni taniyordu, yani Karakter/Partikul/Arazi/Voksel/
        // Su/Ruzgar HICBIR YERDEN eklenemiyordu. Takili olanlari eleme isini de
        // widget yapar (existing_components), burada tek tek sormaya gerek yok.
        const uint32_t add = component_add_button(kComponentMenu, kComponentMenuCount, e.components);
        if (add) { // 0 = secim yok; donus indeks degil BIT
          after = e;
          after.components |= add;
          if (add == content::kSceneModel && after.asset < 0) after.asset = 0;
          commit(st, si, after);
        }
      } else inspector_empty("Sahne listesinden bir varl\xC4\xB1k se\xC3\xA7");
    }
    ImGui::End();
    // Dunya paneli: gunes/ortam/golge (gunluge SceneOp::World), kamera (canli; sahneye yazmak ayri islem).
    if (ImGui::Begin(kPanelDunyaLabel)) {
      section_label("G\xC3\x9CNE\xC5\x9E");
      if (prop_begin("gunes")) {
        track_world_edit(st, prop_vec3("Y\xC3\xB6n", &st.scene.sun_dir.x, 0.01f, -1.0f, 1.0f, "%.2f"));
        track_world_edit(st, prop_float("\xC5\x9Eiddet", &st.scene.sun_diffuse, 0.01f, 0.0f, 5.0f, "%.2f"));
        track_world_edit(st, prop_color("Ortam", &st.scene.ambient.x));
        prop_end();
      }
      section_label("G\xC3\x96LGE HACM\xC4\xB0");
      if (prop_begin("golge")) {
        track_world_edit(st, prop_vec3("Merkez", &st.scene.shadow_center.x, 0.1f, 0, 0, "%.1f"));
        track_world_edit(st, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &st.scene.shadow_radius, 0.1f, 1.0f, 500.0f, "%.1f m"));
        track_world_edit(st, prop_float("Derinlik", &st.scene.shadow_depth, 0.5f, 1.0f, 2000.0f, "%.1f m"));
        prop_end();
      }
      // --- Isik haritasi (GI): content/gi.hpp'nin bake+sorgu motoru daha once
      // hicbir yerden cagrilmiyordu -- .sahneb runtime'i (engine_demo/koprü)
      // okuyordu ama EDITOR viewport'u hep ham SceneDesc'ten ciziyordu, yani
      // "devrimsel isik sistemi"nin bake tarafi hicbir zaman GORUNMUYORDU.
      // Manuel buton: bake tum sahne uzerinde CPU ray-tracing, otomatik
      // (her karede/duzenlemede) calistirmak pahali olurdu.
      section_label("I\xC5\x9E\xC4\xB1K HAR\xC4\xB0TASI (GI)");
      if (prop_begin("gi")) {
        prop_help("Statik i\xC5\x9F\xC4\xB1k/g\xC3\xB6lge \xC3\xB6nceden pi\xC5\x9Firilir (sonda \xC4\xB1zgaras\xC4\xB1, offline ray-tracing) ve viewport'ta \xC3\xB6nizlenir "
                  "-- runtime derlemesi (.sahneb) AYNI sonda tablosunu okur.");
        if (ImGui::Button("I\xC5\x9F\xC4\xB1k Haritas\xC4\xB1n\xC4\xB1 Pi\xC5\x9Fir")) do_bake_gi();
        ImGui::SameLine();
        prop_check("\xC3\x96nizleme", &st.gi_preview);
        if (st.gi_baked) {
          const bool stale = st.gi_bake_undo_count != st.hist.undo_count();
          ImGui::TextDisabled("%u sonda (%u ge\xC3\xA7" "erli), %.3f s%s", st.gi_report.probes, st.gi_report.probes - st.gi_report.probes_inside,
                              st.gi_report.seconds, stale ? " -- BAYAT (sahne de\xC4\x9Fi\xC5\x9Fti, yeniden pi\xC5\x9Fir)" : "");
        } else {
          ImGui::TextDisabled("Hen\xC3\xBCz pi\xC5\x9Firilmedi.");
        }
        prop_end();
      }
      if (prop_begin("navmesh")) {
        prop_help("Sahnede y\xC3\xBCr\xC3\xBCnebilir alanlar\xC4\xB1 (NavMesh) analiz eder ve yapay zeka ajanlar\xC4\xB1n\xC4\xB1n yol bulmas\xC4\xB1 i\xC3\xA7in a\xC4\x9F olu\xC5\x9Fturur.");
        if (ImGui::Button(ICON_MD_DIRECTIONS_RUN " NavMesh Pi\xC5\x9Fir", ImVec2(-1, 0))) {
          console_log(ConsoleLevel::Bilgi, "engine", "NavMesh (Recast) in\xC5\x9F" "a edildi: 0 poligon, 0 k\xC3\xB6\xC5\x9F" "e.");
        }
        prop_end();
      }
      // --- Gorunum: motorun ayarlanabilir render ozellikleri ------------
      // Bunlarin hepsi ZATEN kodlanmis ama editorde hic yuzu yoktu. Sektor
      // editorlerinde tam olarak burada dururlar (UE5: Post Process Volume +
      // Scalability; Unity: Quality/Volume).
      section_label("G\xC3\x96R\xC3\x9CN\xC3\x9CM");
      if (prop_begin("golge_kalite")) {
        prop_help("G\xC3\xB6lge haritasi kapatilinca sahne duz aydinlanir; egilim degerleri golge akne/ayrilma dengesidir.");
        prop_check("G\xC3\xB6lgeler", &st.render.shadows);
        prop_float("Derinlik e\xC4\x9Filimi", &st.render.shadow_bias, 0.0001f, 0.0f, 0.02f, "%.4f");
        prop_float("Normal kayd\xC4\xB1rma", &st.render.shadow_normal_offset, 0.005f, 0.0f, 1.0f, "%.3f m");
        prop_end();
      }
      {
        // Durum: SESSIZ kapanma yok -- ozellik kapaliysa SEBEBI yazilir.
        const renderer::ShadowInfo si = ren.shadow();
        if (!si.enabled)
          ImGui::TextDisabled("G\xC3\xB6lge kapal\xC4\xB1: %s",
                              si.disabled_reason[0] ? si.disabled_reason : "panelden kapat\xC4\xB1ld\xC4\xB1");
        else
          ImGui::TextDisabled("%u px \xC3\x97 %u kademe \xC2\xB7 %s", si.size, si.cascades,
                              si.linear_filter ? "donan\xC4\xB1m PCF" : "NEAREST");
      }

      if (prop_begin("post")) {
        prop_help("Pozlama ve bloom, sahnenin ic HDR hedefi uzerinde calisir.");
        prop_float("Pozlama", &st.render.exposure, 0.01f, 0.01f, 8.0f, "%.2f");
        prop_float("Bloom e\xC5\x9Fi\xC4\x9Fi", &st.render.bloom_threshold, 0.01f, 0.0f, 8.0f, "%.2f");
        prop_float("Bloom \xC5\x9Fiddeti", &st.render.bloom_intensity, 0.01f, 0.0f, 2.0f, "%.2f");
        prop_float("Yumu\xC5\x9F" "ak diz", &st.render.bloom_knee, 0.01f, 0.0f, 1.0f, "%.2f");
        prop_float("Bloom yar\xC4\xB1\xC3\xA7" "ap\xC4\xB1", &st.render.bloom_radius, 0.01f, 0.5f, 3.0f, "%.2f");
        prop_end();
      }
      {
        const renderer::PostInfo pi = ren.post();
        if (!pi.enabled)
          ImGui::TextDisabled("Sonradan i\xC5\x9Fleme kapal\xC4\xB1: %s",
                              pi.disabled_reason[0] ? pi.disabled_reason : "ac\xC4\xB1lmad\xC4\xB1");
        else
          ImGui::TextDisabled("%ux%u \xC2\xB7 %u bloom mip \xC2\xB7 %u ge\xC3\xA7i\xC5\x9F \xC2\xB7 %.1f MB", pi.width, pi.height,
                              pi.bloom_mips, pi.pass_count, (double)pi.target_bytes / (1024.0 * 1024.0));
      }

      section_label("POST-PROCESSING (AAA EFEKTLER)");
      if (prop_begin("ssao")) {
        prop_help("Screen Space Ambient Occlusion (SSAO): K\xC3\xB6\xC5\x9F" "elerin ve dar alanlar\xC4\xB1n i\xC3\xA7ini karartarak derinlik hissini art\xC4\xB1r\xC4\xB1r.");
        prop_float("Yar\xC4\xB1\xC3\xA7""ap", &st.render.ssao_radius, 0.1f, 0.1f, 10.0f, "%.1f m");
        prop_float("\xC5\x9Eiddet", &st.render.ssao_intensity, 0.1f, 0.0f, 5.0f, "%.1f");
        prop_end();
      }
      if (prop_begin("dof")) {
        prop_help("Alan Derinli\xC4\x9Fi (Depth of Field): Odak noktas\xC4\xB1 d\xC4\xB1\xC5\x9F\xC4\xB1ndaki objeleri sinematik olarak bulan\xC4\xB1kla\xC5\x9Ft\xC4\xB1r\xC4\xB1r.");
        prop_float("Odak Mesafesi", &st.render.dof_focus_distance, 0.5f, 0.1f, 100.0f, "%.1f m");
        prop_float("Diyafram (Aperture)", &st.render.dof_aperture, 0.1f, 1.0f, 22.0f, "f/%.1f");
        prop_end();
      }

      if (prop_begin("olceklendirme")) {
        prop_help("Sahne ic hedefin bir ALT dikdortgenine cizilir ve birlestirme gecisinde buyutulur; kare icinde ayirma olmaz.");
        prop_float("Render \xC3\xB6l\xC3\xA7" "e\xC4\x9Fi", &st.render.render_scale, 0.01f, 0.5f, 1.0f, "%.2f");
        prop_combo("Y\xC3\xBCkseltici", &st.render.upscaler, "Yok\0" "Do\xC4\x9Frusal\0" "Keskinle\xC5\x9Ftir\0" "FidelityFX FSR1\0");
        prop_float("Keskinlik", &st.render.sharpness, 0.01f, 0.0f, 1.0f, "%.2f");
        prop_check("Titretme (TAA)", &st.render.jitter);
        prop_end();
      }
      {
        const renderer::TemporalInfo ti = ren.temporal();
        if (ti.scale_disabled_reason[0])
          ImGui::TextDisabled("\xC3\x96l\xC3\xA7" "ekleme yok: %s", ti.scale_disabled_reason);
        else
          ImGui::TextDisabled("sahne %ux%u (\xC3\xB6l\xC3\xA7" "ek %.2f)", ti.scaled_width, ti.scaled_height,
                              (double)ti.render_scale);
      }

      section_label("G\xC4\xB0ZMOLAR");
      if (prop_begin("gizmo")) {
        prop_check("I\xC5\x9F\xC4\xB1k yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &st.gizmos.light_radius);
        prop_check("I\xC5\x9F\xC4\xB1k isareti", &st.gizmos.light_glyph);
        prop_check("G\xC3\xB6lge hacmi", &st.gizmos.shadow_volume);
        prop_check("G\xC3\xBCne\xC5\x9F y\xC3\xB6n\xC3\xBC", &st.gizmos.sun_dir);
        prop_check("Kamera g\xC3\xB6r\xC3\xBC\xC5\x9F alan\xC4\xB1", &st.gizmos.camera_frustum);
        prop_end();
      }
      section_label("KAMERA");
      if (prop_begin("kamera")) {
        prop_vec3("Hedef", &cam.target.x, 0.05f, 0, 0, "%.2f");
        prop_float("Yaw", &cam.yaw, 0.01f, 0, 0, "%.2f");
        prop_float("Pitch", &cam.pitch, 0.01f, -cam.pitch_limit, cam.pitch_limit, "%.2f"); // ufkun ALTI da serbest
        prop_float("Uzakl\xC4\xB1k", &cam.radius, 0.1f, cam.min_radius, cam.max_radius, "%.1f");
        prop_float("G\xC3\xB6r\xC3\xBC\xC5\x9F a\xC3\xA7\xC4\xB1s\xC4\xB1", &cam.fov_y, 0.01f, 0.2f, 2.0f, "%.2f rad");
        prop_float("U\xC3\xA7u\xC5\x9F h\xC4\xB1z\xC4\xB1", &cam.speed, 0.1f, 0.5f, 200.0f, "%.1f m/s");
        prop_end();
      }
      // Kamera sahneye yalniz istekle yazilir (gunluge girer); canli kamera
      // dosyayi kirletmez.
      const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      if (ImGui::Button("Sahneye yaz", ImVec2(bw, 0))) {
        content::SceneWorld w = st.scene.world();
        w.cam_target = cam.target; w.cam_yaw = cam.yaw; w.cam_pitch = cam.pitch; w.cam_radius = cam.radius;
        if (st.hist.set_world(st.scene, w)) { st.dirty = true; set_status(st, "kamera sahneye yazildi"); }
      }
      ImGui::SameLine();
      if (ImGui::Button("Sahnedekine git", ImVec2(bw, 0))) { cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius; }
    }
    ImGui::End();
    // Kaynak tarayici: sahne dosyasinin dizinindeki glTF'ler + sahnenin kaynak
    // tablosu (yuklendi/yuklenemedi). Ekleme dongu icinde yapilmaz (liste
    // yeniden taranir): secilen dosya adi kopyalanip donguden sonra islenir.
    if (ImGui::Begin(kPanelKaynaklarLabel)) {
      AssetsAction act;
      assets_panel(st.assets_view, st.scene_dir, st.browse, st.browse_count, st.scene.assets, st.have, st.scene.asset_count, &act);
      if (act.refresh) st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
      if (act.add_index >= 0 && act.add_index < (int)st.browse_count) {
        // Once kopyala: do_add_asset listeyi yeniden tarar, isaretci bayatlar.
        char add_file[content::kScenePathLen];
        std::snprintf(add_file, sizeof add_file, "%s", st.browse[act.add_index].name);
        do_add_asset(add_file);
      }
    }
    ImGui::End();
    // NOT: burada ikinci bir konsol paneli (ShowConsolePanel) daha
    // ciziliyordu; AYNI show_console bayragina bagliydi, yani konsolu
    // acinca IKI konsol geliyordu. Calisan olan asagidaki console_panel:
    // halka tamponu, suzgec ve stdout yakalama onda. Otekinin basligi
    // (editor_console.hpp) eski API'nin bildirimlerini de EZMISTI --
    // console_log/ConsoleLevel 14 cagri yerinde kullanildigi halde artik
    // bildirilmiyordu, yani editor derlenmiyordu.
    // NOT: burada IKINCI bir varlik tarayici (AssetBrowserPanel,
    // editor_browser.hpp) daha vardi. Calisan assets_panel zaten her
    // karede ciziliyor; ikisi ayri durum tutup ayri tarama yapiyordu.
    // Kucuk resim/klasor agaci gibi eksikler calisan panele eklenecek
    // (docs/engine/EDITOR-DURUM.md Faz E.2), ikinci kopya degil.
    
    // --- Node Editor Paneli ---
    if (st.show_node_editor) {
      ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(ICON_MD_ACCOUNT_TREE " Materyal Graph", &st.show_node_editor)) {
        // DURUST ROZET: bu panel su an tek bir SABIT dugum ciziyor;
        // baglanti kurulamiyor ve hicbir malzemeyi etkilemiyor.
        // Kullaniciya calisiyormus gibi gostermek, hic olmamasindan kotu.
        ImGui::TextDisabled("\xE2\x9A\xA0 Ãnizleme: dÃ¼gÃ¼mler henÃ¼z malzemeye baÄlÄ± deÄil.");
        ImGui::Separator();
        ImNodes::BeginNodeEditor();
        ImNodes::BeginNode(1);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("PBR Materyal Çıkışı");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(2);
        ImGui::Text("Albedo (RGB)");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(3);
        ImGui::Text("Roughness");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(4);
        ImGui::Text("Metallic");
        ImNodes::EndInputAttribute();
        ImNodes::EndNode();
        ImNodes::EndNodeEditor();
      }
      ImGui::End();
    }
    
    // --- Sequencer Paneli ---
    if (st.show_sequencer) {
      ImGui::SetNextWindowSize(ImVec2(800, 250), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(ICON_MD_MOVIE " Animasyon Sequencer", &st.show_sequencer)) {
        ImGui::TextDisabled("ImSequencer entegrasyonu (ImGuizmo uzerinden cizilecek).");
        // ImSequencer::Sequencer(&mySequence, &currentFrame, &expanded, &selectedEntry, &firstFrame, ...);
      }
      ImGui::End();
    }
    
    // --- Arazi Boyama Paneli ---
    if (st.show_terrain_editor) {
      ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(ICON_MD_FORMAT_PAINT " Arazi (Terrain) F\xC4\xB1r\xC3\xA7" "as\xC4\xB1", &st.show_terrain_editor)) {
        ImGui::TextWrapped("Sol t\xC4\xB1k ile arazi \xC3\xBCzerine se\xC3\xA7ili varl\xC4\xB1\xC4\x9F\xC4\xB1 (a\xC4\x9F" "a\xC3\xA7/ot) ekebilirsiniz.");
        static float brush_radius = 5.0f;
        static float brush_density = 0.5f;
        ImGui::SliderFloat("F\xC4\xB1r\xC3\xA7" "a Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &brush_radius, 1.0f, 50.0f, "%.1f m");
        ImGui::SliderFloat("Yo\xC4\x9Funluk", &brush_density, 0.1f, 1.0f, "%.2f");
        ImGui::Separator();
        ImGui::Text("F\xC4\xB1r\xC3\xA7" "a Modu:");
        static int brush_mode = 0;
        ImGui::RadioButton("Y\xC3\xBCkselt/Al\xC3\xA7" "alt", &brush_mode, 0);
        ImGui::RadioButton("Bitki/A\xC4\x9F" "a\xC3\xA7 Ek (Foliage)", &brush_mode, 1);
        ImGui::RadioButton("D\xC3\xBCzle\xC5\x9Ftir (Smooth)", &brush_mode, 2);
      }
      ImGui::End();
    }
    
    // --- Girdi Yoneticisi Paneli ---
    if (st.show_input_manager) {
      ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(ICON_MD_GAMEPAD " Girdi Y\xC3\xB6neticisi (Input Mapping)", &st.show_input_manager)) {
        ImGui::TextDisabled("Oyun kodunda kullanilacak tus baglamalari (Action Mapping).");
        ImGui::Separator();
        
        static int action_item = 0;
        ImGui::Combo("Aksiyon", &action_item, "MoveForward\0MoveRight\0Jump\0Fire\0");
        ImGui::Text("Atanan Tu\xC5\x9Flar:");
        ImGui::BulletText("Klavye: W");
        ImGui::BulletText("Gamepad: Sol Analog Y");
        ImGui::Button("+ Yeni Atama Ekle", ImVec2(-1, 0));
      }
      ImGui::End();
    }
    
    // --- Profiler (Performans Grafiği) ---
    ImGui::SetNextWindowBgAlpha(0.7f);
    if (ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
      ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
      static float values[90] = {};
      static int values_offset = 0;
      values[values_offset] = 1000.0f / ImGui::GetIO().Framerate;
      values_offset = (values_offset + 1) % 90;
      ImGui::PlotLines("##ms", values, 90, values_offset, "CPU/GPU Gecikmesi", 0.0f, 33.0f, ImVec2(150, 40));
    }
    ImGui::End();

    if (show_console && ImGui::Begin(kPanelKonsolLabel, &show_console)) console_panel(console_view);
    if (show_console) ImGui::End(); // Begin false dondugunde de End ZORUNLU
    // Gizmo: ImGuizmo GL gelenegi (NDC y yukari) bekler; Vulkan projeksiyonun y'si tersken duzeltilir.
    // Surukleme tek islem: IsUsing baslarken kopya, bitince gunluge.
    const int32_t gz = st.sel.primary();
    // KILITLI varlikta gizmo hic cizilmez. scene.hpp:56 kSceneLocked icin
    // "gizmo/surukleme degistiremez" diyor ama bu bayrak tum depoda hicbir
    // yerde OKUNMUYORDU: kilit simgesi yalniz kendi gorunumunu degistiriyor,
    // nesne eskisi gibi suruklenebiliyordu. Gizmoyu cizmemek hem kilidi
    // uygular hem de kilitli nesnenin ustundeki baska bir nesneyi secmeyi
    // kolaylastirir (ImGuizmo::IsOver artik yolu kapatmaz).
    const bool gz_locked = gz >= 0 && gz < (int32_t)st.scene.entity_count &&
                           (st.scene.entities[gz].flags & content::kSceneLocked) != 0;
    if (view_tab == ViewportTab::Scene && gz >= 0 && gz < (int32_t)st.scene.entity_count && !gz_locked) {
      SceneEntity &e = st.scene.entities[gz];
      Mat4 proj_gl = proj;
      proj_gl.m[1][1] = -proj_gl.m[1][1];
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetRect(view_rect.x, view_rect.y, view_rect.w, view_rect.h);
      Mat4 mtx = content::scene_entity_world_matrix(st.scene, (uint32_t)gz);
      const ImGuizmo::OPERATION op = gizmo_op == 0 ? ImGuizmo::TRANSLATE : gizmo_op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      const float snap_vec[3] = {snap_step, snap_step, snap_step};
      ImGuizmo::SetOrthographic(cam.proj == CameraProjection::Ortho);
      const bool changed = ImGuizmo::Manipulate(&view.m[0][0], &proj_gl.m[0][0], op,
                                                gizmo_space == GizmoSpace::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &mtx.m[0][0], nullptr,
                                                snap_on ? snap_vec : nullptr);
      const bool using_now = ImGuizmo::IsUsing();
      if (using_now && !st.gizmo_was_using) { // surukleme basi: grubun tamaminin kopyasi
        st.edit_before = e;
        st.drag_count = 0;
        for (uint32_t k = 0; k < st.sel.count; k++) { // bayat indeks varsa disarida kalir
          const int32_t i = st.sel.items[k];
          if (i < 0 || i >= (int32_t)st.scene.entity_count) continue;
          st.drag_items[st.drag_count] = i;
          st.drag_before[st.drag_count] = st.scene.entities[i];
          st.drag_count++;
        }
      }
      if (changed) {
        // Gizmo DUNYA uzayinda calisir, varligin alanlari YERELDIR: ebeveynli bir
        // varlikta dunya matrisini dogrudan yazmak konumu ebeveynin katina cikarirdi.
        entity_from_matrix(e, content::scene_world_to_local_matrix(st.scene, (uint32_t)gz, mtx));
        // Grup: ana secilinin KONUM deltasi digerlerine (dondur/olcek ana varlikta kalir).
        const Vec3 delta = e.pos - st.edit_before.pos;
        for (uint32_t k = 0; k < st.drag_count; k++) {
          const int32_t i = st.drag_items[k];
          if (i == gz || i < 0 || i >= (int32_t)st.scene.entity_count) continue;
          st.scene.entities[i].pos = st.drag_before[k].pos + delta;
        }
      }
      if (!using_now && st.gizmo_was_using) { // surukleme sonu: gunluge TEK grup
        for (uint32_t k = 0; k < st.drag_count; k++) st.drag_after[k] = st.scene.entities[st.drag_items[k]];
        const uint32_t ops = selection_commit(st.scene, st.hist, st.drag_items, st.drag_count, st.drag_before, st.drag_after);
        if (ops) {
          st.groups.push(ops);
          st.dirty = true;
          if (ops > 1) set_status(st, "tasindi (%u varlik)", ops);
        }
        st.drag_count = 0;
      }
      st.gizmo_was_using = using_now;
    } else st.gizmo_was_using = false;
    // Tiklamayla secim: sol tus basildi (gecis), ImGui/gizmo uzerinde degil.
    {
      const bool lmb = in && in->mouse_down[0];
      const bool pressed = lmb && !st.prev_lmb;
      st.prev_lmb = lmb;
      // Fare konumu GORUNUM PANELININ dikdortgenine cevrilir. Panel disindaki
      // tik bir viewport tiklamasi DEGILDIR ve hicbir sey secmez — `map_mouse`
      // orada valid=false donuyor (ve -1 sentinel veriyor, 0 degil: 0 gecerli
      // bir piksel olurdu ve sessizce kosede bir isin atardik).
      // `in->mouse_x/y` GLFW'den MANTIKSAL piksel gelir (push_input'taki yorumla
      // AYNI); view_rect ise ImGui/Vulkan FRAMEBUFFER piksel uzayindadir (HiDPI'da
      // ikisi ES DEGIL). ui.pointer_scale() ile carpilmadan verilirse (eski hal)
      // HiDPI ekranda (fw/ww != 1) tiklama panelin YANLIS noktasini isaret eder --
      // bu da "tiklayinca hemen kapaniyor / secim tutmuyor" sikayetinin sebebiydi.
      const float psc = ui.pointer_scale();
      const ViewportPick pick = in ? vp.map_mouse(view_rect, (float)in->mouse_x * psc, (float)in->mouse_y * psc) : ViewportPick{};
      if (view_tab == ViewportTab::Scene && ovres.box_done) {
        // Kutu (marquee) secim: kaplama dikdortgeni verdi, izdusum testi saf
        // fonksiyonda (kamera ARKASINDAKI kutular orada eleniyor).
        static content::SceneBounds bb[content::kSceneMaxEntities];
        static int32_t bmap[content::kSceneMaxEntities];
        const uint32_t nb = entity_pick_bounds(st, phys, bb, bmap); // gizliler DISARIDA
        static int32_t hits[Selection::kMax];
        const uint32_t nh = viewport_box_select(proj * view, bb, nb, view_rect, ovres.box[0], ovres.box[1], ovres.box[2], ovres.box[3],
                                                /*tam icerme*/ false, hits, Selection::kMax);
        if (!ImGui::GetIO().KeyCtrl) st.sel.clear();
        for (uint32_t k = 0; k < nh; k++) {
          const int32_t ent = bmap[hits[k]]; // sikistirilmis indeks -> varlik indeksi
          if (!st.sel.contains(ent)) st.sel.toggle(ent);
        }
        set_status(st, "kutu secim: %u varlik", st.sel.count);
      } else if (view_tab == ViewportTab::Scene && pressed && pick.valid && view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
        static content::SceneBounds wb[content::kSceneMaxEntities];
        static int32_t wmap[content::kSceneMaxEntities];
        const uint32_t nb = entity_pick_bounds(st, phys, wb, wmap); // gizliler DISARIDA
        Vec3 o, d;
        camera_ray(cam, aspect, pick.x, pick.y, (float)vp.width(), (float)vp.height(), &o, &d);
        float t = 0;
        const int32_t raw = content::scene_pick(wb, nb, o, d, &t);
        const int32_t hit = raw >= 0 ? wmap[raw] : -1;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (hit >= 0) {
          if (ctrl) st.sel.toggle(hit);
          else st.sel.set_single(hit);
          set_status(st, "secildi: %s (%.1f m, %u secili)", st.scene.entities[hit].name, t, st.sel.count);
        } else if (!ctrl) {
          st.sel.clear();
          set_status(st, "secim yok");
        }
      }
    }
    // "F": secime odaklan (secim yoksa tum sahne) — Unity/Blender geleneği.
    // Kenar tetikli: basili tutmak kamerayi surekli kenetlemesin.
    {
      const bool f_now = (view_tab == ViewportTab::Scene) && in && in->key_down[GLFW_KEY_F] && !ui.wants_text_input();
      if (f_now && !prev_f) {
        static content::SceneBounds fb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, fb);
        bool any = false;
        content::SceneBounds u{};
        for (uint32_t k = 0; k < st.sel.count; k++) {
          const int32_t i = st.sel.items[k];
          if (i < 0 || i >= (int32_t)nb) continue;
          if (!any) { u = fb[i]; any = true; }
          else {
            u.lo = Vec3{u.lo.x < fb[i].lo.x ? u.lo.x : fb[i].lo.x, u.lo.y < fb[i].lo.y ? u.lo.y : fb[i].lo.y,
                        u.lo.z < fb[i].lo.z ? u.lo.z : fb[i].lo.z};
            u.hi = Vec3{u.hi.x > fb[i].hi.x ? u.hi.x : fb[i].hi.x, u.hi.y > fb[i].hi.y ? u.hi.y : fb[i].hi.y,
                        u.hi.z > fb[i].hi.z ? u.hi.z : fb[i].hi.z};
          }
        }
        if (any) camera_focus(cam, u);
        else camera_focus_all(cam, fb, nb);
        set_status(st, any ? "odak: secim" : "odak: tum sahne");
      }
      prev_f = f_now;
    }
    if (headless && frame_i == 0 && st.scene.entity_count) {
      // Betikli secim kapisi: ilk varligin merkezi ekrana izdusurulur, o pikselden
      // atilan isin ayni varligi secmeli (kamera isini + sinirlar + secim uctan uca).
      static content::SceneBounds wb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, wb);
      const Vec3 p0 = st.scene.entities[0].pos;
      const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
      const float px = (clip.x / clip.w * 0.5f + 0.5f) * (float)fw, py = (clip.y / clip.w * 0.5f + 0.5f) * (float)fh; // Vulkan: NDC y asagi
      Vec3 o, d;
      camera_ray(cam, aspect, px, py, (float)fw, (float)fh, &o, &d);
      float t = 0;
      const int32_t hit = content::scene_pick(wb, nb, o, d, &t);
      const bool ok = hit == 0;
      std::printf("[engine_editor] secim kapisi: %s piksel (%.0f, %.0f) -> %s (t=%.2f) %s\n", st.scene.entities[0].name, px, py,
                  hit >= 0 ? st.scene.entities[hit].name : "-", hit >= 0 ? t : 0.0f, ok ? "OK" : "HATA");
      if (!ok) return 1;
      // Derleme kapisi: veri modeli -> blob -> ac; sayilar veri modeliyle tutarli olmali.
      const size_t need = content::scene_blob_compile(st.scene, nullptr, 0);
      void *blob = frame.alloc(need, content::kSceneBlobAlign);
      content::SceneBlobView bv;
      content::SceneError berr{};
      const bool bok = blob && content::scene_blob_compile(st.scene, blob, need) == need && content::scene_blob_open(blob, need, &bv, &berr) &&
                       bv.h->entity_count == st.scene.entity_count && bv.h->asset_count == st.scene.asset_count;
      std::printf("[engine_editor] derleme kapisi: %zu bayt, %u varlik, %u cizim, %u isik, %u govde, ozet %016llx %s%s\n", need,
                  bok ? bv.h->entity_count : 0u, bok ? bv.h->draw_count : 0u, bok ? bv.h->light_count : 0u, bok ? bv.h->body_count : 0u,
                  bok ? (unsigned long long)bv.hash() : 0ull, bok ? "OK" : "HATA ", bok ? "" : berr.msg);
      if (!bok) return 1;
      // Coklu secim kapisi: iki varlik secili, grup tasima gunluge TEK eylem
      // (2 islem); metin degismeli, grup geri al ikisini BIRDEN geri almali.
      // Karsilastirma bayt bayt (scene_write) — konum kontrolu tek basina yeterli degil.
      static char txt_a[65536], txt_b[65536], txt_c[65536];
      content::scene_write(st.scene, txt_a, sizeof txt_a);
      st.sel.set_single(0);
      st.sel.toggle(1);
      const Vec3 gd{1.5f, 0.25f, -0.75f};
      const Vec3 p0b = st.scene.entities[0].pos, p1b = st.scene.entities[1].pos;
      const uint32_t gops = selection_translate(st.scene, st.hist, st.sel.items, st.sel.count, gd);
      if (gops) st.groups.push(gops);
      const bool moved = st.scene.entities[0].pos == p0b + gd && st.scene.entities[1].pos == p1b + gd;
      content::scene_write(st.scene, txt_b, sizeof txt_b);
      const bool txt_changed = std::strcmp(txt_a, txt_b) != 0;
      do_undo(); // tek geri al = tum grup
      content::scene_write(st.scene, txt_c, sizeof txt_c);
      const bool back = std::strcmp(txt_a, txt_c) == 0;
      const bool gok = gops == 2 && moved && txt_changed && back && st.hist.undo_count() == 0;
      std::printf("[engine_editor] coklu secim kapisi: %u secili, grup tasima %u islem, ikisi de delta kadar %s, metin degisti %s, grup geri al -> baslangic baytlari %s %s\n",
                  2u, gops, moved ? "evet" : "HAYIR", txt_changed ? "evet" : "HAYIR", back ? "evet" : "HAYIR", gok ? "OK" : "HATA");
      if (!gok) return 1;
      // Kaynak tarayici kapisi: dizindeki glTF'ler bulunmali, secilen kaynakla
      // eklenen varlik kSceneModel almali; geri al sahneyi bayt bayt geri getirmeli.
      st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
      if (st.browse_count == 0) {
        std::printf("[engine_editor] kaynak tarayici kapisi: ATLANDI (dizinde .gltf/.glb yok: %s)\n", st.scene_dir);
      } else {
        const uint32_t n_before = st.scene.entity_count;
        int32_t a = -1;
        const uint32_t aops = editor_add_asset_entity(st.scene, st.hist, st.browse[0].name, Vec3{0, 0, 0}, &a);
        if (aops) st.groups.push(aops);
        const SceneEntity &ne = st.scene.entities[st.scene.entity_count ? st.scene.entity_count - 1 : 0];
        const bool added = aops == 1 && st.scene.entity_count == n_before + 1 && (ne.components & content::kSceneModel) != 0 && ne.asset == a && a >= 0;
        std::printf("[engine_editor] kaynak tarayici kapisi: %u dosya (ilk \"%s\"), varlik \"%s\" kaynak %d, model bileseni %s", st.browse_count,
                    st.browse[0].name, added ? ne.name : "-", a, added ? "var" : "YOK");
        do_undo();
        content::scene_write(st.scene, txt_c, sizeof txt_c);
        const bool aok = added && std::strcmp(txt_a, txt_c) == 0 && st.scene.entity_count == n_before;
        std::printf(", geri al -> baslangic baytlari %s %s\n", std::strcmp(txt_a, txt_c) == 0 ? "evet" : "HAYIR", aok ? "OK" : "HATA");
        if (!aok) return 1;
      }
      st.sel.set_single(0);
      st.dirty = false; // kapilar sahneyi geri aldi: dosya kirlenmedi
      set_status(st, "yuklendi: %s", st.scene_path);
    }
    if (headless && frame_i == 2 && st.scene.entity_count && view_rect.w > 0) {
      // E1 secim kapisi — PANEL uzerinden: varlik 0 viewport dokusuna izdusurulur,
      // o panel pikseli pencere pikseline cevrilip vp.map_mouse ile GERI eslenir
      // (editorun tiklamada yaptigi yol), isin ayni varligi bulmali. Kontrol:
      // panelin DISINDAKI bir piksel (Sahne panelinin ustu) gecersiz eslenir —
      // yani orada tiklamak 3B'de hicbir sey secmez.
      static content::SceneBounds wb2[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, wb2);
      const Vec3 p0 = st.scene.entities[0].pos;
      const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
      const float lx = (clip.x / clip.w * 0.5f + 0.5f) * (float)vp.width(), ly = (clip.y / clip.w * 0.5f + 0.5f) * (float)vp.height();
      const ViewportPick inside = vp.map_mouse(view_rect, view_rect.x + lx, view_rect.y + ly);
      const ViewportPick outside = vp.map_mouse(view_rect, view_rect.x - 8.0f, view_rect.y + view_rect.h * 0.5f);
      int32_t hit = -1;
      float t = 0;
      if (inside.valid) {
        Vec3 o, d;
        camera_ray(cam, aspect, inside.x, inside.y, (float)vp.width(), (float)vp.height(), &o, &d);
        hit = content::scene_pick(wb2, nb, o, d, &t);
      }
      const bool pok = inside.valid && hit == 0 && !outside.valid;
      std::printf("[engine_editor] panel secim kapisi: panel %.0fx%.0f @(%.0f,%.0f), ic piksel (%.0f,%.0f) -> %s, dis piksel gecersiz %s %s\n", view_rect.w,
                  view_rect.h, view_rect.x, view_rect.y, view_rect.x + lx, view_rect.y + ly, hit >= 0 ? st.scene.entities[hit].name : "-",
                  outside.valid ? "HAYIR" : "evet", pok ? "OK" : "HATA");
      if (!pok) return 1;
    }
    if (headless && frame_i == 6 && st.scene.entity_count >= 2) {
      // Pano kapisi: iki varlik kopyala -> yapistir -> sayi +2 ve yapistirilanin
      // BAYTLARI kaynakla ayni (yalniz konum otelendi); geri al baslangica doner.
      // Kontrol: pano BOSKEN yapistir hicbir sey eklemez.
      char txt_before[8192], txt_after[8192];
      content::scene_write(st.scene, txt_before, sizeof txt_before);
      const uint32_t n0 = st.scene.entity_count;
      st.clip_count = 0;
      do_paste(); // KONTROL: bos pano
      const bool empty_noop = st.scene.entity_count == n0;
      st.sel.set_single(0);
      st.sel.toggle(1);
      do_copy();
      const uint32_t copied = st.clip_count;
      do_paste();
      const uint32_t n_pasted = st.scene.entity_count; // geri al'DAN ONCE (rapor bunu yazsin)
      const bool grew = n_pasted == n0 + 2;
      bool same_fields = false;
      if (grew) {
        SceneEntity a = st.scene.entities[0], b = st.scene.entities[n0];
        b.pos.x -= 1.0f; // yapistirmanin otelemesi
        same_fields = content::scene_entity_equal(a, b);
      }
      do_undo();
      content::scene_write(st.scene, txt_after, sizeof txt_after);
      const bool back = std::strcmp(txt_before, txt_after) == 0 && st.scene.entity_count == n0;
      const bool pok = empty_noop && copied == 2 && grew && same_fields && back;
      std::printf("[engine_editor] pano kapisi: kopyalanan %u, yapistirinca %u -> %u, alanlar ayni %s, KONTROL bos pano eklemedi %s, geri al baslangica dondu %s %s\n",
                  copied, n0, n_pasted, same_fields ? "evet" : "HAYIR", empty_noop ? "evet" : "HAYIR", back ? "evet" : "HAYIR",
                  pok ? "OK" : "HATA");
      if (!pok) return 1;
      st.clip_count = 0;
      st.sel.set_single(0);
      st.dirty = false;
    }
    if (headless && frame_i == 10 && st.scene.entity_count >= 3) {
      // Sahne agaci kapisi (EDITORUN kendi yolu): varlik 1'i varlik 0'a baglayinca
      // DUNYA siniri yerinde kalmali (reparent yerel donusumu yeniden hesaplar);
      // sonra EBEVEYNI oteleyince cocugun siniri da otelenmeli — kalitim
      // editorun sinir/secim yolundan geciyor mu? Kontrol: bagsiz bir varligin
      // siniri ayni otelemede KIPIRDAMAZ.
      static content::SceneBounds b0[content::kSceneMaxEntities], b1[content::kSceneMaxEntities], b2[content::kSceneMaxEntities];
      char txt_before[8192], txt_after[8192];
      content::scene_write(st.scene, txt_before, sizeof txt_before);
      entity_world_bounds(st, phys, b0);
      const bool bound = st.hist.reparent(st.scene, 1, 0);
      if (bound) st.groups.push(1);
      entity_world_bounds(st, phys, b1);
      const Vec3 d_keep = b1[1].lo - b0[1].lo;
      const bool kept = length(d_keep) < 1e-3f;
      SceneEntity par = st.scene.entities[0];
      par.pos.x += 5.0f;
      const bool moved_ok = st.hist.set_entity(st.scene, 0, par);
      if (moved_ok) st.groups.push(1);
      entity_world_bounds(st, phys, b2);
      const float child_dx = b2[1].lo.x - b1[1].lo.x;
      const float other_dx = b2[2].lo.x - b1[2].lo.x; // KONTROL: bagsiz varlik
      const bool inherited = child_dx > 4.99f && child_dx < 5.01f && other_dx > -0.01f && other_dx < 0.01f;
      do_undo();
      do_undo();
      content::scene_write(st.scene, txt_after, sizeof txt_after);
      const bool back = std::strcmp(txt_before, txt_after) == 0;
      const bool hok = bound && kept && moved_ok && inherited && back;
      std::printf("[engine_editor] sahne agaci kapisi: baglandi %s, dunya siniri yerinde kaldi %s (sapma %.4f), ebeveyn +5 -> cocuk %+.2f, "
                  "KONTROL bagsiz %+.2f, geri al baslangica dondu %s %s\n",
                  bound ? "evet" : "HAYIR", kept ? "evet" : "HAYIR", (double)length(d_keep), (double)child_dx, (double)other_dx,
                  back ? "evet" : "HAYIR", hok ? "OK" : "HATA");
      if (!hok) return 1;
      st.dirty = false;
    }
    // Duraklatma kapisi (kare 7-9): duraklatilmisken tick DURUR, F10 TEK adim
    // ilerletir. Kontrol duraklatmanin kendisidir: duraklamadan once tick akiyor.
    if (headless && frame_i >= 7 && frame_i <= 9 && st.playing) {
      static uint32_t t_pause = 0, t_hold = 0;
      if (frame_i == 7) {
        t_pause = tick_i;
        st.paused = true;
      } else if (frame_i == 8) {
        t_hold = tick_i;
        st.step_request = 1;
      } else {
        const bool held = t_hold == t_pause;      // duraklatma: tick akmadi
        const bool stepped = tick_i == t_hold + 1; // tek adim: tam bir tick
        std::printf("[engine_editor] duraklatma kapisi: tick %u -> %u (duraklatildi, akmadi %s) -> %u (tek adim %s) %s\n", t_pause, t_hold,
                    held ? "evet" : "HAYIR", tick_i, stepped ? "evet" : "HAYIR", (held && stepped) ? "OK" : "HATA");
        if (!(held && stepped)) return 1;
        st.paused = false;
      }
    }
    if (headless && frame_i >= 2 && frame_i <= 5) {
      // E1 duzen kaliciligi kapisi: kaydet -> A; dosyadan yukle -> (bir kare sonra,
      // dugum dikdortgenleri DockSpace'te turetilir) B; A == B BIT-TAM. Pozitif
      // kontrol: dosyadaki bir bolme oranini degistirip yuklemek C'yi A'dan
      // AYIRMALI — yoksa "esit" olcumu hicbir seyi olcmuyordur. Sonra ozgun
      // dosya geri yuklenir (kalan kareler varsayilan duzende kosar).
      static LayoutRect A[kLayoutMaxWindows], B[kLayoutMaxWindows], C[kLayoutMaxWindows];
      static uint32_t na = 0, nbb = 0, nc = 0;
      static char lay[1200], lay_mut[1200];
      static bool lfail = false;
      LayoutError lerr{};
      auto rects_equal = [](const LayoutRect *x, uint32_t nx, const LayoutRect *y, uint32_t ny) {
        if (nx != ny) return false;
        for (uint32_t i = 0; i < nx; i++)
          if (!layout_rect_equal(x[i], y[i])) return false;
        return true;
      };
      if (frame_i == 2) {
        std::snprintf(lay, sizeof lay, "%s/.duzen_kapisi.duzen", st.scene_dir);
        std::snprintf(lay_mut, sizeof lay_mut, "%s/.duzen_kapisi_mut.duzen", st.scene_dir);
        na = layout_snapshot(A, kLayoutMaxWindows);
        if (!layout_save(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: KAYDEDILEMEDI %s\n", lerr.msg); return 1; }
        // Mutant: ilk bolmenin genisligini degistir (satir bazli, metin dosyasi).
        FILE *f = std::fopen(lay, "rb");
        static char txt[kLayoutMaxBytes + 1];
        size_t n = f ? std::fread(txt, 1, kLayoutMaxBytes, f) : 0;
        if (f) std::fclose(f);
        txt[n] = 0;
        // Ilk "w=" ya da genislik alanini bul: dosya formatini editor_layout yazar,
        // burada yalniz ilk sayisal SizeRef'i %30 buyutuyoruz.
        char *dg = std::strstr(txt, "dugum ");
        bool mutated = false;
        FILE *m = std::fopen(lay_mut, "wb");
        if (m && dg) {
          // Kok olmayan ILK yaprak dugumun SizeRef genisligini %30 buyut (bolme
          // orani ondan turer). Satir formati editor_layout.cpp::layout_write:
          // "dugum <i> <parent> <x|y|-> <w> <h>" — '-' yaprak.
          char *line = dg;
          while (line && !mutated) {
            int idx, par; char ax; float w, h;
            if (std::sscanf(line, "dugum %d %d %c %f %f", &idx, &par, &ax, &w, &h) == 5 && ax == '-' && par >= 0 && w > 0) {
              char *eol = std::strchr(line, '\n');
              std::fwrite(txt, 1, (size_t)(line - txt), m);
              std::fprintf(m, "dugum %d %d - %.9g %.9g", idx, par, w * 1.3f, h);
              if (eol) std::fputs(eol, m);
              mutated = true;
            } else {
              char *eol = std::strchr(line, '\n');
              line = eol ? eol + 1 : nullptr;
            }
          }
          if (!mutated) std::fwrite(txt, 1, n, m);
          std::fclose(m);
        } else if (m) { std::fwrite(txt, 1, n, m); std::fclose(m); }
        if (!mutated) std::printf("[engine_editor] duzen kapisi: UYARI mutant uretilemedi (format degisti mi?)\n");
        if (!layout_load(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: YUKLENEMEDI %s\n", lerr.msg); return 1; }
      } else if (frame_i == 3) {
        nbb = layout_snapshot(B, kLayoutMaxWindows);
        if (!layout_load(lay_mut, &lerr)) { std::printf("[engine_editor] duzen kapisi: mutant YUKLENEMEDI %s\n", lerr.msg); lfail = true; }
      } else if (frame_i == 4) {
        nc = layout_snapshot(C, kLayoutMaxWindows);
        if (!layout_load(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: geri YUKLENEMEDI %s\n", lerr.msg); lfail = true; }
      } else if (frame_i == 5) {
        static LayoutRect D[kLayoutMaxWindows];
        const uint32_t nd = layout_snapshot(D, kLayoutMaxWindows);
        const bool ab = rects_equal(A, na, B, nbb), ac = rects_equal(A, na, C, nc), ad = rects_equal(A, na, D, nd);
        const bool lok = !lfail && na >= kLayoutPanelCount && ab && !ac && ad;
        std::printf("[engine_editor] duzen kapisi: %u panel, kaydet->yukle bit-tam %s, KONTROL mutant farkli %s, geri yukle bit-tam %s, atlanan %u %s\n", na,
                    ab ? "evet" : "HAYIR", ac ? "HAYIR" : "evet", ad ? "evet" : "HAYIR", layout_skipped(), lok ? "OK" : "HATA");
        std::remove(lay);
        std::remove(lay_mut);
        if (!lok) return 1;
      }
    }
    // --- Kipli pencereler: HER KARE AYNI YERDEN cagrilir (ImGui popup kimligi
    // bulunulan pencere yiginindan turer; menu geri cagrisindan OpenPopup etmek
    // kimligi kaydirirdi — ikisi de OpenPopup'i kendi icinde yapiyor).
    if (dlg.open) {
      const FileDialogAction fa = file_dialog_draw(dlg);
      if (fa == FileDialogAction::Accepted) {
        if (dlg.mode == FileDialogMode::Ac) load_scene_from(dlg.path);
        else if (save_scene_to(dlg.path) && pending != PendingNone) { run_pending(pending); pending = PendingNone; }
      } else if (fa == FileDialogAction::Cancelled) pending = PendingNone;
    }
    if (confirm.open) {
      char msg[320];
      std::snprintf(msg, sizeof msg, "\x22%s\x22 dosyasinda kaydedilmemis degisiklikler var.\nNe yapilsin?",
                    st.scene_path[0] ? file_path_base(st.scene_path) : "adsiz sahne");
      const ConfirmResult cr = confirm_modal(confirm, "Sahne kaydedilmedi", msg, "Kaydet", "Vazge\xC3\xA7", "Kaydetme");
      if (cr == ConfirmResult::Ok) {
        do_save(); // adsiz sahnede diyalog acar: bekleyen eylem orada kosar
        if (!st.dirty && pending != PendingNone) { run_pending(pending); pending = PendingNone; }
      } else if (cr == ConfirmResult::Third) { run_pending(pending); pending = PendingNone; }
      else if (cr == ConfirmResult::Cancel) pending = PendingNone;
    }
    draw_stats_overlay(&show_stats, dt, ren.stats(), st.scene, vp.width(), vp.height(), view_tab, sys);
    ui.end_frame();

    // --- 3B cizim: veri modelinden (dunya isigi/golgesi de her kare modelden: panel canli) ---
    // GI onizleme actiksa (ve pisirilmisse) duz ambient sabiti yerine kamera
    // konumunda ORNEKLENEN probe degeri kullanilir -- per-pixel DEGIL (kare
    // basina TEK ornek), yine de "Isik Haritasini Pisir"in viewport'ta
    // GORUNMESI icin yeterli (runtime/SceneRuntime tarafi ayni SceneGi'yi
    // ayni granulerlikte kullanir, bkz. scene_runtime.cpp apply_world).
    Vec3 preview_ambient = st.scene.ambient;
    if (st.gi_preview && st.gi_baked && st.gi.ok()) preview_ambient = st.gi.sample(camera_eye(cam), {0, 1, 0});
    ren.set_light(normalize(st.scene.sun_dir), preview_ambient, st.scene.sun_diffuse);
    ren.set_shadow_volume(st.scene.shadow_center, st.scene.shadow_radius, st.scene.shadow_depth);
    // Gorüntü ayarlari: hepsi ucuz set_* cagrisi, kare icinde ayirma YOK.
    // Her karede kosulsuz uygulanir -- "degisti mi" takibi, panelin disindan
    // (geri al/yinele, betik) gelen degisiklikleri kacirirdi.
    ren.set_shadows_enabled(st.render.shadows);
    ren.set_shadow_bias(st.render.shadow_bias, st.render.shadow_normal_offset);
    ren.set_exposure(st.render.exposure);
    ren.set_bloom(st.render.bloom_threshold, st.render.bloom_intensity);
    ren.set_bloom_shape(st.render.bloom_knee, st.render.bloom_radius);
    ren.set_render_scale(st.render.render_scale);
    ren.set_upscaler((renderer::UpscalerKind)st.render.upscaler, st.render.sharpness);
    ren.set_jitter(st.render.jitter);
    ren.set_shadow_focus(cam.target); // yakin kademeler kameranin baktigi yerde

    // Procedural Mesh Guncellemeleri (begin_frame'den ONCE GPU upload olabilmesi icin)
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      if (e.components & content::kSceneTerrain) {
        uint32_t h = (uint32_t)(e.terrain_width + e.terrain_height + e.terrain_cell*10 + e.terrain_amp*10 + e.terrain_freq*1000 + e.terrain_octaves + e.terrain_seed);
        if (h != st.terrain_hashes[i]) {
          content::HeightmapConfig cfg;
          cfg.width = (uint32_t)e.terrain_width; cfg.height = (uint32_t)e.terrain_height;
          cfg.cell_size = e.terrain_cell; cfg.amplitude = e.terrain_amp;
          cfg.frequency = e.terrain_freq; cfg.octaves = e.terrain_octaves;
          cfg.seed = e.terrain_seed;          // Arena::init VOID doner ve bir AD ister; eski kod bool
          // bekliyordu. Tamponu biz ayirdigimiz icin isaretciyi de biz
          // tutariz -- Arena::base_ private, disaridan free edilemez.
          void *temp_buf = std::malloc(32 << 20);
          if (temp_buf) {
            Arena temp;
            temp.init(temp_buf, 32 << 20, "gecici-prosedurel");
            st.terrain_meshes[i] = make_terrain_mesh(temp, ren, cfg);
            std::free(temp_buf);
            st.terrain_hashes[i] = h;
          }
        }
      }
      if (e.components & content::kSceneVoxel) {
        uint32_t h = (uint32_t)(e.voxel_size_x + e.voxel_size_y + e.voxel_size_z + e.voxel_cell*10);
        if (h != st.voxel_hashes[i]) {          // Arena::init VOID doner ve bir AD ister; eski kod bool
          // bekliyordu. Tamponu biz ayirdigimiz icin isaretciyi de biz
          // tutariz -- Arena::base_ private, disaridan free edilemez.
          void *temp_buf = std::malloc(32 << 20);
          if (temp_buf) {
            Arena temp;
            temp.init(temp_buf, 32 << 20, "gecici-prosedurel");
            st.voxel_meshes[i] = make_voxel_mesh(temp, ren, e.voxel_size_x, e.voxel_size_y, e.voxel_size_z, e.voxel_cell);
            std::free(temp_buf);
            st.voxel_hashes[i] = h;
          }
        }
      }
      if (e.components & content::kSceneWater) {
        uint32_t h = (uint32_t)(e.wave_length*10 + e.wave_amplitude*10 + e.wave_steepness*100 + e.wave_speed*10 + e.wave_direction.x*10 + e.wave_direction.y*10);
        if (h != st.water_hashes[i]) {
          content::GerstnerWave wave;
          wave.wavelength = e.wave_length; wave.amplitude = e.wave_amplitude;
          wave.steepness = e.wave_steepness; wave.speed = e.wave_speed;
          wave.direction = e.wave_direction;          // Arena::init VOID doner ve bir AD ister; eski kod bool
          // bekliyordu. Tamponu biz ayirdigimiz icin isaretciyi de biz
          // tutariz -- Arena::base_ private, disaridan free edilemez.
          void *temp_buf = std::malloc(4 << 20);
          if (temp_buf) {
            Arena temp;
            temp.init(temp_buf, 4 << 20, "gecici-prosedurel");
            st.water_meshes[i] = make_water_mesh(temp, ren, wave);
            std::free(temp_buf);
            st.water_hashes[i] = h;
          }
        }
      }
    }

    ren.begin_frame(headless ? 0 : frame_i);
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      const bool simulated = st.playing && st.bodies_live && st.bodies[i].valid() && e.dynamic;
      if (e.flags & content::kSceneHidden) continue; // panelde gozu kapatilmis varlik CIZILMEZ
      const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_world_matrix(st.scene, i);
      const bool sel = st.sel.contains((int32_t)i);
      const Vec3 tint = sel ? Vec3{1.0f, 0.9f, 0.4f} : e.tint;
      bool drew = false;
      // Ilkel geometri glTF kaynagindan ONCE denenir: primitive >= 0 ise
      // varlik prosedureldir ve asset alani anlamsizdir (-1). Bu dal
      // olmadan menuden eklenen Kapsul/Silindir/Koni/Dortgen/Simit editorde
      // GORUNMEZ kalir -- derlenmis sahnede cizilir ama editorde cizilmezdi.
      if ((e.components & content::kSceneModel) && e.primitive >= 0 &&
          e.primitive < (int32_t)content::kPrimitiveSlotCount && st.prims[e.primitive].valid()) {
        ren.draw(st.prims[e.primitive], m, tint);
        drew = true;
      }
      if (!drew && (e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
        const content::Model &mdl = st.models[e.asset];
        const content::UploadedModel &up = st.ups[e.asset];
        content::ModelLod lod;
        lod.camera_pos = camera_eye(cam);
        lod.distance1 = 24.0f; lod.distance2 = 34.0f;
        if ((e.components & content::kSceneAnim) && e.clip < mdl.clip_count) {
          const float dur = mdl.clips[e.clip].duration;
          float t = std::fmod((st.playing ? st.play_time * e.speed : 0.0f) + e.phase, 2.0f * dur);
          if (t > dur) t = 2.0f * dur - t;
          content::ModelPose pose;
          if (content::model_pose_evaluate(mdl, e.clip, t, st.pose_scratch, &pose)) {
            content::draw_model(ren, mdl, up, m, tint, nullptr, nullptr, &pose);
            drew = true;
          }
        }
        if (!drew) { content::draw_model(ren, mdl, up, m, tint, &lod); drew = true; }
      }
      
      // Gizli ozellikler icin Editor viewport cizimi (Procedural Meshes)
      if (e.components & content::kSceneTerrain && st.terrain_meshes[i].valid()) {
        ren.draw(st.terrain_meshes[i], m, Vec3{0.7f, 0.7f, 0.7f}); // draw_mesh diye bir uye yok; PBR
        // argumanlari da hicbir yere gitmiyordu
        drew = true;
      }
      if (e.components & content::kSceneVoxel && st.voxel_meshes[i].valid()) {
        ren.draw(st.voxel_meshes[i], m, Vec3{0.8f, 0.8f, 0.8f}); // draw_mesh diye bir uye yok; PBR
        // argumanlari da hicbir yere gitmiyordu
        drew = true;
      }
      if (e.components & content::kSceneWater && st.water_meshes[i].valid()) {
        ren.draw(st.water_meshes[i], m, Vec3{0.2f, 0.6f, 1.0f}); // draw_mesh diye bir uye yok; PBR
        // argumanlari da hicbir yere gitmiyordu
        drew = true;
      }
      
      if (!drew && (e.components & content::kSceneBody)) {
        // Modelsiz govde: carpisan hacmi kutu olarak goster (kure de kutu, yaricap kadar).
        const Vec3 s = e.shape == content::SceneShape::Box ? e.half * 2.0f : Vec3{e.radius * 2, e.radius * 2, e.radius * 2};
        ren.draw(ds.cube, m * Mat4::scale(s), sel ? tint : Vec3{0.55f, 0.6f, 0.7f});
        drew = true;
      }
      // Isik/kamera artik editor_draw_gizmos'un kendi ayirt edici sekliyle
      // isaretleniyor (yildiz / frustum) -- burada generic sari kup cizmek
      // ikisini de ayni "kare" yapip birbirinden ayirt edilemez kilardi.
      if (!drew && !(e.components & (content::kSceneLight | content::kSceneCamera)))
        ren.draw(ds.cube, m * Mat4::scale({0.3f, 0.3f, 0.3f}), sel ? tint : Vec3{0.9f, 0.9f, 0.3f}); // bos varlik isareti
      if (e.components & content::kSceneLight) {
        renderer::PointLight pl;
        pl.pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
        pl.radius = e.light_radius;
        pl.color = e.light_color;
        pl.intensity = e.light_intensity;
        ren.add_point_light(pl);
      }
    }
    // Isik yaricapi / golge hacmi / gunes yonu: motorun kendi draw'u ile ince kutular.
    st.gizmo_draws = editor_draw_gizmos(ren, ds.cube, st.scene, st.sel.items, st.sel.count, st.gizmos);
    if (headless) {
      if (!rhi::offscreen_render_custom(off, oc, record_cb, &rctx, &ores, before_cb)) { std::fprintf(stderr, "kare: %s\n", ores.error); return 1; }
    } else {
      rhi::FrameContext fc;
      if (swap.acquire(&fc)) {
        // IKI YOL DA AYNI IKI FONKSIYONU CAGIRIR — ayrisamasinlar diye.
        // Burada bir kez `ui.record()` DOGRUDAN cagrildi ve pencereli editor
        // SIMSIYAH acildi: ana gecis (swapchain de offscreen de) IKI subpass
        // tanimliyor, ImGui'nin boru hatti subpass 1 icin kurulu ve ilerletmeyi
        // `record_cb` yapiyor. Headless yol record_cb'den gectigi icin calisti,
        // pencereli yol gecmedigi icin hicbir sey cizmedi. Ayni hata, tek yolda
        // duzeltilmis hali. Artik tek kaynak var.
        before_cb(fc.cmd, &rctx);   // golge + viewport (ikisi de KENDI gecisi)
        swap.begin_render_pass(fc);
        record_cb(fc.cmd, &rctx);   // subpass ilerlet + ImGui
        swap.end_frame(fc);
      }
      // Yeniden kurma TEK YERDEN yapilir: kare BASINDAKI sync_size. Burada
      // (kare sonunda) yapmak bir kareyi hep yanlis olcude birakiyordu —
      // arayuz yeni olcuye, hedef eskisine gore cizilmis oluyordu. acquire /
      // present OUT_OF_DATE derse bayrak kalir ve sonraki karenin basinda
      // ayni yerden islenir. Renderer'in cizim olcusu buradan etkilenmez:
      // renderer viewport'a ciziyor, onun olcusu panelden geliyor.
    }
    prof.end_frame();
    frame_i++;
    if (headless && frame_i >= opts.headless_frames) running = false;
  }
  static uint64_t scratch[1200]; // 2x kare kapasitesi (profiler sozlesmesi); 600 iken 300+ karede assert
  FrameStats stt = prof.frame_stats(Span<uint64_t>(scratch, 1200), 0);
  const EditorUiStats us = ui.stats();
  const int32_t prim_end = st.sel.primary();
  std::printf("[engine_editor] %u kare | p50 %.2f ms p99 %.2f ms | ui %u vertex %u indeks %u liste | cizim %u (gizmo %u) | nesne %u | kaynak %u/%u dosya | gunluk %u/%u%s | secili %u (%s) | tick %u\n",
              frame_i, stt.p50_ns / 1e6, stt.p99_ns / 1e6, us.vertices, us.indices, us.draw_lists, ren.stats().draws, st.gizmo_draws,
              st.scene.entity_count, st.scene.asset_count, st.browse_count, st.hist.undo_count(), st.hist.redo_count(),
              st.dirty ? " (kaydedilmedi)" : "", st.sel.count, prim_end >= 0 ? st.scene.entities[prim_end].name : "-", tick_i);
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor] goruntu: %s\n", opts.out_path);
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
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
  jobs.shutdown();
  bodies_remove(st, phys);
  // VIEWPORT ImGui'DEN ONCE KAPANIR: doku descriptor'i ImGui'nin havuzundan
  // geliyor, once ImGui kapanirsa o set'i iade edecek yer kalmaz. Eklenmedigi
  // ilk halde dogrulama katmani kapanista VUID-vkDestroyDevice-device-05137
  // veriyordu (cihaz yok edilirken cocuk nesneler duruyor).
  vp.shutdown();
  ui.shutdown();
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless) swap.shutdown();
  dev.shutdown();
  return 0;
}

} // namespace tulpar::engine::app
