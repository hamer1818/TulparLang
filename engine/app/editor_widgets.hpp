// L6 APP — editor_widgets: Ozellikler (inspector) ve Sahne (hierarchy)
// panellerinin bilesik widget'lari. Ham ImGui yerine Unity Inspector /
// Godot Inspector / Unreal Details gorunumu: iki sutunlu ozellik tablosu,
// X/Y/Z rozetli vec3, cerceveli bilesen basligi (onay kutusu + kaldir),
// tur simgeli hiyerarsi satiri, arama kutusu, arac cubugu.
//
// Palet YALNIZ editor_tone / ImGuiCol_* uzerinden (editor_ui.hpp); burada
// ham RGB yok. STL yok, `new` yok; metin sabit tamponlarda ve sinirli
// genislige giren her yazi editor_ellipsize ile kirpilir (sessiz kesme yok).
#pragma once
#include <cstdint>

#include "app/editor_ui.hpp"

namespace tulpar::engine::app {

// --- Geri alma sozlesmesi -----------------------------------------------------
// editor_app'in track_edit'i widget'tan sonra ImGui::IsItemActivated /
// IsItemDeactivated / IsItemDeactivatedAfterEdit sorar. Bilesik bir widget'ta
// (vec3 = uc DragFloat) "son oge" yalniz Z alanidir; Y suruklenirken
// IsItemActivated hic gorulmez ve gunluge girmez. Bu yuzden HER ozellik
// widget'i alt ogelerinin TAMAMI uzerinden toplanmis bir PropItem dondurur:
// activated = herhangi bir alt oge bu kare etkinlesti, vb. Cagiran bunu
// track_edit(st, e, si, item) olarak tuketir; ImGui'nin son ogesine bakmaz.
struct PropItem {
  bool changed = false;                // deger bu kare degisti (widget true dondu)
  bool activated = false;              // bir alt oge bu kare etkin oldu (basildi/odaklandi)
  bool deactivated = false;            // bir alt oge bu kare etkinligini birakti
  bool deactivated_after_edit = false; // ... ve etkinken degeri degistirmisti
  void merge(const PropItem &o) {
    changed |= o.changed;
    activated |= o.activated;
    deactivated |= o.deactivated;
    deactivated_after_edit |= o.deactivated_after_edit;
  }
};

// --- Ozellik tablosu -----------------------------------------------------------
// Iki sutunlu tablo: sol sutun etiket (TextDim, dikey ortali, sigmazsa "…" ve
// ipucu), sag sutun widget (tam genislik). label_fraction: etiket sutununun
// kullanilabilir genislige orani. false donerse (pencere kirpilmis) satir
// cizme; true ise prop_end ZORUNLU.
bool prop_begin(const char *id, float label_fraction = 0.38f);
void prop_end();
// Bir SONRAKI satirin etiket ipucuna eklenecek aciklama (tek kare gecerli).
void prop_help(const char *text);

// Uc DragFloat, her birinin solunda AxisX/AxisY/AxisZ dolgulu X/Y/Z rozeti
// (Unity/Godot). min == max: sinirsiz. Rozet alanin sol kenarina yapisiktir.
PropItem prop_vec3(const char *label, float v[3], float speed, float min = 0, float max = 0, const char *fmt = "%.3f");
PropItem prop_float(const char *label, float *v, float speed, float min = 0, float max = 0, const char *fmt = "%.3f");
PropItem prop_int(const char *label, int *v, int min = 0, int max = 0);
PropItem prop_text(const char *label, char *buf, uint32_t cap);
// Satir genisliginde renk ornegi (uzerinde altigen kod); tiklayinca secici
// acilir. Bir secici oturumu = bir duzenleme: activated acilista,
// deactivated(_after_edit) kapanista (renk acilistakinden farkliysa).
PropItem prop_color(const char *label, float rgb[3]);
PropItem prop_check(const char *label, bool *v);
// items: "kutu\0kure\0" bicimi (ImGui::Combo ile ayni).
PropItem prop_combo(const char *label, int *v, const char *items_zero_separated);
// Kaynak secimi: names[count] (content::SceneDesc::assets ile ayni tip).
// count == 0: "-" gosterir, secilemez. *index < 0: secim yok ("-").
PropItem prop_asset(const char *label, int *index, const char (*names)[128], uint32_t count);
// Kucuk, soluk, buyuk harfli bolum etiketi + saga uzanan ince cizgi
// ("DONUSUM", "MODEL"). Metni cagiran buyuk harfle verir.
void section_label(const char *text);

// --- Bilesen basligi (Unity) -----------------------------------------------------
// Tam genislik cerceveli satir: acilir ok, etkin onay kutusu (enabled null ise
// yok), renkli simge, ad, sagda kucuk "✕" (remove_clicked). Donus: govde acik
// mi; ACIKSA component_end() ZORUNLU (TreePop). remove_clicked null olabilir.
bool component_header(const char *icon, const char *name, bool *enabled, bool *remove_clicked, bool default_open = true,
                      Tone icon_tone = Tone::Accent);
void component_end();
// Tam genislik "+ Bilesen ekle" dugmesi; acilan listeden secilen indeks, yoksa -1.
int component_add_button(const char *const *names, uint32_t count);

// --- Inspector baslik satiri ------------------------------------------------------
// Buyuk simge + daha buyuk yazili ad kutusu + altinda soluk alt baslik
// ("3 bilesen · kutu govde"). Donus: ad kutusunun PropItem'i.
PropItem inspector_title(const char *icon, char *name, uint32_t cap, const char *subtitle, Tone icon_tone = Tone::Accent);
// Secim yokken ortalanmis soluk ipucu ("Sahne listesinden bir varlik sec").
void inspector_empty(const char *hint);

// --- Hiyerarsi -------------------------------------------------------------------
// Buyutec simgeli arama kutusu; doluyken sagda temizleme "✕". Donus: suzgec
// bu kare degisti (yazildi ya da temizlendi).
bool hierarchy_search(char *buf, uint32_t cap);
// Buyuk/kucuk harfe duyarsiz alt dizi (ASCII + Turkce İ/ı/Ğ/Ş/Ö/Ü/Ç katlanir);
// bos suzgec her seyle eslesir. ImGui baglami GEREKMEZ (saf fonksiyon).
bool hierarchy_filter_match(const char *name, const char *filter);

struct HierarchyRow {
  const char *name = nullptr;
  bool selected = false;
  bool has_model = false, has_light = false, has_body = false, has_anim = false;
};
// Tam genislik Selectable: tur simgesi (isik ☀ Warn > model ◆ Text > govde ◼
// AxisZ > bos ○ TextDim), ad (kalan genislige "…"), sagda soluk bilesen
// glifleri. Donus: tiklandi. Cagiran secimi kendi kurar (Ctrl+tik vb.).
bool hierarchy_row(int id, const HierarchyRow &r);
// Ince arac satiri: "+" (acilir: Bos varlik=1, Model=2, Isik=3, Govde=4),
// "−" (yalniz secim varken; 5), sagda soluk "N varlik". 0 = hicbir sey.
int hierarchy_toolbar(uint32_t entity_count, bool has_selection);
// Liste bos / suzgec hicbir seyi gecirmedi: ortalanmis soluk ipucu.
void hierarchy_empty(const char *hint);

// --- Son yerlesim (KAPILAR icin) ---------------------------------------------------
// Kapilar sentetik fareyi nereye koyacagini ve hangi pikseli okuyacagini
// bilmeli; widget'lar son cizimlerinin ekran dikdortgenlerini burada birakir.
// Editor kodu bunlari KULLANMAZ.
struct WidgetRect {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  float cx() const { return (x0 + x1) * 0.5f; }
  float cy() const { return (y0 + y1) * 0.5f; }
};
struct PropVec3Layout {
  WidgetRect badge[3], field[3];
};
struct ComponentHeaderLayout {
  WidgetRect header, check, remove;
};
struct HierarchyRowLayout {
  WidgetRect row;
  char text[96] = {0};     // gercekten cizilen (kirpilmis) ad
  float text_max_w = 0;    // adin sigmasi gereken genislik
  bool ellipsized = false; // ad kirpildi mi
};
const PropVec3Layout &prop_vec3_last_layout();
const ComponentHeaderLayout &component_header_last_layout();
const HierarchyRowLayout &hierarchy_row_last_layout();
// Son prop_* satirinin ana widget dikdortgeni (renk ornegi, kutu, onay...).
const WidgetRect &prop_last_rect();

} // namespace tulpar::engine::app
