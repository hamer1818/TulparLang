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
#include <Jolt/Geometry/Plane.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
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

// Karakter slotu. `Ref<>` kullaniliyor: CharacterVirtual bir RefTarget'tir,
// slot temizlenince (nullptr atanarak) sayac duser ve nesne yok edilir.
struct CharacterSlot {
  JPH::Ref<JPH::CharacterVirtual> ch;
  Vec3 desired{0, 0, 0};
  float jump_speed = 4.0f;
  float step_up = 0.4f;
  bool jump = false;
  bool alive = false;
};

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
  CharacterSlot *chars = nullptr;
  uint32_t char_cap = 0;
};

namespace {
// Karakterleri ilerlet. Fizik adiminin ARDINDAN, SLOT SIRASINDA cagrilir --
// sira sabit oldugu icin sonuc belirlenimli. `Physics::step()` bunu kendisi
// yapar; cagiranin sirayi yanlis kurma sansi YOKTUR.
void update_characters(Physics::Impl *im, float dt) {
  if (!im->chars || dt <= 0.0f) return;
  const JPH::Vec3 gravity = im->system.GetGravity();

  for (uint32_t i = 0; i < im->char_cap; i++) {
    CharacterSlot &s = im->chars[i];
    if (!s.alive || s.ch == nullptr) continue;
    JPH::CharacterVirtual *c = s.ch;

    const JPH::Vec3 up = c->GetUp();
    const bool grounded = c->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

    // Dikey hiz MOTORUN, yatay hiz OYUNCUNUN. Ikisi ayri tutulmazsa ya
    // yercekimi girdi tarafindan silinir ya da oyuncu havada yukari yurur.
    JPH::Vec3 v = c->GetLinearVelocity();
    float vy = v.Dot(up);
    if (grounded) {
      // Yamacta yavasca kaymayi onlemek icin asagi yonlu birikimi sifirla.
      if (vy < 0.0f) vy = 0.0f;
      if (s.jump) vy = s.jump_speed;
    } else {
      vy += gravity.Dot(up) * dt;
    }
    s.jump = false; // KENAR-TETIKLI: basili tutmak zincirleme ziplatmaz

    JPH::Vec3 horiz = to_jph(s.desired);
    horiz -= up * horiz.Dot(up); // istegin dikey bileseni YOK SAYILIR
    c->SetLinearVelocity(horiz + up * vy);

    // ExtendedUpdate (duz Update degil): merdiven cikma + zemine yapisma
    // burada. Bunlar olmadan karakter kucuk basamaklara takilir ve rampadan
    // inerken havada sekerek iner -- "oyun karakteri" hissini veren fark budur.
    JPH::CharacterVirtual::ExtendedUpdateSettings us;
    us.mWalkStairsStepUp = up * s.step_up;
    c->ExtendedUpdate(dt, gravity, us,
                      im->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                      im->system.GetDefaultLayerFilter(Layers::MOVING),
                      {}, {}, *im->temp);
  }
}
} // namespace

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

  // Karakter slotlari: TUM bellek burada, adim icinde tahsis YOK (A2).
  if (cfg.max_characters > 0) {
    impl_->chars = arena.alloc_array<CharacterSlot>(cfg.max_characters);
    if (!impl_->chars) return false;
    for (uint32_t i = 0; i < cfg.max_characters; i++) new (&impl_->chars[i]) CharacterSlot();
    impl_->char_cap = cfg.max_characters;
  }
  return true;
}

void Physics::shutdown() {
  if (!impl_) return;
  // Karakterler Jolt nesneleridir: jobs/temp YOK EDILMEDEN once birakilmali.
  for (uint32_t i = 0; i < impl_->char_cap; i++) impl_->chars[i].~CharacterSlot();
  impl_->char_cap = 0;
  impl_->chars = nullptr;
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
  update_characters(impl_, dt);
  impl_->allocs_last_step = g_allocs.load(std::memory_order_relaxed) - impl_->allocs_before_step;
}

Physics::RaycastResult Physics::raycast(Vec3 origin, Vec3 direction, float max_distance) const {
  RaycastResult result;
  if (!impl_) return result;

  JPH::RVec3 origin_jph = to_jph(origin);
  JPH::Vec3 dir_jph = to_jph(direction) * max_distance;

  JPH::RRayCast ray{origin_jph, dir_jph};
  JPH::RayCastResult hit;

  // Varsayilan filtreler (her seyle carpisir)
  JPH::BroadPhaseLayerFilter bp_filter;
  JPH::ObjectLayerFilter obj_filter;
  JPH::BodyFilter body_filter;

  if (impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, bp_filter, obj_filter, body_filter)) {
    result.hit = true;
    result.body_id = BodyId{hit.mBodyID.GetIndexAndSequenceNumber()};
    result.fraction = hit.mFraction;
    
    JPH::RVec3 hit_pos = ray.GetPointOnRay(hit.mFraction);
    result.point = from_jph(hit_pos);

    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
      const JPH::Body &body = lock.GetBody();
      JPH::Vec3 normal = body.GetShape()->GetSurfaceNormal(hit.mSubShapeID2, hit_pos - body.GetPosition(), body.GetRotation());
      result.normal = from_jph(normal);
    } else {
      result.normal = Vec3{0, 1, 0};
    }
  }
  return result;
}

