// sim/timers.hpp (jsnell/ratas sarmalayicisi) + FixedStep zaman olcegi.
// Burada kanitlanan seyler, sarmalayicinin ASIL degeri olan anlamsal
// ayrintilardir: sürüklenmeyen periyot, bayat handle guvenligi, geri
// cagirma icinden iptal, ve zaman olceginin belirlenimi bozmamasi.
#include "sim/timers.hpp"

#include "core/memory/arena.hpp"
#include "sim/schedule.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {

// Ad `Counter` DEGIL: `using namespace tulpar::engine` acikken motorun is
// sistemindeki Counter ile BELIRSIZ kaliyor (PR #322 derlenmedigi icin
// gorulmemis).
struct TimerCounter {
  uint32_t n = 0;
  uint64_t last_tick = 0;
  TimerSet *set = nullptr;
  Handle self{};
};

void bump(void *u) {
  TimerCounter *c = (TimerCounter *)u;
  c->n++;
  if (c->set) c->last_tick = c->set->now();
}

// Kendini ILK calismada iptal eden geri cagirma.
void bump_then_cancel_self(void *u) {
  TimerCounter *c = (TimerCounter *)u;
  c->n++;
  c->set->cancel(c->self);
}

} // namespace

ENGINE_TEST(timer_one_shot_fires_once_at_the_right_tick) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 32));

  TimerCounter c;
  c.set = &t;
  const Handle h = t.after(5, bump, &c);
  CHECK(h.valid());
  CHECK(t.active(h));
  CHECK(t.remaining(h) == 5);

  t.advance(4);
  CHECK(c.n == 0); // HENUZ degil
  CHECK(t.remaining(h) == 1);

  t.advance(1);
  CHECK(c.n == 1);
  CHECK(c.last_tick == 5); // geri cagirma sirasinda now() == KURULDUGU tick

  // Tek atislik: bir daha ASLA; ve slotu geri verilmis olmali.
  t.advance(100);
  CHECK(c.n == 1);
  CHECK(t.stats().active == 0);
  CHECK(!t.active(h)); // handle artik BAYAT
}

ENGINE_TEST(timer_periodic_does_not_drift) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 32));

  TimerCounter c;
  c.set = &t;
  const Handle h = t.every(7, bump, &c);
  CHECK(h.valid());

  // ASIL TUZAK: buyuk, DUZENSIZ adimlarla ilerlet. Yeniden kurma "simdi"ye
  // gore yapilsaydi her advance cagrisinda faz kayar, 700 tick sonunda
  // sayim 100'un ALTINA duserdi.
  uint32_t total = 0;
  const uint32_t steps[] = {1, 13, 2, 40, 5, 99, 3, 200, 337};
  for (uint32_t s : steps) { t.advance(s); total += s; }
  CHECK(total == 700);
  CHECK(c.n == 100);        // 700 / 7, TAM
  CHECK(c.last_tick == 700); // son atis TAM 700'de -- suruklenme YOK
  CHECK(t.active(h));       // periyodik: hala kurulu
}

ENGINE_TEST(timer_cancel_prevents_firing_and_stale_handles_are_safe) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 32));

  TimerCounter c;
  const Handle h = t.after(3, bump, &c);
  CHECK(t.cancel(h));
  t.advance(10);
  CHECK(c.n == 0);

  // AYNI handle ikinci kez: BAYAT (nesil artti) -> false, cokme YOK.
  CHECK(!t.cancel(h));
  CHECK(!t.active(h));
  CHECK(t.remaining(h) == 0);

  // Slot geri donmus olmali: yeni bir zamanlayici kurulabilir ve eski
  // handle ona ERISEMEZ.
  TimerCounter c2;
  const Handle h2 = t.after(2, bump, &c2);
  CHECK(h2.valid());
  CHECK(h2 != h);     // nesil farkli
  CHECK(!t.cancel(h)); // eski handle YENI zamanlayiciyi iptal ETMEZ
  t.advance(2);
  CHECK(c2.n == 1);
}

ENGINE_TEST(timer_callback_can_cancel_itself) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 32));

  TimerCounter c;
  c.set = &t;
  c.self = t.every(4, bump_then_cancel_self, &c);
  CHECK(c.self.valid());

  // SIRA TUZAGI: periyodik zamanlayici geri cagirma CALISMADAN ONCE
  // yeniden kuruluyor. Geri cagirmanin icindeki cancel() bunu geri
  // almazsa zamanlayici DIRILIR ve sayim 1'de kalmaz.
  t.advance(100);
  CHECK(c.n == 1);
  CHECK(t.stats().active == 0);
}

