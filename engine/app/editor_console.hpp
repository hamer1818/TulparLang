// L6 APP — Editor KONSOLU: motorun, dogrulama katmaninin ve sahne katmaninin
// soyledigi her sey EDITORUN ICINDE gorunur.
//
// NEDEN: bugun `std::fprintf(stderr, "sahne %s: %s\n", ...)` ile bir Vulkan
// VUID satiri arasinda hicbir fark yok — ikisi de ISLETIM SISTEMI TERMINALINE
// gidiyor. Editoru bir masaustu kisayolundan acan kullanici o terminali hic
// gormez; "sahne yuklenmedi" ya da "golgeler kapandi" sessizce kaybolur.
// Durum cubugundaki tek satirlik `set_status` ise SON mesaji tutar, gecmisi
// degil: art arda iki hata olursa ilki okunmadan silinir.
//
// SOZLESME:
//  * AYIRMA YOK. Halka tamponu (kConsoleCapacity x kConsoleMsgLen) statik
//    bellektedir; log cagrisi malloc'a inmez, kare icinde guvenlidir.
//  * TASMA SESSIZ DEGIL. Halka dolunca EN ESKI kayit dusurulur ve dusen sayisi
//    sayilir (console_dropped) — arayuz onu gosterir. "Son 512 satir" ile
//    "toplam 512 satir" ayni sey degildir ve kullanici hangisinde oldugunu
//    bilmelidir.
//  * ARDISIK AYNI MESAJ TOPLANIR (Unity "Collapse"): her karede tekrarlayan
//    bir uyari halkayi bir kare icinde suplurmesin diye ayni satirin `repeat`
//    sayaci artar. Toplama yalniz ARDISIK ve ayni (duzey, etiket, metin)
//    ucusu icindir; araya baska bir satir girerse yeni kayit acilir.
//  * KESME GORUNUR. kConsoleMsgLen'e sigmayan satir "…" ile biter ve
//    ConsoleEntry::truncated isaretlenir; sessizce kirpilmaz.
//
// STDOUT/STDERR YAKALAMA (console_capture_*): motorun kendi printf'leri ve
// dogrulama katmaninin ciktisi C seviyesinde `fprintf` ile yazilir — bir C++
// geri cagrisiyla yakalanamaz. POSIX `pipe` + `dup2` ile fd 1/2 bir boruya
// yonlendirilir, her kare bloklamadan okunur. ISTEGE BAGLIDIR: yakalama
// acilmazsa cikti ESKISI GIBI terminale gider (kapinin olumsuz kontrolu budur).
#pragma once
#include <cstdarg>
#include <cstdint>

