// L1 CORE — Statik is bagimlilik grafigi (500 madde listesi #181 "Job
// Dependency Graph"). core/jobs/job_system.hpp'nin KENDI, ZATEN VAR OLAN
// senkronizasyon ilkeleri (Counter/run/wait) UZERINE kurulu -- YENI bir
// kilit/atomik ICERMEZ, JobSystem'in internal'larina DOKUNMAZ.
//
// Mekanizma: her dugum bir "trambolin" is olarak sarilir -- once TUM
// oncullerinin (predecessor) Counter'larini bekler (js.wait(), fiber
// PARK EDER, thread BLOKLAMAZ -- bu, motorun Jolt fizik job'larinda ZATEN
// kullandigi, kanitlanmis desendir, YENI bir kalip DEGIL), sonra gercek is
// fonksiyonunu calistirir. JobSystem::run()'in KENDISI is bitince Counter'i
// otomatik indirir (job_system.cpp: execute_job -> signal_counter), bu
// yuzden JobGraph tarafinda AYRICA "tamamlandi" sinyali GEREKMEZ.
//
// DONGU KORUMASI (yapisal, calisma-zamani kontrolu DEGIL): bir dugum
// SADECE kendisinden ONCE eklenmis (daha KUCUK indeksli) dugumlere bagimli
// olabilir (add_node() bunu DOGRULAR) -- bir dugum HICBIR ZAMAN kendinden
// SONRA eklenecek bir dugume bagimli olamayacagi icin dongu YAPISAL
// OLARAK IMKANSIZDIR (ayrica calisma-zamaninda dongu ARAMAYA gerek yok).
//
// **ONEMLI DURUM NOTU:** Bu sinif GERCEK is parcaciklariyla (worker thread)
// TEST EDILMEDI -- bu gelistirme makinesinde C++ derleyicisi YOK. Tasarim
// job_system.hpp'nin BELGELENMIS garantilerine (wait() fiber'i park eder;
// Counter thread-safe; is icinden wait() cagirmak zaten Jolt job'larinda
// kullanilan desen) dayanir, ama gercek coklu-thread yurutmede DOGRULANMIS
// DEGILDIR -- bu oturumdaki SAF matematik modullerinden (rollback,
// lag_compensation, vb.) FARKLI bir risk sinifi: onlar HAND-TRACE ile tam
// kanitlandi, bu ise sadece TASARIM incelemesiyle savunulabilir. Ilk
// gercek derlemede/calistirmada ozenle test edilmeli.
#pragma once
#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine {

// KULLANIM KISITI: submit() cagrildiktan SONRA, TUM dugumler tamamlanana
// kadar (done Counter'i js.wait() ile beklenene kadar) bu nesne SABIT bir
// bellek adresinde YASAMAYA DEVAM ETMELI -- tasinamaz/kopyalanamaz/
// yikilmamalidir (arka planda calisan trambolin isleri `this`e ISARETCI
// TUTAR). Yigin degiskeni olarak kullanilacaksa, cevreleyen fonksiyon
// submit() SONRASI js.wait(done) ile TAMAMEN BITMEDEN geri DONMEMELIDIR.
class JobGraph {
 public:
  static constexpr uint32_t kMaxNodes = 64;
  static constexpr uint32_t kMaxDeps = 8; // dugum basina en fazla oncul sayisi

  // TUM ic veri (Node/Trampoline dizileri) SABIT boyutlu sinif-ici diziler
  // -- ayirma yok (A2), init() GEREKMEZ, sadece sifirlama yapar.
  void reset();

  // fn/data: gercek is. deps: bu dugumden ONCE TAMAMLANMASI gereken, ONCEDEN
  // add_node() ile eklenmis dugumlerin indeksleri (add_node()'un DONDURDUGU
  // degerler). Bir dep, bu cagridaki `node_count_`den KUCUK olmak ZORUNDADIR
  // (buyuk/esit ise UINT32_MAX doner, dugum EKLENMEZ -- dongu/ileri-referans
  // yapisal olarak REDDEDILIR). dep_count > kMaxDeps ise de UINT32_MAX doner.
  uint32_t add_node(JobFn fn, void *data, const uint32_t *deps = nullptr, uint32_t dep_count = 0,
                     const char *name = nullptr);

  uint32_t node_count() const { return node_count_; }

  // TUM dugumleri JobSystem'e gonderir (dogru sirada GONDERMEK ONEMLI DEGIL
  // -- her dugum KENDI bagimliliklarini js.wait() ile bekler, kuyruk sirasi
  // sonucu ETKILEMEZ, sadece hangi worker'in bosta bekleyip
  // yardim edecegini etkiler). `done` verilirse, TUM grafik tamamlaninca
  // (JobSystem tarafindan otomatik) sifira inecek sekilde +1 artirilir --
  // cagiran `js.wait(*done)` ile TUM grafigin bitmesini bekleyebilir.
  void submit(JobSystem &js, Counter *done = nullptr);

 private:
  struct Node {
    JobFn fn = nullptr;
    void *data = nullptr;
    const char *name = nullptr;
    uint32_t deps[kMaxDeps] = {};
    uint32_t dep_count = 0;
    Counter counter; // bu dugum bitince JobSystem TARAFINDAN sifira indirilir
  };
  struct Trampoline {
    JobGraph *graph = nullptr;
    JobSystem *js = nullptr;
    uint32_t node_index = 0;
  };
  static void trampoline_entry(void *arg);
  static void all_done_entry(void *arg);

  Node nodes_[kMaxNodes];
  Trampoline trampolines_[kMaxNodes];
  Trampoline all_done_tramp_{};
  uint32_t node_count_ = 0;
};

} // namespace tulpar::engine
