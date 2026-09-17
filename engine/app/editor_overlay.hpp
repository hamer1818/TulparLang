// L6 APP — Gorunum (viewport) KAPLAMASI ve KAYNAKLAR (asset) tarayicisi.
//
// Iki parca, ikisi de yalniz ImGui ile cizer, motorun 3B tarafina dokunmaz:
//
//  1) viewport_overlay: 3B goruntunun USTUNE bilgi katmani (Unity Scene view /
//     Unreal viewport gelenegi): sol-ust "hap" satiri (izdusum, gizmo kipi,
//     kare istatistigi, oynatma cipi), sag-ust EKSEN GOSTERGESI (X/Y/Z, derinlik
//     sirali), sol-alt kamera okumasi, sag-alt ipucu ve odak cercevesi. Neredeyse
//     tamami GetWindowDrawList ile cizilir; ETKILESIMLI OGE yalniz UC YERDE ve
//     yalniz FARE TAM USTUNDEYKEN eklenir (eksen gostergesinin DISKI + iki/uc
//     cip). "Yalniz ustundeyken" sozlesmesi zorunlu: oge her karede eklenseydi
//     goruntunun o kosesi tiklamayi/ustunde-durmayi kaybederdi ve orada secim
//     yapilamazdi. Diskin DISINDA kalan her piksel altindaki ImGui::Image'e
//     duser (kapi bunu olcer). Her sey GetFontSize ve stilden
//     olceklenir; dikdortgen kucukse sigmayan parca ATLANIR (ustuste yazi yok)
//     ve her cizim r'ye kirpilir (disina tek piksel tasmaz — kapi bunu olcer).
//
//  2) assets_panel: Kaynaklar panelinin tum govdesi (Unity Project / Unreal
//     Content Browser): SOLDAN kirpilmis dizin yolu (kuyruk kalir), yenile /
//     izgara-liste / karo boyutu / arama; kaydirilabilir karo izgarasi ya da
//     sikisik liste; altta katlanabilir "Sahnedeki kaynaklar (N)" (yuklendi /
//     YUKLENEMEDI durum noktasi). Panel eylemleri (yenile, sahneye ekle) DISARI
//     verilir: bu birim sahneyi DEGISTIRMEZ, editor_app.cpp AssetsAction'i isler.
//
// Renkler yalniz editor_tone paletinden; ham RGB burada YOK. Ayirma yok (sabit
// tamponlar), STL yok. Kapilar: tests/test_editor_overlay.cpp.
#pragma once
#include <cstdint>

#include "app/editor_camera.hpp"   // CameraAxis, CameraMode, CameraProjection, GizmoSpace
#include "app/editor_ui.hpp"       // AssetFile, Tone, editor_ellipsize
#include "app/editor_viewport.hpp" // ViewportRect
#include "content/scene.hpp"       // kScenePathLen, SceneBounds

