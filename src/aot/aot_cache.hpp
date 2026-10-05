// DERLEME ONBELLEGI — icerik adresli anahtar (2026-10-05).
//
// NIYE: `tulpar build` onbellegi mtime'a bakiyordu (cikti kaynaktan, yerel
// import'lardan, eklenti dosyalarindan ve surucuden yeni mi). Gormedikleri:
// kod uretimini degistiren ortam degiskenleri (TULPAR_NO_FVER ...), runtime
// arsivi, paket-yerel kardes import'lar (tulpar_modules/<p>/<ic>.tpr), gomulu
// bir modulun diskten import ettigi dosya, mtime'i geri alinmis icerik.
// Olculdu (2026-10-05, v3.39.6): TULPAR_NO_FVER=1 ile ikinci `build`
// "Cache hit" deyip float surumlu ESKI ikiliyi birakti; bir sabotaj olcumu
// bu yuzden yanlislikla yesil cikti. `tulpar dosya.tpr` ise HIC onbellek
// kullanmiyordu: her kosu bastan derliyordu (wings ornegi ~0,56 s).
//
// MODEL: anahtar, ciktiyi belirleyen HER girdinin SHA-256 ozetidir:
//   surucu (surum + ikilinin kimligi + yuklu libLLVM), ana kaynagin ve
//   gecisli import'larin ICERIGI (cozum sirasi kodgenle ayni), tulpar.toml,
//   eklentilerin bildirim/modul icerigi + arsiv kimlikleri, runtime/tame
//   arsivlerinin kimlikleri (link arama dizinlerinin hepsinde), baglama
//   surucusunun kimligi, hedef uclusu + CPU, kip/cikti adi, dil, ve
//   aot_cache_env.inc'in DISLAMADIGI her TULPAR_* ortam degiskeni.
//   "Kimlik" = boyut + mtime(ns) + ctime(ns) + inode: icerik okumadan
//   degisimi gorur; ctime kullanici tarafindan geri alinamaz (touch -r
//   mtime'i geri alir, ctime'i degil).
//
// OZ DENETIM: derleme sirasinda kodgenin GERCEKTEN okudugu her dosya
// (note_input) anahtarin kapsadigi dosyalar arasinda olmali; degilse sonuc
// onbellege YAZILMAZ. Cozum kurallari kodgende degisip burada unutulursa
// bedel bayat ikili degil, onbelleksiz derlemedir.
#ifndef TULPAR_AOT_CACHE_HPP
#define TULPAR_AOT_CACHE_HPP

#include <string>
#include <vector>

namespace tulpar {
namespace cache {

enum class Mode { Run, Build };

struct Key {
  bool ok = false;           // false: onbelleklenemez, `why` nedeni
  std::string why;
  std::string hex;           // 32 hex (128 bit)
  std::string material;      // anahtarin duz metni (TULPAR_CACHE_RAPOR=2)
  std::vector<std::string> inputs;  // kapsanan disk dosyalari (kanonik)
};

// Onbellek bu cagrida kullanilabilir mi? false ise `why` nedeni yazar
// (TULPAR_AOT_NOCACHE, bir GOZLEM degiskeni, kok dizin yok ...).
bool usable(std::string &why);

// Kok dizin: TULPAR_CACHE_DIR > XDG_CACHE_HOME/tulpar | ~/.cache/tulpar
// (macOS ~/Library/Caches/tulpar, Windows %LOCALAPPDATA%\tulpar\cache).
std::string root_dir();

// `output_name`: Build kipinde cikti adi (uzantisiz, kullanicinin yazdigi);
// Run kipinde nullptr.
Key compute_key(Mode mode, const char *source, const char *source_path,
                const char *output_name);

// --- Oz denetim: derleme sirasinda okunan dosyalar ---------------------------
void record_begin();
// Kodgen / ayristirici import yukleyicileri cagirir (kayit kapaliysa no-op).
void note_input(const char *path);
std::vector<std::string> record_end();
// Okunan her dosya anahtarda mi? Degilse false + ilk eksik yol.
bool inputs_covered(const Key &k, const std::vector<std::string> &read,
                    std::string &missing);

// --- stderr yakalama (isabette tanilar yeniden basilsin diye) ----------------
// Derleme sirasindaki stderr bir dosyaya yonlenir; end() onu GERCEK stderr'e
// aktarir ve metni dondurur. Derleyici exit()/cokme ile cikarsa da aktarilir
// (atexit + sinyal isleyicisi) — tani kaybolmasin.
class StderrCapture {
 public:
  bool begin(const std::string &path);
  std::string end();
  bool active() const { return active_; }
  ~StderrCapture();

 private:
  bool active_ = false;
};

// --- Calistirma deposu: <kok>/run/<hex>[.exe] ---------------------------------
// Isabet: ikili var -> yolunu ve kayitli tanilari dondurur (LRU icin mtime
// tazelenir).
bool run_lookup(const Key &k, std::string &exe, std::string &diag);
// Derlemenin yazacagi gecici taban ad (<kok>/run/tmp-<pid>-<sayac>); bos =
// depo kullanilamiyor.
std::string run_staging_base();
// `built` ikilisini <hex> adina YAYIMLA: asla uzerine yazmaz (POSIX link(),
// Windows MoveFileEx bayraksiz). Baska surec once yayimladiysa onunki kullanilir.
// Basarida `final_exe` dolar; `built` her durumda yerinden kaldirilir
// (basarisizsa cagiran onu calistirabilsin diye yerinde birakilir: false).
bool run_publish(const Key &k, const std::string &built, const std::string &diag,
                 std::string &final_exe);
// Toplam boyut tavani asildiysa en eski girdileri sil (LRU, mtime).
void evict();

// --- Derleme dizini: <kok>/build/<ozet(cikti yolu)> --------------------------
// Ciktinin (exe yolu) anahtari ve ikili kimligi tutulur; ikili kullanicinin
// dizininde kalir, hicbir sey ona eklenmez/yazilmaz.
bool build_lookup(const Key &k, const std::string &exe_path, std::string &diag);
void build_record(const Key &k, const std::string &exe_path, const std::string &diag);
void build_forget(const std::string &exe_path);

// TULPAR_CACHE_RAPOR=1: karar satirlari stderr'e ("[onbellek] ...");
// =2: ayrica anahtarin duz metni (iska nedenini iki metni karsilastirarak
// bulmak icin). Onbellegin ayarlarini YALNIZ bu dosya okur (kapi).
bool report_enabled();
void report(const char *fmt, ...);
void report_material(const Key &k);

// `tulpar cache [info|clean|dir]`
int cache_cmd_main(int argc, char **argv);

}  // namespace cache
}  // namespace tulpar

#endif  // TULPAR_AOT_CACHE_HPP
