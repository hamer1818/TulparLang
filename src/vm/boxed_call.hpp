// Kutulu ABI ile N isaretcili cagri — TEK KAYNAK.
//
// Tulpar'in kutulu giris noktalari su bicimde:
//   ust duzey fonksiyon  void t_<ad>(VMValue *sonuc, VMValue *a0, ..., VMValue *aN-1)
//   kapanis              void <lambda>(VMValue *sonuc, void *env, VMValue *a0, ...)
// Arite calisma zamaninda bilindigi icin (kayitli arite / kapanis basligi /
// async argc) cagri, aritesi TAM tutan bir imzaya donusturulup yapilmali:
// wasm'in tipli call_indirect'i baska sayiyi affetmez, yerelde ise eksik
// isaretci cagrilanin yazmactan COP okumasi demek (olculdu 2026-09-27: 10
// parametreli fonksiyon call() ile -> SIGSEGV; 3 parametreliye call(ad, x) ->
// SIGSEGV).
//
// Bu cagriyi yapan UC yer vardi ve her biri kendi elle yazilmis switch'ini
// tasiyordu, uc ayri tavanla: call() 8'de SESSIZCE kirpiyordu, kapanis 8'de
// tani basiyordu, async 16'da tani basip VOID donuyordu (cikis 0). Tavan
// artik burada ve uc yol da buradan geciyor.
//
// Neden 32 ve neden sinirsiz degil: tasinabilir C++'ta calisma zamaninda N
// isaretcili cagri kurmanin tek yolu her N icin ayri bir imza (libffi ya da
// el yazimi assembly olmadan; wasm'da ikisi de yok). Her durum bir switch
// kolu; 32 kol uc yolda ~toplam birkac KB kod. Oyun kancalari 0-10 arguman
// aliyor (motor carpisma kancasi + yuzey normali = 10), 32 genis pay.
// Tavanin ustu SESSIZ DEGIL: cagiran aot_runtime_error ile firlatir.
//
// Sicak yol: switch bir atlama tablosuna iner, her kol dogrudan satir ici
// dolayli cagri (sablon her N icin ayri acilir). <=8 arguman icin uretilen
// kod eski elle yazilmis kollarla ayni sekil.
#ifndef TULPAR_BOXED_CALL_HPP
#define TULPAR_BOXED_CALL_HPP

#include "vm.hpp"

#include <cstddef>
#include <utility>

// Dinamik cagrinin (call(), kapanis, async) iletebildigi en fazla kullanici
// parametresi. Gomen (tulpar-engine) de bunu okuyabilir.
#define TULPAR_CALL_MAX_ARGS 32

namespace tulpar_boxed {

template <std::size_t> using VPtr = VMValue *;

template <std::size_t... I>
inline void call_seq(void *fp, VMValue *r, VMValue *a,
                     std::index_sequence<I...>) {
  ((void (*)(VMValue *, VPtr<I>...))fp)(r, &a[I]...);
  (void)a; // N = 0: kullanilmiyor
}

template <std::size_t... I>
inline void call_env_seq(void *fp, VMValue *r, void *env, VMValue *a,
                         std::index_sequence<I...>) {
  ((void (*)(VMValue *, void *, VPtr<I>...))fp)(r, env, &a[I]...);
  (void)a;
}

} // namespace tulpar_boxed

// Kollar 0..TULPAR_CALL_MAX_ARGS. Tavani degistiren bu listeyi de uzatir;
// asagidaki static_assert ikisinin ayrismasini derleme hatasi yapar.
#define TULPAR_BOXED_CALL_CASES(X)                                             \
  X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13)   \
  X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25)     \
  X(26) X(27) X(28) X(29) X(30) X(31) X(32)

namespace tulpar_boxed {
#define TULPAR_BOXED_COUNT_(N) +1
static_assert(0 TULPAR_BOXED_CALL_CASES(TULPAR_BOXED_COUNT_) ==
                  TULPAR_CALL_MAX_ARGS + 1,
              "TULPAR_BOXED_CALL_CASES 0..TULPAR_CALL_MAX_ARGS olmali");
#undef TULPAR_BOXED_COUNT_
} // namespace tulpar_boxed

// `fp`'yi `n` isaretciyle cagir: `a[0..n)` gecerli olmali (n = 0'da a
// kullanilmaz). n tavanin ustundeyse ya da negatifse CAGIRMAZ ve false doner;
// tani cagiranin isi (baglamini o biliyor).
static inline bool tulpar_boxed_call(void *fp, VMValue *r, VMValue *a, int n) {
  switch (n) {
#define TULPAR_BOXED_CASE_(N)                                                  \
  case N:                                                                      \
    tulpar_boxed::call_seq(fp, r, a, std::make_index_sequence<N>{});           \
    return true;
    TULPAR_BOXED_CALL_CASES(TULPAR_BOXED_CASE_)
#undef TULPAR_BOXED_CASE_
  default:
    return false;
  }
}

// Kapanis bicimi: sonuc isaretcisinden sonra ortam (env) gelir.
static inline bool tulpar_boxed_call_env(void *fp, VMValue *r, void *env,
                                         VMValue *a, int n) {
  switch (n) {
#define TULPAR_BOXED_CASE_(N)                                                  \
  case N:                                                                      \
    tulpar_boxed::call_env_seq(fp, r, env, a, std::make_index_sequence<N>{});  \
    return true;
    TULPAR_BOXED_CALL_CASES(TULPAR_BOXED_CASE_)
#undef TULPAR_BOXED_CASE_
  default:
    return false;
  }
}

#endif // TULPAR_BOXED_CALL_HPP
