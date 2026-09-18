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

// Tulpar Koyu temasini gecerli ImGui baglamina uygular: TEK palet (editor_ui.cpp
// icindeki isimli sabitler) tum ImGuiCol_* girdilerini besler, olculer scale ile
// carpilir (ImGuiStyle::ScaleAllSizes). IDEMPOTENT: stil her cagrida sifirdan
// kurulur, bu yuzden tekrar tekrar cagirmak olculeri BUYUTMEZ.
//   scale       : 1.0 = 96 dpi. Kenar yuvarlaklig/bosluk/yazi birlikte olceklenir.
//   srgb_target : hedef ek *_SRGB formatinda mi. ImGui sRGB'den habersizdir; renk
//                 dogrudan yazilir ve donanim onu DOGRUSAL sanip kodlar. true ise
//                 palet sRGB -> dogrusal cevrilir, boylece ekranda yazilan altigen
//                 degerin ta kendisi cikar (kontrol: false, gozle acik/yikanmis).
void editor_apply_theme(float scale, bool srgb_target);

// --- Palet DISA ACIK ---------------------------------------------------------
// Editorun uc arayuz katmani (cerceve: menu/arac/durum cubugu, ozellik
// widget'lari, viewport kaplamasi) AYNI paletten beslensin diye. Renk, stilin
// RENK UZAYINDADIR: editor_apply_theme(srgb_target=true) sonrasi dogrusaldir ve
// dogrudan ImGui::PushStyleColor / ImDrawList'e verilir, bir daha CEVRILMEZ.
// Tema uygulanmadan once (baglam yokken) ham sRGB doner.
enum class Tone : uint8_t {
  Bg0, Bg1, Bg2, Bg3, Bg4, Line, Text, TextDim, Accent, AccentHi, AccentLo, Warn, White,
  AxisX, AxisY, AxisZ, // vec3 rozetleri + eksen gostergesi: kirmizi/yesil/mavi (Unity/Blender gelenegi)
  Ok, Err,             // durum noktalari: yuklendi / yuklenemedi
  Input,               // COKUK yuzey: girdi kutusu zemini (panelden KOYU)
  TextMute,            // pasif metin, yer tutucu (TextDim'den de soluk)
  Select,              // liste/agac secim seridi (doygun DEGIL)
  Count
};
// out[4] = r,g,b,a (0..1). Gecersiz t: Text.
void editor_tone(Tone t, float out[4]);

// --- Tipografi olcegi --------------------------------------------------------
// Tek font yuzu, UC boyut kademesi. Vendored ImGui 1.92 dinamik boyut
// destekledigi icin (PushFont(NULL, px)) ikinci bir TTF gerekmiyor.
// AGIRLIK yok; hiyerarsi boyut + renk kademesi + BUYUK HARF ile kurulur
// (bkz. docs/engine/EDITOR-TASARIM.md §4).
enum class TextSize : uint8_t {
  Sm, // 11/13 — durum cubugu, sutun basligi, ust veri, rozet
  Md, // taban — etiket, deger, menu, dugme
  Lg, // 15/13 — panel basligi, secili nesne adi
};
// Push/pop CIFTLER halinde cagrilir. Baglam yoksa ikisi de sessizce doner.
void push_text_size(TextSize s);
void pop_text_size();

// Metni max_w piksele sigacak sekilde "…" ile kirpar (UTF-8 sinirinda keser).
// ImGui baglami gerekir (CalcTextSize). Sigiyorsa oldugu gibi kopyalar. out her
// zaman NUL ile biter. Donus: yazilan uzunluk (bayt).
uint32_t editor_ellipsize(const char *s, float max_w, char *out, uint32_t cap);

