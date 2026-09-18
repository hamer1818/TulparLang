// L6 APP — Editor komut tablosu (bkz. editor_commands.hpp: neden tek tablo).
//
// Bu dosya ImGui'yi YALNIZ iki sebeple iceriyor:
//   1) Tus sabitleri (ImGuiKey_S ...). Tablo bunlarla yazilir ki kisayolun
//      dogrulugu tek bir yerde, ImGui'nin kendi numaralariyla dursun.
//   2) commands_poll_imgui: basili bilesimi soran TEK fonksiyon
//      (ImGui::IsKeyChordPressed). Baska ImGui cagrisi YOK — tablo, eslesme ve
//      cakisma denetimi bir ImGui BAGLAMI olmadan da kosar, yani headless bir
//      sonda tabloyu olcebilir.
#include "app/editor_commands.hpp"

#include <cstdio>

#include <imgui.h>

namespace tulpar::engine::app {
namespace {

// --- ImGui ile SAYISAL uyum. Baslik imgui.h icermiyor; bu dort satir onun
// bedeli: ImGui bir gun degistirici bitlerini ya da tus numaralarini
// kaydirirsa DERLEME DURUR. Yoksa kisayollar sessizce baska tuslara kayardi
// (tablo derlenir, menu dogru gorunur, tuslar yanlis is yapar).
static_assert((Chord)ImGuiMod_Ctrl == kModCtrl, "ImGuiMod_Ctrl degisti — Chord bitlerini guncelle");
static_assert((Chord)ImGuiMod_Shift == kModShift, "ImGuiMod_Shift degisti — Chord bitlerini guncelle");
static_assert((Chord)ImGuiMod_Alt == kModAlt, "ImGuiMod_Alt degisti — Chord bitlerini guncelle");
static_assert((Chord)ImGuiMod_Super == kModSuper, "ImGuiMod_Super degisti — Chord bitlerini guncelle");
static_assert((Chord)ImGuiMod_Mask_ == kModMask, "ImGuiMod_Mask_ degisti — Chord bitlerini guncelle");
// Tus degeri degistirici bitlerinin ALTINDA kalmali, yoksa "Ctrl" biti bir tus
// numarasinin icinden cikar ve bilesim cozulemez.
static_assert((Chord)ImGuiKey_NamedKey_END <= kModCtrl, "ImGuiKey degerleri degistirici bitlerine tasti");

constexpr Chord kKeyS = (Chord)ImGuiKey_S;
constexpr Chord kKeyB = (Chord)ImGuiKey_B;
constexpr Chord kKeyZ = (Chord)ImGuiKey_Z;
constexpr Chord kKeyY = (Chord)ImGuiKey_Y;
constexpr Chord kKeyA = (Chord)ImGuiKey_A;
constexpr Chord kKeyG = (Chord)ImGuiKey_G;
constexpr Chord kKeyT = (Chord)ImGuiKey_T;
constexpr Chord kKeyR = (Chord)ImGuiKey_R;
constexpr Chord kKeyDelete = (Chord)ImGuiKey_Delete;
constexpr Chord kKeyEscape = (Chord)ImGuiKey_Escape;
constexpr Chord kKeyX = (Chord)ImGuiKey_X;
constexpr Chord kKeyC = (Chord)ImGuiKey_C;
constexpr Chord kKeyV = (Chord)ImGuiKey_V;
constexpr Chord kKeyF5 = (Chord)ImGuiKey_F5;
constexpr Chord kKeyF6 = (Chord)ImGuiKey_F6;
constexpr Chord kKeyF10 = (Chord)ImGuiKey_F10;
constexpr Chord kKeyF11 = (Chord)ImGuiKey_F11;
constexpr Chord kKeyN = (Chord)ImGuiKey_N;
constexpr Chord kKeyO = (Chord)ImGuiKey_O;

// --- VARSAYILAN TABLO -------------------------------------------------------
// Buradaki her satir editor_app.cpp'de BUGUN VAR OLAN bir davranistir; hicbiri
// uydurulmadi. Kisayolsuz iki komut (Ekle/Cogalt, Oynat) bugun yalniz dugme
// olarak var — tabloya kisayolsuz girdiler ki menu/arac cubugu onlari da tek
// kaynaktan uretebilsin (oneri kisayollari raporda, tabloda DEGIL).
//
// Sira = menu sirasi. Alanlar: id, kategori, bayraklar, kisayol, ikincil,
// ascii kimlik, gorunen ad, aciklama.
constexpr CommandDesc k_defaults[] = {
    {CommandId::FileNew, CommandCategory::File, kCmdNone, chord_of(kModCtrl, kKeyN), kChordNone, "dosya.yeni", "Yeni",
     "Bos sahne (kaydedilmemis degisiklik varsa sorulur)"},
    {CommandId::FileOpen, CommandCategory::File, kCmdNone, chord_of(kModCtrl, kKeyO), kChordNone, "dosya.ac", "A\xC3\xA7\xE2\x80\xA6",
     "Sahne dosyasi acar (kaydedilmemis degisiklik varsa sorulur)"},
    {CommandId::FileSave, CommandCategory::File, kCmdNone, chord_of(kModCtrl, kKeyS), kChordNone, "dosya.kaydet", "Kaydet",
     "Sahneyi .sahne dosyasina yazar (kamerayi da)"},
    {CommandId::FileSaveAs, CommandCategory::File, kCmdNone, chord_of(kModCtrl | kModShift, kKeyS), kChordNone, "dosya.farkli_kaydet",
     "Farkl\xC4\xB1 kaydet\xE2\x80\xA6", "Sahneyi baska bir dosyaya yazar ve o dosyayi acik tutar"},
    {CommandId::FileCompile, CommandCategory::File, kCmdNone, chord_of(kModCtrl, kKeyB), kChordNone, "dosya.derle", "Derle",
     "Bellekteki sahneyi .sahneb calisma blobuna derler"},
    {CommandId::EditUndo, CommandCategory::Edit, kCmdWhileTyping, chord_of(kModCtrl, kKeyZ), kChordNone, "duzen.geri_al", "Geri al",
     "Son kullanici eylemini geri alir (grup = tek eylem)"},
    // Yinele'nin IKI kisayolu var: Ctrl+Y (Windows gelenegi) ve Ctrl+Shift+Z
    // (Blender/Krita). Bugun editor_app.cpp'de tek satirda `||` ile duruyor;
    // tabloda ayri alan oldugu icin menu her ikisini de gosterebilir ve cakisma
    // taramasi ikisini de gorur.
    {CommandId::EditRedo, CommandCategory::Edit, kCmdWhileTyping, chord_of(kModCtrl, kKeyY), chord_of(kModCtrl | kModShift, kKeyZ), "duzen.yinele",
     "Yinele", "Geri alinan eylemi yeniden uygular"},
    {CommandId::EditDuplicate, CommandCategory::Edit, kCmdUndoable, kChordNone, kChordNone, "duzen.ekle", "Ekle / \xC3\xA7o\xC4\x9F""alt",
     "Secili varlik varsa kopyasini, yoksa yeni bir varlik ekler"},
    {CommandId::EditDelete, CommandCategory::Edit, kCmdUndoable | kCmdNeedsSelection, kKeyDelete, kChordNone, "duzen.sil", "Sil",
     "Secili varliklarin tamamini siler (tek geri al getirir)"},
    // Pano: kopyalanan varliklar editorun kendi tamponunda durur (isletim
    // sistemi panosu DEGIL — metin degil yapi kopyaliyoruz). Ctrl+C/V metin
    // kutusundayken tabloya GELMEZ (koruma !text_input), orada ImGui'nin kendi
    // kopyala/yapistir'i calisir.
    {CommandId::EditCut, CommandCategory::Edit, kCmdUndoable | kCmdNeedsSelection, chord_of(kModCtrl, kKeyX), kChordNone, "duzen.kes", "Kes",
     "Secili varliklari panoya alir ve siler"},
    {CommandId::EditCopy, CommandCategory::Edit, kCmdNeedsSelection, chord_of(kModCtrl, kKeyC), kChordNone, "duzen.kopyala", "Kopyala",
     "Secili varliklari panoya alir"},
    {CommandId::EditPaste, CommandCategory::Edit, kCmdUndoable, chord_of(kModCtrl, kKeyV), kChordNone, "duzen.yapistir", "Yap\xC4\xB1\xC5\x9Ft\xC4\xB1r",
     "Panodaki varliklari sahneye ekler (tek geri al)"},
    {CommandId::SelectAll, CommandCategory::Select, kCmdNone, chord_of(kModCtrl, kKeyA), kChordNone, "secim.tumu", "T\xC3\xBCm\xC3\xBCn\xC3\xBC se\xC3\xA7",
     "Sahnedeki butun varliklari secime alir"},
    {CommandId::SelectClear, CommandCategory::Select, kCmdNeedsSelection, kKeyEscape, kChordNone, "secim.temizle", "Se\xC3\xA7imi temizle",
     "Secimi bosaltir"},
    {CommandId::ViewGizmos, CommandCategory::View, kCmdCheckable, kKeyG, kChordNone, "gorunum.gizmolar", "Gizmolar\xC4\xB1 a\xC3\xA7/kapa",
     "Isik yaricapi, golge hacmi ve gunes oku tel cizimleri"},
    {CommandId::ViewConsole, CommandCategory::View, kCmdCheckable, kChordNone, kChordNone, "gorunum.konsol", "Konsol",
     "Motor, Vulkan ve sahne iletilerini gosteren paneli ac/kapa"},
    // kCmdWhileTyping KONMADI: bu tablodaki kural degistiricisiz her bilesimi
    // HAM sayar (islev tuslari dahil), yani F5/F6/F10 ile ayni davranis —
    // metin yazarken tetiklenmez.
    {CommandId::ViewFullscreen, CommandCategory::View, kCmdCheckable, kKeyF11, kChordNone, "gorunum.tam_ekran",
     "Tam ekran", "Pencereyi monitorun video kipine gecirir (F11 ile geri doner)"},
    {CommandId::GizmoTranslate, CommandCategory::Gizmo, kCmdCheckable, kKeyT, kChordNone, "gizmo.tasi", "Ta\xC5\x9F\xC4\xB1",
     "ImGuizmo kipi: tasima"},
    {CommandId::GizmoRotate, CommandCategory::Gizmo, kCmdCheckable, kKeyR, kChordNone, "gizmo.dondur", "D\xC3\xB6nd\xC3\xBCr", "ImGuizmo kipi: dondurme"},
    // Dikkat: ham S. editor_app.cpp bugun bunu `key_down[S] && !key_down[CTRL]`
    // diye ELDE ayikliyordu (Ctrl+S kaydederken gizmo kipi de degismesin diye).
    // Bilesim esitligi degistiricileri DE karsilastirdigi icin o ayiklama artik
    // kendiliginden dogru: Ctrl basiliyken ham S ESLESMEZ.
    {CommandId::GizmoScale, CommandCategory::Gizmo, kCmdCheckable, kKeyS, kChordNone, "gizmo.olcekle", "\xC3\x96l\xC3\xA7""ekle", "ImGuizmo kipi: olcekleme"},
    {CommandId::PlayToggle, CommandCategory::Play, kCmdCheckable, kKeyF5, kChordNone, "oynat.baslat_durdur", "Oynat / Durdur",
     "Sim'i sabit adimda calistirir; durunca govdeler kaldirilir"},
    // Duraklat DURDURMAK DEGILDIR: govdeler yerinde kalir, yalniz zaman akmaz.
    // Durdur (PlayToggle) govdeleri kaldirir ve veri modeli yeniden gecerlidir.
    {CommandId::PlayPause, CommandCategory::Play, kCmdCheckable, kKeyF6, kChordNone, "oynat.duraklat", "Duraklat",
     "Oynatmayi dondurur; govdeler yerinde kalir (F10 ile kare ilerlet)"},
    {CommandId::PlayStep, CommandCategory::Play, kCmdNone, kKeyF10, kChordNone, "oynat.kare_ilerlet", "Kare ilerlet",
     "Duraklatilmisken TEK sabit fizik adimi ilerletir"},
};

constexpr uint32_t k_default_count = (uint32_t)(sizeof(k_defaults) / sizeof(k_defaults[0]));
static_assert(k_default_count == kCommandCount, "CommandId listesi ile tablo satirlari ayrismis");

// Tablo CommandId sirasinda mi? Oyleyse arama gerekmez: descs[(id)-1].
// (Dizi disina tasan bir id'nin sessizce yanlis komutu calistirmasi bu satirla
// olanaksiz hale gelir.)
constexpr bool defaults_in_id_order(const CommandDesc *a, uint32_t n) {
  for (uint32_t i = 0; i < n; i++)
    if ((uint32_t)a[i].id != i + 1) return false;
  return true;
}
static_assert(defaults_in_id_order(k_defaults, k_default_count), "tablo satirlari CommandId sirasinda degil");

// CAKISMA KAPISI — derleme zamani. Ayni fonksiyon calisma aninda da kosar
// (commands_find_conflicts), yani kapi ile gercek kod ayni; pozitif kontrol
// (bilerek cakisan bir tablo) sondada yapilir.
static_assert(commands_find_conflicts(k_defaults, k_default_count, nullptr, 0) == 0,
              "editor komut tablosunda CAKISAN kisayol var — commands_find_conflicts() ile bul");

// kCmdWhileTyping yalniz degistiricili (ya da islev tusu gibi ham olmayan)
// bilesimlere konabilir: ham bir harf metin kutusunda YAZI demektir.
constexpr bool typing_flag_is_sane(const CommandDesc *a, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    if ((a[i].flags & kCmdWhileTyping) == 0) continue;
    if (a[i].shortcut != kChordNone && chord_is_raw(a[i].shortcut)) return false;
    if (a[i].shortcut_alt != kChordNone && chord_is_raw(a[i].shortcut_alt)) return false;
  }
  return true;
}
static_assert(typing_flag_is_sane(k_defaults, k_default_count), "kCmdWhileTyping ham bir tusa konmus — metin yazarken tetiklenir");

// --- Tus adlari -------------------------------------------------------------
// ImGui::GetKeyName KULLANILMIYOR: o bir baglam ister (GImGui), yani menu
// metni ancak bir ImGui karesi icinde uretilebilirdi ve headless sonda olcemezdi.
constexpr const char *k_letters[26] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                       "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
constexpr const char *k_digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
constexpr const char *k_fkeys[12] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};