uint32_t Physics::overlap_box(Vec3 center, Vec3 half_extent, Quat rot, OverlapResult* results, uint32_t max_results) const {
  if (!impl_ || max_results == 0 || !results) return 0;
  
  JPH::BoxShape shape(to_jph(half_extent));
  JPH::CollideShapeSettings settings;
  settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
  settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

  JPH::BroadPhaseLayerFilter bp_filter;
  JPH::ObjectLayerFilter obj_filter;
  JPH::BodyFilter body_filter;

  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  impl_->system.GetNarrowPhaseQuery().CollideShape(
      &shape, JPH::Vec3::sReplicate(1.0f), JPH::RMatrix44::sRotationTranslation(to_jph(rot), to_jph(center)), 
      settings, to_jph(center), collector, bp_filter, obj_filter, body_filter);

  uint32_t count = 0;
  for (const JPH::CollideShapeResult& hit : collector.mHits) {
    if (count >= max_results) break;
    results[count].hit = true;
    results[count].body_id = BodyId{hit.mBodyID2.GetIndexAndSequenceNumber()};
    count++;
  }
  return count;
}

uint32_t Physics::overlap_sphere(Vec3 center, float radius, OverlapResult* results, uint32_t max_results) const {
  if (!impl_ || max_results == 0 || !results) return 0;
  
  JPH::SphereShape shape(radius);
  JPH::CollideShapeSettings settings;
  settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
  settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

  JPH::BroadPhaseLayerFilter bp_filter;
  JPH::ObjectLayerFilter obj_filter;
  JPH::BodyFilter body_filter;

  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  impl_->system.GetNarrowPhaseQuery().CollideShape(
      &shape, JPH::Vec3::sReplicate(1.0f), JPH::RMatrix44::sTranslation(to_jph(center)), 
      settings, to_jph(center), collector, bp_filter, obj_filter, body_filter);

  uint32_t count = 0;
  for (const JPH::CollideShapeResult& hit : collector.mHits) {
    if (count >= max_results) break;
    results[count].hit = true;
    results[count].body_id = BodyId{hit.mBodyID2.GetIndexAndSequenceNumber()};
    count++;
  }
  return count;
}

Vec3 Physics::position(BodyId id) const { return from_jph(impl_->system.GetBodyInterface().GetPosition(JPH::BodyID(id.v))); }
Quat Physics::rotation(BodyId id) const { return from_jph(impl_->system.GetBodyInterface().GetRotation(JPH::BodyID(id.v))); }
Vec3 Physics::linear_velocity(BodyId id) const { return from_jph(impl_->system.GetBodyInterface().GetLinearVelocity(JPH::BodyID(id.v))); }
void Physics::set_linear_velocity(BodyId id, Vec3 v) { impl_->system.GetBodyInterface().SetLinearVelocity(JPH::BodyID(id.v), to_jph(v)); }
bool Physics::is_active(BodyId id) const { return impl_->system.GetBodyInterface().IsActive(JPH::BodyID(id.v)); }


// --- Karakter -----------------------------------------------------------

namespace {
CharacterSlot *char_slot(Physics::Impl *im, CharacterId id) {
  if (!im || !im->chars || !id.valid() || id.v >= im->char_cap) return nullptr;
  CharacterSlot *s = &im->chars[id.v];
  return (s->alive && s->ch != nullptr) ? s : nullptr;
}
} // namespace

