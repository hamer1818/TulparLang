#ifndef TULPAR_IMPORT_RESOLVE_HPP
#define TULPAR_IMPORT_RESOLVE_HPP

// `import "<ad>"` icin DISK adaylari — TEK KAYNAK (oyun geri bildirimi #6,
// 2026-10-08).
//
// Bu kural eskiden dort yerde ayri ayri yaziliydi (kodgen import_load_module,
// derleme onbellegi resolve_import, typeinfer load_import_source,
// ayristiricinin on tarama yukleyicisi) ve AYRISMISTI: typeinfer ic ice
// import'u ice aktaranin dizininden hic aramiyordu. Ayrisan bir kural ya
// yanlis modulu denetler ya da (onbellekte) bayat ikili verir (Tuzaklar 7n).
// Hepsi artik bunu cagiriyor.
//
// Sira (gomulu stdlib ve yerel eklenti modulu CAGIRANDA, bundan ONCE):
//   1. <from_dir>/<ad>.tpr   ice aktaran dosyanin dizini (paket-yerel kardes)
//   2. <from_dir>/<ad>       ice aktaran dosyanin dizini, yazildigi gibi
//   3. <ad>                  calisma dizini (eski kural)
//   4. <ad>.tpr              calisma dizini (eski kural)
//   5. tulpar_modules/<ad>/<ad>.tpr
//   6. tulpar_modules/<ad>.tpr
// 1-2 yalniz from_dir bos degilse ve ad goreliyse denenir. from_dir ANA dosya
// icin onun dizini ("" = calisma dizini: `tulpar oyun.tpr` eskisiyle ayni),
// bir modul icin cozulen dosyanin dizini. 2 yeni (eskiden yalniz 1 vardi):
// `a/b/m1.tpr` icindeki `import "d/c.tpr"` artik `a/b/d/c.tpr`yi buluyor,
// program hangi dizinden baslatilirsa baslatilsin. Eski kural (3-4) geri
// donus: bugune kadar calisma dizinine gore yazilmis her import ayni dosyayi
// bulur — ice aktaranin dizininde AYNI ADLI BASKA bir dosya yoksa. Varsa
// yeni kural kazanir ve `shadowed` dolar (cagiran uyarir).
//
// Yalniz DUZENLI dosyalar aday: Linux'ta fopen bir DIZINI de acar ve
// `import "davranis"` (davranis/ dizini + davranis.tpr) dizini okumaya
// kalkardi.

#include <string>

namespace tulpar {
namespace imports {

struct Resolution {
  bool found = false;
  std::string path;      // acilacak yol (calisma dizinine gore ya da mutlak)
  std::string dir;       // path'in dizini ("" = calisma dizini)
  int slot = 0;          // 1..6 (yukaridaki sira)
  // 1-2 kazandiysa ve calisma dizini adayi (3-4) da BASKA bir dosyaysa onun
  // yolu: belirsizlik, eski kural onu secerdi.
  std::string shadowed;
};

Resolution resolve_disk(const std::string &name, const std::string &from_dir);

// "a/b/c.tpr" -> "a/b"; "c.tpr" -> ""; "/c.tpr" -> "/".
std::string dir_of(const std::string &path);

// Ayni dosya mi karari icin kimlik: mutlak, normallestirilmis yol (yoksa yol).
std::string identity(const std::string &path);

}  // namespace imports
}  // namespace tulpar

#endif  // TULPAR_IMPORT_RESOLVE_HPP
