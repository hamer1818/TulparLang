#include "audio/spatial.hpp"

#include <cmath>

namespace tulpar::engine::audio {

SpatialGains spatial_compute(const SpatialListener &listener, Vec3 source_pos, float occlusion,
                             float min_distance, float max_distance) {
  SpatialGains g;
  const Vec3 to_source = source_pos - listener.pos;
  const float dist = length(to_source);

  // Esit-guc (equal-power) pan: sag ekseniyle izdusum [-1,1]'e, sonra
  // [0, PI/2]'ye eslenip cos/sin ile guc korunur (left^2+right^2 == 1).
  // dist==0 (kaynak==dinleyici) => normalize() {0,0,0} doner => pan 0 (merkez).
  float pan = dot(normalize(to_source), listener.right);
  if (pan < -1.0f) pan = -1.0f;
  if (pan > 1.0f) pan = 1.0f;
  const float theta = (pan + 1.0f) * (kPi * 0.25f);
  g.left = std::cos(theta);
  g.right = std::sin(theta);

  // Ters-kare mesafe azalimi (renderer'daki nokta isik dususuyle ayni fizik,
  // Is 3 -- mesh.frag). min_distance altinda 1.0'da kilitlenir; max_distance
  // ve otesinde SERT kesim (0) -- kasitli: yumusak sonme yerine acik bir
  // sinir, test edilmesi/akil yurutulmesi kolay olsun diye.
  if (dist >= max_distance) {
    g.distance_atten = 0.0f;
  } else {
    const float d = dist < min_distance ? min_distance : dist;
    g.distance_atten = (min_distance * min_distance) / (d * d);
  }

  // Okluzyon -> alcak-gecis katsayisi ONERISI (filtreyi cagiran uygular).
  // 0.1 tabani: alpha'nin sifira gitmesi (neredeyse tam sessizlik/DC) yerine
  // hep bir miktar sinyal gecsin -- "tam engelli" bile duyulabilir kalsin.
  float lp = 1.0f - occlusion;
  if (lp < 0.1f) lp = 0.1f;
  if (lp > 1.0f) lp = 1.0f;
  g.lowpass_alpha = lp;

  return g;
}

} // namespace tulpar::engine::audio
