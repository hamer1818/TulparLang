// L6 APP — Gorunum (viewport) KAPLAMASI ve KAYNAKLAR (asset) tarayicisi.
//
// Iki parca, ikisi de yalniz ImGui ile cizer, motorun 3B tarafina dokunmaz:
//
//  1) viewport_overlay: 3B goruntunun USTUNE bilgi katmani (Unity Scene view /
//     Unreal viewport gelenegi): sol-ust "hap" satiri (izdusum, gizmo kipi,
//     kare istatistigi, oynatma cipi), sag-ust EKSEN GOSTERGESI (X/Y/Z, derinlik
//     sirali), sol-alt kamera okumasi, sag-alt ipucu ve odak cercevesi. Yalniz
//     GetWindowDrawList ile cizilir — ETKILESIMLI OGE YOK, altindaki ImGui::Image
//     tiklamayi/ustunde-durmayi almaya devam eder. Her sey GetFontSize ve stilden
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

#include "app/editor_ui.hpp"       // AssetFile, Tone, editor_ellipsize
#include "app/editor_viewport.hpp" // ViewportRect
#include "content/scene.hpp"       // kScenePathLen

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
};

// ImGui::Image(...) HEMEN sonrasinda, ayni pencere icinde cagrilir. r = imgenin
// ekran dikdortgeni (ViewportRect{origin.x, origin.y, w, h}).
void viewport_overlay(const ViewportRect &r, const OverlayInfo &info, OverlayLayout *out_layout = nullptr);

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
