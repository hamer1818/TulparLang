// L1 CORE — Sifir Tahsisli Event Bus (Olay Yolu).
// DEVAM_PLANI.md Faz B (Gameplay) kapsaminda, bilesenlerin birbirine dogrudan
// bagimli olmadan (decoupled) haberlesmesini saglar.
//
// Yapisal secimler (VIZYON.md SS1 ve A2 kurali):
// 1. Dinamik bellek ayirmasi (new/malloc) YOKTUR.
// 2. Kilit (mutex) YOKTUR. Tek is parcacikli simulasyon adiminda calisir.
// 3. Kuyruk tabanlidir (immediate dispatch degil) — olaylar emit() ile birikir,
//    kare sonunda dispatch() ile topluca islenir. Bu, sonsuz dongu (olay icinde
//    olay tetikleme) riskini sinirlar.
// 4. Rollback (geriye sarma) uyumlulugu icin ic durumu sabittir; rollback sirasinda
//    gecmis kareler yeniden oynatilirken dispatch'ler aynen tekrar eder.
#pragma once
#include <cstdint>
#include <cstring>

namespace tulpar::engine::core {

// Type-erased, kucuk yuklu (payload) olay. 32 bayt cogu oyun mekanigi 
// (hasar, tetikleyici, state degisimi) icin yeterlidir.
struct Event {
  uint32_t type_id;
  uint32_t payload_size;
  uint8_t payload[32];
};

using EventCallback = void (*)(const Event &event, void *user_data);

struct EventListener {
  uint32_t type_id;
  EventCallback callback;
  void *user_data;
};

class EventBus {
 public:
  static constexpr uint32_t kMaxListeners = 128;
  static constexpr uint32_t kMaxEventsPerFrame = 256;

  bool init() {
    listener_count_ = 0;
    event_count_ = 0;
    return true;
  }

  // Yeni bir dinleyici kaydeder. Ayni tipte birden fazla dinleyici olabilir.
  bool subscribe(uint32_t type_id, EventCallback callback, void *user_data = nullptr);

  // Type-safe yardimci: Sabit boyutlu struct'lari dogrudan emit eder.
  template <typename T>
  bool emit(uint32_t type_id, const T &payload) {
    return emit_raw(type_id, &payload, sizeof(T));
  }
  
  // Yuksuz (parametresiz) olay gonderimi.
  bool emit(uint32_t type_id) {
    return emit_raw(type_id, nullptr, 0);
  }

  // Kuyruktaki olaylari bagli dinleyicilere gonderir ve kuyrugu bosaltir.
  void dispatch();
  void clear() { event_count_ = 0; }
  
  uint32_t pending_event_count() const { return event_count_; }

 private:
  bool emit_raw(uint32_t type_id, const void *payload, uint32_t payload_size);

  EventListener listeners_[kMaxListeners];
  uint32_t listener_count_ = 0;

  Event event_queue_[kMaxEventsPerFrame];
  uint32_t event_count_ = 0;
};

} // namespace tulpar::engine::core