const char *key_name(Chord k) {
  const int key = (int)k;
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return k_letters[key - ImGuiKey_A];
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return k_digits[key - ImGuiKey_0];
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) return k_fkeys[key - ImGuiKey_F1];
  switch (key) {
  case ImGuiKey_Tab: return "Tab";
  case ImGuiKey_LeftArrow: return "Sol";
  case ImGuiKey_RightArrow: return "Sag";
  case ImGuiKey_UpArrow: return "Yukari";
  case ImGuiKey_DownArrow: return "Asagi";
  case ImGuiKey_PageUp: return "PageUp";
  case ImGuiKey_PageDown: return "PageDown";
  case ImGuiKey_Home: return "Home";
  case ImGuiKey_End: return "End";
  case ImGuiKey_Insert: return "Insert";
  case ImGuiKey_Delete: return "Delete";
  case ImGuiKey_Backspace: return "Backspace";
  case ImGuiKey_Space: return "Bosluk";
  case ImGuiKey_Enter: return "Enter";
  case ImGuiKey_Escape: return "Esc";
  case ImGuiKey_Apostrophe: return "'";
  case ImGuiKey_Comma: return ",";
  case ImGuiKey_Minus: return "-";
  case ImGuiKey_Period: return ".";
  case ImGuiKey_Slash: return "/";
  case ImGuiKey_Semicolon: return ";";
  case ImGuiKey_Equal: return "=";
  case ImGuiKey_LeftBracket: return "[";
  case ImGuiKey_Backslash: return "\\";
  case ImGuiKey_RightBracket: return "]";
  case ImGuiKey_GraveAccent: return "`";
  case ImGuiKey_KeypadEnter: return "NumEnter";
  default: return nullptr;
  }
}

