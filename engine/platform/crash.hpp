// L0 PLATFORM — crash reporter (plan Faz 0, EK C.1.4). Native motorda sahadan
// gelen cokme yigin izi olmadan cozulmez; sonradan eklemek aci.
//
// Sinyal isleyici (SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT) ALTERNATIF yiginda
// kosar (yigin tasmasi ve fiber bekci sayfasi da raporlanabilsin), raporu
// yalniz async-signal-safe cagrilarla yazar: build id, sinyal, hata adresi,
// thread adi, cerceveler "modul+ofset" olarak. Sembol cozumu CIHAZDA DEGIL,
// gelistirici makinesinde: engine/tools/symbolize.py + stripsiz ikili
// (sembol sunucusu = build id'ye gore saklanan stripsiz ikililer).
// Rapor yazildiktan sonra sinyal varsayilan isleyiciye birakilir (core dump
// / Android debuggerd yine calisir).
#pragma once
#include <cstddef>

namespace tulpar::engine::platform {

struct CrashConfig {
  const char *report_dir = ".";     // rapor dosyasi: <dir>/crash_<pid>_<n>.txt
  const char *build_id = "dev";     // derleme kimligi (git hash vb.)
  size_t alt_stack_bytes = 64 * 1024;
};

// Isleyicileri kurar. Bir kez cagrilir; tekrar cagri no-op (true).
bool crash_reporter_install(const CrashConfig &cfg);
// Kurulu mu?
bool crash_reporter_installed();
// Su anki cagri yiginini rapor bicimiyle `out`a yazar (test/teshis).
// Donus: cerceve sayisi.
int crash_capture_frames(void **pcs, int max);

} // namespace tulpar::engine::platform
