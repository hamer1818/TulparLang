#include "content/primitives.hpp"

namespace tulpar::engine::content {

namespace {

using renderer::Renderer;
using renderer::Vertex;

// Ureticiler indeks sayisi donduruyor; create_mesh tepe sayisini da istiyor.
// En buyuk indeks + 1 tam olarak yazilan tepe sayisidir (ureticiler tepeleri
// sirayla yazar ve hepsini en az bir kez indeksler).
uint32_t verts_used(const uint32_t *idx, uint32_t n) {
  uint32_t m = 0;
  for (uint32_t i = 0; i < n; i++)
    if (idx[i] + 1u > m) m = idx[i] + 1u;
  return m;
}

} // namespace

void build_primitive_meshes(renderer::Renderer &r, renderer::MeshHandle *out) {
  if (!out) return;
  for (uint32_t i = 0; i < kPrimitiveSlotCount; i++) out[i] = renderer::MeshHandle{};

  // TEK gecici tampon, `static`: ~640 tepe * 32 B + 3328 indeks * 4 B ~ 33 KB.
  // Yiginda degil cunku bazi hostlarda ana yigin 1 MB; heap'te de degil cunku
  // AllocGate global `new`i sayiyor ve bu yol ayirma yapmamali. Fonksiyon
  // yukleme aninda TEK SEFER ve tek is parcacigindan cagriliyor.
  static Vertex v[Renderer::kPrimitiveMaxVerts];
  static uint32_t idx[Renderer::kPrimitiveMaxIndices];

  uint32_t n = 0;

  // Kup: parcacik yuvasiyla AYNI mesh'i paylasiyor — ayni geometri icin iki
  // GPU tamponu ayirmanin anlami yok.
  n = Renderer::cube(v, idx);
  const renderer::MeshHandle cube_mesh = r.create_mesh(v, verts_used(idx, n), idx, n);
  out[kPrimCube] = cube_mesh;
  out[kPrimParticle] = cube_mesh;

  n = Renderer::plane(v, idx);
  out[kPrimPlane] = r.create_mesh(v, verts_used(idx, n), idx, n);

  n = Renderer::sphere(v, idx);
  out[kPrimSphere] = r.create_mesh(v, verts_used(idx, n), idx, n);

  n = Renderer::capsule(v, idx);
  out[kPrimCapsule] = r.create_mesh(v, verts_used(idx, n), idx, n);

  n = Renderer::cylinder(v, idx);
  out[kPrimCylinder] = r.create_mesh(v, verts_used(idx, n), idx, n);

  n = Renderer::cone(v, idx);
  out[kPrimCone] = r.create_mesh(v, verts_used(idx, n), idx, n);

  n = Renderer::quad(v, idx);
  out[kPrimQuad] = r.create_mesh(v, verts_used(idx, n), idx, n);

  n = Renderer::torus(v, idx);
  out[kPrimTorus] = r.create_mesh(v, verts_used(idx, n), idx, n);
}

} // namespace tulpar::engine::content