constexpr const char *k_category_names[kCommandCategoryCount] = {"Dosya", "D\xC3\xBCzen", "Se\xC3\xA7im", "G\xC3\xB6r\xC3\xBCn\xC3\xBCm", "Gizmo", "Oynat"};

// id -> dizi indeksi. Gecersiz id icin kCommandCount (= "yok") doner.
uint32_t index_of(CommandId id) {
  const uint32_t v = (uint32_t)id;
  if (v == 0 || v > kCommandCount) return kCommandCount;
  return v - 1;
}

const CommandDesc k_empty{};

} // namespace

// --- Salt okunur erisim -----------------------------------------------------

const CommandDesc *command_defaults() { return k_defaults; }

const CommandDesc &command_default(CommandId id) {
  const uint32_t i = index_of(id);
  return i < kCommandCount ? k_defaults[i] : k_empty;
}

const char *command_category_name(CommandCategory c) {
  const uint32_t i = (uint32_t)c;
  return i < kCommandCategoryCount ? k_category_names[i] : "";
}

uint32_t command_shortcut_text(Chord c, char *buf, uint32_t cap) {
  if (buf == nullptr || cap == 0) return 0;
  buf[0] = 0;
  if (c == kChordNone) return 0;
  uint32_t n = 0;
  auto put = [&](const char *s) {
    while (*s != 0 && n + 1 < cap) buf[n++] = *s++;
  };
  // Sira sabit (Ctrl, Shift, Alt, Super) ki ayni bilesim her yerde AYNI metni
  // versin — menu ile ipucu ayrisirsa kullanici iki farkli kisayol sanir.
  if (c & kModCtrl) put("Ctrl+");
  if (c & kModShift) put("Shift+");
  if (c & kModAlt) put("Alt+");
  if (c & kModSuper) put("Super+");
  const char *kn = key_name(chord_key(c));
  if (kn != nullptr) put(kn);
  else {
    // Bilinmeyen tus: sessizce bos birakmak yerine numarasini bas — menude
    // "Ctrl+Tus534" gorunur ve eksik ad hemen fark edilir.
    char tmp[16];
    std::snprintf(tmp, sizeof tmp, "Tus%u", (unsigned)chord_key(c));
    put(tmp);
  }
  buf[n] = 0;
  return n;
}

