#include "sim/timers.hpp"

#include <new>

#include "core/memory/arena.hpp"

// ratas BURADA, .cpp icinde. Global ad alanina `Tick`/`TimerWheel`/
// `TimerEventInterface` koydugu icin baslikta OLAMAZ (bkz. timers.hpp).
#include "ratas/timer-wheel.h"

namespace tulpar::engine::sim {

// Ileri bildirim: Impl, Pool<Node>'u yalniz ISARETCI olarak tutar, bu yuzden
// burada TAM TIP gerekmez. Anonim ad alani BILINCLI kullanilmadi: anonim
// ad alanindaki bir tipin, disaridaki bir sinifin uyesi olmasi
// -Wsubobject-linkage uyarisi uretir. Bu tip zaten .cpp'ye ozel.
struct Node;

struct TimerSet::Impl {
  ::TimerWheel wheel;
  Pool<Node> pool;
  Handle *pending = nullptr; // bu advance()'te calisip serbest birakilacaklar
  uint32_t pending_n = 0;
  uint32_t capacity = 0;
  uint64_t fired = 0;
  uint32_t rejected = 0;
};

// Cagiran-sahipli olay dugumu: carkin ic ice bagli listesinin DUGUMU
// oldugu icin ayrica TAHSIS gerekmez -- dugumun kendisi havuz slotudur.
struct Node final : public ::TimerEventInterface {
  TimerSet::Impl *owner;
  TimerFn fn;
  void *user;
  uint32_t period; // 0 = tek atislik
  Handle self;

  Node(TimerSet::Impl *o, TimerFn f, void *u, uint32_t p)
      : owner(o), fn(f), user(u), period(p), self(Handle::invalid()) {}

  void execute() override {
    owner->fired++;
    if (period != 0) {
      // SIRA ONEMLI: once yeniden kur, sonra calistir.
      // Cark, geri cagirma sirasinda `now()` olarak olayin KURULDUGU tick'i
      // dondurur; bu yuzden `period` buradan olculunce faz SURUKLENMEZ.
      // Ayrica geri cagirma kendini cancel() ederse bu kurulum geri alinir --
      // ters sirada yapilsaydi iptal edilmis zamanlayici DIRILIRDI.
      owner->wheel.schedule(this, period);
    } else if (owner->pending_n < owner->capacity) {
      // Slotu BURADA serbest birakmiyoruz: `this` kendi metodunun icinde
      // yok edilmis olurdu. Supurme, advance() carktan cikinca yapilir.
      owner->pending[owner->pending_n++] = self;
    }
    fn(user);
  }
};

bool TimerSet::init(Arena &arena, uint32_t capacity) {
  if (impl_ || capacity == 0) return false;
  void *mem = arena.alloc(sizeof(Impl), alignof(Impl));
  if (!mem) return false;
  Impl *im = new (mem) Impl();
  if (!im->pool.init(arena, capacity, "timers")) return false;
  im->pending = arena.alloc_array<Handle>(capacity);
  if (!im->pending) return false;
  im->capacity = capacity;
  impl_ = im;
  return true;
}

Handle TimerSet::after(uint32_t delay_ticks, TimerFn fn, void *user) {
  if (!impl_ || !fn || delay_ticks == 0) {
    if (impl_) impl_->rejected++;
    return Handle::invalid();
  }
  const Handle h = impl_->pool.create(impl_, fn, user, 0u);
  if (!h.valid()) { // havuz dolu -> SESSIZ basarisizlik YOK, sayilir
    impl_->rejected++;
    return h;
  }
  Node *n = impl_->pool.get(h);
  n->self = h;
  impl_->wheel.schedule(n, delay_ticks);
  return h;
}

Handle TimerSet::every(uint32_t period_ticks, TimerFn fn, void *user) {
  if (!impl_ || !fn || period_ticks == 0) {
    if (impl_) impl_->rejected++;
    return Handle::invalid();
  }
  const Handle h = impl_->pool.create(impl_, fn, user, period_ticks);
  if (!h.valid()) {
    impl_->rejected++;
    return h;
  }
  Node *n = impl_->pool.get(h);
  n->self = h;
  impl_->wheel.schedule(n, period_ticks);
  return h;
}

bool TimerSet::cancel(Handle h) {
  if (!impl_) return false;
  Node *n = impl_->pool.get(h); // BAYAT handle -> nullptr (nesil etiketi)
  if (!n) return false;
  n->cancel();          // carktan cikar (kurulu degilse de guvenli)
  impl_->pool.destroy(h); // nesil artar -> bekleyen supurme bunu ATLAR
  return true;
}

bool TimerSet::active(Handle h) const {
  if (!impl_) return false;
  const Node *n = impl_->pool.get(h);
  return n && n->active();
}

uint32_t TimerSet::remaining(Handle h) const {
  if (!impl_) return 0;
  const Node *n = impl_->pool.get(h);
  if (!n || !n->active()) return 0;
  const Tick now = impl_->wheel.now();
  const Tick at = n->scheduled_at();
  return at > now ? (uint32_t)(at - now) : 0;
}

void TimerSet::advance(uint32_t ticks) {
  if (!impl_ || ticks == 0) return; // carkin sozlesmesi delta != 0 ister
  impl_->pending_n = 0;
  impl_->wheel.advance(ticks);

  // Calismis tek atisliklarin slotlarini SIMDI geri ver. `alive` kontrolu
  // sart: bir geri cagirma, daha once bu advance icinde calismis baska bir
  // zamanlayiciyi cancel() etmis olabilir -- o zaman nesil artmistir ve
  // handle artik bayattir; iki kez serbest birakmayiz.
  for (uint32_t i = 0; i < impl_->pending_n; i++) {
    const Handle h = impl_->pending[i];
    if (impl_->pool.alive(h)) impl_->pool.destroy(h);
  }
  impl_->pending_n = 0;
}

uint64_t TimerSet::now() const { return impl_ ? impl_->wheel.now() : 0; }

uint32_t TimerSet::ticks_to_next_event(uint32_t max) const {
  if (!impl_) return max;
  const Tick t = impl_->wheel.ticks_to_next_event((Tick)max);
  return t > (Tick)max ? max : (uint32_t)t;
}

TimerStats TimerSet::stats() const {
  TimerStats s;
  if (!impl_) return s;
  s.fired = impl_->fired;
  s.active = impl_->pool.live_count();
  s.rejected = impl_->rejected;
  return s;
}

} // namespace tulpar::engine::sim
