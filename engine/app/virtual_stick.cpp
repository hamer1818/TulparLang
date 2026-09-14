#include "app/virtual_stick.hpp"

#include <cmath>

namespace tulpar::engine::app {

void VirtualStick::update(const platform::TouchState &t, uint64_t now_ns) {
  look_delta = {0, 0};
  action = false;
  // Mevcut parmaklar hala basili mi?
  const platform::TouchPoint *mp = nullptr, *lp = nullptr;
  for (uint32_t i = 0; i < platform::TouchState::kMax; i++) {
    const platform::TouchPoint &p = t.pts[i];
    if (!p.down) continue;
    if (p.id == move_id_) mp = &p;
    if (p.id == look_id_) lp = &p;
  }
  if (move_id_ >= 0 && !mp) { move_id_ = -1; move_active = false; move = {0, 0}; }
  if (look_id_ >= 0 && !lp) {
    // Kalkti: kisa ve az hareketliyse eylem
    float held_ms = (float)((now_ns - look_down_ns_) / 1000000ull);
    if (held_ms <= tap_max_ms && look_moved_ <= tap_max_move_px) action = true;
    look_id_ = -1;
    look_active = false;
  }
  // Yeni parmaklar
  for (uint32_t i = 0; i < platform::TouchState::kMax; i++) {
    const platform::TouchPoint &p = t.pts[i];
    if (!p.down || p.id == move_id_ || p.id == look_id_) continue;
    if (p.x < t.width * 0.5f) {
      if (move_id_ < 0) { move_id_ = p.id; stick_origin = {p.x, p.y}; move_active = true; mp = &p; }
    } else if (look_id_ < 0) {
      look_id_ = p.id; look_last_ = look_start_ = {p.x, p.y}; look_down_ns_ = now_ns; look_moved_ = 0; look_active = true; lp = &p;
    }
  }
  if (mp) {
    float dx = (mp->x - stick_origin.x) / stick_radius_px, dy = -(mp->y - stick_origin.y) / stick_radius_px;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1.0f) { dx /= len; dy /= len; }
    move = {dx, dy};
  }
  if (lp) {
    look_delta = {lp->x - look_last_.x, lp->y - look_last_.y};
    look_moved_ += std::fabs(look_delta.x) + std::fabs(look_delta.y);
    look_last_ = {lp->x, lp->y};
  }
}

} // namespace tulpar::engine::app