bool command_accepts_input(const CommandDesc &d, const InputGuards &g) {
  if (d.flags & kCmdWhileTyping) return true;
  if (g.text_input) return false;
  // Ham kisayol icin TEK basina "metin yazilmiyor" yetmez: ImGui klavyeyi
  // tutuyorsa (kaydirac surukleniyor, kipli pencere acik) T/R/S/Delete gitmez.
  const bool raw = chord_is_raw(d.shortcut) || (d.shortcut_alt != kChordNone && chord_is_raw(d.shortcut_alt));
  return !(raw && g.keyboard_captured);
}

const CommandDesc *commands_match(const CommandDesc *a, uint32_t n, Chord pressed, const InputGuards &g) {
  if (a == nullptr || pressed == kChordNone) return nullptr;
  for (uint32_t i = 0; i < n; i++) {
    if (a[i].shortcut != pressed && a[i].shortcut_alt != pressed) continue;
    if (!command_accepts_input(a[i], g)) return nullptr; // eslesti ama koruma engelledi
    return &a[i];
  }
  return nullptr;
}

uint32_t commands_in_category(const CommandDesc *a, uint32_t n, CommandCategory c, const CommandDesc **out, uint32_t cap) {
  if (a == nullptr) return 0;
  uint32_t found = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (a[i].category != c) continue;
    if (out != nullptr && found < cap) out[found] = &a[i];
    found++;
  }
  return found; // cap'ten buyuk olabilir: tasma GORUNUR
}

