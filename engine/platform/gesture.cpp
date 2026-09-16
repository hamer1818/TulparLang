#include "platform/gesture.hpp"

#include <cmath>

namespace tulpar::engine::platform {

namespace {
// KATMAN KURALI: platform L0'dir, core L1'dir -- L0 YUKARI cagiramaz
// (tools/layer_check.py bunu build hatasi yapar). Bu dosyanin
// core/math/vec.hpp'den TEK ihtiyaci `kPi` sabitiydi; bir sabit ugruna
// katman bagimliligi kurmak yerine burada yerel olarak tanimlaniyor.
// Deger core/math/vec.hpp'deki kPi ile AYNI olmalidir.
constexpr float kPi = 3.14159265358979323846f;

// a-b farkini (-pi, pi] araligina sarar (interp.cpp'nin lerp_deg'iyle AYNI
// "en kisa yol" ilkesi, ama fark icin -- 350->10 derece +20 olmali, -340 degil).
float angle_diff(float a, float b) {
  float d = a - b;
  while (d > kPi) d -= 2.0f * kPi;
  while (d <= -kPi) d += 2.0f * kPi;
  return d;
}
} // namespace

GestureRecognizer::GestureRecognizer(float tap_max_duration_s, float tap_max_move_px)
    : tap_max_duration_s_(tap_max_duration_s), tap_max_move_px_(tap_max_move_px) {}

const TouchPoint *GestureRecognizer::find_down(const TouchState &state, int32_t id) const {
  for (uint32_t i = 0; i < TouchState::kMax; i++)
    if (state.pts[i].down && state.pts[i].id == id) return &state.pts[i];
  return nullptr;
}

GestureEvents GestureRecognizer::update(const TouchState &state, double now_s) {
  GestureEvents out;

  // 1) Kalkmis (artik down OLMAYAN) izlenen dokunuslar: TAP kontrolu + gerekirse pinch bitisi.
  for (uint32_t i = 0; i < kMaxTracked; i++) {
    TrackedTouch &tr = tracked_[i];
    if (tr.id < 0) continue;
    if (find_down(state, tr.id)) continue; // hala basili, bu turda islenmeyecek
    const int32_t lifted_id = tr.id;
    if (!tr.moved_too_far && (now_s - tr.start_time_s) <= (double)tap_max_duration_s_)
      out.push(Gesture{GestureType::kTap, tr.start_x, tr.start_y, 1.0f, 0.0f});
    tr.id = -1;
    if (pinch_active_ && (lifted_id == pinch_id_a_ || lifted_id == pinch_id_b_)) {
      out.push(Gesture{GestureType::kPinchEnd, 0, 0, 1.0f, 0.0f});
      pinch_active_ = false;
      pinch_id_a_ = pinch_id_b_ = -1;
    }
  }

  // 2) Yeni (henuz izlenmeyen) down dokunuslari izlemeye al.
  for (uint32_t i = 0; i < TouchState::kMax; i++) {
    const TouchPoint &p = state.pts[i];
    if (!p.down) continue;
    bool already = false;
    for (uint32_t j = 0; j < kMaxTracked; j++)
      if (tracked_[j].id == p.id) { already = true; break; }
    if (already) continue;
    for (uint32_t j = 0; j < kMaxTracked; j++) {
      if (tracked_[j].id < 0) {
        tracked_[j] = TrackedTouch{p.id, p.x, p.y, now_s, false};
        break;
      }
    }
  }

  // 3) Hareket esigini kontrol et (hala basili olanlar icin) -- TAP'i KALICI OLARAK bozar.
  for (uint32_t i = 0; i < kMaxTracked; i++) {
    TrackedTouch &tr = tracked_[i];
    if (tr.id < 0 || tr.moved_too_far) continue;
    const TouchPoint *p = find_down(state, tr.id);
    if (!p) continue;
    const float dx = p->x - tr.start_x, dy = p->y - tr.start_y;
    if (dx * dx + dy * dy > tap_max_move_px_ * tap_max_move_px_) tr.moved_too_far = true;
  }

  // 4) Pinch: TAM OLARAK 2 aktif dokunma varsa baslat, ikisi de hala basiliysa guncelle.
  const TouchPoint *pa = pinch_id_a_ >= 0 ? find_down(state, pinch_id_a_) : nullptr;
  const TouchPoint *pb = pinch_id_b_ >= 0 ? find_down(state, pinch_id_b_) : nullptr;
  if (pinch_active_ && (!pa || !pb)) {
    // Guvenlik agi: normalde 1. adim yakalar, buraya dusmemeli.
    out.push(Gesture{GestureType::kPinchEnd, 0, 0, 1.0f, 0.0f});
    pinch_active_ = false;
    pinch_id_a_ = pinch_id_b_ = -1;
  } else if (!pinch_active_) {
    uint32_t down_n = 0;
    const TouchPoint *a = nullptr, *b = nullptr;
    for (uint32_t i = 0; i < TouchState::kMax; i++) {
      if (!state.pts[i].down) continue;
      down_n++;
      if (!a) a = &state.pts[i];
      else if (!b) b = &state.pts[i];
      else { a = nullptr; b = nullptr; break; } // 2'den fazla parmak: pinch BASLATMA
    }
    if (down_n == 2 && a && b) {
      pinch_id_a_ = a->id;
      pinch_id_b_ = b->id;
      const float dx = b->x - a->x, dy = b->y - a->y;
      pinch_prev_dist_ = std::sqrt(dx * dx + dy * dy);
      pinch_prev_angle_ = std::atan2(dy, dx);
      pinch_active_ = true;
      out.push(Gesture{GestureType::kPinchBegin, (a->x + b->x) * 0.5f, (a->y + b->y) * 0.5f, 1.0f, 0.0f});
      // Pinch'e katilan iki parmak artik TAP ADAYI DEGIL.
      for (uint32_t j = 0; j < kMaxTracked; j++)
        if (tracked_[j].id == pinch_id_a_ || tracked_[j].id == pinch_id_b_) tracked_[j].moved_too_far = true;
    }
  } else {
    const float dx = pb->x - pa->x, dy = pb->y - pa->y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float angle = std::atan2(dy, dx);
    const float scale = pinch_prev_dist_ > 1e-4f ? dist / pinch_prev_dist_ : 1.0f;
    const float rot = angle_diff(angle, pinch_prev_angle_);
    pinch_prev_dist_ = dist;
    pinch_prev_angle_ = angle;
    out.push(Gesture{GestureType::kPinchUpdate, (pa->x + pb->x) * 0.5f, (pa->y + pb->y) * 0.5f, scale, rot});
  }

  return out;
}

} // namespace tulpar::engine::platform
