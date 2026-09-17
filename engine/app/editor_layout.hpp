// L6 APP — Editor panel duzeninin BELIRLENIMLI kaliciligi: varsayilan yerlesimi
// programatik kurar, gecerli duzeni kendi metin dosyamiza yazar, geri okur ve
// panel dikdortgenlerini OLCULEBILIR bicimde disari verir.
//
// NEDEN KENDI FORMATIMIZ (imgui.ini DEGIL): editor_ui.cpp'de io.IniFilename =
// nullptr — ImGui'nin kendi ini'si kapali. O dosya metin olsa da hem icerigi
// hem SIRASI ImGui'nin ic durumuna baglidir (dugum kimlikleri DockContext'in
// sayacindan uretilir, pencere listesi sirasizdir) ve surumle degisir; uzerine
// "kaydet -> yukle -> panel dikdortgenleri BIT BIT ayni" diye bir kapi
// kurulamaz. Buradaki dosya .sahne ile ayni disiplinde: ASCII, satir tabanli,
// ayni girdi -> AYNI BAYT.
//
// BELIRLENIMLILIK NEREDEN GELIYOR (dordu de gerekli):
//   1) Dugum sirasi = agacin ON-SIRALI (preorder) gezintisi — ImGui'nin ic
//      tablosundaki sira DEGIL. Kok 0, sonra cocuk 0 alt agaci, sonra cocuk 1.
//   2) Pencere sirasi = dugum icinde SEKME sirasi (ImGuiTabBar::Tabs), sekme
//      cubugu yoksa ada gore siralama. Yazilan `sira` ham DockOrder degil,
//      0..n-1 KANONIK sirasidir; boylece yaz(oku(yaz(x))) == yaz(x).
//   3) Dugum KIMLIKLERI dosyada YOK. ImGui onlari DockContextGenNodeID ile
//      kendisi uretir; kaydedilirlerse dosya ImGui'nin ic sayacina baglanirdi.
//      Agac yeniden BOLMELERLE kurulur, kimlikler yeniden turetilir. Dosyadaki
//      tek kimlik `kok` satirindaki dockspace kimligidir ve o da BILGI icindir
//      (etkin kok host'un verdigi kimliktir, bkz. layout_set_dockspace_id).
//   4) Geometri olarak SizeRef yazilir — ImGui'nin DockNodeTreeUpdatePosSize
//      fonksiyonunun TEK girdisi odur (Pos/Size her karede ondan turetilir).
//      Ayni kok dikdortgeni + ayni stil + ayni SizeRef = ayni piksel.
// Ondalik sayilar .sahne yazicisiyla ayni kuralla yazilir: %.6g'den %.9g'ye,
// strtof ile BIT-TAM geri okunan ilk gosterim (bkz. content/scene.cpp).
//
// AYIRMA: bu birimde heap ayirma yoktur (sabit diziler + cagiranin tamponu).
// Duzen islemleri (kur/yukle) ImGui'nin kendi ayiricisini calistirir, ama
// yalniz duzen degistiginde — KARE ICINDE degil.
//
// KARE ICINDE CAGRILIR: layout_apply_default / layout_load / layout_save /
// layout_snapshot ImGui::NewFrame ile ImGui::Render arasinda cagrilmalidir
// (DockBuilder DockSpace'i kullanir, o da gecerli pencereyi okur). Kare disinda
// cagrilirsa sessizce degil, acik bir hatayla doner.
#pragma once
#include <cstddef>
#include <cstdint>

#include <imgui.h>

