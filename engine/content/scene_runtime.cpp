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

// Cizim kaydindaki malzeme alanlari -> renderer::PbrParams. DIKKAT: PbrParams'ta
// `emissive_strength` YOK (emissive dogrudan dogrusal terim olarak ekleniyor),
// o yuzden siddet burada renge CARPILIR — degeri ayri bir alanda tasiyip
// kullanmamak sessiz veri kaybi olurdu.
renderer::PbrParams pbr_of(const SceneBlobDraw &d) {
  renderer::PbrParams p;
  p.metallic = d.metallic;
  p.roughness = d.roughness;
  p.reflectance = d.reflectance;
  p.emissive = Vec3{d.emissive[0], d.emissive[1], d.emissive[2]} * d.emissive_strength;
  return p;
}

} // namespace

// --- v6 prosedurel mesh ureticileri -----------------------------------------
// Hepsi YUKLEME aninda, cagiranin arenasindan GECICI yer alarak calisir
// (mark/reset_to): mesh GPU'ya gittikten sonra CPU tarafi tamponlara ihtiyac
// yok. std::malloc DEGIL — motorun tek bellek kaynagi arena, ve AllocGate
// global ayirmalari sayiyor.
//
// ISIMSIZ ALANIN DISINDALAR (bildirimleri scene_runtime.hpp'de): EDITOR de
// ayni ucunu cagirir. Editor canli SceneDesc'ten onizleme cizer, SceneRuntime
// derlenmis blobtan; ikisi AYNI geometriyi uretmezse editorde gorulen sey
// oyunda cikan sey olmaz. PR #331 bu ureticileri editore ELLE kopyalamisti ve
// kopyadaki gerstner formulu YANLISTI (k * theta iki kez uygulaniyordu), yani
// editorun suyu derlenmis sahnenin suyundan baska dalgalaniyordu.

renderer::MeshHandle make_terrain_mesh(Arena &tmp, renderer::Renderer &r, const HeightmapConfig &cfg) {
  if (cfg.width < 2 || cfg.height < 2) return renderer::MeshHandle{};
  const uint32_t nverts = cfg.width * cfg.height;
  float *heights = tmp.alloc_array<float>(nverts);
  renderer::Vertex *verts = tmp.alloc_array<renderer::Vertex>(nverts);
  if (!heights || !verts) return renderer::MeshHandle{};
  generate_heightmap(cfg, heights);
  for (uint32_t z = 0; z < cfg.height; z++) {
    for (uint32_t x = 0; x < cfg.width; x++) {
      const uint32_t i = z * cfg.width + x;
      verts[i].pos = {(float)x * cfg.cell_size, heights[i], (float)z * cfg.cell_size};
      verts[i].uv = {(float)x / (float)(cfg.width - 1), (float)z / (float)(cfg.height - 1)};
      verts[i].nrm = {0, 1, 0}; // kenar halkasi: merkezi fark yok, yukari
    }
  }
  // Merkezi fark (Sobel degil): komsu yukseklik farkindan egim. Kenarlar
  // disarida birakildi cunku disarida komsu YOK; orada duz yukari kalir.
  for (uint32_t z = 1; z + 1 < cfg.height; z++) {
    for (uint32_t x = 1; x + 1 < cfg.width; x++) {
      const float hl = heights[z * cfg.width + x - 1], hr = heights[z * cfg.width + x + 1];
      const float hd = heights[(z - 1) * cfg.width + x], hu = heights[(z + 1) * cfg.width + x];
      verts[z * cfg.width + x].nrm = normalize(Vec3{hl - hr, 2.0f * cfg.cell_size, hd - hu});
    }
  }
  const uint32_t nindices = (cfg.width - 1) * (cfg.height - 1) * 6;
  uint32_t *indices = tmp.alloc_array<uint32_t>(nindices);
  if (!indices) return renderer::MeshHandle{};
  uint32_t k = 0;
  for (uint32_t z = 0; z + 1 < cfg.height; z++) {
    for (uint32_t x = 0; x + 1 < cfg.width; x++) {
      const uint32_t i0 = z * cfg.width + x, i1 = i0 + 1, i2 = (z + 1) * cfg.width + x, i3 = i2 + 1;
      indices[k++] = i0; indices[k++] = i2; indices[k++] = i1;
      indices[k++] = i1; indices[k++] = i2; indices[k++] = i3;
    }
  }
  return r.create_mesh(verts, nverts, indices, nindices);
}

