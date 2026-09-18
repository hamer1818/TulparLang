// L6 APP — Editor komut tablosu: menu cubugu, arac cubugu ve (ileride) komut
// paleti AYNI tablodan uretilir. Saf veri + fonksiyon isaretcisi; ImGui'ye
// BAGLI DEGIL (tek istisna asagida: commands_poll_imgui, bildirimi ImGui turu
// icermez).
//
// NEDEN — bugun editor_app.cpp'de durum su:
//   if (!ImGui::GetIO().WantTextInput) {
//     if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) do_save();
//     ... sekiz satir daha ...
//   }
//   if (ImGui::BeginMainMenuBar()) { if (ImGui::Button("Kaydet")) do_save(); ... }
// yani ayni komut IKI yerde yaziyor ve aralarinda hicbir bag yok. Uc somut
// sonucu var, ucu de bu dosyanin varlik sebebi:
//   1) KESFEDILEBILIRLIK: menudeki "Derle" dugmesi Ctrl+B oldugunu soylemez.
//      Kisayol yalniz kaynakta yazar. Menu metnini tablodan uretince ikisi
//      ayni satirdan gelir, ayrisamaz.
//   2) CAKISMA SESSIZDIR: ayni bilesim iki komuta yazilirsa ikisi de calisir
//      (kare dongusunde arka arkaya iki `if`). Kimse fark etmez. Burada tablo
//      DERLEME ZAMANINDA taranir — static_assert, bkz. editor_commands.cpp.
//   3) TUTARSIZ KORUMA: bugun Ctrl+* kisayollari WantTextInput ile, ham T/R/S
//      ise wants_keyboard() ile korunuyor — yani bir kaydiraci SURUKLERKEN
//      Delete tusu hala varlik siliyor. InputGuards bu iki kuraldan TEKINI
//      kuruyor (asagida "Kim ne zaman eslesir").
//
// Sozlesme: hicbir ayirma yok (sabit diziler), STL yok, istisna yok. Tablo
// bir CommandTable nesnesinde KOPYA tutulur, boylece kisayollar calisma
// aninda (ayar dosyasi, ileride kisayol ekrani) degistirilebilir; varsayilan
// tanimlar command_defaults() ile her zaman elde edilir.
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

// --- Kisayol bilesimi -------------------------------------------------------
// Chord, ImGuiKeyChord ile BIT UYUMLUDUR: ust dort bit degistiriciler
// (ImGuiMod_*), alti tus degeri (ImGuiKey_*). Bu dosya imgui.h'i ICERMEZ —
// sayilarin ImGui ile ayni oldugu editor_commands.cpp icinde static_assert ile
// dogrulanir, yani ImGui bir surumde numaralari degistirirse DERLEME PATLAR,
// kisayollar sessizce kaymaz.
using Chord = uint32_t;
enum : Chord {
  kChordNone = 0,
  kModCtrl = 1u << 12,  // == ImGuiMod_Ctrl
  kModShift = 1u << 13, // == ImGuiMod_Shift
  kModAlt = 1u << 14,   // == ImGuiMod_Alt
  kModSuper = 1u << 15, // == ImGuiMod_Super
  kModMask = 0xF000u,   // == ImGuiMod_Mask_
};
constexpr Chord chord_of(Chord mods, Chord key) { return mods | key; }
constexpr Chord chord_key(Chord c) { return c & ~kModMask; }
constexpr Chord chord_mods(Chord c) { return c & kModMask; }
// HAM kisayol: Ctrl/Alt/Super yok. Shift TEK BASINA hamdir — "Shift+G" hala bir
// harf tusudur ve metin yazarken tetiklenmemelidir.
constexpr bool chord_is_raw(Chord c) { return (c & (kModCtrl | kModAlt | kModSuper)) == 0; }

// --- Komut kimligi ----------------------------------------------------------
// Sayisal degerler KAYDEDILMEZ (ayar dosyasi CommandDesc::key metnini kullanir),
// bu yuzden araya yeni komut eklemek serbesttir. Sira MENU SIRASIDIR.
enum class CommandId : uint16_t {
  None = 0,
  FileNew,        // Ctrl+N
  FileOpen,       // Ctrl+O
  FileSave,       // Ctrl+S
  FileSaveAs,     // Ctrl+Shift+S
  FileCompile,    // Ctrl+B
  EditUndo,       // Ctrl+Z
  EditRedo,       // Ctrl+Y / Ctrl+Shift+Z
  EditDuplicate,  // (kisayolsuz; "Ekle" dugmesi)
  EditDelete,     // Delete
  EditCut,        // Ctrl+X
  EditCopy,       // Ctrl+C
  EditPaste,      // Ctrl+V (pano bossa etkin degil)
  SelectAll,      // Ctrl+A
  SelectClear,    // Esc
  ViewGizmos,     // G
  ViewConsole,    // (kisayolsuz) Konsol panelini ac/kapa
  ViewFullscreen, // F11 (host desteklemiyorsa soluk)
  GizmoTranslate, // T
  GizmoRotate,    // R
  GizmoScale,     // S
  PlayToggle,     // F5
  PlayPause,      // F6  (oynatilirken duraklat; govdeler yerinde kalir)
  PlayStep,       // F10 (duraklatilmisken TEK sabit adim ilerlet)
  Count
};
// Tablo boyu. None sayilmaz; descs[(uint32_t)id - 1] dogrudan indekstir
// (editor_commands.cpp bunu static_assert ile garanti eder).
constexpr uint32_t kCommandCount = (uint32_t)CommandId::Count - 1;

