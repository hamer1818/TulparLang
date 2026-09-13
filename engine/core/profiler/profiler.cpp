#include "core/profiler/profiler.hpp"

#include <algorithm>
#include <atomic>

#include "core/jobs/job_system.hpp"
#include "platform/fatal.hpp"
#include "platform/time.hpp"

namespace tulpar::engine {

Profiler *Profiler::instance_ = nullptr;

namespace {
std::atomic<uint64_t> g_zone_alloc{0}; // halka indeksi (atomik)
std::atomic<uint64_t> g_zone_dropped{0};
std::atomic<uint32_t> g_thread_ids{0};
thread_local uint32_t t_thread_id = UINT32_MAX;
thread_local uint32_t t_zone_stack[32];
thread_local uint32_t t_zone_depth = 0;

uint32_t thread_id() {
  if (t_thread_id == UINT32_MAX) t_thread_id = g_thread_ids.fetch_add(1);
  return t_thread_id;
}

// Acik bolge yigini: fiber uzerindeysek fiber'in, degilse thread'in.
struct ZoneStack {
  uint32_t *stack;
  uint32_t *depth;
};
ZoneStack zone_stack() {
  if (Fiber *f = JobSystem::current_fiber()) return ZoneStack{f->zone_stack, &f->zone_depth};
  return ZoneStack{t_zone_stack, &t_zone_depth};
}
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
  zs.stack[(*zs.depth)++] = (uint32_t)(idx % cfg_.zone_capacity);
}

void Profiler::zone_end() {
  ZoneStack zs = zone_stack();
  if (*zs.depth == 0) return; // dusurulmus bolgenin kapanisi
  uint32_t idx = zs.stack[--(*zs.depth)];
  zones_[idx].t1 = platform::now_ns();
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
