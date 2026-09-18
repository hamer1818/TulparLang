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
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
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

#if defined(_WIN32)
#include <malloc.h>   // _aligned_malloc / _aligned_free
#else
#include <dlfcn.h>
#endif

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
#if defined(_WIN32)
      // Windows'ta dladdr yok; ham adres yaziliyor. Modul+ofsete cevirmek icin
      // crash raporundaki modul tabanlari kullanilabilir (platform/crash.cpp).
      std::fprintf(stderr, " 0x%llx", (unsigned long long)(uintptr_t)pcs[i]);
#else
      Dl_info info;
      if (dladdr(pcs[i], &info) && info.dli_fbase)
        std::fprintf(stderr, " +0x%llx", (unsigned long long)((uintptr_t)pcs[i] - (uintptr_t)info.dli_fbase));
#endif
    }
    std::fprintf(stderr, "\n");
  }
  return std::malloc(n);
}
void *jph_realloc(void *p, size_t, size_t n) { g_allocs.fetch_add(1, std::memory_order_relaxed); return std::realloc(p, n); }
void jph_free(void *p) { if (p) g_frees.fetch_add(1, std::memory_order_relaxed); std::free(p); }
void *jph_aligned_alloc(size_t n, size_t a) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  const size_t align = a < sizeof(void *) ? sizeof(void *) : a;
#if defined(_WIN32)
  // Windows'ta hizali bellek AYRI bir cifttir: _aligned_malloc ile alinan
  // blok free() ile birakilamaz (heap bozulur) — jph_aligned_free de
  // _aligned_free cagiriyor. Jolt zaten hizali ayirma/birakma icin ayri
  // kancalar tuttugu icin bu eslesme dogru kurulabiliyor.
  return _aligned_malloc(n, align);
#else
  void *p = nullptr;
  if (posix_memalign(&p, align, n) != 0) return nullptr;
  return p;
#endif
}
void jph_aligned_free(void *p) {
  if (p) g_frees.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
  _aligned_free(p);   // _aligned_malloc'un ESI; free() ile birakmak bozar
#else
  std::free(p);
#endif
}

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

  // NOBETCI: bu nesne yok edilirken kuyrukta ya da calismakta olan Jolt isi
  // KALMAMALI. `jobs_` havuzu bizimle birlikte gider ve kuyrukta duran her
  // girdi CIPLAK bir `Job*`; sahibi olmeden calisirsa cop isaretci cagirilir.
  //
  // Bu tam olarak CI macOS/arm64'te olculen cokmenin sinifi (2026-09-16):
  // `thread: tulpar-job`, SIGSEGV, fault_addr 0x8bc94512aa864210 — null degil,
  // COP. Yigin izi iki cerceveydi (fiber yigini cozucuyu kesiyor), yani
  // sessiz ve teshisi zor. Asil duzeltme sirada: `jobs.shutdown()` artik
  // fizikten ONCE cagriliyor, yani bu sayac sifir olmak ZORUNDA. Nobetci o
  // sozlesmeyi ayakta tutuyor: biri sirayi bozarsa sessiz UAF yerine tam
  // burada, adiyla patlar.
  ~FiberJoltJobs() override {
    // Bizim kuyrugumuzda, bu nesnenin `jobs_` havuzunu gosteren girdiler
    // KALMIS olabilir ve bu NORMALDIR: Jolt'un bariyeri beklerken isleri
    // kendi thread'inde de kosturuyor, bizim girdiler bayat ama refli kaliyor.
    // Tehlike o girdilerin varligi degil, HAVUZ OLDUKTEN SONRA bir worker'in
    // onlari cekmesi — o zaman `job->Execute()` serbest bellege gider.
    //
    // Olculdu (CI macOS/arm64): `thread: tulpar-job`, SIGSEGV, fault_addr
    // 0x8bc94512aa864210 (null DEGIL, COP). Iki worker'li kosucuda kuyrukta
    // 276 girdi birikmisti; 15 worker'li yerel makinede birikmedigi icin
    // hic uretilemedi.
    //
    // Iki durumdan biri saglanmali, ikisini de BURADA garantiliyoruz:
    //   * is sistemi KOSUYOR   -> birikinti tukenene kadar bekle (worker'lar
    //                             bosaltir; kuyruk spin-poll'lu, ilerler),
    //   * is sistemi DURMUS    -> thread'ler join edilmis, girdiler ATIL,
    //                             beklemek KILITLENME olurdu.
    // Boylece dogruluk cagiranin kapanis SIRASINA bagli kalmiyor. Sira yine
    // de duzeltildi (jobs.shutdown() alt sistemlerden once) — bu ikinci hat.
    //
    // Bekleme SINIRLI: `shutdown` ana thread'den cagriliyor (worker degil), yani
    // bosaltacak thread'ler serbest ve kuyruk spin-poll'lu — ilerlemeli. Yine de
    // sonsuz sessiz bekleme CI'da en kotu sonuctur; sinira dayanirsak ADIYLA
    // patliyoruz, cunku o noktada havuzu yikmak zaten UAF olurdu.
    if (js_ && js_->running()) {
      uint32_t spins = 0;
      while (outstanding_.load(std::memory_order_acquire) != 0) {
        platform::thread_yield();
        if (++spins > 20u * 1000u * 1000u)
          ENGINE_ASSERT_MSG(false,
                            "Jolt is uyarlayicisi: %u is kuyrukta takildi (is sistemi kosuyor ama bosalmiyor)",
                            outstanding_.load(std::memory_order_acquire));
      }
    }
  }

