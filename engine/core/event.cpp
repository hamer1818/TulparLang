#include "core/event.hpp"

namespace tulpar::engine::core {

bool EventBus::subscribe(uint32_t type_id, EventCallback callback, void *user_data) {
  if (listener_count_ >= kMaxListeners) return false;
  listeners_[listener_count_++] = {type_id, callback, user_data};
  return true;
}

bool EventBus::emit_raw(uint32_t type_id, const void *payload, uint32_t payload_size) {
  if (event_count_ >= kMaxEventsPerFrame) return false;
  if (payload_size > sizeof(Event::payload)) return false; // Yük fazla buyuk

  Event &ev = event_queue_[event_count_++];
  ev.type_id = type_id;
  ev.payload_size = payload_size;
  if (payload && payload_size > 0) {
    std::memcpy(ev.payload, payload, payload_size);
  }
  return true;
}

void EventBus::dispatch() {
  // Kuyruk boyunca dolas ve her olayi ilgili dinleyicilere ulastir.
  for (uint32_t i = 0; i < event_count_; i++) {
    const Event &ev = event_queue_[i];
    for (uint32_t j = 0; j < listener_count_; j++) {
      if (listeners_[j].type_id == ev.type_id) {
        listeners_[j].callback(ev, listeners_[j].user_data);
      }
    }
  }
  // Dispatch bitince kuyrugu sifirla.
  event_count_ = 0;
}

} // namespace tulpar::engine::core