// --- CommandTable -----------------------------------------------------------

CommandTable::CommandTable() {
  for (uint32_t i = 0; i < kCommandCount; i++) descs_[i] = k_defaults[i];
}

bool CommandTable::bind(CommandId id, CommandFn fn, void *ctx, CommandQuery enabled, CommandQuery checked) {
  const uint32_t i = index_of(id);
  if (i >= kCommandCount) return false;
  binds_[i].fn = fn;
  binds_[i].ctx = ctx;
  binds_[i].enabled = enabled;
  binds_[i].checked = checked;
  return true;
}

bool CommandTable::bound(CommandId id) const {
  const uint32_t i = index_of(id);
  return i < kCommandCount && binds_[i].fn != nullptr;
}

bool CommandTable::enabled(CommandId id) const {
  const uint32_t i = index_of(id);
  if (i >= kCommandCount || binds_[i].fn == nullptr) return false;
  return binds_[i].enabled == nullptr || binds_[i].enabled(binds_[i].ctx);
}

bool CommandTable::checked(CommandId id) const {
  const uint32_t i = index_of(id);
  if (i >= kCommandCount || binds_[i].checked == nullptr) return false;
  return binds_[i].checked(binds_[i].ctx);
}

bool CommandTable::invoke(CommandId id) {
  const uint32_t i = index_of(id);
  if (i >= kCommandCount || binds_[i].fn == nullptr) return false;
  if (binds_[i].enabled != nullptr && !binds_[i].enabled(binds_[i].ctx)) return false;
  binds_[i].fn(binds_[i].ctx);
  return true;
}

