// L6 APP — Editor arayuzu: Dear ImGui (vendored, MIT) + ImGuizmo, motorun
// Vulkan cihazi/gecisi uzerinde. Yalniz MASAUSTU editor icin; oyun ici HUD
// kendi 2B cekirdegimizde kalir (0 ayirma, telefon). ImGui malloc kullanir
// (AllocGate 'new' saymaz); editor karesi 0-ayirma kapisinin disindadir.
// Vulkan fonksiyonlari bizim dlopen'li yukleyiciden (LoadFunctions), prototip yok.
#pragma once
#include <cstdint>

#include "content/scene.hpp"
#include "platform/window.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::app {

struct EditorUiStats {
  uint32_t vertices = 0, indices = 0, draw_lists = 0;
};

class EditorUi {
public:
  // rp/subpass: ImGui'nin cizecegi gecis (renk subpass'i). image_count: swapchain
  // goruntu sayisi (>= 2). font_ttf: varsa TrueType (Turkce glifler), yoksa gomulu.
  bool init(rhi::Device &dev, VkRenderPass rp, uint32_t subpass, uint32_t image_count, const char *font_ttf, float font_px);
  void shutdown();
  // Kare: girdi (null = headless, girdi yok), gorunen olcu (mantiksal piksel), dt.
  void begin_frame(const platform::InputState *in, float width, float height, float dt);
  void end_frame(); // ImGui::Render
  void record(VkCommandBuffer cb); // renk subpass'i icinde, 3B ve HUD'dan sonra
  EditorUiStats stats() const { return stats_; }
  bool ok() const { return ok_; }
  const char *last_error() const { return err_; }
  // Fare ImGui pencerelerinin uzerinde mi (sahne kamerasi o zaman girdi almaz).
  bool wants_mouse() const;
  bool wants_keyboard() const;

private:
  rhi::Device *dev_ = nullptr;
  bool ok_ = false;
  bool prev_keys_[512] = {};
  bool prev_mouse_[3] = {};
  double prev_scroll_ = 0;
  EditorUiStats stats_{};
  char err_[128] = {0};
};


// --- Editor mantigi (ImGui'siz) ---------------------------------------------
// Secim kumesi, islem gruplari, kaynak tarayici ve tel gizmo cizimi burada
// yasar; editor paneli (editor_app.cpp) yalniz bunlari cagirir. Neden bu
// dosyada: engine_tests app/ icinden yalniz bu TU'yu derler, kapilar boylece
// editorun CALISTIRDIGI kodu olcer (ikinci bir kopya degil).

// Coklu secim: items[0] = ana secili (gizmo ona bagli), kalani grup.
struct Selection {
  static constexpr uint32_t kMax = content::kSceneMaxEntities;
  int32_t items[kMax] = {};
  uint32_t count = 0;
  int32_t primary() const { return count ? items[0] : -1; }
  bool contains(int32_t i) const;
  void clear() { count = 0; }
  void set_single(int32_t i);               // i < 0: temizler
  bool toggle(int32_t i);                   // Ctrl+tik: ekle/cikar; donus = eklendi mi
  void erase(int32_t i);
  void after_remove(int32_t removed);       // varlik silindi: indeksleri kaydir
  uint32_t sorted_desc(int32_t *out) const; // grup silme sirasi (buyukten kucuge)
};

// Bir kullanici eylemi gunlukte N ardisik islem olabilir (grup tasima = N
// set_entity, grup silme = N remove_entity). content::SceneHistory islem
// basina calisir; grup sinirlari burada tutulur, geri al/yinele grubu birlikte
// isler. Bilinmeyen sinir (tasma / temizlenmis) = 1 islem: hicbir zaman
// gunlukten fazlasini tuketmez, cagiran undo donusune bakar.
class OpGroups {
public:
  void push(uint32_t n); // n islem = tek eylem (n == 0 yok sayilir)
  uint32_t undo_size();
  uint32_t redo_size();
  void clear() { count_ = cursor_ = 0; }
  uint32_t depth() const { return cursor_; }

private:
  static constexpr uint32_t kCap = 256;
  uint32_t sizes_[kCap] = {};
  uint32_t count_ = 0, cursor_ = 0;
};

// Kaynak tarayici: sahne dosyasinin dizinindeki glTF dosyalari (POSIX dirent).
struct AssetFile {
  char name[content::kScenePathLen] = {0};
  bool in_scene = false; // sahnenin kaynak tablosunda kayitli mi
  int32_t index = -1;    // kayitliysa kaynak indeksi
};
// dir icindeki *.gltf / *.glb dosyalari, ada gore sirali (belirlenimli: readdir
// sirasi dosya sistemine bagli). Donus: bulunan sayi (cap ile sinirli).
uint32_t editor_scan_assets(const char *dir, const content::SceneDesc &d, AssetFile *out, uint32_t cap);
// Kaynagi sahneye ekler (varsa mevcut indeks) ve o kaynakla yeni bir varlik
// kurar (kSceneModel). Kaynak tablosu eklemesi gunluge GIRMEZ (tablo append-only;
// geri al varligi siler, kaynak satiri kalir). Donus: gunluge giren islem sayisi
// (0 = eklenemedi); out_asset = kaynak indeksi.
uint32_t editor_add_asset_entity(content::SceneDesc &d, content::SceneHistory &h, const char *file, Vec3 pos, int32_t *out_asset);

// Surukleme bitince grubu gunluge yazar: once hepsi 'before'a dondurulur, sonra
// SceneHistory once/sonra kaydeder (her varlik bir islem). Donus: islem sayisi.
uint32_t selection_commit(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n,
                          const content::SceneEntity *before, const content::SceneEntity *after);
// Grup tasima: secili varliklarin tamamini delta kadar oteler, tek grup.
uint32_t selection_translate(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n, Vec3 delta);
// Grup silme: buyukten kucuge (indeksler kaymasin). Donus: islem sayisi.
uint32_t selection_remove(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n);

// Isik/golge gizmolari: AYRI BIR CIZGI BORU HATTI YOK — motorun kendi draw'u
// ile ince kutulardan tel cerceve (birim kup mesh'i, +-0.5).
struct GizmoOptions {
  bool light_radius = true;  // isik varliklarinin yaricapi (tel kutu)
  bool shadow_volume = true; // Dunya panelindeki golge hacmi (tel kutu)
  bool sun_dir = true;       // gunes yonu (ok; isiga dogru)
  float thickness = 0.06f;   // tel kalinligi (dunya birimi)
};
// Donus: yapilan ren.draw cagrisi sayisi (secili isik daha parlak cizilir).
uint32_t editor_draw_gizmos(renderer::Renderer &ren, renderer::MeshHandle cube, const content::SceneDesc &d, const int32_t *sel,
                            uint32_t n, const GizmoOptions &o);

} // namespace tulpar::engine::app