protected:
  void QueueJob(Job *job) override {
    job->AddRef(); // kuyrukta yasadigi surece
    outstanding_.fetch_add(1, std::memory_order_acq_rel);
    js_->run(::tulpar::engine::JobDecl{run_one, job, "jolt"}, nullptr);
  }
  void QueueJobs(Job **jobs, JPH::uint n) override {
    for (JPH::uint i = 0; i < n; i++) QueueJob(jobs[i]);
  }
  void FreeJob(Job *job) override { jobs_.DestructObject(job); }

private:
  static void run_one(void *p) {
    Job *job = static_cast<Job *>(p);
    // Sahibi Release'DEN ONCE okunur: Release son referansi dusurunce Job
    // yok ediliyor ve `GetJobSystem()` serbest bellege bakardi.
    auto *self = static_cast<FiberJoltJobs *>(job->GetJobSystem());
    job->Execute();
    job->Release();
    self->outstanding_.fetch_sub(1, std::memory_order_acq_rel);
  }
  FiberJobSystem *js_;
  std::atomic<uint32_t> outstanding_{0};
  JPH::FixedSizeFreeList<Job> jobs_;
};

inline JPH::Vec3 to_jph(Vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat to_jph(Quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline Vec3 from_jph(JPH::Vec3 v) { return Vec3{v.GetX(), v.GetY(), v.GetZ()}; }
inline Quat from_jph(JPH::Quat q) { return Quat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

// TEMAS DINLEYICISI. Jolt bu geri cagrimlari IS PARCACIKLARINDAN ve es zamanli
// cagirir, yani halka yazimi atomik olmak zorunda: her yazar `next_` uzerinden
// kendi yuvasini rezerve eder. Kilit YOK (fizik adiminda kilit beklemek adimi
// serilestirirdi) ve AYIRMA yok (halka arena'da, kapasite init'te sabit).
//
// Halka dolunca olay DUSER ve `dropped_` artar. Bu sayac disari veriliyor:
// sessiz kirpilma, oyunun "carpma gelmedi" sanip yanlis mantik kurmasi demek
// olurdu ve hicbir sey kizarmazdi.
class ContactRing final : public JPH::ContactListener {
public:
  void setup(ContactEvent *buf, uint32_t cap) { buf_ = buf; cap_ = cap; }
  void clear() { next_.store(0, std::memory_order_relaxed); dropped_.store(0, std::memory_order_relaxed); }
  uint32_t count() const {
    const uint32_t n = next_.load(std::memory_order_acquire);
    return n < cap_ ? n : cap_;
  }
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  const ContactEvent &at(uint32_t i) const { return buf_[i]; }

  void OnContactAdded(const JPH::Body &b1, const JPH::Body &b2, const JPH::ContactManifold &m,
                      JPH::ContactSettings &) override {
    record(b1, b2, m);
  }
  // Kalici temaslar KAYDEDILMIYOR: bir kutunun zeminde durmasi her adimda olay
  // uretirdi ve halka tek karede dolardi. Oyunun sordugu soru "ne zaman
  // carptim", "hala degiyor muyum" degil (onun icin ortusme sorgusu var).

private:
  void record(const JPH::Body &b1, const JPH::Body &b2, const JPH::ContactManifold &m) {
    if (!buf_ || !cap_) return;
    const uint32_t slot = next_.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= cap_) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    ContactEvent &e = buf_[slot];
    e.a.v = b1.GetID().GetIndexAndSequenceNumber();
    e.b.v = b2.GetID().GetIndexAndSequenceNumber();
    e.point = from_jph(JPH::Vec3(m.GetWorldSpaceContactPointOn1(0)));
    e.normal = from_jph(m.mWorldSpaceNormal);
    // Carpma SIDDETI: temas noktasindaki goreli hizin normal boyu. Jolt
    // manifoldu itki tasimiyor; goreli hiz cozumden ONCE dogru buyuklugu
    // veriyor ve belirlenimli (ayni girdi ayni sayi).
    const JPH::Vec3 p = JPH::Vec3(m.GetWorldSpaceContactPointOn1(0));
    const JPH::Vec3 v1 = b1.GetPointVelocity(p);
    const JPH::Vec3 v2 = b2.GetPointVelocity(p);
    const float rel = (v2 - v1).Dot(m.mWorldSpaceNormal);
    e.speed = rel < 0 ? -rel : rel;
  }
  ContactEvent *buf_ = nullptr;
  uint32_t cap_ = 0;
  std::atomic<uint32_t> next_{0};
  std::atomic<uint32_t> dropped_{0};
};

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
  ContactRing contacts;
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
  if (cfg.max_contact_events) {
    void *cbuf = arena.alloc(sizeof(ContactEvent) * cfg.max_contact_events, alignof(ContactEvent));
    if (!cbuf) return false;
    impl_->contacts.setup(static_cast<ContactEvent *>(cbuf), cfg.max_contact_events);
    impl_->system.SetContactListener(&impl_->contacts);
  }

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

uint32_t Physics::contact_count() const { return impl_ ? impl_->contacts.count() : 0; }
uint32_t Physics::contact_overflow() const { return impl_ ? impl_->contacts.dropped() : 0; }
ContactEvent Physics::contact(uint32_t i) const {
  if (!impl_ || i >= impl_->contacts.count()) return ContactEvent{};
  return impl_->contacts.at(i);
}
void Physics::clear_contacts() {
  if (impl_) impl_->contacts.clear();
}

bool Physics::raycast(Vec3 origin, Vec3 dir, float max_distance, RayHit *hit) const {
  if (hit) *hit = RayHit{};
  if (!impl_ || max_distance <= 0.0f) return false;
  const float len = length(dir);
  if (len <= 1e-8f) return false;
  const JPH::Vec3 d = to_jph(dir * (1.0f / len)) * max_distance;
  const JPH::RRayCast ray{to_jph(origin), d};
  JPH::RayCastResult res;
  if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, res)) return false;
  if (hit) {
    hit->body = BodyId{res.mBodyID.GetIndexAndSequenceNumber()};
    hit->distance = res.mFraction * max_distance;
    const JPH::RVec3 p = ray.GetPointOnRay(res.mFraction);
    hit->point = Vec3{(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()};
    // Yuzey normali govde kilidi ister (sorgu baska thread'den de gelebilir).
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), res.mBodyID);
    if (lock.Succeeded()) hit->normal = from_jph(lock.GetBody().GetWorldSpaceSurfaceNormal(res.mSubShapeID2, p));
  }
  return true;
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