namespace tulpar::engine::app {

// --- 1) Gorunum kaplamasi ----------------------------------------------------
struct OverlayInfo {
  // Kamera GORUNUM matrisi: motorun Mat4'u, SUTUN-MAJOR (renderer::set_camera'ya
  // verilenin aynisi; &cam.view().m[0][0]'dan 16 float kopyalanir). Yalniz
  // 3x3 donus kismi okunur; ceviri sutunu yok sayilir.
  float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float cam_eye[3] = {0, 0, 0};
  float cam_target[3] = {0, 0, 0};
  int gizmo_op = 0; // 0 tasi, 1 dondur, 2 olcekle (editor_app gizmo_op ile ayni)
  bool gizmos_visible = true;
  bool playing = false;
  bool hovered = false; // fare goruntunun ustunde (ImGui::IsItemHovered)
  bool focused = false; // panel odakli (ImGui::IsWindowFocused)
  float frame_ms = 0;   // son kare suresi (0 = bilinmiyor: istatistik hapi gizlenir)
  uint32_t draw_calls = 0, entity_count = 0;
  const char *hint = nullptr; // sag-alt ipucu; nullptr = yok
  // Tiklanabilir ciplerin ETIKETI buradan gelir — kaplama kamerayi DEGISTIRMEZ,
  // yalniz "tiklandi" der (OverlayResult); durumu degistiren editor_app.cpp'dir.
  CameraProjection proj = CameraProjection::Perspective;
  CameraMode cam_mode = CameraMode::Orbit;
  GizmoSpace gizmo_space = GizmoSpace::World;
  // Golgeleme kipi etiketi ("Duz", "Tel kafes", ...). nullptr = hap cizilmez
  // (bugun editorde tek kip var; alan ileriye donuk ve kapilari bozmuyor).
  const char *shading = nullptr;
};

// Eksen gostergesinin SAF izdusumu (cihazsiz, ImGui'siz — kapi bunu olcer).
// Dunya +X/+Y/+Z birim vektorlerinin gorunum uzayindaki karsiligi: (x, y) ekran
// pikseli ofseti (merkeze gore, EKRAN y'si ASAGI), depth = kameraya dogru (+1)
// / uzaga (-1). radius = ucun merkezden uzakligi (piksel).
struct AxisProjection {
  float x[3] = {0, 0, 0};
  float y[3] = {0, 0, 0};
  float depth[3] = {0, 0, 0};
};
void overlay_project_axes(const float view[16], float radius, AxisProjection *out);

// Ekran dikdortgeni (kaplamanin olcum ciktilari icin). x < 0 = yok.
struct OverlayRect {
  float x = -1, y = 0, w = 0, h = 0;
  bool valid() const { return x >= 0 && w > 0 && h > 0; }
  bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Cizimin OLCUM ciktisi: neyin cizildigi ve nereye (kapilar piksel orneklerken
// yerlesimi yeniden turetmez, buradan okur). nullptr verilebilir.
struct OverlayLayout {
  bool top_row = false;   // sol-ust hap satiri (en az bir hap)
  bool gizmo = false;     // sag-ust eksen gostergesi
  bool camera = false;    // sol-alt kamera hapi
  bool hint = false;      // sag-alt ipucu
  bool border = false;    // odak/ustunde cercevesi
  float gizmo_cx = 0, gizmo_cy = 0; // gostergenin merkezi (ekran)
  float gizmo_r = 0;                // eksen ucu yaricapi (merkezden uc merkezine)
  float gizmo_end_r = 0;            // uc diskinin yaricapi
  uint32_t pills = 0;               // cizilen hap sayisi (ust satir)
  // Tiklanabilir ciplerin ekran dikdortgeni (kapilar sentetik tiki buraya atar,
  // yerlesimi yeniden turetmez). x < 0 = o cip bu karede CIZILMEDI (dar panel).
  OverlayRect chip_proj{}, chip_mode{}, chip_space{};
  bool box = false; // kutu (marquee) secim dikdortgeni cizildi
};

// Kaplamanin bu karede urettigi ETKILESIM. Kaplama hicbir durumu degistirmez;
// cagiran (editor_app.cpp) bunlari kamera/secim uzerinde uygular.
struct OverlayResult {
  int axis_clicked = -1; // -1 yok; degilse (int)CameraAxis — camera_align'a verilir
  int axis_hovered = -1; // vurgulanan uc (yalniz gorsel; kapi bunu da olcer)
  bool ortho_toggled = false;       // "Perspektif/Ortografik" cipine tiklandi
  bool mode_toggled = false;        // "Yorunge/Ucus" cipine tiklandi
  bool gizmo_space_toggled = false; // "Dunya/Yerel" cipine tiklandi
  // Kutu (marquee) secim. box_active: surukleme SURUYOR (dikdortgen cizildi).
  // box_done: BU KARE birakildi -> secim UYGULANIR. Ikisi de box[] doludur.
  bool box_active = false, box_done = false;
  float box[4] = {0, 0, 0, 0}; // x0, y0, x1, y1 EKRAN pikseli, min/max normalize
  // Kaplamanin bir ogesi fareyi aldi (gosterge diski ya da bir cip). Cagiran bu
  // karede 3B secim isini ATMAZ — yoksa gostergeye tiklamak ayni anda arkadaki
  // nesneyi de secerdi.
  bool consumed_mouse = false;
};

// ImGui::Image(...) HEMEN sonrasinda, ayni pencere icinde cagrilir. r = imgenin
// ekran dikdortgeni (ViewportRect{origin.x, origin.y, w, h}). out_res verilirse
// bu karenin etkilesimi doldurulur (bkz. OverlayResult).
//
// ⚠ Argumanlarin sirasi BILEREK (layout, result): eski iki/uc argumanli cagrilar
// (editor_app.cpp ve mevcut kapilar) degismeden derlensin diye.
//
// CAGIRANIN SOZLESMESI (bu sirayla):
//   if (res.axis_clicked >= 0) camera_align(cam, (CameraAxis)res.axis_clicked);
//   if (res.ortho_toggled) cam.proj = ...; if (res.mode_toggled) cam.mode = ...;
//   if (res.box_done) -> viewport_box_select ile secimi kur (tek tik YOK)
//   else if (tek tik && !res.consumed_mouse) -> isinla sec
// res.consumed_mouse true iken 3B secim isini ATMA ve kamerayi fareyle surme.
void viewport_overlay(const ViewportRect &r, const OverlayInfo &info, OverlayLayout *out_layout = nullptr, OverlayResult *out_res = nullptr);

// Kutu (marquee) secimin SAF matematigi — ImGui'siz, cihazsiz (kapi bunu olcer).
//
// Her sinir kutusunun 8 kosesi view_proj ile izdusurulur, ekran dikdortgenine
// (view) eslenir ve dikdortgenle karsilastirilir.
//
// ⚠ KAMERANIN ARKASI: w <= 0 olan kose BOLUNEMEZ. Klasik hata bolmeyi yine de
// yapmaktir — negatif w izdusumu kokten AYNALAR ve arkadaki nesne dikdortgenin
// icine "dusermis" gibi gorunur (kullanici sahnenin yarisini kazara secer).
// Burada: tum koseler arkadaysa kutu ASLA secilmez; bir kismi arkadaysa
// (yakin duzlemi kesiyor) yalniz ONDEKI koselerin ekran kutusu kullanilir ve
// "tam icerme" istendiginde kutu secilmez — cunku gorunmeyen parcasi
// dikdortgenin icinde OLDUGU iddia edilemez.
//
// require_full_containment: true = kutu TAMAMEN dikdortgenin icinde olmali
// (Unreal varsayilani), false = kesismesi yeter (Unity/Blender varsayilani).
// out == nullptr verilebilir: yalniz SAYAR. Donus: out'a YAZILAN sayi (cap ile
// sinirli; cap dolarsa kalanlar atlanir — cagiran cap'i secim tavani kadar versin).
uint32_t viewport_box_select(const Mat4 &view_proj, const content::SceneBounds *bounds, uint32_t n, const ViewportRect &view, float x0, float y0,
                             float x1, float y1, bool require_full_containment, int32_t *out, uint32_t cap);

// Yolu SOLDAN "…" ile kirpar: kuyruk (dosya/dizin adi) kalir, tercihen bir '/'
// sinirinda ("…/tests/assets"). ImGui baglami gerekir. out NUL ile biter;
// donus: yazilan bayt. editor_ellipsize'in (sagdan) ikizi.
uint32_t overlay_ellipsize_left(const char *s, float max_w, char *out, uint32_t cap);

// --- 2) Kaynaklar paneli -----------------------------------------------------
struct AssetsView {
  bool grid = true;        // karo izgarasi / sikisik liste
  float tile = 96.0f;      // karo genisligi (piksel, 64..192'ye kenetlenir)
  char filter[64] = {0};   // ad suzgeci (buyuk/kucuk harf duyarsiz alt dizi)
  bool scene_open = true;  // "Sahnedeki kaynaklar" bolumu acik mi
};
struct AssetsAction {
  bool refresh = false; // dizin yeniden taransin
  int add_index = -1;   // files[i] sahneye eklensin (-1 = yok)
};
// Yerlesimin OLCUM ciktisi (kapilar bir karoya "tiklarken" izgarayi yeniden
// turetmez): govde cocugunun ekran dikdortgeni, karo olcusu, sutun sayisi ve
// ilk karonun sol-ust kosesi. Liste kipinde tile_h = satir yuksekligi, cols = 1.
struct AssetsLayout {
  float body_x = 0, body_y = 0, body_w = 0, body_h = 0;
  float origin_x = 0, origin_y = 0; // ilk karo/satir sol-ust (ekran)
  float tile_w = 0, tile_h = 0, gap = 0;
  int cols = 0;
  uint32_t shown = 0;   // suzgecten gecen dosya
  bool empty = false;   // bos durum metni cizildi
};
// Kaynaklar panelinin govdesi (ImGui::Begin/End cagiranindir). files: dizindeki
// glTF'ler (editor_scan_assets), scene_assets/loaded: sahnenin kaynak tablosu.
// out: bu karede istenen eylemler (her karede sifirlanir).
void assets_panel(AssetsView &v, const char *dir, const AssetFile *files, uint32_t file_count,
                  const char (*scene_assets)[content::kScenePathLen], const bool *loaded, uint32_t scene_asset_count,
                  AssetsAction *out, AssetsLayout *out_layout = nullptr);

} // namespace tulpar::engine::app
