#include "sim/physics.hpp"

// Jolt: Jolt.h HER ZAMAN ilk (JPH makrolari). Jolt tipleri bu .cpp'nin disina cikmaz.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "platform/crash.hpp"
#include "platform/fatal.hpp"
#include "platform/thread.hpp"

namespace tulpar::engine::sim {

namespace {
// --- Jolt ayirici kancasi: sayilir (A2 olcumu) ------------------------------
std::atomic<uint64_t> g_allocs{0}, g_frees{0};
// Teshis: TULPAR_ENGINE_JOLT_ALLOC_TRACE=1 ile adim ici ayirmalarin boyutu basilir.
bool g_trace = false;
void *jph_alloc(size_t n) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (g_trace) {
    // Kim ayirdi? Cerceveler modul+ofset olarak; symbolize.py ile cozulur.
    void *pcs[12];
    int k = platform::crash_capture_frames(pcs, 12);
    std::fprintf(stderr, "[jolt-alloc] %zu B", n);
    for (int i = 1; i < k && i < 8; i++) {
      Dl_info info;
      if (dladdr(pcs[i], &info) && info.dli_fbase)
        std::fprintf(stderr, " +0x%llx", (unsigned long long)((uintptr_t)pcs[i] - (uintptr_t)info.dli_fbase));
    }
    std::fprintf(stderr, "\n");
  }
  return std::malloc(n);
}
void *jph_realloc(void *p, size_t, size_t n) { g_allocs.fetch_add(1, std::memory_order_relaxed); return std::realloc(p, n); }
void jph_free(void *p) { if (p) g_frees.fetch_add(1, std::memory_order_relaxed); std::free(p); }
void *jph_aligned_alloc(size_t n, size_t a) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  void *p = nullptr;
  if (posix_memalign(&p, a < sizeof(void *) ? sizeof(void *) : a, n) != 0) return nullptr;
  return p;
}
void jph_aligned_free(void *p) { if (p) g_frees.fetch_add(1, std::memory_order_relaxed); std::free(p); }

bool g_jolt_registered = false;
void jolt_global_init() {
  if (g_jolt_registered) return;
  JPH::Allocate = jph_alloc;
  JPH::Reallocate = jph_realloc;
  JPH::Free = jph_free;
  JPH::AlignedAllocate = jph_aligned_alloc;
  JPH::AlignedFree = jph_aligned_free;
  JPH::Factory::sInstance = new JPH::Factory();
  JPH::RegisterTypes();
  g_jolt_registered = true;
}

// --- Katmanlar: 2 nesne katmani (statik / hareketli), 2 genis faz katmani ----
namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
} // namespace Layers
namespace BPLayers {
constexpr JPH::BroadPhaseLayer NON_MOVING(0);
constexpr JPH::BroadPhaseLayer MOVING(1);
constexpr JPH::uint NUM = 2;
} // namespace BPLayers

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
  JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
    return l == Layers::NON_MOVING ? BPLayers::NON_MOVING : BPLayers::MOVING;
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
    return l == BPLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
  }
#endif
};
class ObjectVsBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer l, JPH::BroadPhaseLayer bp) const override {
    return l == Layers::MOVING || bp == BPLayers::MOVING; // statik-statik carpismaz
  }
};
class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a == Layers::MOVING || b == Layers::MOVING;
  }
};

// --- Jolt JobSystem -> fiber JobSystem uyarlayicisi -------------------------
// Dikkat: bu sinif JPH::JobSystem'den turedigi icin ciplak `JobSystem` adi
// JPH::JobSystem'e cozulur; bizimki tam nitelenir.
using FiberJobSystem = ::tulpar::engine::JobSystem;
class FiberJoltJobs final : public JPH::JobSystemWithBarrier {
public:
  FiberJoltJobs(FiberJobSystem *js, uint32_t max_jobs, uint32_t max_barriers) : js_(js) {
    JobSystemWithBarrier::Init(max_barriers);
    jobs_.Init(max_jobs, max_jobs); // sabit havuz; sayfa ayirmasi init'te
  }
  int GetMaxConcurrency() const override { return (int)js_->worker_count() + 1; }
  JobHandle CreateJob(const char *name, JPH::ColorArg color, const JobFunction &fn, JPH::uint32 deps) override {
    JPH::uint32 idx;
    for (;;) {
      idx = jobs_.ConstructObject(name, color, this, fn, deps);
      if (idx != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex) break;
      platform::thread_yield(); // havuz dolu: worker'lar bosaltir (kapasite init'te)
    }
    Job *job = &jobs_.Get(idx);
    JobHandle h(job);
    if (deps == 0) QueueJob(job);
    return h;
  }

protected:
  void QueueJob(Job *job) override {
    job->AddRef(); // kuyrukta yasadigi surece
    js_->run(::tulpar::engine::JobDecl{run_one, job, "jolt"}, nullptr);
  }
  void QueueJobs(Job **jobs, JPH::uint n) override {
    for (JPH::uint i = 0; i < n; i++) QueueJob(jobs[i]);
  }
  void FreeJob(Job *job) override { jobs_.DestructObject(job); }

private:
  static void run_one(void *p) {
    Job *job = static_cast<Job *>(p);
    job->Execute();
    job->Release();
  }
  FiberJobSystem *js_;
  JPH::FixedSizeFreeList<Job> jobs_;
};

