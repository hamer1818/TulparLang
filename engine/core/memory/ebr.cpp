#include "core/memory/ebr.hpp"

#include <atomic>

#include "core/jobs/spinlock.hpp" // motorun KENDI kilidi -- std::mutex L1'de yasak
#include "platform/fatal.hpp"

namespace tulpar::engine::core {

namespace {

// 3 donem (epoch): bir thread en fazla bir donem geride kalabilir, bu yuzden
// "simdiki", "onceki" ve "geri kazanilabilir" olmak uzere uc kova yeter.
constexpr uint32_t kEpochs = 3;
// Sabit tavanlar: L1'de dinamik buyume YOK. Asilirsa SESSIZCE degil,
// sayilarak raporlanir (bkz. ebr_dropped / ebr_thread_overflow).
constexpr uint32_t kMaxThreads = 64;
constexpr uint32_t kMaxRetired = 2048;

struct RetiredNode {
  void *ptr;
  EbrDestructor dtor;
  RetiredNode *next;
};

// --- Thread duyurusu: TEK ATOMIK KELIME -------------------------------
// 0            = bu thread okuma bolgesinde DEGIL
// (epoch<<1)|1 = bu thread `epoch` doneminde okuma yapiyor
//
// **Neden tek kelime:** donem ile "aktif" bayragini AYRI iki atomige
// yazmak GERCEK bir use-after-free acar. Senaryo: T global donemi X-1
// olarak okur, `local_epoch = X-1` yazar, ama `active = true` yazmadan
// once temizleyici kosar. Temizleyici T'yi pasif gordugu icin atlar,
// donemi ilerletir ve X-1 kovasini SERBEST BIRAKIR. T ise tam o anda
// aktiflesip X-1'de gordugu isaretciyi dereference eder -> cokme.
// Tek kelimeye atomik yazmak bu araligi kapatir: temizleyici ya T'yi
// pasif gorur (T henuz hicbir sey okumamistir) ya da donemiyle birlikte
// aktif gorur.
struct ThreadState {
  std::atomic<uint32_t> announce{0};
};

std::atomic<uint32_t> g_global_epoch{0};

SpinLock g_lock; // yalniz retire/flush/kayit icin; OKUMA yolu kilitsizdir
ThreadState g_thread_pool[kMaxThreads];
ThreadState *g_threads[kMaxThreads];
uint32_t g_thread_count = 0;
uint32_t g_thread_overflow = 0;

RetiredNode g_node_pool[kMaxRetired];
RetiredNode *g_free_head = nullptr;
RetiredNode *g_retired[kEpochs] = {nullptr, nullptr, nullptr};
uint32_t g_dropped = 0;

thread_local ThreadState *t_state = nullptr;

// Bir kovayi bosalt. Cagiran g_lock'u TUTUYOR olmali.
void reclaim_bucket(uint32_t bucket) {
  RetiredNode *curr = g_retired[bucket];
  while (curr) {
    RetiredNode *next = curr->next;
    if (curr->dtor && curr->ptr) curr->dtor(curr->ptr);
    curr->ptr = nullptr;
    curr->dtor = nullptr;
    curr->next = g_free_head; // dugumu havuza geri ver
    g_free_head = curr;
    curr = next;
  }
  g_retired[bucket] = nullptr;
}

} // namespace

void ebr_init() {
  SpinGuard g(g_lock);
  g_global_epoch.store(0, std::memory_order_relaxed);
  // Serbest listeyi kur: TUM dugumler onceden ayrilmis, calisma aninda
  // `new` YOK (A2: kare icinde tahsis sifir).
  g_free_head = nullptr;
  for (uint32_t i = 0; i < kMaxRetired; i++) {
    g_node_pool[i].ptr = nullptr;
    g_node_pool[i].dtor = nullptr;
    g_node_pool[i].next = g_free_head;
    g_free_head = &g_node_pool[i];
  }
  for (uint32_t i = 0; i < kEpochs; i++) g_retired[i] = nullptr;
  for (uint32_t i = 0; i < kMaxThreads; i++) {
    g_thread_pool[i].announce.store(0, std::memory_order_relaxed);
    g_threads[i] = nullptr;
  }
  g_thread_count = 0;
  g_thread_overflow = 0;
  g_dropped = 0;
}

void ebr_shutdown() {
  SpinGuard g(g_lock);
  // Kapanista TUM kovalar bosaltilir -- donem ilerletmeden, kosulsuz.
  for (uint32_t i = 0; i < kEpochs; i++) reclaim_bucket(i);
  g_thread_count = 0;
}

void ebr_register_thread() {
  if (t_state) return;
  SpinGuard g(g_lock);
  if (g_thread_count >= kMaxThreads) {
    g_thread_overflow++; // SESSIZ degil: kayitsiz thread korumasiz kalir
    return;
  }
  t_state = &g_thread_pool[g_thread_count];
  t_state->announce.store(0, std::memory_order_relaxed);
  g_threads[g_thread_count++] = t_state;
}

void ebr_unregister_thread() {
  if (!t_state) return;
  SpinGuard g(g_lock);
  t_state->announce.store(0, std::memory_order_release);
  for (uint32_t i = 0; i < g_thread_count; i++) {
    if (g_threads[i] == t_state) {
      g_threads[i] = g_threads[g_thread_count - 1]; // sonuncuyla takas
      g_threads[--g_thread_count] = nullptr;
      break;
    }
  }
  t_state = nullptr;
}

void ebr_enter() {
  // Kayitsiz thread: SESSIZCE devam etmek KORUMASIZ okuma demektir ve
  // use-after-free'yi gorunmez kilar. Hata ayiklamada fatal.
  ENGINE_ASSERT_MSG(t_state != nullptr, "ebr_enter: thread kayitli degil (ebr_register_thread cagrilmali)");
  if (!t_state) return;

  uint32_t e = g_global_epoch.load(std::memory_order_acquire);
  t_state->announce.store((e << 1) | 1u, std::memory_order_seq_cst);
  // Duyurudan SONRA donem degismis olabilir. Degistiyse bir kez tazele:
  // bayat bir duyuru guvenlidir (temizleyiciyi DURDURUR, yanlis serbest
  // birakmaz) ama gereksiz yere ilerlemeyi engeller.
  const uint32_t e2 = g_global_epoch.load(std::memory_order_acquire);
  if (e2 != e) t_state->announce.store((e2 << 1) | 1u, std::memory_order_seq_cst);
}

void ebr_exit() {
  if (!t_state) return;
  t_state->announce.store(0, std::memory_order_release);
}

void ebr_retire(void *ptr, EbrDestructor dtor) {
  if (!ptr) return;
  SpinGuard g(g_lock);
  if (!g_free_head) {
    // Havuz dolu. dtor'u BURADA cagirmak use-after-free olurdu (bir thread
    // hala okuyor olabilir), bu yuzden nesne BILINCLI OLARAK sizdirilir --
    // cokmek yerine sizmak. Sayac bunu GORUNUR kilar.
    g_dropped++;
    return;
  }
  RetiredNode *node = g_free_head;
  g_free_head = node->next;

  const uint32_t bucket = g_global_epoch.load(std::memory_order_acquire) % kEpochs;
  node->ptr = ptr;
  node->dtor = dtor;
  node->next = g_retired[bucket];
  g_retired[bucket] = node;
}

void ebr_flush() {
  SpinGuard g(g_lock);
  const uint32_t global_e = g_global_epoch.load(std::memory_order_acquire);

  // Aktif TUM threadler guncel donemde mi? Biri geride ise ilerlemek,
  // onun hala okudugu belleği serbest birakmak demektir.
  for (uint32_t i = 0; i < g_thread_count; i++) {
    const uint32_t a = g_threads[i]->announce.load(std::memory_order_seq_cst);
    if ((a & 1u) == 0u) continue;      // pasif: hicbir seye erismiyor
    if ((a >> 1) != global_e) return;  // geride: ilerletme YOK
  }

  const uint32_t next_e = global_e + 1;
  g_global_epoch.store(next_e, std::memory_order_release);

  // (next_e + 1) % 3, next_e - 2 ile ayni kovadir: iki donem once
  // emekliye ayrilanlar. O donemde okuyan herkes coktan cikmistir.
  reclaim_bucket((next_e + 1) % kEpochs);
}

uint32_t ebr_dropped() { return g_dropped; }
uint32_t ebr_thread_overflow() { return g_thread_overflow; }

} // namespace tulpar::engine::core