const CommandDesc &CommandTable::desc(CommandId id) const {
  const uint32_t i = index_of(id);
  return i < kCommandCount ? descs_[i] : k_empty;
}

uint32_t CommandTable::in_category(CommandCategory c, const CommandDesc **out, uint32_t cap) const {
  return commands_in_category(descs_, kCommandCount, c, out, cap);
}

const CommandDesc *CommandTable::match(Chord pressed, const InputGuards &g) const {
  return commands_match(descs_, kCommandCount, pressed, g);
}

bool CommandTable::dispatch(Chord pressed, const InputGuards &g) {
  const CommandDesc *d = match(pressed, g);
  return d != nullptr && invoke(d->id);
}

bool CommandTable::rebind_shortcut(CommandId id, Chord primary, Chord alt) {
  const uint32_t i = index_of(id);
  if (i >= kCommandCount) return false;
  const Chord old_p = descs_[i].shortcut, old_a = descs_[i].shortcut_alt;
  descs_[i].shortcut = primary;
  descs_[i].shortcut_alt = alt;
  if (commands_find_conflicts(descs_, kCommandCount, nullptr, 0) != 0) {
    descs_[i].shortcut = old_p; // cakisma: baglama REDDEDILDI, tablo eski haline dondu
    descs_[i].shortcut_alt = old_a;
    return false;
  }
  return true;
}

uint32_t CommandTable::conflicts(CommandConflict *out, uint32_t cap) const {
  return commands_find_conflicts(descs_, kCommandCount, out, cap);
}

uint32_t CommandTable::unbound(CommandId *out, uint32_t cap) const {
  uint32_t found = 0;
  for (uint32_t i = 0; i < kCommandCount; i++) {
    if (binds_[i].fn != nullptr) continue;
    if (out != nullptr && found < cap) out[found] = descs_[i].id;
    found++;
  }
  return found;
}

// --- ImGui koprusu ----------------------------------------------------------

uint32_t commands_poll_imgui(CommandTable &t, const InputGuards &g) {
  uint32_t ran = 0;
  const CommandDesc *a = t.all();
  for (uint32_t i = 0; i < CommandTable::count(); i++) {
    if (a[i].shortcut == kChordNone && a[i].shortcut_alt == kChordNone) continue;
    if (!command_accepts_input(a[i], g)) continue;
    // IsKeyChordPressed degistiricileri TAM esler (io.KeyMods != mods -> false),
    // bu yuzden ham S, Ctrl+S basiliyken tetiklenmez.
    const bool hit = (a[i].shortcut != kChordNone && ImGui::IsKeyChordPressed((ImGuiKeyChord)a[i].shortcut)) ||
                     (a[i].shortcut_alt != kChordNone && ImGui::IsKeyChordPressed((ImGuiKeyChord)a[i].shortcut_alt));
    if (hit && t.invoke(a[i].id)) ran++;
  }
  return ran;
}

} // namespace tulpar::engine::app
