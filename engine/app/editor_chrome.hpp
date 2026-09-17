// L6 APP — Editor CERCEVESI (chrome): menu cubugu, arac cubugu, durum cubugu.
//
// NEDEN ayri dosya: editor_app.cpp'de "menu cubugu" bugune kadar bes duz dugme
// ve arkasina sikistirilmis bir hata ayiklama dokumuydu ("kure_1 | gunluk 0/1
// | kare 11 | ..."). Profesyonel bir editorde (Unity/Unreal/Godot/Blender) bu
// uc ayri satirdir ve her birinin isi bellidir:
//   menu cubugu  : komutlarin TAMAMI, kategori basina bir menu, kisayol sutunu.
//                  Icerigi editor_commands.hpp'deki tablodan uretilir — burada
//                  hicbir komut adi/kisayolu ELLE yazilmaz (tek kaynak).
//   arac cubugu  : sik kullanilan komutlarin buyuk dugmeleri (oynat, gizmo kipi,
//                  yakalama, kaydet/derle) + sag tarafta sahne adi ve "kirli" noktasi.
//   durum cubugu : tek satir, soluk, hicbir zaman sarmaz — mesaj solda, olcumler
//                  sagda ince dikey cizgilerle ayrilmis.
//
// SIRA SOZLESMESI (uygulama her kare BU sirayla cagirir):
//   chrome_menu_bar(t, s);
//   chrome_toolbar(t, s, &out);
//   chrome_status_bar(s);
//   ImGui::DockSpaceOverViewport(...);   // paneller
// Uc cubuk da ImGui::BeginViewportSideBar ile ana viewport'un CALISMA ALANINI
// (WorkRect) daraltir: menu + arac ustten, durum alttan. DockSpaceOverViewport
// o alani doldurdugu icin dockspace tam cubuklarin ARASINA oturur; hicbir panel
// cubuklarin altina kacmaz. Dikkat: ImGui daraltmayi BIR SONRAKI kareye uygular
// (BuildWorkInset -> WorkInset, bkz. imgui_widgets.cpp BeginViewportSideBar),
// yani ilk karede dockspace cubuklarin altina uzanir, ikinci kareden itibaren
// oturur. Kapi (tests/test_editor_chrome.cpp) bunu ikinci karede olcer.
//
// Renk: yalniz editor_tone() ve ImGui::GetStyleColorVec4() — ham RGB yok.
// Sozlesme: ayirma yok (sabit char tamponlari), STL yok, istisna yok.
#pragma once
#include <cstdint>

#include "app/editor_commands.hpp"

