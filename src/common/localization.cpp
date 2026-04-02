#include "localization.hpp"
#include <cstdlib>
#include <cstring>

namespace tulpar {
namespace i18n {

static bool contains_tr_marker(const char *value) {
  if (!value || !*value) {
    return false;
  }

  const char *markers[] = {"tr_TR", "tr-TR", "tr_", "tr.", "turkish", "tr"};
  for (const char *marker : markers) {
    if (strstr(value, marker) != nullptr) {
      return true;
    }
  }
  return false;
}

bool is_turkish_locale() {
  const char *envs[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
  for (const char *env_name : envs) {
    const char *value = getenv(env_name);
    if (contains_tr_marker(value)) {
      return true;
    }
  }
  return false;
}

const char *tr_en(const char *tr_text, const char *en_text) {
  return is_turkish_locale() ? tr_text : en_text;
}

const char *tr_for_en(const char *en_text) {
  if (!en_text) {
    return "";
  }

  if (strcmp(en_text, "Runtime Error: ") == 0)
    return "Calisma Zamani Hatasi: ";
  if (strcmp(en_text, "Parser Error: %s\n") == 0)
    return "Ayristirici Hatasi: %s\n";
  if (strcmp(en_text, "Parse error: %s\n") == 0)
    return "Ayristirma hatasi: %s\n";
  if (strcmp(en_text, "Lexer Error: Block comment not terminated (started at line %d, col %d)\n") == 0)
    return "Sozcukleyici Hatasi: Blok yorumu kapatilmadi (baslangic satir %d, sutun %d)\n";
  if (strcmp(en_text, "Lexer Error: Unknown character '%c' at line %d, col %d\n") == 0)
    return "Sozcukleyici Hatasi: Bilinmeyen karakter '%c' (satir %d, sutun %d)\n";
  if (strcmp(en_text, "Type Error: %s\n") == 0)
    return "Tip Hatasi: %s\n";
  if (strcmp(en_text, "JIT Error: Code buffer overflow (need %zu, have %zu)\n") == 0)
    return "JIT Hatasi: Kod tamponu tasmasi (gerekli %zu, mevcut %zu)\n";
  if (strcmp(en_text, "[AOT] Error: Failed to parse source\n") == 0)
    return "[AOT] Hata: Kaynak ayristirilamadi\n";
  if (strcmp(en_text, "[AOT] Error: Failed to create LLVM backend\n") == 0)
    return "[AOT] Hata: LLVM backend olusturulamadi\n";
  if (strcmp(en_text, "[AOT] Error: Failed to emit object file\n") == 0)
    return "[AOT] Hata: Object dosyasi uretilemedi\n";
  if (strcmp(en_text, "[AOT] Error: Linking failed (code %d). Check clang installation and libraries.\n") == 0)
    return "[AOT] Hata: Baglama basarisiz (kod %d). clang kurulumu ve kutuphaneleri kontrol edin.\n";
  if (strcmp(en_text, "Undefined var in array access: %s\n") == 0)
    return "Dizi erisiminde tanimsiz degisken: %s\n";
  if (strcmp(en_text, "Undefined var in increment: %s\n") == 0)
    return "Artirimda tanimsiz degisken: %s\n";
  if (strcmp(en_text, "Undefined var in decrement: %s\n") == 0)
    return "Azaltimda tanimsiz degisken: %s\n";
  if (strcmp(en_text, "Undefined var in compound assign: %s\n") == 0)
    return "Bilesik atamada tanimsiz degisken: %s\n";
  if (strcmp(en_text, "Undefined var in assignment: %s\n") == 0)
    return "Atamada tanimsiz degisken: %s\n";
  if (strcmp(en_text, "Error: Could not import file '%s'\n") == 0)
    return "Hata: Import dosyasi acilamadi '%s'\n";
  if (strcmp(en_text, "Error: Array only accepts elements of type ") == 0)
    return "Hata: Dizi yalnizca su turde eleman kabul eder: ";
  if (strcmp(en_text, "Error: Array index out of bounds: %d (length: %d)\n") == 0)
    return "Hata: Dizi indeksi sinir disinda: %d (uzunluk: %d)\n";
  if (strcmp(en_text, "Error: Array index must be integer (line %d)\n") == 0)
    return "Hata: Dizi indeksi tamsayi olmali (satir %d)\n";
  if (strcmp(en_text, "Error: Object key must be string (line %d)\n") == 0)
    return "Hata: Nesne anahtari string olmali (satir %d)\n";
  if (strcmp(en_text, "Error: Key '%s' not found in object\n") == 0)
    return "Hata: Nesnede '%s' anahtari bulunamadi\n";
  if (strcmp(en_text, "Error: String index must be integer (line %d)\n") == 0)
    return "Hata: String indeksi tamsayi olmali (satir %d)\n";
  if (strcmp(en_text, "Error: String index out of bounds (0-%d, given %d) (line %d)\n") == 0)
    return "Hata: String indeksi sinir disinda (0-%d, verilen %d) (satir %d)\n";
  if (strcmp(en_text, "Error: UTF-8 character could not be decoded\n") == 0)
    return "Hata: UTF-8 karakteri cozumlenemedi\n";
  if (strcmp(en_text, "Error: Accessed value is not an array, object, or string (line %d)\n") == 0)
    return "Hata: Erisilen deger dizi, nesne veya string degil (satir %d)\n";
  if (strcmp(en_text, "Error: Function '%s' not found for call()\n") == 0)
    return "Hata: call() icin '%s' fonksiyonu bulunamadi\n";
  if (strcmp(en_text, "Error: Unsupported type for toInt() (line %d)\n") == 0)
    return "Hata: toInt() icin desteklenmeyen tur (satir %d)\n";
  if (strcmp(en_text, "Error: Unsupported type for toFloat() (line %d)\n") == 0)
    return "Hata: toFloat() icin desteklenmeyen tur (satir %d)\n";
  if (strcmp(en_text, "Error: Unsupported type for toString() (line %d)\n") == 0)
    return "Hata: toString() icin desteklenmeyen tur (satir %d)\n";
  if (strcmp(en_text, "Error: Missing field in JSON: %s (fromJson)\n") == 0)
    return "Hata: JSON icinde eksik alan: %s (fromJson)\n";
  if (strcmp(en_text, "Error: length() only for array/object/string (line %d)\n") == 0)
    return "Hata: length() yalnizca dizi/nesne/string icin kullanilir (satir %d)\n";
  if (strcmp(en_text, "Error: push() first argument must be array (line %d)\n") == 0)
    return "Hata: push() ilk argumani dizi olmali (satir %d)\n";
  if (strcmp(en_text, "Error: pop() first argument must be array (line %d)\n") == 0)
    return "Hata: pop() ilk argumani dizi olmali (satir %d)\n";
  if (strcmp(en_text, "Error: pow() expects numeric arguments (line %d)\n") == 0)
    return "Hata: pow() sayisal argumanlar bekler (satir %d)\n";
  if (strcmp(en_text, "Error: Modulo by zero! (line %d)\n") == 0)
    return "Hata: Sifira mod alma! (satir %d)\n";
  if (strcmp(en_text, "Error: mod() expects integer arguments (line %d)\n") == 0)
    return "Hata: mod() tamsayi argumanlar bekler (satir %d)\n";
  if (strcmp(en_text, "SQL Error: %s\n") == 0)
    return "SQL Hatasi: %s\n";
  if (strcmp(en_text, "Error: Object expected for method call (line %d)\n") == 0)
    return "Hata: Metot cagrisi icin nesne bekleniyor (satir %d)\n";
  if (strcmp(en_text, "Error: Type marker not found for method (line %d)\n") == 0)
    return "Hata: Metot icin tip isaretleyicisi bulunamadi (satir %d)\n";
  if (strcmp(en_text, "Error: Undefined method '%s' (line %d)\n") == 0)
    return "Hata: Tanimsiz metot '%s' (satir %d)\n";
  if (strcmp(en_text, "Error: For type '%s', all arguments must be named or none\n") == 0)
    return "Hata: '%s' tipi icin tum argumanlar ya isimli olmali ya da hicbiri isimli olmamali\n";
  if (strcmp(en_text, "Error: Field '%s' not found in type '%s'\n") == 0)
    return "Hata: '%s' alani '%s' tipinde bulunamadi\n";
  if (strcmp(en_text, "Error: Field '%s' assigned twice in type '%s'\n") == 0)
    return "Hata: '%s' alani '%s' tipinde iki kez atandi\n";
  if (strcmp(en_text, "Error: Field '%s' must be int\n") == 0)
    return "Hata: '%s' alani int olmali\n";
  if (strcmp(en_text, "Error: Field '%s' must be float\n") == 0)
    return "Hata: '%s' alani float olmali\n";
  if (strcmp(en_text, "Error: Field '%s' must be str\n") == 0)
    return "Hata: '%s' alani str olmali\n";
  if (strcmp(en_text, "Error: Field '%s' must be bool\n") == 0)
    return "Hata: '%s' alani bool olmali\n";
  if (strcmp(en_text, "Error: Field '%s' must be type '%s'\n") == 0)
    return "Hata: '%s' alani '%s' tipinde olmali\n";
  if (strcmp(en_text, "Error: Field '%s' expects type '%s'\n") == 0)
    return "Hata: '%s' alani '%s' tipi bekliyor\n";
  if (strcmp(en_text, "Error: Default '%s' must be int\n") == 0)
    return "Hata: Varsayilan '%s' int olmali\n";
  if (strcmp(en_text, "Error: Default '%s' must be float\n") == 0)
    return "Hata: Varsayilan '%s' float olmali\n";
  if (strcmp(en_text, "Error: Default '%s' must be str\n") == 0)
    return "Hata: Varsayilan '%s' str olmali\n";
  if (strcmp(en_text, "Error: Default '%s' must be bool\n") == 0)
    return "Hata: Varsayilan '%s' bool olmali\n";
  if (strcmp(en_text, "Error: Default '%s' must be type '%s'\n") == 0)
    return "Hata: Varsayilan '%s' '%s' tipinde olmali\n";
  if (strcmp(en_text, "Error: Default '%s' expects type '%s'\n") == 0)
    return "Hata: Varsayilan '%s' '%s' tipi bekliyor\n";
  if (strcmp(en_text, "Error: Missing field '%s' for type '%s'\n") == 0)
    return "Hata: '%s' tipi icin eksik alan '%s'\n";
  if (strcmp(en_text, "Error: Expected %d arguments for type '%s', got %d\n") == 0)
    return "Hata: %d arguman '%s' tipi icin bekleniyordu, %d alindi\n";
  if (strcmp(en_text, "Error: Undefined function '%s'\n") == 0)
    return "Hata: Tanimsiz fonksiyon '%s'\n";
  if (strcmp(en_text, "Error: All elements in array literal must be of type ") == 0)
    return "Hata: Dizi literal'indeki tum elemanlar su tipte olmali: ";
  if (strcmp(en_text, "Error: Invalid assignment left-hand side\n") == 0)
    return "Hata: Atamanin sol tarafi gecersiz\n";
  if (strcmp(en_text, "Error: '%s' is not defined\n") == 0)
    return "Hata: '%s' tanimli degil\n";
  if (strcmp(en_text, "Error: Object key must be string\n") == 0)
    return "Hata: Nesne anahtari string olmali\n";
  if (strcmp(en_text, "Error: Array index must be integer\n") == 0)
    return "Hata: Dizi indeksi tamsayi olmali\n";
  if (strcmp(en_text, "Error: Array index out of bounds (line %d)\n") == 0)
    return "Hata: Dizi indeksi sinir disinda (satir %d)\n";
  if (strcmp(en_text, "Error: Intermediate segment must be array or object (line %d)\n") == 0)
    return "Hata: Ara segment dizi veya nesne olmali (satir %d)\n";
  if (strcmp(en_text, "Error: Target container must be array or object (line %d)\n") == 0)
    return "Hata: Hedef konteyner dizi veya nesne olmali (satir %d)\n";
  if (strcmp(en_text, "Error: Could not open import '%s' (line %d)\n") == 0)
    return "Hata: Import acilamadi '%s' (satir %d)\n";
  if (strcmp(en_text, "  Hint: Available embedded libraries: wings, router, http_utils\n") == 0)
    return "  Ipucu: Mevcut gomulu kutuphaneler: wings, router, http_utils\n";
  if (strcmp(en_text, "Expected %d arguments but got %d.") == 0)
    return "%d arguman bekleniyordu fakat %d alindi.";
  if (strcmp(en_text, "Invalid function constant") == 0)
    return "Gecersiz fonksiyon sabiti";
  if (strcmp(en_text, "Modulo by zero") == 0)
    return "Sifira mod alma";
  if (strcmp(en_text, "Modulo requires integers") == 0)
    return "Modulo tamsayi ister";
  if (strcmp(en_text, "Operand must be a number.") == 0)
    return "Islenen sayi olmali.";
  if (strcmp(en_text, "Can only call functions.") == 0)
    return "Yalnizca fonksiyonlar cagrilabilir.";
  if (strcmp(en_text, "Can only tail call functions.") == 0)
    return "Tail call yalnizca fonksiyonlarda kullanilir.";
  if (strcmp(en_text, "Stack overflow.") == 0)
    return "Yigin tasmasi.";
  if (strcmp(en_text, "thread_create expects function name as string.") == 0)
    return "thread_create fonksiyon adini string olarak bekler.";
  if (strcmp(en_text, "Thread function '%s' not found.") == 0)
    return "Thread fonksiyonu '%s' bulunamadi.";
  if (strcmp(en_text, "call() expects a function name string.") == 0)
    return "call() fonksiyon adi string bekler.";
  if (strcmp(en_text, "Too many nested try blocks.") == 0)
    return "Cok fazla ic ice try blogu.";
  if (strcmp(en_text, "Unknown opcode %d") == 0)
    return "Bilinmeyen opcode %d";
  if (strcmp(en_text, "Expected array for push.") == 0)
    return "push icin dizi bekleniyordu.";
  if (strcmp(en_text, "Array index must be integer.") == 0)
    return "Dizi indeksi tamsayi olmali.";
  if (strcmp(en_text, "Array index out of bounds.") == 0)
    return "Dizi indeksi sinir disinda.";
  if (strcmp(en_text, "Object key must be string.") == 0)
    return "Nesne anahtari string olmali.";
  if (strcmp(en_text, "String index must be integer.") == 0)
    return "String indeksi tamsayi olmali.";
  if (strcmp(en_text, "String index out of bounds.") == 0)
    return "String indeksi sinir disinda.";
  if (strcmp(en_text, "Index access only supported on Arrays/Objects/Strings.") == 0)
    return "Indeks erisimi yalnizca Diziler/Nesneler/Stringler icin desteklenir.";
  if (strcmp(en_text, "Index assignment only supported on Arrays/Objects.") == 0)
    return "Indeks atamasi yalnizca Diziler/Nesneler icin desteklenir.";
  if (strcmp(en_text, "Expected object for property access.") == 0)
    return "Ozellik erisimi icin nesne bekleniyordu.";
  if (strcmp(en_text, "Expected object for property set.") == 0)
    return "Ozellik atamasi icin nesne bekleniyordu.";
  if (strcmp(en_text, "Undefined gloabl '%s'") == 0)
    return "Tanimsiz global '%s'";
  if (strcmp(en_text, "  Hint: Available libraries: wings, router, http_utils\n") == 0)
    return "  Ipucu: Mevcut kutuphaneler: wings, router, http_utils\n";
  if (strcmp(en_text, "Compiler Error: 'break' outside of loop at line %d\n") == 0)
    return "Derleyici Hatasi: 'break' dongu disinda (satir %d)\n";
  if (strcmp(en_text, "Compiler Error: 'continue' outside of loop at line %d\n") == 0)
    return "Derleyici Hatasi: 'continue' dongu disinda (satir %d)\n";
  if (strcmp(en_text, "Warning: Unexpected type for toBool(): %s (applying truthiness) (line %d)\n") == 0)
    return "Uyari: toBool() icin beklenmeyen tur: %s (truthiness uygulanacak) (satir %d)\n";
  if (strcmp(en_text, "Socket error: %d\n") == 0)
    return "Soket hatasi: %d\n";
  if (strcmp(en_text, "Bind error: %d\n") == 0)
    return "Bind hatasi: %d\n";
  if (strcmp(en_text, "Listen error: %d\n") == 0)
    return "Listen hatasi: %d\n";
  if (strcmp(en_text, "Compilation error: ") == 0)
    return "Derleme hatasi: ";
  if (strcmp(en_text, "Compiler Warning: Unhandled expression type %d\n") == 0)
    return "Derleyici Uyarisi: Ele alinmayan ifade tipi %d\n";
  if (strcmp(en_text, "Compiler Warning: Unhandled statement type %d\n") == 0)
    return "Derleyici Uyarisi: Ele alinmayan komut tipi %d\n";
  if (strcmp(en_text, "[AOT] Warning: Optimization failed: %s\n") == 0)
    return "[AOT] Uyari: Optimizasyon basarisiz: %s\n";

  return en_text;
}

} // namespace i18n
} // namespace tulpar

