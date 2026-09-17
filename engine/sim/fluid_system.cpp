#include "sim/fluid_system.hpp"

#include <cmath>

namespace tulpar::engine::sim {

namespace {
constexpr float kEps = 1e-9f;
} // namespace

float sph_poly6(float r, float h) {
  if (h <= 0.0f || r < 0.0f || r > h) return 0.0f;
  const float h2 = h * h;
  const float d = h2 - r * r;
  const float h9 = h2 * h2 * h2 * h2 * h; // h^9
  return (315.0f / (64.0f * kPi * h9)) * d * d * d;
}

float sph_spiky_grad_magnitude(float r, float h) {
  if (h <= 0.0f || r < 0.0f || r > h) return 0.0f;
  const float h3 = h * h * h;
  const float h6 = h3 * h3;
  const float d = h - r;
  return (45.0f / (kPi * h6)) * d * d;
}

float sph_viscosity_laplacian(float r, float h) {
  if (h <= 0.0f || r < 0.0f || r > h) return 0.0f;
  const float h3 = h * h * h;
  const float h6 = h3 * h3;
  return (45.0f / (kPi * h6)) * (h - r);
}

void sph_compute_density(SphParticle *particles, uint32_t count, const SphParams &params) {
  if (!particles || count == 0) return;
  const float h = params.smoothing_radius;

  for (uint32_t i = 0; i < count; i++) {
    float density = 0.0f;
    for (uint32_t j = 0; j < count; j++) {
      // j == i DAHIL: parcacik KENDI yogunluguna da katkida bulunur
      // (W(0,h) > 0) -- Muller 2003'un formulu boyle, atlanirsa yogunluk
      // sistematik olarak DUSUK cikar ve akiskan cokerdi.
      const float r = length(particles[j].pos - particles[i].pos);
      density += params.particle_mass * sph_poly6(r, h);
    }
    particles[i].density = density;
    // Durum denklemi (Desbrun'un gaz denkleminin SIKISTIRILAMAZ-yaklasik
    // hali): basinc yalniz DINLENME yogunlugundan SAPMAYLA orantili.
    particles[i].pressure = params.stiffness * (density - params.rest_density);
  }
}

void sph_step(SphParticle *particles, uint32_t count, const SphParams &params, float dt,
              Vec3 *accel_scratch) {
  if (!particles || !accel_scratch || count == 0 || dt <= 0.0f) return;
  const float h = params.smoothing_radius;

  // FAZ 1: tum ivmeler -- HICBIR parcacik henuz guncellenmedi, bu yuzden
  // her i ayni (tutarli) durumu gorur.
  for (uint32_t i = 0; i < count; i++) {
    Vec3 f_pressure{0, 0, 0};
    Vec3 f_viscosity{0, 0, 0};

    for (uint32_t j = 0; j < count; j++) {
      if (j == i) continue; // kendi uzerine kuvvet YOK (r=0'da yon tanimsiz)
      const Vec3 rij = particles[i].pos - particles[j].pos;
      const float r = length(rij);
      if (r > h || r < kEps) continue;
      if (particles[j].density < kEps) continue; // henuz yogunluk hesaplanmamis/bos

      const Vec3 dir = rij * (1.0f / r);

      // Basinc: simetriklestirilmis (p_i+p_j)/2 -- Newton'un 3. yasasini
      // korur (aksi halde sistem kendi kendine ivmelenir).
      const float shared_p = (particles[i].pressure + particles[j].pressure) * 0.5f;
      f_pressure -= dir * (params.particle_mass * shared_p / particles[j].density *
                           sph_spiky_grad_magnitude(r, h));

      // Viskozite: hiz FARKINA orantili -> sistemden enerji ALIR, eklemez.
      const Vec3 dv = particles[j].vel - particles[i].vel;
      f_viscosity += dv * (params.viscosity * params.particle_mass / particles[j].density *
                           sph_viscosity_laplacian(r, h));
    }

    const float rho = particles[i].density > kEps ? particles[i].density : params.rest_density;
    accel_scratch[i] = (f_pressure + f_viscosity) * (1.0f / rho) + params.gravity;
  }

  // FAZ 2: hepsini BIRDEN ilerlet. Yari-kapali (semi-implicit) Euler --
  // content/particles.hpp ile AYNI sira: once hiz, SONRA o yeni hizla konum.
  for (uint32_t i = 0; i < count; i++) {
    particles[i].vel += accel_scratch[i] * dt;
    particles[i].pos += particles[i].vel * dt;
  }
}

} // namespace tulpar::engine::sim