ENGINE_TEST(timer_rejects_zero_delay_and_reports_pool_exhaustion) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 4)); // bilerek KUCUK

  TimerCounter c;
  // 0 gecikme: carkin sozlesmesi delta != 0 ister. Sessizce 1'e
  // yuvarlamak yerine REDDEDILIR ve SAYILIR.
  CHECK(!t.after(0, bump, &c).valid());
  CHECK(!t.every(0, bump, &c).valid());
  CHECK(!t.after(5, nullptr, &c).valid()); // null geri cagirma
  CHECK(t.stats().rejected == 3);

  for (int i = 0; i < 4; i++) CHECK(t.after(10, bump, &c).valid());
  CHECK(t.stats().active == 4);
  // 5.'si: havuz dolu -> SESSIZ basarisizlik YOK
  CHECK(!t.after(10, bump, &c).valid());
  CHECK(t.stats().rejected == 4);
}

ENGINE_TEST(timer_ticks_to_next_event_enables_sleeping) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 32));

  // Hicbir zamanlayici yok -> tavan doner (cagiran istedigi kadar uyur).
  CHECK(t.ticks_to_next_event(1000) == 1000);

  TimerCounter c;
  t.after(50, bump, &c);
  t.after(20, bump, &c); // EN YAKIN olan bu
  CHECK(t.ticks_to_next_event(1000) == 20);

  t.advance(20);
  CHECK(c.n == 1);
  CHECK(t.ticks_to_next_event(1000) == 30); // kalan: 50 - 20
}

ENGINE_TEST(timer_advance_zero_is_a_no_op) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "timers"));
  TimerSet t;
  CHECK(t.init(sys, 8));
  TimerCounter c;
  const Handle h = t.after(1, bump, &c);
  // Carkin sozlesmesi delta != 0; 0 gecmek ic durumu bozardi.
  t.advance(0);
  CHECK(t.now() == 0);
  CHECK(c.n == 0);
  CHECK(t.active(h));
  t.advance(1);
  CHECK(c.n == 1);
}

// --- FixedStep: zaman olcegi / duraklatma ------------------------------

ENGINE_TEST(fixed_step_time_scale_scales_input_not_step_size) {
  // ASIL IDDIA: agir cekim `step_s`i DEGISTIRMEZ. Degistirseydi her tick
  // baska bir integrasyon adimi kullanirdi ve kayit/replay gecersizlesirdi.
  FixedStep fs;
  const float original_step = fs.step_s;
  fs.time_scale = 0.5f;
  fs.max_ticks_per_frame = 1000;

  uint32_t total = 0;
  for (int f = 0; f < 120; f++) total += fs.advance(1.0f / 60.0f);

  CHECK(fs.step_s == original_step); // DEGISMEDI
  CHECK(total == 60);                // 120 kare, yarim hiz -> 60 tick
  CHECK(fs.tick == 60);

  // Iki kat hiz
  FixedStep fast;
  fast.time_scale = 2.0f;
  fast.max_ticks_per_frame = 1000;
  uint32_t t2 = 0;
  for (int f = 0; f < 60; f++) t2 += fast.advance(1.0f / 60.0f);
  CHECK(fast.step_s == original_step);
  CHECK(t2 == 120);
}

ENGINE_TEST(fixed_step_pause_freezes_without_discarding_accumulator) {
  FixedStep fs;
  // Yarim adimlik birikim biriktir.
  CHECK(fs.advance(fs.step_s * 0.5f) == 0);
  const float acc = fs.accumulator;
  CHECK(acc > 0.0f);

  fs.paused = true;
  for (int i = 0; i < 100; i++) CHECK(fs.advance(1.0f / 60.0f) == 0);
  CHECK(fs.tick == 0);
  // Birikim ATILMADI -> devam edince sicrama olmaz.
  CHECK(fs.accumulator == acc);

  fs.paused = false;
  CHECK(fs.advance(fs.step_s * 0.5f) == 1); // 0.5 + 0.5 = tam bir adim
  CHECK(fs.tick == 1);
}

ENGINE_TEST(fixed_step_frame_advance_works_while_paused) {
  FixedStep fs;
  fs.paused = true;
  CHECK(fs.advance(1.0f) == 0);

  fs.pending_steps = 3; // editor/hata ayiklama: "bir kare ilerle" x3
  CHECK(fs.advance(0.0f) == 1);
  CHECK(fs.advance(0.0f) == 1);
  CHECK(fs.advance(0.0f) == 1);
  CHECK(fs.tick == 3);
  CHECK(fs.pending_steps == 0);
  CHECK(fs.advance(1.0f) == 0); // yine donmus
  CHECK(fs.tick == 3);
}

ENGINE_TEST(fixed_step_zero_or_negative_scale_freezes) {
  FixedStep fs;
  fs.time_scale = 0.0f;
  CHECK(fs.advance(1.0f) == 0);
  fs.time_scale = -1.0f; // anlamsiz: geri sarma DESTEKLENMEZ, donar
  CHECK(fs.advance(1.0f) == 0);
  CHECK(fs.tick == 0);
  CHECK(fs.accumulator == 0.0f); // negatif birikim OLUSMADI
}
