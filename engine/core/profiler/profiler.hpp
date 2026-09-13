// L1 CORE — Profiler (Faz 0'da yazilir, sonra degil: "olcemedigin seyi
// optimize edemezsin"). Kare zaman cizelgesi + bolgeler + arena gorunumu.
//
//   * Kapasiteler init'te arenadan: kare halkasi, bolge halkasi. Kare icinde
//     ayirma yok; bolge halkasi dolarsa bolge DUSURULUR ve sayilir.
//   * Bolge yigini (acik bolgeler) fiber'daysa fiber'da, degilse thread'de
//     durur — fiber baska thread'de devam edince bolge kapanisi dogru yere
//     gider.
//   * Istatistik A7: p50/p99/max, ortalama sadece bilgi.
//   * Cikti: metin (CI) + Chrome trace JSON (chrome://tracing, Perfetto).
#pragma once
#include <cstdint>
#include <cstdio>

#include "core/containers/span.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine {

struct ProfileZone {
  const char *name;
  uint64_t t0;
  uint64_t t1;
  uint32_t thread;
  uint32_t depth;
};

struct ProfileFrame {
  uint64_t t_begin;
  uint64_t t_end;
  uint32_t zone_begin; // bolge halkasinda indeks
  uint32_t zone_count;
};

struct FrameStats {
  uint32_t count = 0;
  uint64_t mean_ns = 0;
  uint64_t p50_ns = 0;
  uint64_t p99_ns = 0;
  uint64_t max_ns = 0;
};

struct ProfilerConfig {
  uint32_t frame_capacity = 600;   // halka: 10 s @ 60 fps
  uint32_t zone_capacity = 65536;  // halka
  uint32_t arena_capacity = 16;    // izlenen arena sayisi
};

class Profiler {
public:
  bool init(Arena &arena, const ProfilerConfig &cfg);

  void begin_frame();
  void end_frame();

  void zone_begin(const char *name);
  void zone_end();

  void watch_arena(const Arena *a);

  // Son `count` karenin (0 = hepsi) istatistigi. `scratch` en az count uint64.
  FrameStats frame_stats(Span<uint64_t> scratch, uint32_t count = 0) const;
  // Saf: verilen orneklerden hesaplar (test edilebilir; scratch sort icin).
  static FrameStats stats_from(Span<const uint64_t> samples, Span<uint64_t> scratch);

  uint64_t frames_recorded() const { return frames_total_; }
  uint64_t zones_dropped() const { return zones_dropped_; }
  uint32_t zone_count_last_frame() const;

  void dump_text(FILE *out, Span<uint64_t> scratch) const;
  // Chrome trace event format. Basari: true.
  bool write_trace_json(const char *path) const;

  static Profiler *instance() { return instance_; }

private:
  static Profiler *instance_;
  ProfilerConfig cfg_{};
  ProfileFrame *frames_ = nullptr;
  ProfileZone *zones_ = nullptr;
  const Arena **arenas_ = nullptr;
  uint32_t arena_count_ = 0;
  uint64_t frames_total_ = 0;
  uint64_t zones_total_ = 0;   // atomik olarak alinan bolge sayisi (halka indeksi)
  uint64_t zones_dropped_ = 0;
  uint64_t frame_zone_start_ = 0;
  bool in_frame_ = false;
};

// RAII bolge.
struct ScopedZone {
  explicit ScopedZone(const char *name) {
    if (Profiler::instance()) Profiler::instance()->zone_begin(name);
  }
  ~ScopedZone() {
    if (Profiler::instance()) Profiler::instance()->zone_end();
  }
  ScopedZone(const ScopedZone &) = delete;
  ScopedZone &operator=(const ScopedZone &) = delete;
};

#define ENGINE_ZONE_CONCAT2(a, b) a##b
#define ENGINE_ZONE_CONCAT(a, b) ENGINE_ZONE_CONCAT2(a, b)
#define ENGINE_ZONE(name) ::tulpar::engine::ScopedZone ENGINE_ZONE_CONCAT(_engine_zone_, __LINE__)(name)

} // namespace tulpar::engine