// UYARI: blob'da voksel HUCRELERI yok, yalniz izgara boyu + hucre kenari
// (SceneBlobVoxel). Yani bu bir YER TUTUCU dolgu (izgaraya sigan kure) —
// gercek voksel verisi bicime girdiginde burasi onu okuyacak. Yer tutucu
// oldugu ACIKCA yazili olsun diye tek satirlik bir doldurma birakildi:
// sessizce bos bir mesh dondurmek "voksel calismiyor" hatasini gizlerdi.
renderer::MeshHandle make_voxel_mesh(Arena &tmp, renderer::Renderer &r, uint32_t nx, uint32_t ny, uint32_t nz, float cell) {
  if (!nx || !ny || !nz) return renderer::MeshHandle{};
  VoxelGrid grid;
  grid.nx = nx; grid.ny = ny; grid.nz = nz; grid.voxel_size = cell;
  uint8_t *cells = tmp.alloc_array<uint8_t>(nx * ny * nz);
  if (!cells) return renderer::MeshHandle{};
  const Vec3 c{nx * 0.5f, ny * 0.5f, nz * 0.5f};
  const float rad = nx * 0.5f;
  for (uint32_t z = 0; z < nz; z++)
    for (uint32_t y = 0; y < ny; y++)
      for (uint32_t x = 0; x < nx; x++)
        cells[(z * ny + y) * nx + x] = length(Vec3{(float)x - c.x, (float)y - c.y, (float)z - c.z}) < rad ? 1 : 0;
  grid.cells = cells;
  int16_t *mask = tmp.alloc_array<int16_t>(voxel_mesh_mask_capacity(grid));
  if (!mask) return renderer::MeshHandle{};
  const VoxelMeshCounts counts = count_voxel_mesh(grid, mask);
  if (counts.vertices == 0) return renderer::MeshHandle{};
  VoxelVertex *vv = tmp.alloc_array<VoxelVertex>(counts.vertices);
  uint32_t *indices = tmp.alloc_array<uint32_t>(counts.indices);
  renderer::Vertex *verts = tmp.alloc_array<renderer::Vertex>(counts.vertices);
  if (!vv || !indices || !verts) return renderer::MeshHandle{};
  build_voxel_mesh(grid, mask, vv, indices);
  for (uint32_t i = 0; i < counts.vertices; i++) {
    verts[i].pos = vv[i].pos;
    verts[i].nrm = vv[i].nrm;
    verts[i].uv = vv[i].uv;
  }
  return r.create_mesh(verts, counts.vertices, indices, counts.indices);
}