CharacterId Physics::add_character(const CharacterConfig &cfg) {
  if (!impl_ || !impl_->chars) return CharacterId{};
  // Jolt'un CapsuleShape'i yarim-silindir > 0 ve yaricap > 0 diye ASSERT eder.
  // Gecersiz yapilandirmayi ASSERT'e dusurmek yerine burada REDDEDIYORUZ.
  const float half_cyl = cfg.height * 0.5f - cfg.radius;
  if (cfg.radius <= 0.0f || half_cyl <= 0.0f) return CharacterId{};

  uint32_t idx = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < impl_->char_cap; i++)
    if (!impl_->chars[i].alive) { idx = i; break; }
  if (idx == 0xFFFFFFFFu) return CharacterId{}; // havuz dolu

  // Jolt'un sozlesmesi: sekil oyle kurulmali ki TABANI (0,0,0)'da olsun.
  // CapsuleShape merkezlidir; bu yuzden yarim-silindir + yaricap kadar
  // YUKARI otelenir. Bu yapilmazsa karakter zemine YARI YARIYA gomulu
  // dogar ve konumu ayagini degil govde merkezini gosterir.
  JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(half_cyl, cfg.radius);
  JPH::RefConst<JPH::Shape> shape = new JPH::RotatedTranslatedShape(
      JPH::Vec3(0.0f, half_cyl + cfg.radius, 0.0f), JPH::Quat::sIdentity(), capsule);

  JPH::CharacterVirtualSettings cs;
  cs.mShape = shape;
  cs.mMass = cfg.mass;
  cs.mMaxSlopeAngle = JPH::DegreesToRadians(cfg.max_slope_deg);
  // Taban duzlemi: kapsulun yaricapi kadar asagisi hala "destek" sayilir.
  // Varsayilan (-1e10) HER temasi destek sayardi -- duvara surtunen
  // karakter havada ziplayabilirdi.
  cs.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -cfg.radius);

  CharacterSlot &slot = impl_->chars[idx];
  slot.ch = new JPH::CharacterVirtual(&cs, to_jph(cfg.position), JPH::Quat::sIdentity(), &impl_->system);
  slot.desired = Vec3{0, 0, 0};
  slot.jump = false;
  slot.jump_speed = cfg.jump_speed;
  slot.step_up = cfg.step_up;
  slot.alive = true;
  return CharacterId{idx};
}

void Physics::remove_character(CharacterId id) {
  CharacterSlot *s = char_slot(impl_, id);
  if (!s) return;
  s->ch = nullptr; // Ref sayaci duser -> nesne yok edilir
  s->alive = false;
}

void Physics::set_character_input(CharacterId id, Vec3 desired_horizontal_velocity, bool jump) {
  CharacterSlot *s = char_slot(impl_, id);
  if (!s) return;
  s->desired = desired_horizontal_velocity;
  // `jump` BIRIKIR: girdi step()ten once birden fazla kez yazilsa bile
  // istek kaybolmaz; step() onu tuketip sifirlar.
  if (jump) s->jump = true;
}

Vec3 Physics::character_position(CharacterId id) const {
  const CharacterSlot *s = char_slot(impl_, id);
  if (!s) return Vec3{0, 0, 0};
  const JPH::RVec3 p = s->ch->GetPosition();
  return Vec3{(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()};
}

Vec3 Physics::character_velocity(CharacterId id) const {
  const CharacterSlot *s = char_slot(impl_, id);
  return s ? from_jph(s->ch->GetLinearVelocity()) : Vec3{0, 0, 0};
}

GroundState Physics::character_ground_state(CharacterId id) const {
  const CharacterSlot *s = char_slot(impl_, id);
  if (!s) return GroundState::InAir;
  switch (s->ch->GetGroundState()) {
    case JPH::CharacterBase::EGroundState::OnGround: return GroundState::OnGround;
    case JPH::CharacterBase::EGroundState::OnSteepGround: return GroundState::OnSteepGround;
    case JPH::CharacterBase::EGroundState::NotSupported: return GroundState::NotSupported;
    default: return GroundState::InAir;
  }
}

bool Physics::character_grounded(CharacterId id) const {
  return character_ground_state(id) == GroundState::OnGround;
}

Vec3 Physics::character_ground_normal(CharacterId id) const {
  const CharacterSlot *s = char_slot(impl_, id);
  return s ? from_jph(s->ch->GetGroundNormal()) : Vec3{0, 1, 0};
}

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
  // Karakterler rijit govde DEGIL, bu yuzden GetBodies() onlari DONDURMEZ.
  // Ozeti burada kapatmazsak desync kapisi (sim/desync.cpp) oyuncunun
  // kendisindeki sapmayi KACIRIRDI. Sira slot sirasidir: belirlenimli.
  for (uint32_t i = 0; i < impl_->char_cap; i++) {
    const CharacterSlot &cs = impl_->chars[i];
    if (!cs.alive || cs.ch == nullptr) continue;
    const JPH::RVec3 p = cs.ch->GetPosition();
    const JPH::Vec3 lv = cs.ch->GetLinearVelocity();
    float v[6] = {(float)p.GetX(), (float)p.GetY(), (float)p.GetZ(),
                  lv.GetX(), lv.GetY(), lv.GetZ()};
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
