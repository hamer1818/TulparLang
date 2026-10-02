// YEREL EKLENTI (native extension) — K303, 2026-10-02.
//
// Derleyiciyi yeniden derlemeden bir C kitapligini Tulpar'a baglamanin yolu.
// Bir eklenti DIZINDIR; icinde `tulpar-ext.json` (bildirim), istege bagli
// Tulpar modulleri (`import "<ad>"` ile cozulur) ve link arsivleri durur.
// Bildirim VERIDIR: fonksiyon adi, parametre/donus tipleri, C sembolu,
// platforma gore link kitapliklari. Derleyici hicbir eklentiyi adiyla
// tanimaz — bu dosyada ve cagiranlarinda hicbir eklentiye ozgu ad yoktur.
//
// Bulunma (oncelik sirasiyla; ayni `name` ikinci kez gelirse ilki kazanir):
//   1. `--ext <yol>`        (komut satiri, tekrarlanabilir)
//   2. TULPAR_EXT_PATH      (yol listesi; POSIX ':' / Windows ';')
//   3. tulpar.toml [ext] paths = ["..."]  (calisma dizinindeki proje
//      dosyasi, yollar ona gore; LSP belgenin dizininden yukari arar)
// <yol> ya bir eklenti dizini (icinde tulpar-ext.json) ya da dogrudan bir
// .json bildirimidir. Verilen bir yol bulunamaz/bozuksa bu SESSIZ bir atlama
// degil, hatadir: eklentiyi kim verdiyse onu kullanmak istiyordu.
//
// Cagri ABI'si: DUZ SKALER C. Derleyici her cagride argumani bildirilen C
// tipine cevirip sembolu DOGRUDAN cagirir (VMValue sarmalayicisi yok; tipli
// ifadelerde kutulama da yok). C tarafinin bildirimle UYUSTUGU derleyicinin
// denetleyemeyecegi tek seydir (statik arsivde tip bilgisi yok) — eklenti
// yazari bunu kendi derlemesinde kilitler (bkz. docs/mindmap/Eklentiler.md).
#ifndef TULPAR_EXT_EXTENSIONS_HPP
#define TULPAR_EXT_EXTENSIONS_HPP

#include <string>
#include <vector>

namespace tulpar {
namespace ext {

// Bildirimdeki tip adlari ve C karsiliklari:
//   i32 -> int32_t   i64 / int -> int64_t   f32 -> float   f64 / float -> double
//   bool -> int (0/1)   str -> const char * (NUL sonlu; parametrede yalniz
//   cagri suresince gecerli, donuste derleyici hemen KOPYALAR; NULL = "")
//   void (yalniz donus)
enum class CType { Void, I32, I64, F32, F64, Bool, Str };

const char *ctype_name(CType t);

enum class Platform { Linux, MacOS, Windows, Android, Web };
constexpr int kPlatformCount = 5;
const char *platform_key(Platform p);

struct Function {
  std::string name;    // Tulpar'daki ad
  std::string symbol;  // C sembolu
  std::string doc;
  std::vector<CType> params;
  std::vector<std::string> param_names;
  CType ret = CType::Void;
  int ext_index = -1;
  // LSP/hover icin: "ad(a: i32, b: str): f64"
  std::string signature() const;
};

struct LinkSpec {
  bool present = false;
  std::vector<std::string> lib_dirs;  // mutlak (bildirim dizinine gore cozuldu)
  std::vector<std::string> libs;      // -l<ad>
  std::vector<std::string> flags;     // ham link bayraklari
  bool group = false;                 // GNU ld: --start-group/--end-group
};

struct Module {
  std::string name;  // import adi
  std::string path;  // mutlak dosya yolu
};

struct Extension {
  std::string name;
  std::string version;
  std::string description;
  std::string dir;            // bildirimin dizini (mutlak)
  std::string manifest_path;  // mutlak
  std::string origin;         // "--ext", "TULPAR_EXT_PATH", "tulpar.toml"
  std::vector<Module> modules;
  LinkSpec link[kPlatformCount];
  int function_count = 0;
  bool used = false;          // bu derlemede bir fonksiyonu/modulu kullanildi
};

// ---- Bulunma ---------------------------------------------------------------

// argv'den `--ext <yol>` / `--ext=<yol>` ciftlerini ayiklar ve kaydeder.
// Calistirilan betikten SONRAKI argumanlar programa aittir: ilk `.tpr`
// konumsal argumandan sonra yalniz `build` kipinde ayiklanir. Yeni argc'yi
// dondurur; `--ext` degersizse -1 (hata basildi).
int take_cli_args(int argc, char **argv);

// CLI + TULPAR_EXT_PATH + ./tulpar.toml. Bir kez yukler (tekrar cagri
// no-op). Hata -> false ve `err` (tam ileti, iki dilli).
bool load_default(std::string &err);

// LSP: belgenin dizininden yukari ilk tulpar.toml'u bulup [ext] yollarini
// yukler (CLI/env de dahil, load_default gibi). Ayni toml ikinci kez okunmaz.
bool load_for_document_dir(const std::string &dir, std::string &err);

// Yalniz testler/araclar: bir yolu dogrudan yukle.
bool load_path(const std::string &path, const char *origin, std::string &err);

// ---- Sorgu -----------------------------------------------------------------

const std::vector<Extension> &extensions();
const std::vector<Function> &functions();
const Function *find_function(const char *name);

// `import "<ad>"` bir eklenti modulu mu? Oyleyse kaynagi okur; `path` dosya
// yolu, `dir` modulun dizini (ic import'lar oradan cozulur), `ext_index`
// sahibi eklenti (kodgen onu mark_used ile isaretler; typeinfer isaretlemez).
bool read_module(const char *name, std::string &src, std::string &path,
                 std::string &dir, int *ext_index = nullptr);
// Okumadan: bu ad bir eklenti modulu mu (LSP / typeinfer on-taramasi).
const Extension *module_owner(const char *name, std::string *path);

// Hicbir eklenti yoksa bile `import "<ad>"` cozulemeyince basilacak ipucu.
std::string unresolved_import_hint(const char *name);

// ---- Derleme durumu --------------------------------------------------------

void mark_used(int ext_index);
void reset_used();
bool any_used();

// Kullanilan eklentilerin link bayraklari (bastaki bosluk dahil; hicbiri
// kullanilmadiysa ""). Android icin `abi` ("arm64-v8a" / "x86_64")
// `{abi}` yer tutucusunu doldurur. Kullanilan bir eklentinin bu hedef icin
// link bolumu yoksa false + `err`.
bool link_flags(Platform target, const char *abi, std::string &out,
                std::string &err);

// Derleyicinin kostugu platform (masaustu hedefi).
Platform host_platform();

// `tulpar build` onbellegi icin: yuklu eklentilerin bildirim, modul ve
// link arsivlerinin en yeni mtime'i (yoksa 0).
long long newest_mtime();

}  // namespace ext
}  // namespace tulpar

#endif  // TULPAR_EXT_EXTENSIONS_HPP
