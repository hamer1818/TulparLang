// L0 PLATFORM — ADPF Performance Hint API (420 madde listesi #176/#353:
// "Performance Hint API"). `platform/thermal.hpp`'nin ADPF kardesi -- ayni
// gerekce: gercek API (<android/performance_hint.h>) API 33 ister, bu
// projenin build hedefi android-26 (tools/android_run.sh) -- dogrudan
// linklemek eski cihazlarda dinamik baglayici hatasiyla coker. AYNI
// dlopen+dlsym deseni (rhi/vk_api.cpp, platform/thermal.cpp) kullanilir.
//
// Amac: bir "is parcasi" (ornek: bir render karesi) icin isletim sistemi
// zamanlayicisina hedef sure + gercek sure bildirmek -- zamanlayici
// gelecekteki calismalar icin CPU frekans/cekirdek atamasini buna gore
// onceden ayarlar. Bu dosya yalniz GUVENLI arayuzu saglar; gercek kare
// dongusune baglama (thread id'leri toplama, her kare report_actual
// cagirma) ayri bir entegrasyon isi (Faz C sonrasi).
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

// Opak tutamac: APerformanceHintSession* sarar. impl==nullptr => gecersiz
// (API yok/oturum acilamadi) -- her fonksiyon bunu GUVENLE yok sayar.
struct PerfHintSession {
  void *impl = nullptr;
  bool valid() const { return impl != nullptr; }
};

bool perf_hint_available();

// thread_ids: bu is parcasini calistiran thread'lerin OS tid'leri (Linux/
// Android'de gettid()). initial_target_ns: ilk hedef sure (nanosaniye).
// API yoksa/basarisizsa gecersiz (impl==nullptr) bir oturum doner.
PerfHintSession perf_hint_create_session(const int32_t *thread_ids, uint64_t count, int64_t initial_target_ns);
void perf_hint_update_target(PerfHintSession session, int64_t target_ns);
void perf_hint_report_actual(PerfHintSession session, int64_t actual_ns);
void perf_hint_close_session(PerfHintSession session);

} // namespace tulpar::engine::platform
