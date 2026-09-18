// L6 APP — Sahne DOSYA ISLEMLERI: editor ici dosya tarayici (Ac / Kaydet),
// son dosyalar listesi, kaydedilmemis degisiklik korumasi ve yol yardimcilari.
//
// NEDEN: bugun editor yalniz `--scene <yol>` ile acilan TEK bir dosyayi
// tanir. Yeni yok, Ac yok, Farkli kaydet yok — baska bir sahneye gecmek icin
// editoru KAPATIP komut satirindan yeniden acmak gerekiyor. Bir editor icin
// bu, kaydedilmemis isin da tek korumasinin "kullanici unutmasin" olmasi
// demektir.
//
// NEDEN KENDI DIYALOGUMUZ (GTK/Qt/portal DEGIL): motorun hicbir masaustu
// arayuz bagimliligi yok (pencere GLFW, o da dlopen'li). Bir dosya
// secici icin GTK baglamak L0'a koca bir bagimlilik eklerdi ve Android'de
// zaten calismazdi. Diyalog ImGui ile cizilir, dizini POSIX `dirent` ile
// okur — editor_scan_assets ile AYNI disiplin: girdi ADA GORE SIRALANIR,
// cunku readdir sirasi dosya sistemine baglidir ve bir listeyi iki ayri
// makinede farkli gostermek bir arayuzde kabul edilemez.
//
// SOZLESME:
//  * Ayirma yok: sabit diziler + cagiranin tamponu. STL yok.
//  * Sessiz kirpma yok: kapasiteyi asan YOL bir hatadir (fonksiyon false
//    doner), kapasiteyi asan DIZIN GIRDISI sayilir (FileDialog::truncated) ve
//    arayuzde gosterilir.
//  * Son dosyalar dosyasi .sahne / .duzen ile ayni disiplinde: ASCII, satir
//    tabanli, AYNI GIRDI -> AYNI BAYT (kapi bunu bayt bayt olcer).
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::app {

constexpr uint32_t kFilePathLen = 1024; // tam yol (NUL dahil) — editor_app ile ayni tavan
constexpr uint32_t kFileNameLen = 128;  // tek girdi adi (NUL dahil)
constexpr uint32_t kFileListMax = 512;  // bir dizinde LISTELENEN en fazla girdi
constexpr uint32_t kFileCrumbMax = 32;  // yol kirinti (breadcrumb) sayisi

// --- Saf yol yardimcilari (ImGui gerekmez; kapilar dogrudan olcer) ----------
// "a/b" + "c.sahne" -> "a/b/c.sahne". Sigmazsa false (SESSIZ KIRPMA YOK).
bool file_path_join(const char *dir, const char *name, char *out, uint32_t cap);
// Ust dizin ("/a/b" -> "/a", "/a" -> "/", "/" -> "/"). Kok zaten koksa false.
bool file_path_parent(const char *dir, char *out, uint32_t cap);
// Yolun son bileseni ("a/b/c.sahne" -> "c.sahne"). Bos yol -> "".
const char *file_path_base(const char *path);
bool file_exists(const char *path);
bool file_is_dir(const char *path);
// Uzantiyi ZORLAR: girdide ext zaten varsa (harf duyarsiz, TAM eslesme) oldugu
// gibi kopyalar, yoksa ekler. "a" + ".sahne" -> "a.sahne"; "a.sahne" -> ayni;
// "a.sahneb" -> "a.sahneb.sahne" (".sahneb" ".sahne" DEGILDIR); "a.SAHNE" ->
// kullanicinin yazdigi gibi kalir. Bos ad ya da '/' ile biten yol: false.
// Sigmazsa false — Farkli kaydet'te sessizce baska bir dosyaya yazmaktansa hata.
bool scene_path_with_extension(const char *in, const char *ext, char *out, uint32_t cap);

// --- Dizin listesi ----------------------------------------------------------
struct FileEntry {
  char name[kFileNameLen] = {0};
  bool dir = false;
};
struct FileListResult {
  uint32_t count = 0;     // out'a yazilan girdi
  uint32_t dirs = 0;      // bunlarin ilk kaci dizin (dizinler ONCE yazilir)
  uint32_t truncated = 0; // kapasiteye SIGMAYAN girdi (sessiz degil)
  bool ok = false;        // dizin acilabildi mi
  char err[192] = {0};    // acilamadiysa gorunur sebep
};
// dir icindeki girdiler: once DIZINLER, sonra DOSYALAR, ikisi de ada gore
// sirali (belirlenimli — readdir sirasi dosya sistemine baglidir). Gizli
// girdiler (".") atlanir; ".." listeye KONMAZ (diyalog onu ayri satir cizer).
// ext != nullptr ve bos degilse yalniz o uzantiyla biten dosyalar (harf
// duyarsiz); dizinler her zaman gecer.
FileListResult file_list_dir(const char *dir, const char *ext, FileEntry *out, uint32_t cap);

// --- Dosya diyalogu (ImGui kipli pencere) -----------------------------------
enum class FileDialogMode : uint8_t { Ac, Kaydet };
enum class FileDialogAction : uint8_t { None, Accepted, Cancelled };

struct FileDialog {
  FileDialogMode mode = FileDialogMode::Ac;
  char title[64] = {0};
  char ext[16] = {0};            // ".sahne"; bos = suzgec yok
  char dir[kFilePathLen] = {0};  // gecerli dizin
  char name[kFileNameLen] = {0}; // Kaydet kipindeki ad kutusu
  char path[kFilePathLen] = {0}; // Accepted: secilen TAM yol
  char err[192] = {0};           // gorunur hata satiri ("" = yok)
  FileEntry items[kFileListMax];
  FileListResult list;
  int32_t sel = -1;          // items indeksi (-1 = secim yok)
  bool open = false;         // kipli pencere acik mi
  bool need_open = false;    // bu karede ImGui::OpenPopup cagrilsin
  bool ask_overwrite = false; // Kaydet: dosya var, onay bekleniyor
  bool focus_name = false;   // ad kutusuna odak (acilista / dosya seciminde)
  // Olcum ciktisi (kapilar okur; editor kodu kullanmaz)
  uint32_t shown = 0; // cizilen satir (".." dahil degil)
};

// Diyalogu hazirlar ve acar: start_dir listelenir (acilamazsa err dolar ama
// diyalog yine acilir — kullanici kirintidan yukari cikabilsin). Kaydet
// kipinde start_dir bir DOSYA yolu da olabilir: dizini + adi ondan alinir.
// Donus: dizin listelenebildi mi (diyalog her halukarda acilir).
bool file_dialog_open(FileDialog &d, FileDialogMode mode, const char *start_dir, const char *ext, const char *title);
// Kare icinde cagrilir (ImGui::NewFrame/Render arasi). Accepted dondugunde
// secilen yol d.path'tedir. None = hala acik ya da kapali.
FileDialogAction file_dialog_draw(FileDialog &d);
// Diyalogu dizini yeniden okuyarak tazeler (Yenile dugmesi / dizin degisimi).
void file_dialog_refresh(FileDialog &d);
// Diyalogun SU ANDAKI hedefi: Kaydet kipinde ad kutusu + zorlanmis uzanti,
// Ac kipinde secili girdi. false = hedef yok / yol tavani asildi.
bool file_dialog_target_path(const FileDialog &d, char *out, uint32_t cap);
// Hedef dosya diskte DURUYOR mu — "uzerine yazma onayi" kosulunun TA KENDISI.
// Diyalogun kendisi de bunu cagirir, boylece kapi ile arayuz ayrisamaz.
bool file_dialog_would_overwrite(const FileDialog &d);

// --- Onay kutusu (kaydedilmemis degisiklik korumasi) ------------------------
enum class ConfirmResult : uint8_t { None, Ok, Cancel, Third };
struct ConfirmState {
  bool open = false;      // true yapmak = "bu kare ac"
  bool need_open = false; // ic kullanim
  int32_t reason = 0;     // cagiranin serbest etiketi (hangi eylem bekliyor)
};
// Kipli onay penceresi. third == nullptr ise IKI dugme (Ok / Vazgec), degilse
// UC (ok / third / cancel) — "Kaydet / Kaydetme / Vazgec" tam olarak budur.
// Esc = Cancel, Enter = Ok. Kapaninca s.open false olur.
ConfirmResult confirm_modal(ConfirmState &s, const char *title, const char *message, const char *ok, const char *cancel,
                            const char *third = nullptr);

// --- Son dosyalar -----------------------------------------------------------
constexpr uint32_t kRecentMax = 10;
// Listeyi bosaltir (kapilar arasinda durum sizmasin).
void recent_clear();
// Dosyadan yukler. Dosya YOKSA false doner ama liste bos ve tutarli kalir —
// "ilk calistirma" bir hata degildir; sebep recent_last_error()'dadir.
bool recent_load(const char *path);
// En one ekler; ayni yol zaten varsa ONE TASINIR (kopya olmaz). Tavan asilinca
// en eski duser. Yol bos ya da kapasiteden uzunsa yok sayilir.
void recent_push(const char *scene_path);
// En yeni ONCE. Donus: yazilan sayi. Isaretciler bir sonraki degisiklige kadar
// gecerlidir (ic diziye bakar).
uint32_t recent_list(const char **out, uint32_t cap);
uint32_t recent_count();
// i. girdinin dosyasi diskte DURUYOR mu (menude soluk gostermek icin).
// recent_load / recent_push / recent_refresh sirasinda olculur.
bool recent_exists(uint32_t i);
void recent_refresh(); // varlik bayraklarini yeniden olcer
bool recent_save(const char *path);
const char *recent_last_error();
// Metin bicimi (kapi bayt bayt karsilastirir; snprintf gibi gereken uzunlugu
// doner). AYNI LISTE -> AYNI BAYT.
size_t recent_write(char *buf, size_t cap);
bool recent_parse(const char *text, size_t len);

} // namespace tulpar::engine::app