namespace tulpar::engine::app {

constexpr uint32_t kLayoutVersion = 1;
constexpr uint32_t kLayoutNameLen = 32;    // NUL dahil
constexpr uint32_t kLayoutMaxNodes = 32;   // kok + bolmeler + yapraklar
constexpr uint32_t kLayoutMaxWindows = 24;
constexpr uint32_t kLayoutMaxBytes = 4096; // dosya tavani (tavan ASILIRSA hata)

// Editorun kalici panelleri. Adlar editor_app.cpp'deki ImGui::Begin(...)
// adlariyla BIREBIR ayni olmali: dosyadaki ad bir ImGui pencere kimligidir.
// Bu tablo ayni zamanda SOZLESMEDIR — yuklemede tabloda olmayan bir ad HATA,
// kaydetmede tabloda olmayan bir pencere YAZILMAZ (sayisi: layout_skipped()).
constexpr const char *kPanelSahne = "Sahne";           // hiyerarsi
constexpr const char *kPanelGorunum = "Gorunum";       // 3B goruntu kapisi
constexpr const char *kPanelOzellikler = "Ozellikler"; // secili varlik
constexpr const char *kPanelKaynaklar = "Kaynaklar";   // kaynak tarayici
constexpr const char *kPanelDunya = "Dunya";           // gunes/golge/kamera
constexpr const char *kPanelKonsol = "Konsol";         // motor/Vulkan/sahne iletileri
constexpr uint32_t kLayoutPanelCount = 6;
// Gorunen ETIKETLER: ImGui'nin "###" kurali — kimlik ### SONRASIDIR, oncesi
// yalniz gosterilir. Boylece sekmede "Görünüm" yazar, dosyada/kimlikte
// "Gorunum" kalir: etiket dil ya da surumle degisse de kayitli duzen yuklenir.
constexpr const char *kPanelSahneLabel = "Sahne";
constexpr const char *kPanelGorunumLabel = "G\xC3\xB6r\xC3\xBCn\xC3\xBCm###Gorunum";        // Görünüm
constexpr const char *kPanelOzelliklerLabel = "\xC3\x96zellikler###Ozellikler";           // Özellikler
constexpr const char *kPanelKaynaklarLabel = "Kaynaklar";
constexpr const char *kPanelDunyaLabel = "D\xC3\xBCnya###Dunya";                          // Dünya
constexpr const char *kPanelKonsolLabel = "Konsol";
const char *layout_panel_name(uint32_t i); // i < kLayoutPanelCount, yoksa null
bool layout_is_panel(const char *name);

// Varsayilan yerlesimin sektor oranlari (kok olcusunun kesri).
constexpr float kLayoutDefaultLeft = 0.18f;   // Sahne
constexpr float kLayoutDefaultRight = 0.22f;  // Ozellikler
constexpr float kLayoutDefaultBottom = 0.25f; // Kaynaklar + Dunya (sekmeli)

struct LayoutError {
  char msg[192];
  uint32_t line = 0; // 0 = satirsiz (dosya acilamadi, docking kapali, ...)
};

// Dosyanin bellek karsiligi. ImGui'siz uretilebilir/yazilabilir/okunabilir —
// kapi format ile ImGui durumunu AYRI olcebilsin diye.
struct LayoutNode {
  int32_t parent = -1;             // -1 = kok (yalniz 0. dugum)
  int32_t child[2] = {-1, -1};     // -1 = yaprak
  uint8_t axis = 2;                // 0 = X (yan yana), 1 = Y (alt alta), 2 = yaprak
  float w = 0, h = 0;              // SizeRef (bkz. dosya basligi, madde 4)
};
struct LayoutWindow {
  char name[kLayoutNameLen] = {0};
  uint32_t node = 0;  // LayoutFile::nodes indeksi (yaprak olmali)
  uint32_t order = 0; // dugum icindeki kanonik sekme sirasi (0..n-1)
};
struct LayoutFile {
  uint32_t version = kLayoutVersion;
  uint32_t root_id = 0;      // yazildigi andaki dockspace kimligi (BILGI)
  float root_w = 0, root_h = 0; // kok dugumun o andaki olcusu (BILGI + kurulum)
  LayoutNode nodes[kLayoutMaxNodes];
  uint32_t node_count = 0;
  LayoutWindow windows[kLayoutMaxWindows];
  uint32_t window_count = 0;
};
// Bit-tam esitlik (ondalik alanlar bit karsilastirilir) — round-trip kapisi icin.
bool layout_file_equal(const LayoutFile &a, const LayoutFile &b);

// Bir panelin olculen dikdortgeni. Dolgu (padding) baytI YOKTUR: 32 + 4*4 + 4 + 4
// = 56 bayt, hizalama 4 — kapi iki anlik goruntuyu memcmp ile de olcebilir.
// x/y/w/h = pencerenin KENETLI DUGUMUNUN dikdortgeni (pencerenin kendi
// Pos/Size'i degil): sekmeli bir dugumde secili olmayan pencerenin Pos/Size'i
// BAYATTIR, dugum dikdortgeni ise hangi sekmenin secili oldugundan bagimsizdir.
struct LayoutRect {
  char name[kLayoutNameLen] = {0};
  float x = 0, y = 0, w = 0, h = 0;
  uint32_t node = 0xFFFFFFFFu; // on-sirali dugum indeksi; 0xFFFFFFFF = yuzen
  uint32_t flags = 0;          // bit0 = kenetli
};
constexpr uint32_t kLayoutRectDocked = 1u << 0;
bool layout_rect_equal(const LayoutRect &a, const LayoutRect &b); // bit-tam

// --- Host'un kullandigi kimlik ---------------------------------------------
// Host, ImGui::DockSpace(id, ...) cagirdigi kimligi buraya bildirir (ya da
// layout_apply_default onu kendisi kaydeder). Kaydetme/yukleme bu kimligi
// kullanir; dosyadaki `kok` kimligi yalniz izlenebilirlik icindir, boylece
// host penceresinin adi degisse bile eski bir dosya yuklenebilir.
void layout_set_dockspace_id(ImGuiID id);
ImGuiID layout_dockspace_id();

// --- Ana API ----------------------------------------------------------------
// Varsayilan yerlesimi kurar: solda Sahne (%18), ortada Gorunum, sagda
// Ozellikler (%22), altta Kaynaklar + Dunya sekmeli (%25). w/h = dockspace'in
// piksel olcusu. Kimligi kaydeder (layout_set_dockspace_id).
bool layout_apply_default(ImGuiID dockspace_id, float w, float h);

// Gecerli duzeni `path`e yazar / dosyadan yukleyip uygular. false donerse sebep
// err'de (verildiyse) ve her hâlükârda layout_last_error()'dadir.
bool layout_save(const char *path, LayoutError *err = nullptr);
bool layout_load(const char *path, LayoutError *err = nullptr);
// Son basarisiz cagrinin sebebi ("" = hata yok). layout_save/load'un tek
// argumanli bicimi de sessiz kalmasin diye.
const char *layout_last_error();
// Son layout_save/layout_capture sirasinda agacta bulunup TABLODA OLMADIGI icin
// yazilmayan pencere sayisi (ornegin kenetlenmis bir ImGui hata ayiklama
// penceresi). Sessiz degil: cagiran sorabilir, raporlayabilir.
uint32_t layout_skipped();

// Panellerin olculen dikdortgenleri, kPanel* tablosu sirasinda. Donus: yazilan
// dikdortgen sayisi (henuz yaratilmamis pencere atlanir). max < gereken ise
// yalniz sigan kadari yazilir ve donus max'tir.
uint32_t layout_snapshot(LayoutRect *out, uint32_t max);

// --- Alt seviye (kapi + arac) -----------------------------------------------
// Varsayilan yerlesimi ImGui'ye DOKUNMADAN uretir (format kapisi icin).
bool layout_default(ImGuiID dockspace_id, float w, float h, LayoutFile *out, LayoutError *err = nullptr);
// Yasayan ImGui agacini okur.
bool layout_capture(ImGuiID dockspace_id, LayoutFile *out, LayoutError *err = nullptr);
// LayoutFile -> yasayan ImGui agaci (DockBuilder). Kok kimlik: layout_dockspace_id()
// ayarliysa O, degilse f.root_id.
bool layout_apply(const LayoutFile &f, LayoutError *err = nullptr);
// LayoutFile -> metin (NUL sonlu). Donus: gereken uzunluk (NUL haric; snprintf
// gibi, cap asilsa da dogru). Ayni LayoutFile her zaman ayni baytlari verir.
size_t layout_write(const LayoutFile &f, char *buf, size_t cap);
// Metin -> LayoutFile. Hata: satir numarali err, false.
bool layout_parse(const char *text, size_t len, LayoutFile *out, LayoutError *err = nullptr);

} // namespace tulpar::engine::app