namespace tulpar::engine::app {

// --- Kayit modeli -----------------------------------------------------------
enum class ConsoleLevel : uint8_t { Bilgi = 0, Uyari = 1, Hata = 2, Count = 3 };
constexpr uint32_t kConsoleLevelCount = (uint32_t)ConsoleLevel::Count;

constexpr uint32_t kConsoleCapacity = 512; // halka girdisi (en eski dusurulur)
constexpr uint32_t kConsoleMsgLen = 256;   // NUL dahil; asilirsa "…" ile biter
constexpr uint32_t kConsoleTagLen = 16;    // NUL dahil ("vulkan", "sahne", ...)

// Standart etiketler. Serbest metin de gecerlidir (kConsoleTagLen'e kirpilir),
// bunlar yalniz yakalama siniflandiricisinin ve cagiranlarin ortak sozlugu.
constexpr const char *kConsoleTagEditor = "editor";
constexpr const char *kConsoleTagEngine = "motor";
constexpr const char *kConsoleTagVulkan = "vulkan";
constexpr const char *kConsoleTagScene = "sahne";

struct ConsoleEntry {
  ConsoleLevel level = ConsoleLevel::Bilgi;
  bool truncated = false;              // mesaj kConsoleMsgLen'e sigmadi ("…")
  char tag[kConsoleTagLen] = {0};
  uint32_t frame = 0;                  // ILK gorulusteki kare numarasi
  uint32_t last_frame = 0;             // son tekrarin karesi (repeat > 1 ise)
  uint32_t repeat = 1;                 // ardisik ayni satir sayisi (Unity xN)
  uint32_t seq = 0;                    // monoton sira: halka kaysa da SABIT kimlik
  char msg[kConsoleMsgLen] = {0};
};

struct ConsoleCounts {
  uint32_t n[kConsoleLevelCount] = {0, 0, 0}; // halkada DURAN kayitlar (tekrar = 1)
  uint32_t bilgi() const { return n[0]; }
  uint32_t uyari() const { return n[1]; }
  uint32_t hata() const { return n[2]; }
  uint32_t total() const { return n[0] + n[1] + n[2]; }
};

// --- Yazma ------------------------------------------------------------------
// Kare numarasi: her kayda damgalanir (hangi karede oldugu bir hata avinda
// tek ipucu olabilir). Cagiran kare basinda bir kez set eder.
void console_set_frame(uint32_t frame);
uint32_t console_frame();

void console_log(ConsoleLevel level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void console_logv(ConsoleLevel level, const char *tag, const char *fmt, va_list ap);
// Bicimlendirilmemis HAZIR satir (yakalama yolu bunu kullanir: yakalanan metin
// bir bicim dizesi DEGILDIR — icindeki '%' bicimlendiriciye verilemez).
void console_log_raw(ConsoleLevel level, const char *tag, const char *text);
void console_clear();

// --- Okuma ------------------------------------------------------------------
uint32_t console_size();                  // halkada duran kayit
uint32_t console_dropped();               // tasmada DUSEN en eski kayit sayisi
uint32_t console_total();                 // acilistan beri uretilen kayit (toplananlar haric)
ConsoleCounts console_counts();           // duzey basina (halkadakiler)
const ConsoleEntry *console_at(uint32_t i); // 0 = EN ESKI; yoksa nullptr
const ConsoleEntry *console_by_seq(uint32_t seq); // kaydigi icin secim seq ile tutulur

// --- Panel gorunumu ---------------------------------------------------------
struct ConsoleView {
  bool show[kConsoleLevelCount] = {true, true, true}; // duzey suzgeci
  bool autoscroll = true;                             // yeni satir geldikce alta kay
  char search[64] = {0};                              // metin/etiket aramasi (harf duyarsiz)
  int32_t selected = -1;                              // secili kaydin seq'i (-1 = yok)
  bool expanded = false;                              // secili satirin tam metni acik mi
  // --- Olcum ciktisi (kapilar bunu okur; editor kodu KULLANMAZ) ---
  uint32_t shown = 0;       // suzgecten gecen satir sayisi (son cizim)
  uint32_t clipped = 0;     // ImGuiListClipper'in GERCEKTEN cizdigi satir
  bool scrolled = false;    // bu karede alta kaydirildi
};

// Bir kaydin suzgecten gecip gecmedigi — SAF (ImGui gerekmez). Panel ve kapi
// AYNI kurali kullanir, ayrisamazlar.
bool console_row_visible(const ConsoleEntry &e, const ConsoleView &v);
// Suzgecten gecen kayitlarin indeksleri (console_at indeksi), eski -> yeni.
// Donus: yazilan sayi (cap ile sinirli).
uint32_t console_filtered(const ConsoleView &v, uint32_t *out, uint32_t cap);

// Panelin TUM govdesi (ImGui::Begin/End cagiranindir): arac cubugu (Temizle,
// otomatik kaydirma, uc duzey dugmesi + sayilar, arama) + ImGuiListClipper ile
// liste. Renk yalniz editor_tone'dan.
void console_panel(ConsoleView &v);

// --- stdout/stderr yakalama -------------------------------------------------
// Tek bir akisin (stdout ya da stderr) yakalama durumu.
struct ConsoleCaptureStream {
  int saved_fd = -1;  // dup()'lanmis OZGUN fd (end'de geri konur)
  int read_fd = -1;   // borunun okuma ucu (O_NONBLOCK)
  int target_fd = -1; // 1 ya da 2
  bool is_err = false;
  // Satir sonu HENUZ gelmemis kuyruk. Bir yazma cagrisi satirin ortasinda
  // bolunebilir; parca satir sonu gelene kadar burada BEKLER (yarim satiri
  // kayit yapmak, ayni satiri ikiye bolup iki kere gostermek demektir).
  char partial[kConsoleMsgLen];
  uint32_t partial_len = 0;
  bool overflowing = false; // satir tampona sigmadi: '\n' gelene kadar at
};
struct ConsoleCapture {
  ConsoleCaptureStream out, err;
  bool active = false;
  bool echo = true;      // yakalanan satir OZGUN fd'ye de yazilsin mi (terminal sussuz kalmasin)
  // Olcum: kapi ve arayuz "hicbir sey gelmedi" ile "yakalama kapali"yi ayirt etsin.
  uint32_t lines = 0;    // halkaya giren satir
  uint32_t bytes = 0;    // okunan bayt
  uint32_t long_lines = 0; // kConsoleMsgLen'i asan satir (gorunur "…")
  char err_msg[128] = {0}; // begin basarisizsa sebep
};

// fd 1 ve 2'yi boruya yonlendirir. Basarisizsa false ve c->err_msg dolar; hicbir
// sey degistirilmemis olur (kismi kurulum geri alinir). stdout tampon kipi
// UNBUFFERED'a cekilir: bir boruya yazan libc TAM TAMPONLAMAYA gecer ve cikti
// kilobaytlar birikene kadar konsola HIC ULASMAZDI.
bool console_capture_begin(ConsoleCapture *c);
// Etkin yakalamayi bloklamadan okur, satirlara boler, siniflandirir ve halkaya
// yazar. Kare basina BIR kez cagrilir. Etkin yakalama yoksa hicbir sey yapmaz.
void console_capture_drain();
// Ozgun fd'leri geri koyar ve yarim kalmis parcayi (varsa) halkaya yazar —
// son satir '\n' ile bitmediyse bile kaybolmasin.
void console_capture_end(ConsoleCapture *c);
// Etkin yakalama (yoksa nullptr) — arayuz "yakalama acik mi"yi gosterebilsin.
const ConsoleCapture *console_capture_active();

// Yakalanan bir satirin duzeyi/etiketi — SAF, kapi dogrudan olcer.
// Duzey sirasi: once HATA/ERROR, sonra UYARI/WARNING, sonra ciplak VUID.
// ("Validation Warning: [ VUID-... ]" satirinda VUID'e once bakilsa UYARI
// HATA olarak gosterilirdi; sira bu yuzden boyle.)
ConsoleLevel console_classify_level(const char *line, bool from_stderr);
// Etiket: "[engine_editor]"/"[editor]" -> editor, "[engine]" -> motor,
// VUID/Validation/UNASSIGNED- -> vulkan, "sahne " -> sahne, kalani motor.
const char *console_classify_tag(const char *line);

} // namespace tulpar::engine::app