// Su yuzeyi: yer degistirme content/water_wave.hpp'nin DOGRULANMIS
// gerstner_displacement'indan geliyor (formulu burada yeniden turetmek, o
// dosyanin nokta nokta olculen testini atlamak olurdu). Normaller sonlu
// farkla: komsu tepelerin capraz carpimi — analitik turevi ikinci kez yazmak
// yerine tek kaynak (yer degistirme) uzerinden tutarli kalir.
renderer::MeshHandle make_water_mesh(Arena &tmp, renderer::Renderer &r, const GerstnerWave &wave) {
  constexpr uint32_t kN = 64;
  constexpr float kCell = 2.0f;
  const uint32_t nverts = kN * kN;
  renderer::Vertex *verts = tmp.alloc_array<renderer::Vertex>(nverts);
  if (!verts) return renderer::MeshHandle{};
  for (uint32_t z = 0; z < kN; z++) {
    for (uint32_t x = 0; x < kN; x++) {
      const float wx = ((float)x - kN * 0.5f) * kCell, wz = ((float)z - kN * 0.5f) * kCell;
      const Vec3 d = gerstner_displacement(wave, wx, wz, 0.0f);
      const uint32_t i = z * kN + x;
      verts[i].pos = {wx + d.x, d.y, wz + d.z};
      verts[i].uv = {(float)x / (float)(kN - 1), (float)z / (float)(kN - 1)};
      verts[i].nrm = {0, 1, 0};
    }
  }
  for (uint32_t z = 1; z + 1 < kN; z++) {
    for (uint32_t x = 1; x + 1 < kN; x++) {
      const Vec3 px = verts[z * kN + x + 1].pos - verts[z * kN + x - 1].pos;
      const Vec3 pz = verts[(z + 1) * kN + x].pos - verts[(z - 1) * kN + x].pos;
      Vec3 n = normalize(cross(pz, px));
      if (n.y < 0) n = n * -1.0f; // yukari bakan yari kure (dalga tepeleri kivrilmaz)
      verts[z * kN + x].nrm = n;
    }
  }
  const uint32_t nindices = (kN - 1) * (kN - 1) * 6;
  uint32_t *indices = tmp.alloc_array<uint32_t>(nindices);
  if (!indices) return renderer::MeshHandle{};
  uint32_t k = 0;
  for (uint32_t z = 0; z + 1 < kN; z++) {
    for (uint32_t x = 0; x + 1 < kN; x++) {
      const uint32_t i0 = z * kN + x, i1 = i0 + 1, i2 = (z + 1) * kN + x, i3 = i2 + 1;
      indices[k++] = i0; indices[k++] = i2; indices[k++] = i1;
      indices[k++] = i1; indices[k++] = i2; indices[k++] = i3;
    }
  }
  return r.create_mesh(verts, nverts, indices, nindices);
}

