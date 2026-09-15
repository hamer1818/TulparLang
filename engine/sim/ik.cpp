#include "sim/ik.hpp"

#include <cmath>

namespace tulpar::engine::sim {

TwoBoneIkResult solve_two_bone_ik(Vec3 root, Vec3 target, Vec3 pole, float len1, float len2) {
  TwoBoneIkResult r;
  const float min_reach = std::fabs(len1 - len2);
  const float max_reach = len1 + len2;

  Vec3 d = target - root;
  float dist = length(d);

  // Yon (axis1): kok->hedef. Hedef koke COK yakinsa (dist~0) yon tanimsiz --
  // pole'u yedek yon olarak kullan (o da tanimsizsa +Z sabit yedek).
  Vec3 axis1;
  if (dist < 1e-6f) {
    Vec3 fallback = pole - root;
    float flen = length(fallback);
    axis1 = flen > 1e-6f ? fallback * (1.0f / flen) : Vec3{0, 0, 1};
  } else {
    axis1 = d * (1.0f / dist);
  }

  // Kosinus teoremi ACOS'un tanim disina cikmamasi icin dist'i [min_reach,
  // max_reach] araligina KIRP (yalniz ACI hesabinda -- end pozisyonu asagida
  // HAM dist ile ayrica clamp'lenir, boylece end tam olarak dogru sinira
  // oturur, acos hassasiyetinden ETKILENMEZ).
  const float eps = 1e-4f;
  float dist_for_angle = dist;
  if (dist_for_angle < min_reach + eps) dist_for_angle = min_reach + eps;
  if (dist_for_angle > max_reach - eps) dist_for_angle = max_reach - eps;

  float cos_a = (len1 * len1 + dist_for_angle * dist_for_angle - len2 * len2) /
                (2.0f * len1 * dist_for_angle);
  cos_a = cos_a < -1.0f ? -1.0f : (cos_a > 1.0f ? 1.0f : cos_a);
  const float angle_a = std::acos(cos_a);
  const float sin_a = std::sin(angle_a);

  // Bukulme yonu: pole'un axis1'e DIK bileseni (Gram-Schmidt). Pole tam
  // axis1 uzerindeyse (belirsiz) herhangi bir dik yon uretilir.
  Vec3 pole_dir = pole - root;
  Vec3 bend = pole_dir - axis1 * dot(pole_dir, axis1);
  float bend_len = length(bend);
  if (bend_len < 1e-6f) {
    Vec3 arbitrary = std::fabs(axis1.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    bend = cross(axis1, arbitrary);
    bend_len = length(bend);
  }
  bend = bend * (1.0f / bend_len);

  // axis1, bend ORTONORMAL oldugu icin |mid-root| = len1 TAM OLARAK (clamp'ten
  // BAGIMSIZ, cos^2+sin^2=1 kimligi geregi) -- yalniz ACI (bukulme miktari)
  // dist_for_angle'in ne kadar kirpildigina bagli.
  r.mid = root + axis1 * (len1 * cos_a) + bend * (len1 * sin_a);

  const float end_dist = dist < min_reach ? min_reach : (dist > max_reach ? max_reach : dist);
  r.end = root + axis1 * end_dist;
  r.reached = (dist >= min_reach - eps) && (dist <= max_reach + eps);
  return r;
}

} // namespace tulpar::engine::sim
