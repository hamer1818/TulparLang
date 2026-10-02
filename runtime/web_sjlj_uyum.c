/*
 * WEB SjLj UYUMU — LLVM <= 18 ile derlenmis `tulpar` surucusu icin
 * (2026-10-02). Yalniz wasm/build_tame_web.sh derler (web runtime arsivi);
 * yerel derlemeye girmez.
 *
 * try/catch modulde `setjmp`e iner; web hedefinde surucu LLVM'in
 * WebAssemblyLowerEmscriptenEHSjLj gecisini acar (llvm_backend.cpp
 * enable_web_sjlj_lowering). Gecisin URETTIGI ABI LLVM surumune bagli:
 *   - LLVM >= 19: __wasm_setjmp / __wasm_setjmp_test  (Emscripten 5.0 tasir)
 *   - LLVM <= 18: saveSetjmp / testSetjmp + getTempRet0 (Emscripten 5.0'da YOK)
 * Yayinlanan ikililer CI'da LLVM 18 ile derleniyor; bu dosya olmadan onlarin
 * web derlemesi try iceren her programda `undefined symbol: saveSetjmp` ile
 * linkte duserdi. Asagidaki, Emscripten'in 3.1.5x'e kadar compiler-rt'de
 * tasidigi uygulamanin aynisi (emscripten_setjmp.c).
 *
 * longjmp tarafi iki ABI'de ORTAK: runtime'daki aot_throw (em++, varsayilan
 * SUPPORT_LONGJMP=emscripten) `emscripten_longjmp(env, val)` cagirir ve o
 * yalniz setThrew(env, val) yapar; eski ABI'li kullanici kodu ardindan
 * testSetjmp(*env, ...) ile kimligi buradaki saveSetjmp'in `*env`e yazdigi
 * degerle karsilastirir.
 */
#ifdef __EMSCRIPTEN__
#include <stdint.h>
#include <stdlib.h>

/* compiler-rt/emscripten_tempret.s: "llvm gecislerinin urettigi cagrilar
 * icin" tutulan takma ad. */
extern void setTempRet0(uint32_t value);

typedef struct TulparSjljGirdi {
  uint32_t id;
  uint32_t label;
} TulparSjljGirdi;

static _Thread_local uint32_t tulpar_setjmp_id = 0;

TulparSjljGirdi *saveSetjmp(uint32_t *env, uint32_t label, TulparSjljGirdi *table,
                            uint32_t size) {
  uint32_t i = 0;
  tulpar_setjmp_id++;
  *env = tulpar_setjmp_id;
  while (i < size) {
    if (table[i].id == 0) {
      table[i].id = tulpar_setjmp_id;
      table[i].label = label;
      table[i + 1].id = 0; /* sonraki yuvayi hazirla */
      setTempRet0(size);
      return table;
    }
    i++;
  }
  /* tablo doldu: buyut (gecis tabloyu malloc ile ayirip free ile birakir) */
  size *= 2;
  table = (TulparSjljGirdi *)realloc(table, sizeof(TulparSjljGirdi) * (size + 1));
  table = saveSetjmp(env, label, table, size);
  setTempRet0(size);
  return table;
}

uint32_t testSetjmp(uint32_t id, TulparSjljGirdi *table, uint32_t size) {
  uint32_t i = 0;
  while (i < size) {
    uint32_t curr = table[i].id;
    if (curr == 0) break;
    if (curr == id) return table[i].label;
    i++;
  }
  return 0;
}
#endif