class EditorUi {
public:
  // rp/subpass: ImGui'nin cizecegi gecis (renk subpass'i). image_count: swapchain
  // goruntu sayisi (>= 2). font_ttf: varsa TrueType (Turkce glifler), yoksa gomulu.
  // ui_scale: DPI / kullanici olcegi (bkz. set_ui_scale). srgb_target: bkz.
  // editor_apply_theme. Eski dort-argumanli cagrilar degismeden derlenir.
  // icon_ttf: metin fontunun atlasina BIRLESTIRILECEK ikon fontu (Material
  // Icons). null/bulunamadi: sessizce atlanir, arayuz calisir ama ikon
  // yerine eksik-glif kutusu cizilir. Neden gerekli: DejaVuSans bir METIN
  // fontudur ve editorun kullandigi sembollerin bir kismini (kamera, ses,
  // betik, istatistik... olculdu: 16 kod noktasi, 34 cagri) ICERMEZ.
  bool init(rhi::Device &dev, VkRenderPass rp, uint32_t subpass, uint32_t image_count, const char *font_ttf, float font_px,
            float ui_scale = 1.0f, bool srgb_target = true, const char *icon_ttf = nullptr);
  void shutdown();
  // Ikon fontu atlasa birlestirildi mi. false ise ICON_MD_* glifleri
  // eksik-glif kutusu olarak cizilir (font dosyasi bulunamadi).
  bool icons_ok() const { return icons_ok_; }
  // Kare: girdi (null = headless, girdi yok), gorunen olcu (GORUNTU piksel), dt.
  void begin_frame(const platform::InputState *in, float width, float height, float dt);
  void end_frame(); // ImGui::Render
  void record(VkCommandBuffer cb); // renk subpass'i icinde, 3B ve HUD'dan sonra
  EditorUiStats stats() const { return stats_; }
  bool ok() const { return ok_; }
  const char *last_error() const { return err_; }
  // Arayuz olcegi (DPI ya da kullanici tercihi): stil olculeri + yazi boyutu.
  // Kare icinde de degistirilebilir (1.92 yazi tipini yeniden pisirir). Gecerli
  // bir deger 0.5..4 arasina KIRPILIR; gecersiz bir deger (<= 0, NaN) ise sessizce
  // kirpilmaz, VARSAYILANA (1.0) doner — sifirlanmis bir alan arayuzu okunmaz
  // kucukluge dusurmesin. Kaynak disaridadir: pencere sistemi contents-scale'i,
  // ayar dosyasi ya da menudeki bir kaydirac.
  void set_ui_scale(float s);
  float ui_scale() const { return ui_scale_; }
  // Isaretci olcegi: girdi MANTIKSAL pikselde gelirken (glfwGetCursorPos) gorunen
  // olcu CERCEVE TAMPONU pikselinde ise (HiDPI'da ikisi ayni degildir) fare konumu
  // bununla carpilir. 1.0 = ayni olcek. Yanlis birakilirsa fare arayuzun yaninda
  // durur: tiklamalar ISKALAR ve wants_mouse() yanlis cevap verir.
  void set_pointer_scale(float s);
  float pointer_scale() const { return pointer_scale_; }
  // Fare ImGui pencerelerinin uzerinde mi (sahne kamerasi o zaman girdi almaz).
  // ImGui suruklemenin NEREDE basladigini kendi izler (io.MouseDownOwned): 3B'de
  // baslayan surukleme pencerenin uzerinden gecse de yakalanmaz, tersi de boyle
  // — kosul her iki uca da dogru cevap verir, yeter ki tus olaylari beslensin.
  bool wants_mouse() const;
  // ImGui bir ogeyi etkin tutuyor mu (metin kutusu, kaydirac surukleme) ya da
  // kipli pencere acik mi. Klavye gezinmesi ACIK olsa bile bu bayrak SADECE
  // bunlarla true olur (io.ConfigNavCaptureKeyboard = false): yoksa odakli bir
  // pencere varken surekli true kalir ve editorun ham tus kisayollari (T/R/S) olur.
  bool wants_keyboard() const;
  // Su an METIN yaziliyor mu. Ham harf kisayollari icin dogru kosul budur;
  // wants_keyboard() daha genistir (kaydirac suruklerken de true).
  bool wants_text_input() const;

private:
  void push_input(const platform::InputState *in);

  rhi::Device *dev_ = nullptr;
  bool ok_ = false;
  bool prev_keys_[512] = {};
  bool prev_mouse_[3] = {};
  double prev_scroll_ = 0;
  float ui_scale_ = 1.0f;
  float pointer_scale_ = 1.0f;
  float font_px_ = 0;
  bool srgb_target_ = true;
  bool icons_ok_ = false; // ikon fontu atlasa girdi mi (kapilar bunu sorar)
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
  bool light_radius = true;    // isik varliklarinin yaricapi (tel kutu)
  bool light_glyph = true;     // isik varligini isaretleyen yildiz/isin sekli (kup DEGIL)
  bool shadow_volume = true;   // Dunya panelindeki golge hacmi (tel kutu)
  bool sun_dir = true;         // gunes yonu (ok; isiga dogru)
  bool camera_frustum = true;  // kamera varliklarinin govdesi + gorus alani (tel kafes)
  float thickness = 0.06f;     // tel kalinligi (dunya birimi)
};
// Donus: yapilan ren.draw cagrisi sayisi (secili isik daha parlak cizilir).
uint32_t editor_draw_gizmos(renderer::Renderer &ren, renderer::MeshHandle cube, const content::SceneDesc &d, const int32_t *sel,
                            uint32_t n, const GizmoOptions &o);

} // namespace tulpar::engine::app
