#include "sim/anticheat.hpp"

namespace tulpar::engine::sim {

SpeedCheckResult speed_check(Vec3 prev_pos, Vec3 new_pos, float dt, float max_speed) {
  SpeedCheckResult r;
  if (dt <= 0.0f) return r;
  const float dist = length(new_pos - prev_pos);
  r.implied_speed = dist / dt;
  r.suspicious = r.implied_speed > max_speed;
  return r;
}

} // namespace tulpar::engine::sim
