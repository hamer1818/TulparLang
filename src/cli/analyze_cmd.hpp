// `tulpar analyze <file.tpr>` (K157) — statik rapor, derleme/link yok:
//   1. ayirma: dosyanin her ust duzey fonksiyonu icin `@no_alloc`
//      kuraliyla (typeinfer'in gecisli, beyaz listeli denetimi) "ayirmasiz"
//      ya da "ayiriyor — neden (satir)";
//   2. hizli yol: TULPAR_PERF_HINTS ipuclari (kanitli dizi erisimi
//      kurulamayan donguler, K167).
// Cikis: 0 temiz, 1 tip sorunu (@no_alloc ihlali dahil), 2 kullanim/G-C/
// ayristirma hatasi.

#ifndef TULPAR_ANALYZE_CMD_H
#define TULPAR_ANALYZE_CMD_H

namespace tulpar {

int analyze_cmd_main(int argc, char **argv);

} // namespace tulpar

#endif
