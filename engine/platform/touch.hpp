// L0 PLATFORM — dokunmatik durum: host (Android olay kuyrugu / masaustu fare)
// doldurur, uygulama okur. Sabit kapasite, ayirma yok. Koordinatlar piksel,
// framebuffer uzayinda (Android on-dondurmede pencere uzayi = gorunen yon).
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

struct TouchPoint {
  int32_t id = -1;
  float x = 0, y = 0;
  bool down = false;
};

struct TouchState {
  static constexpr uint32_t kMax = 10;
  TouchPoint pts[kMax];
  uint32_t count = 0;      // aktif (down) nokta sayisi
  float width = 1, height = 1; // ekran olcusu (piksel)
  uint64_t time_ns = 0;

  TouchPoint *find(int32_t id) {
    for (uint32_t i = 0; i < kMax; i++) if (pts[i].down && pts[i].id == id) return &pts[i];
    return nullptr;
  }
  TouchPoint *begin(int32_t id, float x, float y) {
    for (uint32_t i = 0; i < kMax; i++)
      if (!pts[i].down) { pts[i] = TouchPoint{id, x, y, true}; count++; return &pts[i]; }
    return nullptr;
  }
  void move(int32_t id, float x, float y) { if (TouchPoint *p = find(id)) { p->x = x; p->y = y; } }
  void end(int32_t id) { if (TouchPoint *p = find(id)) { p->down = false; if (count) count--; } }
  void clear() { for (uint32_t i = 0; i < kMax; i++) pts[i].down = false; count = 0; }
};

} // namespace tulpar::engine::platform