namespace tulpar::engine::app {

// Uygulamanin her kare doldurdugu duz girdi. Isaretciler kare boyunca gecerli
// kalmali (EditorState alanlari); kopyalanmaz.
struct ChromeState {
  bool playing = false;              // sim kosuyor mu (arac cubugu: ▶/■)
  bool dirty = false;                // kaydedilmemis degisiklik (turuncu nokta)
  const char *scene_path = nullptr;  // tam yol; cubuklar yalniz taban adini gosterir
  const char *primary_name = nullptr;// ana secili varligin adi, secim yoksa nullptr
  uint32_t selection_count = 0;      // secili varlik sayisi ("kure_1 +2")
  uint32_t undo_count = 0, redo_count = 0;
  uint32_t frame = 0, tick = 0;
  float frame_ms = 0.0f;             // son karenin suresi
  uint32_t entity_count = 0;
  int gizmo_op = 0;                  // 0 tasi, 1 dondur, 2 olcekle
  bool snap = false;                 // yakalama acik mi (tabloda YOK; ChromeOutput ile geri doner)
  float snap_value = 1.0f;           // yakalama adimi
  bool gizmos_visible = true;        // bilgi: uygulama doldurur; arac cubugu ViewGizmos'un
                                     // tablodaki checked() sorgusunu kullanir (menu ile ayni kaynak)
  const char *status = nullptr;      // durum mesaji (solda, soluk, sigmazsa "…")
  float cam_eye[3] = {0, 0, 0};      // kamera gozu (durum cubugu sag ucu)
};

// Arac cubugundan uygulamaya donen tek sey: yakalama. Yakalama bir KOMUT degil
// (tabloda yok, kisayolu yok), bu yuzden tablo uzerinden degil buradan doner.
struct ChromeOutput {
  bool snap_toggled = false;       // "Yakala" dugmesine basildi: uygulama s.snap'i tersine cevirir
  bool snap_value_changed = false; // adim kaydiraci degisti
  float snap_value = 0.0f;         // her zaman gecerli: degismediyse s.snap_value'nun kopyasi
};

// Menu cubugu: ImGui::BeginMainMenuBar, CommandCategory sirasinda bir menu,
// her menude o kategorinin komutlari (MenuItem: ad, kisayol, isaret, etkinlik).
// Tiklanan oge t.invoke(id) ile calisir; etkin olmayanlar soluk cizilir.
// Menude tablodan GELMEYEN ek oge (bugun: Dosya > "Son dosyalar" alt menusu).
// Kategorinin son komutundan SONRA, EndMenu'den once cagrilir — yani yalniz o
// menu ACIKKEN. Kanca olmasinin sebebi: menu cubugu komut tablosundan uretilir
// ve tablo yalnizca KOMUT tasir; "son dosyalar" bir veri listesidir, komut degil.
struct ChromeMenuExtra {
  void (*fn)(void *ctx, CommandCategory cat) = nullptr;
  void *ctx = nullptr;
};
void chrome_menu_bar(CommandTable &t, const ChromeState &s, ChromeMenuExtra extra = ChromeMenuExtra{});

// Arac cubugu: menu cubugunun altinda tam genislik serit. Soldan saga: oynat/durdur,
// gizmo kipi (uclu bolumlu dugme), yakalama (+ adim), gizmo gorunurlugu,
// kaydet/derle; sagda sahne dosyasinin taban adi ve kirli noktasi.
// out nullptr olabilir (yakalama degisikligi o zaman kaybolur — uygulama vermeli).
void chrome_toolbar(CommandTable &t, const ChromeState &s, ChromeOutput *out);

// Durum cubugu: en altta tek satir. Solda mesaj (soluk, sigmazsa kirpilir),
// sagda dikey cizgilerle ayrilmis olcumler (secim, gunluk, kare/tick, ms,
// varlik sayisi, kamera).
void chrome_status_bar(const ChromeState &s);

// Cubuk yukseklikleri (piksel, stil olcegi dahil). ImGui baglami ister; kare
// icinde cagrilir. Uygulama yerlesim hesabinda, kapilar calisma alani
// sozlesmesinde kullanir.
float chrome_menu_height();
float chrome_toolbar_height();
float chrome_status_height();

// --- Menu modeli -------------------------------------------------------------
// Menu cubugunun cizdigi SIRA: kategori kategori, tablo sirasinda; ayirac
// girdileri CommandId::None. chrome_menu_bar TAM bu listeyi cizer, kapi da
// ayni listeyi sayar (ikinci bir kopya yok). Donus: yazilan girdi sayisi
// (ayiraclar dahil; cap'ten buyuk olabilir — sessiz kirpma yok).
uint32_t chrome_menu_model(const CommandDesc *descs, uint32_t n, CommandId *out, uint32_t cap);

// --- Sonda dikdortgenleri (kapilar icin) ------------------------------------
// Cubuklar cizdikleri onemli parcalarin EKRAN dikdortgenini (x0,y0,x1,y1)
// kaydeder; kapi pikselini tahmin etmek yerine OLCULEN yerden okur. "Son
// cagri"nin degeridir: bir sonraki chrome_* cagrisi kendi girdilerini sifirlar.
// Baglam kapandiktan sonra da okunabilir (duz sayilar).
enum class ChromeRect : uint8_t {
  MenuBar, Toolbar, StatusBar,                              // cubuklarin pencere dikdortgeni
  Transport, GizmoSegments, SnapToggle, SnapValue, GizmoVisible, // arac cubugu parcalari
  SceneName, DirtyDot,                                      // sag blok: ad + nokta hucresi (nokta cizilmese de hucre var)
  StatusMessage, StatusSegments,                            // durum cubugu: sol mesaj, sag olcum blogu
  Count
};
bool chrome_probe_rect(ChromeRect r, float out[4]);
bool chrome_probe_menu_header(CommandCategory c, float out[4]); // menu basligi (cubuktaki)
bool chrome_probe_menu_item(CommandId id, float out[4]);        // yalniz ACIK menude cizildiyse
bool chrome_probe_tool(CommandId id, float out[4]);             // arac cubugundaki dugme

// Son chrome_menu_bar / chrome_toolbar cagrisinin sayimlari.
struct ChromeStats {
  uint32_t menus_submitted = 0;  // BeginMenu cagrisi (kategori sayisi; bos kategori atlanir)
  uint32_t items_enumerated = 0; // modelden gecen komut girdisi (ayirac haric) — acik/kapali fark etmez
  uint32_t items_submitted = 0;  // gercekten cizilen MenuItem (yalniz acik menuler)
  uint32_t tools_submitted = 0;  // arac cubugunda cizilen komut dugmesi
};
ChromeStats chrome_stats();

} // namespace tulpar::engine::app
