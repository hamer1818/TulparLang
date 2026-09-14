#include "core/profiler/profiler.hpp"

#include <algorithm>
#include <atomic>

#include "core/jobs/job_system.hpp"
#include "platform/fatal.hpp"
#include "platform/time.hpp"

#if ENGINE_TRACY
// Tracy istemcisi (BSD-3, vendored): kendi profiler'imiz her zaman calisir,
// Tracy acikken ayni bolgeler/kareler ona da basilir (UI bedava, GPU bolgeleri
// cihaz gelince). Bolge adlari dinamik srcloc ile (___tracy_alloc_srcloc_name).
#include <cstring>
#include <tracy/TracyC.h>
#endif

namespace tulpar::engine {

Profiler *Profiler::instance_ = nullptr;

namespace {
std::atomic<uint64_t> g_zone_alloc{0}; // halka indeksi (atomik)
std::atomic<uint64_t> g_zone_dropped{0};
std::atomic<uint32_t> g_thread_ids{0};
thread_local uint32_t t_thread_id = UINT32_MAX;
thread_local uint32_t t_zone_stack[32];
thread_local uint32_t t_zone_depth = 0;
thread_local uint32_t t_tracy_ctx[32][4];

uint32_t thread_id() {
  if (t_thread_id == UINT32_MAX) t_thread_id = g_thread_ids.fetch_add(1);
  return t_thread_id;
}

// Acik bolge yigini: fiber uzerindeysek fiber'in, degilse thread'in.
struct ZoneStack {
  uint32_t *stack;
  uint32_t *depth;
  uint32_t (*tracy_ctx)[4];
};
ZoneStack zone_stack() {
  if (Fiber *f = JobSystem::current_fiber()) return ZoneStack{f->zone_stack, &f->zone_depth, f->tracy_ctx};
  return ZoneStack{t_zone_stack, &t_zone_depth, t_tracy_ctx};
}
#if ENGINE_TRACY
// Ad isaretcisi -> statik srcloc tablosu (256 benzersiz bolge adi; asilirsa
// son giris paylasilir ve sayac artar). Kilit yok: yayinlama atomik indeksle.
constexpr uint32_t kTracySrclocMax = 256;
___tracy_source_location_data g_tracy_srcloc[kTracySrclocMax];
std::atomic<uint32_t> g_tracy_srcloc_n{0};
std::atomic<uint32_t> g_tracy_srcloc_overflow{0};
const ___tracy_source_location_data *tracy_srcloc(const char *name) {
  const uint32_t n = g_tracy_srcloc_n.load(std::memory_order_acquire);
  for (uint32_t i = 0; i < n; i++)
    if (g_tracy_srcloc[i].name == name) return &g_tracy_srcloc[i];
  const uint32_t i = g_tracy_srcloc_n.fetch_add(1, std::memory_order_acq_rel);
  if (i >= kTracySrclocMax) {
    g_tracy_srcloc_overflow.fetch_add(1, std::memory_order_relaxed);
    g_tracy_srcloc_n.store(kTracySrclocMax, std::memory_order_release);
    return &g_tracy_srcloc[kTracySrclocMax - 1];
  }
  g_tracy_srcloc[i].name = name;
  g_tracy_srcloc[i].function = "";
  g_tracy_srcloc[i].file = "";
  g_tracy_srcloc[i].line = 0;
  g_tracy_srcloc[i].color = 0;
  return &g_tracy_srcloc[i];
}
#endif
} // namespace

bool Profiler::init(Arena &arena, const ProfilerConfig &cfg) {
  cfg_ = cfg;
  frames_ = arena.alloc_array_zeroed<ProfileFrame>(cfg.frame_capacity);
  zones_ = arena.alloc_array_zeroed<ProfileZone>(cfg.zone_capacity);
  arenas_ = arena.alloc_array_zeroed<const Arena *>(cfg.arena_capacity);
  if (!frames_ || !zones_ || !arenas_) return false;
  instance_ = this;
  return true;
}

void Profiler::begin_frame() {
  ENGINE_ASSERT(!in_frame_);
  in_frame_ = true;
  frame_zone_start_ = g_zone_alloc.load(std::memory_order_acquire);
  ProfileFrame &f = frames_[frames_total_ % cfg_.frame_capacity];
  f.t_begin = platform::now_ns();
  f.t_end = 0;
  f.zone_begin = (uint32_t)(frame_zone_start_ % cfg_.zone_capacity);
  f.zone_count = 0;
}

void Profiler::end_frame() {
  ENGINE_ASSERT(in_frame_);
  ProfileFrame &f = frames_[frames_total_ % cfg_.frame_capacity];
  f.t_end = platform::now_ns();
  uint64_t end = g_zone_alloc.load(std::memory_order_acquire);
  uint64_t n = end - frame_zone_start_;
  f.zone_count = (uint32_t)(n > cfg_.zone_capacity ? cfg_.zone_capacity : n);
  zones_total_ = end;
  zones_dropped_ = g_zone_dropped.load(std::memory_order_relaxed);
  frames_total_++;
  in_frame_ = false;
#if ENGINE_TRACY
  ___tracy_emit_frame_mark(nullptr);
#endif
}

void Profiler::zone_begin(const char *name) {
  ZoneStack zs = zone_stack();
  if (*zs.depth >= 32) {
    g_zone_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  uint64_t idx = g_zone_alloc.fetch_add(1, std::memory_order_acq_rel);
  ProfileZone &z = zones_[idx % cfg_.zone_capacity];
  z.name = name;
  z.t0 = platform::now_ns();
  z.t1 = 0;
  z.thread = thread_id();
  z.depth = *zs.depth;
#if ENGINE_TRACY
  {
    // Statik kaynak konumu (ad isaretcisi basina bir kez): dinamik srcloc
    // (___tracy_alloc_srcloc_name) tracy-csvexport istatistiginde GORUNMUYOR
    // (olculdu 2026-09-14: 4297 bolge yakalandi, disa aktarim bos) ve her
    // bolgede ayirma yapiyordu. Adlar dize sabitidir (ENGINE_ZONE("...")).
    const ___tracy_source_location_data *sl = tracy_srcloc(name);
    TracyCZoneCtx c = ___tracy_emit_zone_begin(sl, 1);
    static_assert(sizeof(TracyCZoneCtx) <= sizeof(zs.tracy_ctx[0]), "TracyCZoneCtx 16 bayti asti");
    std::memcpy(zs.tracy_ctx[*zs.depth], &c, sizeof c);
  }
#endif
  zs.stack[(*zs.depth)++] = (uint32_t)(idx % cfg_.zone_capacity);
}

void Profiler::zone_end() {
  ZoneStack zs = zone_stack();
  if (*zs.depth == 0) return; // dusurulmus bolgenin kapanisi
  uint32_t idx = zs.stack[--(*zs.depth)];
  zones_[idx].t1 = platform::now_ns();
#if ENGINE_TRACY
  {
    TracyCZoneCtx c;
    std::memcpy(&c, zs.tracy_ctx[*zs.depth], sizeof c);
    ___tracy_emit_zone_end(c);
  }
#endif
  // Fiber gocmusse kapanis thread'i farkli olabilir; bolge acildigi thread'de
  // kalir (zaman cizelgesinde tek satir), goc sayisi job istatistiginde.
}

void Profiler::watch_arena(const Arena *a) {
  ENGINE_ASSERT(arena_count_ < cfg_.arena_capacity);
  arenas_[arena_count_++] = a;
}

uint32_t Profiler::zone_count_last_frame() const {
  if (frames_total_ == 0) return 0;
  return frames_[(frames_total_ - 1) % cfg_.frame_capacity].zone_count;
}

FrameStats Profiler::stats_from(Span<const uint64_t> samples, Span<uint64_t> scratch) {
  FrameStats s;
  s.count = (uint32_t)samples.size();
  if (s.count == 0) return s;
  ENGINE_ASSERT(scratch.size() >= samples.size());
  uint64_t sum = 0;
  for (size_t i = 0; i < samples.size(); i++) {
    scratch[i] = samples[i];
    sum += samples[i];
  }
  std::sort(scratch.begin(), scratch.begin() + samples.size());
  s.mean_ns = sum / s.count;
  s.p50_ns = scratch[(s.count - 1) / 2];
  s.p99_ns = scratch[(size_t)((s.count - 1) * 99) / 100];
  s.max_ns = scratch[s.count - 1];
  return s;
}

FrameStats Profiler::frame_stats(Span<uint64_t> scratch, uint32_t count) const {
  uint64_t avail = frames_total_ < cfg_.frame_capacity ? frames_total_ : cfg_.frame_capacity;
  if (count == 0 || count > avail) count = (uint32_t)avail;
  if (count == 0) return FrameStats{};
  ENGINE_ASSERT(scratch.size() >= (size_t)count * 2);
  // Ilk yarisi ornek, ikinci yarisi sort alani.
  uint64_t *samples = scratch.data();
  for (uint32_t i = 0; i < count; i++) {
    const ProfileFrame &f = frames_[(frames_total_ - count + i) % cfg_.frame_capacity];
    samples[i] = f.t_end - f.t_begin;
  }
  return stats_from(Span<const uint64_t>(samples, count), scratch.sub(count, count));
}

void Profiler::dump_text(FILE *out, Span<uint64_t> scratch) const {
  FrameStats s = frame_stats(scratch);
  std::fprintf(out, "[profiler] kare=%llu  ort=%.3f ms  p50=%.3f ms  p99=%.3f ms  max=%.3f ms  bolge=%llu dusen=%llu\n",
               (unsigned long long)frames_total_, s.mean_ns / 1e6, s.p50_ns / 1e6,
               s.p99_ns / 1e6, s.max_ns / 1e6, (unsigned long long)zones_total_,
               (unsigned long long)zones_dropped_);
  for (uint32_t i = 0; i < arena_count_; i++) {
    const MemoryStats &m = arenas_[i]->stats();
    std::fprintf(out, "[arena] %-10s kullanim=%zu  tepe=%zu / %zu B  ayirma=%u  tasma=%u  reset=%u\n",
                 arenas_[i]->name(), m.used, m.peak, m.capacity, m.alloc_count,
                 m.overflow_count, m.reset_count);
  }
}

bool Profiler::write_trace_json(const char *path) const {
  FILE *f = std::fopen(path, "w");
  if (!f) return false;
  std::fputs("{\"traceEvents\":[", f);
  bool first = true;
  uint64_t avail = frames_total_ < cfg_.frame_capacity ? frames_total_ : cfg_.frame_capacity;
  uint64_t t_origin = avail ? frames_[(frames_total_ - avail) % cfg_.frame_capacity].t_begin : 0;
  for (uint64_t k = 0; k < avail; k++) {
    uint64_t fi = frames_total_ - avail + k;
    const ProfileFrame &fr = frames_[fi % cfg_.frame_capacity];
    std::fprintf(f, "%s{\"name\":\"frame\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":0}",
                 first ? "" : ",", (fr.t_begin - t_origin) / 1e3, (fr.t_end - fr.t_begin) / 1e3);
    first = false;
    for (uint32_t z = 0; z < fr.zone_count; z++) {
      const ProfileZone &zn = zones_[(fr.zone_begin + z) % cfg_.zone_capacity];
      if (!zn.name || zn.t1 < zn.t0) continue;
      std::fprintf(f, ",{\"name\":\"%s\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":%u}",
                   zn.name, (zn.t0 - t_origin) / 1e3, (zn.t1 - zn.t0) / 1e3, zn.thread + 1);
    }
  }
  std::fputs("]}\n", f);
  return std::fclose(f) == 0;
}

} // namespace tulpar::engine