inline JPH::Vec3 to_jph(Vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat to_jph(Quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline Vec3 from_jph(JPH::Vec3 v) { return Vec3{v.GetX(), v.GetY(), v.GetZ()}; }
inline Quat from_jph(JPH::Quat q) { return Quat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

uint64_t fnv1a(const void *p, size_t n, uint64_t h) {
  const uint8_t *b = static_cast<const uint8_t *>(p);
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
  return h;
}
} // namespace

struct Physics::Impl {
  PhysicsConfig cfg;
  BPLayerInterface bp_iface;
  ObjectVsBPFilter obj_bp_filter;
  ObjectPairFilter pair_filter;
  JPH::TempAllocatorImpl *temp = nullptr;
  JPH::JobSystem *jobs = nullptr; // thread havuzu YA DA fiber uyarlayicisi
  JPH::PhysicsSystem system;
  uint64_t allocs_before_step = 0;
  uint64_t allocs_last_step = 0;
};

bool Physics::init(Arena &arena, const PhysicsConfig &cfg) {
  jolt_global_init();
  void *mem = arena.alloc(sizeof(Impl), alignof(Impl) > 16 ? alignof(Impl) : 16);
  if (!mem) return false;
  impl_ = new (mem) Impl();
  impl_->cfg = cfg;
  impl_->temp = new JPH::TempAllocatorImpl(cfg.temp_bytes);
  if (cfg.jobs) {
    impl_->jobs = new FiberJoltJobs(cfg.jobs, cfg.max_jolt_jobs, JPH::cMaxPhysicsBarriers);
  } else {
    uint32_t threads = cfg.threads ? cfg.threads : (platform::cpu_count() > 1 ? platform::cpu_count() - 1 : 1);
    impl_->jobs = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, (int)threads);
  }
  impl_->system.Init(cfg.max_bodies, 0, cfg.max_body_pairs, cfg.max_contacts, impl_->bp_iface,
                     impl_->obj_bp_filter, impl_->pair_filter);
  impl_->system.SetGravity(to_jph(cfg.gravity));
  return true;
}

void Physics::shutdown() {
  if (!impl_) return;
  delete impl_->jobs;
  delete impl_->temp;
  impl_->~Impl(); // bellek arenada kalir
  impl_ = nullptr;
}

static BodyId add_body(Physics::Impl *impl, const JPH::ShapeRefC &shape, Vec3 pos, Quat rot, bool dynamic) {
  JPH::BodyCreationSettings s(shape, to_jph(pos), to_jph(rot),
                              dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                              dynamic ? Layers::MOVING : Layers::NON_MOVING);
  JPH::BodyID id = impl->system.GetBodyInterface().CreateAndAddBody(
      s, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
  if (id.IsInvalid()) return BodyId{};
  return BodyId{id.GetIndexAndSequenceNumber()};
}

BodyId Physics::add_box(Vec3 half, Vec3 pos, Quat rot, bool dynamic) {
  JPH::ShapeRefC shape = new JPH::BoxShape(to_jph(half));
  return add_body(impl_, shape, pos, rot, dynamic);
}

BodyId Physics::add_sphere(float radius, Vec3 pos, bool dynamic) {
  JPH::ShapeRefC shape = new JPH::SphereShape(radius);
  return add_body(impl_, shape, pos, Quat::identity(), dynamic);
}

void Physics::remove(BodyId id) {
  if (!id.valid()) return;
  JPH::BodyID b(id.v);
  JPH::BodyInterface &bi = impl_->system.GetBodyInterface();
  bi.RemoveBody(b);
  bi.DestroyBody(b);
}

void Physics::step(float dt, int collision_steps) {
  impl_->allocs_before_step = g_allocs.load(std::memory_order_relaxed);
  static bool trace_env = std::getenv("TULPAR_ENGINE_JOLT_ALLOC_TRACE") != nullptr;
  g_trace = trace_env;
  impl_->system.Update(dt, collision_steps, impl_->temp, impl_->jobs);
  g_trace = false;
  impl_->allocs_last_step = g_allocs.load(std::memory_order_relaxed) - impl_->allocs_before_step;
}

Vec3 Physics::position(BodyId id) const { return from_jph(impl_->system.GetBodyInterface().GetPosition(JPH::BodyID(id.v))); }
Quat Physics::rotation(BodyId id) const { return from_jph(impl_->system.GetBodyInterface().GetRotation(JPH::BodyID(id.v))); }
Vec3 Physics::linear_velocity(BodyId id) const { return from_jph(impl_->system.GetBodyInterface().GetLinearVelocity(JPH::BodyID(id.v))); }
void Physics::set_linear_velocity(BodyId id, Vec3 v) { impl_->system.GetBodyInterface().SetLinearVelocity(JPH::BodyID(id.v), to_jph(v)); }
bool Physics::is_active(BodyId id) const { return impl_->system.GetBodyInterface().IsActive(JPH::BodyID(id.v)); }

uint64_t Physics::state_hash() const {
  uint64_t h = 0xcbf29ce484222325ull;
  JPH::BodyIDVector ids;
  impl_->system.GetBodies(ids); // Jolt: govde kimlikleri (sirali, belirlenimli)
  for (const JPH::BodyID &id : ids) {
    JPH::RVec3 p = impl_->system.GetBodyInterface().GetPosition(id);
    JPH::Quat q = impl_->system.GetBodyInterface().GetRotation(id);
    float v[7] = {p.GetX(), p.GetY(), p.GetZ(), q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
    h = fnv1a(v, sizeof v, h);
  }
  return h;
}

PhysicsStats Physics::stats() const {
  PhysicsStats s;
  s.bodies = impl_ ? impl_->system.GetNumBodies() : 0;
  s.allocs_total = g_allocs.load(std::memory_order_relaxed);
  s.frees_total = g_frees.load(std::memory_order_relaxed);
  s.allocs_last_step = impl_ ? impl_->allocs_last_step : 0;
  return s;
}

} // namespace tulpar::engine::sim