bool SceneRuntime::init(Arena &arena, renderer::Renderer &r, const SceneBlobView &view, const char *dir) {
  view_ = view;
  gi_.init(view); // probe yoksa ok()==false doner, apply_world duz ambient'a duser
  stats_ = SceneRuntimeStats{};
  bodies_live_ = false;
  // CIHAZ VAR MI? `init()` cagrilmamis bir Renderer gecerli bir nesnedir ama
  // GPU kaynagi yaratamaz. Motorun CPU tarafini olcen kapilar bilerek boyle
  // bir Renderer veriyor (test_scene_blob.cpp `scene_runtime_applies_baked_gi_ambient`
  // yalnizca `ambient()` degerini sinar). v6 ile bu fonksiyon ilkel mesh
  // tablosunu, prosedurel mesh'leri ve malzemeleri kurmaya basladi; kosulsuz
  // yapilinca o kapi `fault_addr: 0x48` ile cokuyordu (olculdu 2026-09-19) ve
  // yigin izi GI'yi isaret ettigi icin sebep tamamen yanlis yerde aranirdi.
  // Cihazsiz kosumda GPU tarafi ATLANIR, CPU tarafi (govdeler, parcacik havuzu,
  // GI, tutamac tablolarinin sifirlanmasi) aynen kurulur.
  const bool gpu = r.ready();
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
    have_[i] = gpu && gltf_load(arena, path, &models_[i]) && upload_model(r, arena, models_[i], &ups_[i]);
    if (have_[i]) stats_.assets_loaded++;
    else if (!gpu) stats_.assets_failed++;  // cihaz yok: sayilir, suclanacak dosya yok
    else { stats_.assets_failed++; std::printf("[scene_runtime] kaynak yuklenemedi: %s (%s)\n", path, models_[i].error); }
  }

  // --- v6: prosedurel bilesenler -------------------------------------------
  // Parcacik havuzu arenadan; init sonrasi ayirma yok (particles.hpp sozlesmesi).
  if (!particles_.init(arena, 4096, Vec3{0, -9.8f, 0})) return false;
  particle_seed_ = 0;
  // Ilkel mesh tablosu SAHNE BASINA DEGIL, renderer basina bir kez: kopru
  // (eng_scene_load) ayni SceneRuntime'i bolum gecislerinde yeniden init
  // ediyor; kosulsuz kurmak her gecis icin 8 GPU mesh sizdirirdi.
  if (gpu && !prims_[kPrimCube].valid()) build_primitive_meshes(r, prims_);
  // Onceki sahnenin mesh'leri bu sahnede GECERSIZ: tutamaclari temizle, yoksa
  // yeni sahnede o varlik indeksinde eski arazi cizilir.
  for (uint32_t i = 0; i < kSceneMaxEntities; i++) {
    terrain_meshes_[i] = renderer::MeshHandle{};
    voxel_meshes_[i] = renderer::MeshHandle{};
    water_meshes_[i] = renderer::MeshHandle{};
    entity_mats_[i] = renderer::MaterialHandle{};
  }

  // Buradan asagisi TAMAMEN GPU: prosedurel mesh'ler ve malzemeler. Cihazsiz
  // kosumda sahne CPU tarafiyla kurulmus sayilir (yukaridaki her sey yapildi).
  if (!gpu) return true;

  // Prosedurel mesh'ler: her biri arenanin GECICI ucundan calisir, sonra geri
  // sarilir — kalici olan yalniz GPU tarafi.
  const size_t tmp_mark = arena.mark();
  for (uint32_t i = 0; i < view.h->terrain_count; i++) {
    const SceneBlobTerrain &t = view.terrains[i];
    HeightmapConfig cfg;
    cfg.width = (uint32_t)t.width; cfg.height = (uint32_t)t.height;
    cfg.cell_size = t.cell; cfg.amplitude = t.amp; cfg.frequency = t.freq;
    cfg.octaves = t.octaves; cfg.seed = t.seed;
    terrain_meshes_[t.entity] = make_terrain_mesh(arena, r, cfg);
    arena.reset_to(tmp_mark);
  }
  for (uint32_t i = 0; i < view.h->voxel_count; i++) {
    const SceneBlobVoxel &v = view.voxels[i];
    voxel_meshes_[v.entity] = make_voxel_mesh(arena, r, v.size_x, v.size_y, v.size_z, v.cell);
    arena.reset_to(tmp_mark);
  }
  for (uint32_t i = 0; i < view.h->water_count; i++) {
    const SceneBlobWater &w = view.waters[i];
    GerstnerWave wave;
    wave.steepness = w.steepness; wave.amplitude = w.amplitude; wave.wavelength = w.wavelength;
    wave.direction = {w.direction[0], w.direction[1]};
    wave.speed = w.speed;
    water_meshes_[w.entity] = make_water_mesh(arena, r, wave);
    arena.reset_to(tmp_mark);
  }

  // Malzemeler de YUKLEME aninda: create_material kare icinde cagrilirsa
  // "kare basina 0 ayirma" kapisi duser.
  for (uint32_t i = 0; i < view.h->draw_count; i++) {
    const SceneBlobDraw &d = view.draws[i];
    if (d.primitive < 0 || d.entity >= kSceneMaxEntities) continue;
    entity_mats_[d.entity] = r.create_material(r.default_texture(), Vec3{1, 1, 1}, pbr_of(d));
  }
  if (view.h->terrain_count) {
    renderer::PbrParams p; p.metallic = 0.1f; p.roughness = 0.9f;
    terrain_mat_ = r.create_material(r.default_texture(), Vec3{1, 1, 1}, p);
  }
  if (view.h->voxel_count) {
    renderer::PbrParams p; p.metallic = 0.0f; p.roughness = 0.5f;
    voxel_mat_ = r.create_material(r.default_texture(), Vec3{1, 1, 1}, p);
  }
  if (view.h->water_count) {
    renderer::PbrParams p; p.metallic = 0.1f; p.roughness = 0.1f;
    water_mat_ = r.create_material(r.default_texture(), Vec3{1, 1, 1}, p);
  }
  return true;
}

