#include "content/scene_runtime.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "content/gltf.hpp"
#include "content/hash.hpp"

namespace tulpar::engine::content {

namespace {
Vec3 v3(const float *f) { return {f[0], f[1], f[2]}; }
Quat q4(const float *f) { return {f[0], f[1], f[2], f[3]}; }
} // namespace

bool SceneRuntime::init(Arena &arena, renderer::Renderer &r, const SceneBlobView &view, const char *dir) {
  view_ = view;
  gi_.init(view); // probe yoksa ok()==false doner, apply_world duz ambient'a duser
  stats_ = SceneRuntimeStats{};
  bodies_live_ = false;
  body_ids_ = view.h->body_count ? arena.alloc_array<sim::BodyId>(view.h->body_count) : nullptr;
  if (view.h->body_count && !body_ids_) return false;
  for (uint32_t i = 0; i < view.h->body_count; i++) body_ids_[i] = sim::BodyId{};
  pose_scratch_ = view.h->anim_count ? arena.alloc_array<PoseScratch>(1) : nullptr;
  if (view.h->anim_count && !pose_scratch_) return false;
  char path[1024];
  for (uint32_t i = 0; i < view.h->asset_count && i < kSceneMaxAssets; i++) {
    std::snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", view.asset_path(i));
    models_[i] = Model{};
    ups_[i] = UploadedModel{};
    have_[i] = gltf_load(arena, path, &models_[i]) && upload_model(r, arena, models_[i], &ups_[i]);
    if (have_[i]) stats_.assets_loaded++;
    else { stats_.assets_failed++; std::printf("[scene_runtime] kaynak yuklenemedi: %s (%s)\n", path, models_[i].error); }
  }
  return true;
}

void SceneRuntime::apply_world(renderer::Renderer &r) const {
  const SceneWorld w = view_.world();
  Vec3 ambient = w.ambient;
  if (gi_.ok()) {
    // Kaba ornek: sahne AABB'sinin ortasi, yukari bakan normal. Per-pixel
    // DEGIL (Tier 2 takip planinda) -- yine de duz sabitten daha dogru, ve
    // bake yoksa (ok()==false) bu dal hic girilmez, eski deger korunur.
    const Vec3 lo{view_.h->bounds_lo[0], view_.h->bounds_lo[1], view_.h->bounds_lo[2]};
    const Vec3 hi{view_.h->bounds_hi[0], view_.h->bounds_hi[1], view_.h->bounds_hi[2]};
    ambient = gi_.sample((lo + hi) * 0.5f, {0, 1, 0});
  }
  r.set_light(normalize(w.sun_dir), ambient, w.sun_diffuse);
  r.set_shadow_volume(w.shadow_center, w.shadow_radius, w.shadow_depth);
}

uint32_t SceneRuntime::spawn(sim::Physics &ph) {
  if (bodies_live_) return stats_.bodies;
  uint32_t n = 0;
  for (uint32_t i = 0; i < view_.h->body_count; i++) {
    const SceneBlobBody &b = view_.bodies[i];
    if (b.shape == (uint32_t)SceneShape::Box) body_ids_[i] = ph.add_box(v3(b.half), v3(b.pos), q4(b.quat), b.dynamic != 0);
    else body_ids_[i] = ph.add_sphere(b.radius, v3(b.pos), b.dynamic != 0);
    if (body_ids_[i].valid()) n++;
  }
  bodies_live_ = true;
  stats_.bodies = n;
  return n;
}
void SceneRuntime::despawn(sim::Physics &ph) {
  if (!bodies_live_) return;
  for (uint32_t i = 0; i < view_.h->body_count; i++) {
    if (body_ids_[i].valid()) ph.remove(body_ids_[i]);
    body_ids_[i] = sim::BodyId{};
  }
  bodies_live_ = false;
  stats_.bodies = 0;
}

Mat4 SceneRuntime::entity_matrix(uint32_t i, const sim::Physics *ph) const {
  const SceneBlobEntity &e = view_.entities[i];
  if (ph && bodies_live_ && e.body >= 0) {
    const SceneBlobBody &b = view_.bodies[e.body];
    const sim::BodyId id = body_ids_[e.body];
    if (b.dynamic && id.valid()) return Mat4::translate(ph->position(id)) * to_mat4(ph->rotation(id)) * Mat4::scale(v3(b.scale));
  }
  return view_.entity_matrix(i);
}

sim::BodyId SceneRuntime::entity_body(uint32_t i) const {
  if (!bodies_live_ || i >= view_.h->entity_count) return sim::BodyId{};
  const int32_t b = view_.entities[i].body;
  if (b < 0 || (uint32_t)b >= view_.h->body_count) return sim::BodyId{};
  return body_ids_[b];
}

bool SceneRuntime::entity_dynamic(uint32_t i) const {
  if (i >= view_.h->entity_count) return false;
  const int32_t b = view_.entities[i].body;
  return b >= 0 && (uint32_t)b < view_.h->body_count && view_.bodies[b].dynamic != 0;
}

void SceneRuntime::draw(renderer::Renderer &r, Vec3 cam_pos, float time_s, const sim::Physics *ph) {
  stats_.draws = 0;
  stats_.lights = 0;
  for (uint32_t k = 0; k <= kModelMaxLods; k++) stats_.lod[k] = 0;
  ModelLod lod;
  lod.camera_pos = cam_pos;
  lod.distance1 = 24.0f;
  lod.distance2 = 34.0f;
  for (uint32_t i = 0; i < view_.h->draw_count; i++) {
    const SceneBlobDraw &d = view_.draws[i];
    if (d.asset >= kSceneMaxAssets || !have_[d.asset]) continue;
    const Model &mdl = models_[d.asset];
    const UploadedModel &up = ups_[d.asset];
    const SceneBlobEntity &e = view_.entities[d.entity];
    const Mat4 m = entity_matrix(d.entity, ph);
    bool drew = false;
    if (e.anim >= 0 && pose_scratch_) {
      const SceneBlobAnim &a = view_.anims[e.anim];
      if (a.clip < mdl.clip_count) {
        const float dur = mdl.clips[a.clip].duration;
        float t = std::fmod(time_s * a.speed + a.phase, 2.0f * dur); // ileri-geri (editorle ayni)
        if (t > dur) t = 2.0f * dur - t;
        ModelPose pose;
        if (model_pose_evaluate(mdl, a.clip, t, *pose_scratch_, &pose)) {
          draw_model(r, mdl, up, m, v3(d.tint), nullptr, nullptr, &pose);
          drew = true;
        }
      }
    }
    if (!drew) draw_model(r, mdl, up, m, v3(d.tint), &lod, stats_.lod);
    stats_.draws++;
  }
  for (uint32_t i = 0; i < view_.h->light_count; i++) {
    const SceneBlobLight &l = view_.lights[i];
    renderer::PointLight pl;
    const Mat4 m = entity_matrix(l.entity, ph);
    pl.pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
    pl.radius = l.radius;
    pl.color = v3(l.color);
    pl.intensity = l.intensity;
    if (r.add_point_light(pl)) stats_.lights++;
  }
}


// ===========================================================================
// SceneNav — blob'daki bake edilmis navmesh'in sorgu yuzu. Burada Recast YOK:
// veri sahne derleyicisinde (scene_compile) uretildi, runtime yalniz Detour ile
// sorguluyor. Arama kutusu / filtre / kismi yol islemi sim::NavMesh::find_path
// ile bire bir ayni — kapi (test_scene_blob) ikisinin AYNI yolu verdigini olcer.
// ===========================================================================
struct SceneNav::Impl {
  dtQueryFilter filter;
};

bool SceneNav::init(Arena &arena, const SceneBlobView &view, int max_nodes) {
  shutdown();
  if (!view.h || !view.has_nav()) return false;
  const uint32_t n = view.h->nav_size;
  // Detour tile'a baglanti kurarken veriyi DEGISTIRIR; blob salt okunur
  // olabilir (mmap'li pack), o yuzden tek seferlik kopya.
  void *data = arena.alloc(n, kSceneBlobAlign);
  void *impl_mem = arena.alloc(sizeof(Impl), alignof(Impl));
  if (!data || !impl_mem) return false;
  std::memcpy(data, view.nav, n);
  impl_ = new (impl_mem) Impl(); // alloc kurucu calistirmaz (Tuzaklar 8u)
  dtNavMesh *mesh = dtAllocNavMesh();
  if (!mesh) return false;
  // Bayrak 0: veri arenanin, Detour serbest birakmaz.
  if (dtStatusFailed(mesh->init(static_cast<unsigned char *>(data), (int)n, 0))) {
    dtFreeNavMesh(mesh);
    return false;
  }
  dtNavMeshQuery *q = dtAllocNavMeshQuery();
  if (!q || dtStatusFailed(q->init(mesh, max_nodes))) {
    if (q) dtFreeNavMeshQuery(q);
    dtFreeNavMesh(mesh);
    return false;
  }
  impl_->filter.setIncludeFlags(0xFFFF);
  impl_->filter.setExcludeFlags(0);
  mesh_ = mesh;
  query_ = q;
  polys_ = view.h->nav_polys;
  // Ozet init SONRASI alinir: dtNavMesh baglantilari tile verisinin ICINE yazar
  // (sim::NavMesh::data_hash da ayni noktadaki veriyi ozetler) — iki tarafin
  // bit-esitligi ancak boyle karsilastirilabilir.
  data_hash_ = content_fnv1a(data, n);
  return true;
}

void SceneNav::shutdown() {
  if (query_) dtFreeNavMeshQuery(static_cast<dtNavMeshQuery *>(query_));
  if (mesh_) dtFreeNavMesh(static_cast<dtNavMesh *>(mesh_));
  query_ = nullptr;
  mesh_ = nullptr;
  impl_ = nullptr; // arena bellegi: cagiranin
  polys_ = 0;
}

bool SceneNav::nearest_point(Vec3 p, Vec3 *out) const {
  if (!query_) return false;
  auto *q = static_cast<dtNavMeshQuery *>(query_);
  const float ext[3] = {2.0f, 4.0f, 2.0f};
  const float pos[3] = {p.x, p.y, p.z};
  dtPolyRef ref = 0;
  float nearest[3];
  if (dtStatusFailed(q->findNearestPoly(pos, ext, &impl_->filter, &ref, nearest)) || !ref) return false;
  *out = Vec3{nearest[0], nearest[1], nearest[2]};
  return true;
}

// sim::NavMesh::raycast'in birebir aynisi — SceneNav blob'daki ayni Detour
// verisini sorguladigi icin iki yolun ayni cevabi vermesi bir KAPI ile
// olculuyor (nav_scene_matches_sim). Kod kopyasi bilincli: iki sinif ayri
// katmanlarda (sim L2 / content L6) ve birbirine bagimli degil.
bool SceneNav::raycast(Vec3 from, Vec3 to, float *t_hit) const {
  if (t_hit) *t_hit = 1.0f;
  if (!query_) return false;
  auto *q = static_cast<dtNavMeshQuery *>(query_);
  const float ext[3] = {2.0f, 4.0f, 2.0f};
  const float s[3] = {from.x, from.y, from.z};
  const float e[3] = {to.x, to.y, to.z};
  dtPolyRef sref = 0;
  float sp[3];
  if (dtStatusFailed(q->findNearestPoly(s, ext, &impl_->filter, &sref, sp)) || !sref) return false;
  float t = 0, norm[3];
  dtPolyRef path[64];
  int npath = 0;
  if (dtStatusFailed(q->raycast(sref, sp, e, &impl_->filter, &t, norm, path, &npath, 64))) return false;
  if (t_hit) *t_hit = t > 1.0f ? 1.0f : t;
  return t < 1.0f; // t >= 1 (FLT_MAX): engel yok
}

int SceneNav::find_path(Vec3 from, Vec3 to, Vec3 *out, int max_points, bool *partial) const {
  if (!query_) return 0;
  auto *q = static_cast<dtNavMeshQuery *>(query_);
  const float ext[3] = {2.0f, 4.0f, 2.0f};
  const float s[3] = {from.x, from.y, from.z};
  const float e[3] = {to.x, to.y, to.z};
  dtPolyRef sref = 0, eref = 0;
  float sp[3], ep[3];
  q->findNearestPoly(s, ext, &impl_->filter, &sref, sp);
  q->findNearestPoly(e, ext, &impl_->filter, &eref, ep);
  if (!sref || !eref) return 0;
  dtPolyRef polys[256];
  int npolys = 0;
  const dtStatus st = q->findPath(sref, eref, sp, ep, &impl_->filter, polys, &npolys, 256);
  if (dtStatusFailed(st) || npolys == 0) return 0;
  bool part = (st & DT_PARTIAL_RESULT) != 0;
  float end[3] = {ep[0], ep[1], ep[2]};
  if (polys[npolys - 1] != eref) {
    q->closestPointOnPoly(polys[npolys - 1], ep, end, nullptr);
    part = true;
  }
  float straight[64 * 3];
  unsigned char flags[64];
  dtPolyRef refs[64];
  int n = 0;
  const int cap = max_points < 64 ? max_points : 64;
  q->findStraightPath(sp, end, polys, npolys, straight, flags, refs, &n, cap, 0);
  for (int i = 0; i < n; i++) out[i] = Vec3{straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]};
  if (partial) *partial = part;
  return n;
}

} // namespace tulpar::engine::content