// Menu cubugu basliklari. Sira = menulerin soldan saga sirasi.
enum class CommandCategory : uint8_t { File, Edit, Select, View, Gizmo, Play, Count };
constexpr uint32_t kCommandCategoryCount = (uint32_t)CommandCategory::Count;

// Komut bayraklari — arayuzun komutu NASIL gosterecegini anlatan meta veri.
enum : uint16_t {
  kCmdNone = 0,
  kCmdUndoable = 1u << 0,       // sahne gunlugune islem yazar (geri alinabilir)
  kCmdNeedsSelection = 1u << 1, // secim bossa anlamsiz (menude soluk gosterilir)
  kCmdCheckable = 1u << 2,      // menude isaretli/isaretsiz (ac-kapa ya da secenek)
  kCmdWhileTyping = 1u << 3,    // METIN YAZARKEN DE gecerli. Yalniz degistiricili ya
                                // da islev tuslari icin; ham harfe konursa derleme
                                // patlar (editor_commands.cpp static_assert).
};

// Geri cagri: ImGui'den, editor durumundan, her seyden BAGIMSIZ. ctx cagiranin
// kendi durumudur (editor_app.cpp'de EditorState*).
using CommandFn = void (*)(void *ctx);
// Sorgu: etkin mi / isaretli mi. nullptr = "her zaman etkin" / "isaretsiz".
using CommandQuery = bool (*)(const void *ctx);

struct CommandDesc {
  CommandId id = CommandId::None;
  CommandCategory category = CommandCategory::File;
  uint16_t flags = kCmdNone;
  Chord shortcut = kChordNone;     // birincil bilesim (0 = kisayolsuz komut)
  Chord shortcut_alt = kChordNone; // ikincil (orn. Yinele: Ctrl+Y ve Ctrl+Shift+Z)
  const char *key = "";            // DEGISMEZ ascii kimlik: ayar dosyasi + palet aramasi
  const char *name = "";           // gorunen ad (TR)
  const char *help = "";           // tek satirlik aciklama (ipucu / palet alt satiri)
};

// Cakisma kaydi. a == b ise komut KENDI iki kisayoluyla cakisiyor demektir.
struct CommandConflict {
  CommandId a = CommandId::None;
  CommandId b = CommandId::None;
  Chord chord = kChordNone;
};

// Kare basi girdi durumu. Ikisi de EditorUi'dan gelir; ayrimi API'de GORUNUR
// tutmak bilincli: "metin yaziliyor" ile "ImGui klavyeyi tutuyor" AYNI SEY
// DEGILDIR (kaydirac suruklerken ikincisi true, birincisi false).
struct InputGuards {
  bool text_input = false;        // EditorUi::wants_text_input()
  bool keyboard_captured = false; // EditorUi::wants_keyboard() (kaydirac surukleme dahil)
};

// Kim ne zaman eslesir:
//   kCmdWhileTyping isaretli       -> her zaman (metin kutusunun icinde bile)
//   ham kisayol (T, Delete, Esc)   -> !text_input VE !keyboard_captured
//   degistiricili (Ctrl+S ...)     -> !text_input
// Ham kisayolun daha siki olmasinin sebebi dosya basindaki (3) numarali madde.
bool command_accepts_input(const CommandDesc &d, const InputGuards &g);

// --- Varsayilan tablo (salt okunur) -----------------------------------------
const CommandDesc *command_defaults();              // kCommandCount girdi
const CommandDesc &command_default(CommandId id);   // gecersiz id: bos tanim
const char *command_category_name(CommandCategory); // menu basligi (TR)

// Kisayolu insan okunur metne cevirir: "Ctrl+Shift+Z", "Delete", "T".
// Kisayolsuz komut icin bos metin yazar ve 0 doner. Donus: yazilan uzunluk.
// ImGui BAGLAMI GEREKTIRMEZ (kendi tus adi tablosu) — headless sondada da calisir.
uint32_t command_shortcut_text(Chord c, char *buf, uint32_t cap);

// --- Tablo uzerinde islemler (hem varsayilan hem CommandTable icin) ---------
// Basilan bilesime karsilik gelen komut (tam eslesme: degistiriciler DE esit
// olmali, bu yuzden Ctrl+S basiliyken ham S eslesmez). Yoksa nullptr.
const CommandDesc *commands_match(const CommandDesc *a, uint32_t n, Chord pressed, const InputGuards &g);
// Kategorideki komutlar, tablo sirasinda. Donus: BULUNAN sayi (cap'ten buyuk
// olabilir — sessiz kirpma yok, cagiran tasmayi gorur).
uint32_t commands_in_category(const CommandDesc *a, uint32_t n, CommandCategory c, const CommandDesc **out, uint32_t cap);