void SceneRuntime::update(float dt, const sim::Physics *ph) {
  if (!view_.particles || dt <= 0) return;
  // Tohum KARE SAYACI, saat degil: ayni sahne + ayni kare sayisi her
  // platformda AYNI parcacik dizisini verir (particles.hpp'nin determinizm
  // sozu bunun uzerine kurulu; dt'den tohum uretmek onu bozardi).
  Rng rng(++particle_seed_);
  for (uint32_t i = 0; i < view_.h->particle_count; i++) {
    const SceneBlobParticle &ep = view_.particles[i];
    if (!(ep.spawn_rate > 0)) continue;
    // Tam sayi kismi + kesirli kismin OLASILIGI: saniyede 200 parcacik isteyen
    // bir yayici 60 fps'te karede 3.33 dogurur. Tek bir "rastgele < oran"
    // testi kare basina en fazla 1 ile sinirlar ve yuksek hizlari sessizce kirpar.
    const float want = ep.spawn_rate * dt;
    uint32_t k = (uint32_t)want;
    if (rng.next_float() < want - (float)k) k++;
    if (!k) continue;
    const Mat4 m = entity_matrix(ep.entity, ph);
    ParticleEmitterConfig cfg;
    cfg.spawn_pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
    cfg.base_velocity = v3(ep.velocity);
    cfg.velocity_jitter = v3(ep.jitter);
    cfg.lifetime_min = ep.lifetime_min;
    cfg.lifetime_max = ep.lifetime_max;
    cfg.size_start = ep.size_start;
    cfg.size_end = ep.size_end;
    particles_.emit(cfg, k, rng);
  }
  particles_.update(dt);
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
    const SceneBlobEntity &e = view_.entities[d.entity];
    const Mat4 m = entity_matrix(d.entity, ph);
    // Prosedurel ilkel kaynaga TERCIH edilir: ikisi de doluysa varligi editorde
    // ilkel olarak gorduk demektir (bkz. content/primitives.hpp yuva tablosu).
    if (d.primitive >= 0 && (uint32_t)d.primitive < kPrimitiveSlotCount && prims_[d.primitive].valid() &&
        entity_mats_[d.entity].valid()) {
      r.draw(prims_[d.primitive], entity_mats_[d.entity], m, v3(d.tint));
      stats_.draws++;
      continue;
    }
    if (d.asset < 0 || (uint32_t)d.asset >= kSceneMaxAssets || !have_[d.asset]) continue;
    const Model &mdl = models_[d.asset];
    const UploadedModel &up = ups_[d.asset];
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
  // --- v6: prosedurel mesh'ler. Tablolar bossa hicbir sey degismez, yani
  // bilesensiz bir sahnenin kare cikisi v4 ile BIT-TAM ayni kalir.
  for (uint32_t i = 0; i < view_.h->terrain_count; i++) {
    const SceneBlobTerrain &t = view_.terrains[i];
    if (!terrain_meshes_[t.entity].valid()) continue;
    r.draw(terrain_meshes_[t.entity], terrain_mat_, entity_matrix(t.entity, ph), Vec3{0.7f, 0.7f, 0.7f});
    stats_.draws++;
  }
  for (uint32_t i = 0; i < view_.h->voxel_count; i++) {
    const SceneBlobVoxel &v = view_.voxels[i];
    if (!voxel_meshes_[v.entity].valid()) continue;
    r.draw(voxel_meshes_[v.entity], voxel_mat_, entity_matrix(v.entity, ph), Vec3{0.8f, 0.8f, 0.8f});
    stats_.draws++;
  }
  for (uint32_t i = 0; i < view_.h->water_count; i++) {
    const SceneBlobWater &w = view_.waters[i];
    if (!water_meshes_[w.entity].valid()) continue;
    r.draw(water_meshes_[w.entity], water_mat_, entity_matrix(w.entity, ph), Vec3{0.1f, 0.4f, 0.8f});
    stats_.draws++;
  }
  // Parcaciklar kup ilkeliyle (prims_[kPrimParticle]); malzeme varsayilan —
  // her parcacik icin ayri malzeme tutmak kare icinde ayirma demek olurdu.
  if (prims_[kPrimParticle].valid()) {
    for (uint32_t i = 0; i < particles_.alive_count(); i++) {
      const Particle &p = particles_.particle(i);
      r.draw(prims_[kPrimParticle], Mat4::translate(p.pos) * Mat4::scale({p.size, p.size, p.size}), Vec3{0.9f, 0.9f, 0.9f});
      stats_.draws++;
    }
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