// CAKISMA DENETIMI — bu dosyanin en onemli parcasi.
// Ayni bilesimi paylasan her (komut, komut) ciftini bulur; her komutun IKI
// kisayolu da capraz karsilastirilir ve bir komutun kendi iki kisayolu esitse
// o da cakismadir. kChordNone ATLANIR — kisayolsuz komutlar birbiriyle
// "cakismis" gorunmez (tablonun yarisi kisayolsuz olabilir).
// Donus: BULUNAN toplam cakisma (cap'ten buyuk olabilir; out yalniz ilk cap
// tanesini alir — sayim kirpilmaz ki "0 disinda bir sey" her zaman gorunsun).
// constexpr: ayni fonksiyon derleme zamaninda static_assert ile de kosar
// (out = nullptr). Boylece kapi ile gercek kod AYNIDIR, ikinci bir kopya yok.
constexpr uint32_t commands_find_conflicts(const CommandDesc *a, uint32_t n, CommandConflict *out, uint32_t cap) {
  uint32_t found = 0;
  for (uint32_t i = 0; i < n; i++) {
    const Chord ci[2] = {a[i].shortcut, a[i].shortcut_alt};
    if (ci[0] != kChordNone && ci[0] == ci[1]) { // komut kendi kendisiyle
      if (out != nullptr && found < cap) out[found] = CommandConflict{a[i].id, a[i].id, ci[0]};
      found++;
    }
    for (uint32_t j = i + 1; j < n; j++) {
      const Chord cj[2] = {a[j].shortcut, a[j].shortcut_alt};
      for (uint32_t x = 0; x < 2; x++)
        for (uint32_t y = 0; y < 2; y++)
          if (ci[x] != kChordNone && ci[x] == cj[y]) {
            if (out != nullptr && found < cap) out[found] = CommandConflict{a[i].id, a[j].id, ci[x]};
            found++;
          }
    }
  }
  return found;
}

// --- Calisan tablo ----------------------------------------------------------
// Tanimlarin KOPYASI + geri cagri baglantilari. Kopya, kisayolun calisma
// aninda degistirilebilmesi icin; baglanti, geri cagrilarin (editor_app.cpp'de
// yakalayan lambda'lar) derleme zamani sabiti OLAMAMASI icin.
struct CommandBinding {
  CommandFn fn = nullptr;
  CommandQuery enabled = nullptr; // nullptr = her zaman etkin
  CommandQuery checked = nullptr; // nullptr = isaretsiz
  void *ctx = nullptr;
};

class CommandTable {
public:
  CommandTable(); // varsayilan tanimlar; HICBIR geri cagri bagli degil

  // Baglama. Bilinmeyen id yok sayilir (donus false).
  bool bind(CommandId id, CommandFn fn, void *ctx, CommandQuery enabled = nullptr, CommandQuery checked = nullptr);
  bool bound(CommandId id) const;
  bool enabled(CommandId id) const; // bagli degilse false
  bool checked(CommandId id) const;
  bool invoke(CommandId id); // bagli + etkin degilse CALISTIRMAZ, false doner

  const CommandDesc &desc(CommandId id) const;
  const CommandDesc *all() const { return descs_; }
  static constexpr uint32_t count() { return kCommandCount; }
  uint32_t in_category(CommandCategory c, const CommandDesc **out, uint32_t cap) const;
  const CommandDesc *match(Chord pressed, const InputGuards &g) const;
  // Eslesirse komutu calistirir. Donus: calistirildi mi.
  bool dispatch(Chord pressed, const InputGuards &g);

  // Kisayol degistirme (ayar dosyasi / kisayol ekrani). CAKISMA OLUSTURAN
  // baglama REDDEDILIR (false) ve tablo DEGISMEZ — sessizce ikinci bir sahip
  // yaratmaktansa gurultulu hata.
  bool rebind_shortcut(CommandId id, Chord primary, Chord alt);
  uint32_t conflicts(CommandConflict *out, uint32_t cap) const;

  // Kurulum denetimi: hangi komutlar HALA baglanmadi. editor_app kurulumdan
  // sonra bunu 0 beklemeli — yoksa menude tiklanmayan bir satir kalir.
  uint32_t unbound(CommandId *out, uint32_t cap) const;

private:
  CommandDesc descs_[kCommandCount];
  CommandBinding binds_[kCommandCount];
};

// ImGui koprusu — TEK ImGui'ye bagli fonksiyon; bildiriminde ImGui turu YOK,
// bu yuzden bu baslik imgui.h'siz derlenir. Tablodaki her kisayolu
// ImGui::IsKeyChordPressed ile sorar, korumalardan gecenleri calistirir.
// Kare basi BIR KEZ, ui.begin_frame() SONRASI cagrilir. Donus: calisan komut
// sayisi (normalde 0 ya da 1; iki farkli kisayol ayni karede basilabilir).
uint32_t commands_poll_imgui(CommandTable &t, const InputGuards &g);

} // namespace tulpar::engine::app
