#include "content/scene_blob.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <DetourAlloc.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <RecastAlloc.h>

#include "content/cluster_dag.hpp"
#include "content/gi.hpp"
#include "content/gltf.hpp"
#include "content/hash.hpp"
#include "content/scene_compile.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::content {

namespace {
size_t align_up(size_t v) { return (v + kSceneBlobAlign - 1) & ~(size_t)(kSceneBlobAlign - 1); }
void put3(float *dst, Vec3 v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }
Vec3 get3(const float *src) { return {src[0], src[1], src[2]}; }

// Derleme plani: once boyutlar/ofsetler (tek gecis), sonra yazim. String
// tablosu: kaynak yollari, sonra varlik adlari, hepsi NUL sonlu, sirali.
struct Plan {
  uint32_t draws = 0, anims = 0, lights = 0, bodies = 0, strings = 0, residents = 0, nav = 0;
  uint32_t particles = 0, terrains = 0, waters = 0, winds = 0, voxels = 0, characters = 0;
  uint32_t dag_meshes = 0, dag_nodes = 0, dag_indices = 0, dag_children = 0, gi_probes = 0;
  size_t off_asset = 0, off_entity = 0, off_draw = 0, off_anim = 0, off_light = 0, off_body = 0, off_string = 0, off_resident = 0,
         off_particle = 0, off_terrain = 0, off_water = 0, off_wind = 0, off_voxel = 0, off_character = 0,
         off_nav = 0, off_dag_mesh = 0, off_dag_node = 0, off_dag_index = 0, off_dag_child = 0, off_gi = 0, total = 0;
};
Plan plan_of(const SceneDesc &d, const SceneBlobExtras *x) {
  Plan p;
  if (x) {
    p.residents = x->residents && x->resident_count ? x->resident_count : 0;
    p.nav = x->nav_data && x->nav_size ? x->nav_size : 0;
    // DAG: dort tablo da dolu olmali (cocuk tablosu bos olabilir: tek seviye).
    if (x->dag_meshes && x->dag_mesh_count && x->dag_nodes && x->dag_node_count && x->dag_indices && x->dag_index_count) {
      p.dag_meshes = x->dag_mesh_count;
      p.dag_nodes = x->dag_node_count;
      p.dag_indices = x->dag_index_count;
      p.dag_children = x->dag_children ? x->dag_child_count : 0;
    }
    // GI: izgara boyutu sonda sayisiyla tutarli olmali (bozuk girdi bolumu dusurur).
    if (x->gi_probes && x->gi_probe_count && x->gi_spacing > 0 &&
        (uint64_t)x->gi_dim[0] * x->gi_dim[1] * x->gi_dim[2] == (uint64_t)x->gi_probe_count)
      p.gi_probes = x->gi_probe_count;
  }
  for (uint32_t i = 0; i < d.asset_count; i++) p.strings += (uint32_t)std::strlen(d.assets[i]) + 1;
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const SceneEntity &e = d.entities[i];
    if (e.components & kSceneModel) p.draws++;
    if (e.components & kSceneAnim) p.anims++;
    if (e.components & kSceneLight) p.lights++;
    if (e.components & kSceneBody) p.bodies++;
    if (e.components & kSceneParticle) p.particles++;
    if (e.components & kSceneTerrain) p.terrains++;
    if (e.components & kSceneWater) p.waters++;
    if (e.components & kSceneWind) p.winds++;
    if (e.components & kSceneVoxel) p.voxels++;
    if (e.components & kSceneCharacter) p.characters++;
    p.strings += (uint32_t)std::strlen(e.name) + 1;
  }
  if (p.strings == 0) p.strings = 1; // en az bir NUL: ofset 0 her zaman gecerli
  size_t o = sizeof(SceneBlobHeader);
  p.off_asset = o; o = align_up(o + sizeof(SceneBlobAsset) * d.asset_count);
  p.off_entity = o; o = align_up(o + sizeof(SceneBlobEntity) * d.entity_count);
  p.off_draw = o; o = align_up(o + sizeof(SceneBlobDraw) * p.draws);
  p.off_anim = o; o = align_up(o + sizeof(SceneBlobAnim) * p.anims);
  p.off_light = o; o = align_up(o + sizeof(SceneBlobLight) * p.lights);
  p.off_body = o; o = align_up(o + sizeof(SceneBlobBody) * p.bodies);
  
  p.off_particle = o; o = align_up(o + sizeof(SceneBlobParticle) * p.particles);
  p.off_terrain = o; o = align_up(o + sizeof(SceneBlobTerrain) * p.terrains);
  p.off_water = o; o = align_up(o + sizeof(SceneBlobWater) * p.waters);
  p.off_wind = o; o = align_up(o + sizeof(SceneBlobWind) * p.winds);
  p.off_voxel = o; o = align_up(o + sizeof(SceneBlobVoxel) * p.voxels);
  p.off_character = o; o = align_up(o + sizeof(SceneBlobCharacter) * p.characters);

  p.off_string = o; o = align_up(o + p.strings);
  p.off_resident = o; o = align_up(o + sizeof(SceneBlobResident) * p.residents);
  p.off_nav = o; o = align_up(o + p.nav);
  p.off_dag_mesh = o; o = align_up(o + sizeof(SceneBlobDagMesh) * p.dag_meshes);
  p.off_dag_node = o; o = align_up(o + sizeof(SceneBlobDagNode) * p.dag_nodes);
  p.off_dag_index = o; o = align_up(o + sizeof(uint32_t) * p.dag_indices);
  p.off_dag_child = o; o = align_up(o + sizeof(uint32_t) * p.dag_children);
  p.off_gi = o; o = align_up(o + sizeof(SceneBlobGiProbe) * p.gi_probes);
  p.total = o;
  return p;
}

struct Err {
  SceneError *e;
  bool fail(const char *what) {
    if (e) { e->line = 0; std::snprintf(e->msg, sizeof e->msg, "sahne blob: %s", what); }
    return false;
  }
};
// Tablo [off, off + n*rec) blob icinde mi, 16 hizali mi?
bool table_ok(uint32_t off, uint32_t n, size_t rec, size_t total) {
  if (off % kSceneBlobAlign != 0) return false;
  const uint64_t end = (uint64_t)off + (uint64_t)n * rec;
  return off >= sizeof(SceneBlobHeader) && end <= total;
}
} // namespace

uint64_t scene_blob_fnv1a(const void *data, size_t n, uint64_t h) { return content_fnv1a(data, n, h); }

bool scene_blob_path_for(const char *scene_path, char *out, size_t cap) {
  const size_t n = std::strlen(scene_path);
  const char *ext = ".sahne";
  const size_t el = std::strlen(ext);
  int w;
  if (n >= el && std::strcmp(scene_path + n - el, ext) == 0) w = std::snprintf(out, cap, "%.*s.sahneb", (int)(n - el), scene_path);
  else w = std::snprintf(out, cap, "%s.sahneb", scene_path);
  return w > 0 && (size_t)w < cap;
}

size_t scene_blob_compile(const SceneDesc &d, void *buf, size_t cap) { return scene_blob_compile_ex(d, nullptr, buf, cap); }

size_t scene_blob_compile_ex(const SceneDesc &d, const SceneBlobExtras *x, void *buf, size_t cap) {
  const Plan p = plan_of(d, x);
  if (!buf || cap < p.total) return p.total;
  uint8_t *b = static_cast<uint8_t *>(buf);
  std::memset(b, 0, p.total); // dolgu ve rezerve alanlar 0: deterministik baytlar
  SceneBlobHeader h{};
  h.magic = kSceneBlobMagic; h.version = kSceneBlobVersion; h.total_size = (uint32_t)p.total; h.endian = kSceneBlobEndian;
  h.header_size = sizeof(SceneBlobHeader);
  put3(h.sun_dir, d.sun_dir); h.sun_diffuse = d.sun_diffuse;
  put3(h.ambient, d.ambient); h.shadow_radius = d.shadow_radius;
  put3(h.shadow_center, d.shadow_center); h.shadow_depth = d.shadow_depth;
  put3(h.cam_target, d.cam_target); h.cam_yaw = d.cam_yaw;
  h.cam_pitch = d.cam_pitch; h.cam_radius = d.cam_radius;
  h.asset_count = d.asset_count; h.asset_offset = (uint32_t)p.off_asset;
  h.entity_count = d.entity_count; h.entity_offset = (uint32_t)p.off_entity;
  h.draw_count = p.draws; h.draw_offset = (uint32_t)p.off_draw;
  h.anim_count = p.anims; h.anim_offset = (uint32_t)p.off_anim;
  h.light_count = p.lights; h.light_offset = (uint32_t)p.off_light;
  h.body_count = p.bodies; h.body_offset = (uint32_t)p.off_body;
  
  h.particle_count = p.particles; h.particle_offset = (uint32_t)p.off_particle;
  h.terrain_count = p.terrains; h.terrain_offset = (uint32_t)p.off_terrain;
  h.water_count = p.waters; h.water_offset = (uint32_t)p.off_water;
  h.wind_count = p.winds; h.wind_offset = (uint32_t)p.off_wind;
  h.voxel_count = p.voxels; h.voxel_offset = (uint32_t)p.off_voxel;
  h.character_count = p.characters; h.character_offset = (uint32_t)p.off_character;

  h.string_offset = (uint32_t)p.off_string; h.string_size = p.strings;
  h.resident_count = p.residents; h.resident_offset = (uint32_t)p.off_resident;
  h.nav_offset = (uint32_t)p.off_nav; h.nav_size = p.nav;
  h.dag_mesh_count = p.dag_meshes; h.dag_mesh_offset = (uint32_t)p.off_dag_mesh;
  h.dag_node_count = p.dag_nodes; h.dag_node_offset = (uint32_t)p.off_dag_node;
  h.dag_index_count = p.dag_indices; h.dag_index_offset = (uint32_t)p.off_dag_index;
  h.dag_child_count = p.dag_children; h.dag_child_offset = (uint32_t)p.off_dag_child;
  h.gi_probe_count = p.gi_probes; h.gi_probe_offset = (uint32_t)p.off_gi;
  if (x) {
    h.resident_cpu = x->resident_cpu; h.resident_gpu = x->resident_gpu;
    h.peak_transient = x->peak_transient; h.peak_bytes = x->peak_bytes;
    h.budget_flags = x->budget_flags;
    h.nav_polys = p.nav ? x->nav_polys : 0; h.nav_verts = p.nav ? x->nav_verts : 0;
    h.nav_tris = p.nav ? x->nav_tris : 0;
    h.nav_agent_radius = x->nav_agent_radius; h.nav_agent_height = x->nav_agent_height;
    h.nav_cell_size = x->nav_cell_size; h.nav_agent_climb = x->nav_agent_climb;
    if (p.residents) std::memcpy(b + p.off_resident, x->residents, sizeof(SceneBlobResident) * p.residents);
    if (p.nav) std::memcpy(b + p.off_nav, x->nav_data, p.nav);
    if (p.dag_meshes) {
      h.dag_levels = x->dag_levels; h.dag_device_class = x->dag_device_class;
      std::memcpy(b + p.off_dag_mesh, x->dag_meshes, sizeof(SceneBlobDagMesh) * p.dag_meshes);
      std::memcpy(b + p.off_dag_node, x->dag_nodes, sizeof(SceneBlobDagNode) * p.dag_nodes);
      std::memcpy(b + p.off_dag_index, x->dag_indices, sizeof(uint32_t) * p.dag_indices);
      if (p.dag_children) std::memcpy(b + p.off_dag_child, x->dag_children, sizeof(uint32_t) * p.dag_children);
    }
    if (p.gi_probes) {
      h.gi_dim[0] = x->gi_dim[0]; h.gi_dim[1] = x->gi_dim[1]; h.gi_dim[2] = x->gi_dim[2];
      h.gi_flags = x->gi_flags;
      h.gi_origin[0] = x->gi_origin[0]; h.gi_origin[1] = x->gi_origin[1]; h.gi_origin[2] = x->gi_origin[2];
      h.gi_spacing = x->gi_spacing;
      h.gi_rays = x->gi_rays; h.gi_bounces = x->gi_bounces; h.gi_valid = x->gi_valid;
      std::memcpy(b + p.off_gi, x->gi_probes, sizeof(SceneBlobGiProbe) * p.gi_probes);
    }
  }

  auto *assets = reinterpret_cast<SceneBlobAsset *>(b + p.off_asset);
  auto *ents = reinterpret_cast<SceneBlobEntity *>(b + p.off_entity);
  auto *draws = reinterpret_cast<SceneBlobDraw *>(b + p.off_draw);
  auto *anims = reinterpret_cast<SceneBlobAnim *>(b + p.off_anim);
  auto *lights = reinterpret_cast<SceneBlobLight *>(b + p.off_light);
  auto *bodies = reinterpret_cast<SceneBlobBody *>(b + p.off_body);
  
  auto *particles = reinterpret_cast<SceneBlobParticle *>(b + p.off_particle);
  auto *terrains = reinterpret_cast<SceneBlobTerrain *>(b + p.off_terrain);
  auto *waters = reinterpret_cast<SceneBlobWater *>(b + p.off_water);
  auto *winds = reinterpret_cast<SceneBlobWind *>(b + p.off_wind);
  auto *voxels = reinterpret_cast<SceneBlobVoxel *>(b + p.off_voxel);
  auto *characters = reinterpret_cast<SceneBlobCharacter *>(b + p.off_character);

  char *strings = reinterpret_cast<char *>(b + p.off_string);
  uint32_t soff = 0;
  auto intern = [&](const char *s) {
    const uint32_t at = soff;
    const size_t n = std::strlen(s) + 1;
    std::memcpy(strings + soff, s, n);
    soff += (uint32_t)n;
    return at;
  };
  for (uint32_t i = 0; i < d.asset_count; i++) {
    SceneBlobAsset a{};
    a.path_len = (uint32_t)std::strlen(d.assets[i]);
    a.path = intern(d.assets[i]);
    assets[i] = a;
  }
  uint32_t nd = 0, na = 0, nl = 0, nb = 0;
  uint32_t npart = 0, nterr = 0, nwat = 0, nwin = 0, nvox = 0, nchar = 0;
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const SceneEntity &e = d.entities[i];
    // Sahne agaci burada DUZLESIR (PLAN §6 "sahne bir blob + kod"): blob'da
    // ebeveyn alani YOK, cunku turetilmis her sey derleme aninda hesaplanmistir.
    // Bu yuzden matris/kuaterniyon/olcek/konum DUNYA uzayindadir; kok varlikta
    // bunlar yerel degerlerle BIT-TAM aynidir (scene_entity_world_* erken donus),
    // yani hiyerarsisiz sahnelerin blob'u Faz E2 oncesiyle bayt bayt ayni kalir.
    const Mat4 m = scene_entity_world_matrix(d, i);
    const Quat q = scene_entity_world_rotation(d, i);
    const Vec3 wscale = scene_entity_world_scale(d, i);
    SceneBlobEntity be{};
    std::memcpy(be.world, &m.m[0][0], sizeof be.world);
    put3(be.pos, {m.m[3][0], m.m[3][1], m.m[3][2]}); be.components = e.components;
    be.quat[0] = q.x; be.quat[1] = q.y; be.quat[2] = q.z; be.quat[3] = q.w;
    put3(be.scale, wscale); be.name = intern(e.name);
    be.draw = be.anim = be.light = be.body = -1;
    if (e.components & kSceneModel) {
      SceneBlobDraw dr{};
      dr.entity = i; dr.asset = e.asset; dr.primitive = e.primitive; put3(dr.tint, e.tint);
      dr.metallic = e.metallic; dr.roughness = e.roughness; dr.reflectance = e.reflectance;
      put3(dr.emissive, e.emissive); dr.emissive_strength = e.emissive_strength;
      draws[nd] = dr; be.draw = (int32_t)nd++;
    }
    if (e.components & kSceneAnim) {
      SceneBlobAnim an{};
      an.entity = i; an.clip = e.clip; an.phase = e.phase; an.speed = e.speed;
      anims[na] = an; be.anim = (int32_t)na++;
    }
    if (e.components & kSceneLight) {
      SceneBlobLight li{};
      li.entity = i; put3(li.pos, {m.m[3][0], m.m[3][1], m.m[3][2]});
      put3(li.color, e.light_color); li.intensity = e.light_intensity; li.radius = e.light_radius;
      li.reserved[0] = (float)(uint32_t)e.light_type; // SceneLightType (blob versiyonunu buyutmemek icin reserved'da)
      lights[nl] = li; be.light = (int32_t)nl++;
    }
    if (e.components & kSceneBody) {
      SceneBlobBody bo{};
      bo.entity = i; bo.shape = (uint32_t)e.shape; bo.dynamic = e.dynamic ? 1u : 0u;
      put3(bo.half, e.half * wscale); bo.radius = e.radius * wscale.x; // scene_spawn_bodies ile ayni (DUNYA olcegi)
      put3(bo.pos, {m.m[3][0], m.m[3][1], m.m[3][2]});
      bo.quat[0] = q.x; bo.quat[1] = q.y; bo.quat[2] = q.z; bo.quat[3] = q.w;
      put3(bo.scale, wscale);
      bodies[nb] = bo; be.body = (int32_t)nb++;
    }
    if (e.components & kSceneParticle) {
      SceneBlobParticle p{};
      p.entity = i;
      p.spawn_rate = e.particle_spawn_rate; p.lifetime_min = e.particle_lifetime_min; p.lifetime_max = e.particle_lifetime_max;
      p.size_start = e.particle_size_start; p.size_end = e.particle_size_end;
      put3(p.velocity, e.particle_velocity); put3(p.jitter, e.particle_jitter);
      particles[npart++] = p;
    }
    if (e.components & kSceneTerrain) {
      SceneBlobTerrain t{};
      t.entity = i;
      t.width = e.terrain_width; t.height = e.terrain_height; t.cell = e.terrain_cell;
      t.amp = e.terrain_amp; t.freq = e.terrain_freq; t.octaves = e.terrain_octaves;
      t.seed = e.terrain_seed;
      terrains[nterr++] = t;
    }
    if (e.components & kSceneWater) {
      SceneBlobWater w{};
      w.entity = i;
      w.steepness = e.wave_steepness; w.amplitude = e.wave_amplitude; w.wavelength = e.wave_length;
      w.direction[0] = e.wave_direction.x; w.direction[1] = e.wave_direction.y;
      waters[nwat++] = w;
    }
    if (e.components & kSceneWind) {
      SceneBlobWind w{};
      w.entity = i;
      w.direction[0] = e.wind_direction.x; w.direction[1] = e.wind_direction.y;
      w.strength = e.wind_strength; w.gustiness = e.wind_gustiness; w.gust_freq = e.wind_gust_freq;
      w.seed = e.wind_seed;
      winds[nwin++] = w;
    }
    if (e.components & kSceneVoxel) {
      SceneBlobVoxel v{};
      v.entity = i;
      v.size_x = e.voxel_size_x; v.size_y = e.voxel_size_y; v.size_z = e.voxel_size_z;
      v.cell = e.voxel_cell;
      voxels[nvox++] = v;
    }
    if (e.components & kSceneCharacter) {
      SceneBlobCharacter c{};
      c.entity = i;
      c.radius = e.char_radius; c.height = e.char_height; c.step_height = e.char_max_slope;
      characters[nchar++] = c;
    }

    ents[i] = be;
    const SceneBounds wb = scene_world_bounds(scene_entity_local_bounds(e, nullptr), m);
    lo = vmin(lo, wb.lo); hi = vmax(hi, wb.hi);
  }
  if (d.entity_count == 0) { lo = {0, 0, 0}; hi = {0, 0, 0}; }
  put3(h.bounds_lo, lo); put3(h.bounds_hi, hi);
  if (soff == 0) strings[0] = 0; // bos tablo: tek NUL
  std::memcpy(b, &h, sizeof h);
  // Ozet: hash alanindan sonrasi (header_size'dan itibaren + baslik kalani).
  const size_t hash_from = offsetof(SceneBlobHeader, header_size);
  const uint64_t hv = scene_blob_fnv1a(b + hash_from, p.total - hash_from);
  auto *hp = reinterpret_cast<SceneBlobHeader *>(b);
  hp->hash_lo = (uint32_t)(hv & 0xFFFFFFFFu);
  hp->hash_hi = (uint32_t)(hv >> 32);
  return p.total;
}

bool scene_blob_open(const void *data, size_t size, SceneBlobView *out, SceneError *err) {
  Err E{err};
  *out = SceneBlobView{};
  if (!data) return E.fail("veri yok");
  if ((uintptr_t)data % kSceneBlobAlign != 0) return E.fail("veri 16 hizali degil");
  if (size < sizeof(SceneBlobHeader)) return E.fail("baslik icin cok kisa");
  const auto *h = static_cast<const SceneBlobHeader *>(data);
  if (h->magic != kSceneBlobMagic) return E.fail("magic uyusmuyor (sahne blob degil)");
  if (h->version != kSceneBlobVersion) {
    char m[128];
    std::snprintf(m, sizeof m, "desteklenmeyen blob surumu %u (bu motor surum %u okur; .sahne dosyasini yeniden derleyin: engine_sahnec)",
                  h->version, kSceneBlobVersion);
    return E.fail(m);
  }
  if (h->endian != kSceneBlobEndian) return E.fail("bayt sirasi uyusmuyor");
  if (h->header_size != sizeof(SceneBlobHeader)) return E.fail("baslik boyutu uyusmuyor");
  if (h->total_size != size) return E.fail("toplam boyut dosya boyutuyla uyusmuyor (kesik ya da fazla)");
  if (h->total_size % kSceneBlobAlign != 0) return E.fail("toplam boyut 16 hizali degil");
  const size_t hash_from = offsetof(SceneBlobHeader, header_size);
  const uint64_t hv = scene_blob_fnv1a(static_cast<const uint8_t *>(data) + hash_from, size - hash_from);
  if ((uint32_t)(hv & 0xFFFFFFFFu) != h->hash_lo || (uint32_t)(hv >> 32) != h->hash_hi) return E.fail("ozet uyusmuyor (bozuk icerik)");
  if (h->entity_count > kSceneMaxEntities || h->asset_count > kSceneMaxAssets) return E.fail("kapasite asimi");
  if (!table_ok(h->asset_offset, h->asset_count, sizeof(SceneBlobAsset), size)) return E.fail("kaynak tablosu sinir disi");
  if (!table_ok(h->entity_offset, h->entity_count, sizeof(SceneBlobEntity), size)) return E.fail("varlik tablosu sinir disi");
  if (!table_ok(h->draw_offset, h->draw_count, sizeof(SceneBlobDraw), size)) return E.fail("cizim tablosu sinir disi");
  if (!table_ok(h->anim_offset, h->anim_count, sizeof(SceneBlobAnim), size)) return E.fail("animasyon tablosu sinir disi");
  if (!table_ok(h->light_offset, h->light_count, sizeof(SceneBlobLight), size)) return E.fail("isik tablosu sinir disi");
  if (!table_ok(h->body_offset, h->body_count, sizeof(SceneBlobBody), size)) return E.fail("govde tablosu sinir disi");
  if (!table_ok(h->particle_offset, h->particle_count, sizeof(SceneBlobParticle), size)) return E.fail("partikul tablosu sinir disi");
  if (!table_ok(h->terrain_offset, h->terrain_count, sizeof(SceneBlobTerrain), size)) return E.fail("arazi tablosu sinir disi");
  if (!table_ok(h->water_offset, h->water_count, sizeof(SceneBlobWater), size)) return E.fail("su tablosu sinir disi");
  if (!table_ok(h->wind_offset, h->wind_count, sizeof(SceneBlobWind), size)) return E.fail("ruzgar tablosu sinir disi");
  if (!table_ok(h->voxel_offset, h->voxel_count, sizeof(SceneBlobVoxel), size)) return E.fail("voksel tablosu sinir disi");
  if (!table_ok(h->character_offset, h->character_count, sizeof(SceneBlobCharacter), size)) return E.fail("karakter tablosu sinir disi");
  if (h->string_size == 0 || !table_ok(h->string_offset, h->string_size, 1, size)) return E.fail("metin tablosu sinir disi");
  if (!table_ok(h->resident_offset, h->resident_count, sizeof(SceneBlobResident), size)) return E.fail("yerlesik tablosu sinir disi");
  if (h->resident_count > kSceneMaxAssets) return E.fail("yerlesik tablosu kapasite asimi");
  if (!table_ok(h->nav_offset, h->nav_size, 1, size)) return E.fail("navmesh bolumu sinir disi");
  if (!table_ok(h->dag_mesh_offset, h->dag_mesh_count, sizeof(SceneBlobDagMesh), size)) return E.fail("kume DAG mesh tablosu sinir disi");
  if (!table_ok(h->dag_node_offset, h->dag_node_count, sizeof(SceneBlobDagNode), size)) return E.fail("kume DAG dugum tablosu sinir disi");
  if (!table_ok(h->dag_index_offset, h->dag_index_count, sizeof(uint32_t), size)) return E.fail("kume DAG indeks tablosu sinir disi");
  if (!table_ok(h->dag_child_offset, h->dag_child_count, sizeof(uint32_t), size)) return E.fail("kume DAG cocuk tablosu sinir disi");
  if (h->dag_mesh_count && (h->dag_node_count == 0 || h->dag_index_count == 0)) return E.fail("kume DAG bolumu eksik");
  if (!table_ok(h->gi_probe_offset, h->gi_probe_count, sizeof(SceneBlobGiProbe), size)) return E.fail("GI sonda tablosu sinir disi");
  if (h->gi_probe_count > kGiBlobMaxProbes) return E.fail("GI sonda tablosu kapasite asimi");
  if (h->gi_probe_count) {
    if ((uint64_t)h->gi_dim[0] * h->gi_dim[1] * h->gi_dim[2] != (uint64_t)h->gi_probe_count)
      return E.fail("GI izgara boyutu sonda sayisiyla tutarsiz");
    if (!(h->gi_spacing > 0)) return E.fail("GI izgara adimi pozitif degil");
    if (h->gi_valid > h->gi_probe_count) return E.fail("GI gecerli sonda sayisi toplamdan buyuk");
  } else if (h->gi_dim[0] || h->gi_dim[1] || h->gi_dim[2] || h->gi_flags || h->gi_valid) {
    return E.fail("GI bolumu yok ama izgara alanlari dolu");
  }
  const uint8_t *b = static_cast<const uint8_t *>(data);
  const char *strings = reinterpret_cast<const char *>(b + h->string_offset);
  if (strings[h->string_size - 1] != 0) return E.fail("metin tablosu NUL ile bitmiyor");
  SceneBlobView v;
  v.h = h;
  v.assets = reinterpret_cast<const SceneBlobAsset *>(b + h->asset_offset);
  v.entities = reinterpret_cast<const SceneBlobEntity *>(b + h->entity_offset);
  v.draws = reinterpret_cast<const SceneBlobDraw *>(b + h->draw_offset);
  v.anims = reinterpret_cast<const SceneBlobAnim *>(b + h->anim_offset);
  v.lights = reinterpret_cast<const SceneBlobLight *>(b + h->light_offset);
  v.bodies = reinterpret_cast<const SceneBlobBody *>(b + h->body_offset);
  v.particles = reinterpret_cast<const SceneBlobParticle *>(b + h->particle_offset);
  v.terrains = reinterpret_cast<const SceneBlobTerrain *>(b + h->terrain_offset);
  v.waters = reinterpret_cast<const SceneBlobWater *>(b + h->water_offset);
  v.winds = reinterpret_cast<const SceneBlobWind *>(b + h->wind_offset);
  v.voxels = reinterpret_cast<const SceneBlobVoxel *>(b + h->voxel_offset);
  v.characters = reinterpret_cast<const SceneBlobCharacter *>(b + h->character_offset);
  v.residents = h->resident_count ? reinterpret_cast<const SceneBlobResident *>(b + h->resident_offset) : nullptr;
  v.nav = h->nav_size ? b + h->nav_offset : nullptr;
  v.dag_meshes = h->dag_mesh_count ? reinterpret_cast<const SceneBlobDagMesh *>(b + h->dag_mesh_offset) : nullptr;
  v.dag_nodes = h->dag_node_count ? reinterpret_cast<const SceneBlobDagNode *>(b + h->dag_node_offset) : nullptr;
  v.dag_indices = h->dag_index_count ? reinterpret_cast<const uint32_t *>(b + h->dag_index_offset) : nullptr;
  v.dag_children = h->dag_child_count ? reinterpret_cast<const uint32_t *>(b + h->dag_child_offset) : nullptr;
  v.gi_probes = h->gi_probe_count ? reinterpret_cast<const SceneBlobGiProbe *>(b + h->gi_probe_offset) : nullptr;
  v.strings = strings;
  for (uint32_t i = 0; i < h->resident_count; i++)
    if (v.residents[i].asset >= h->asset_count) return E.fail("yerlesik kaydi tanimsiz kaynaga bakiyor");
  // Kume DAG: her dilim tablolarin icinde mi, dugum araliklari dilimin icinde mi,
  // cocuk baglantilari gecerli dugumu mu gosteriyor, indeksler mesh vertex'i mi.
  for (uint32_t i = 0; i < h->dag_mesh_count; i++) {
    const SceneBlobDagMesh &dm = v.dag_meshes[i];
    if (dm.asset >= h->asset_count) return E.fail("kume DAG dilimi tanimsiz kaynaga bakiyor");
    if ((uint64_t)dm.node_first + dm.node_count > h->dag_node_count) return E.fail("kume DAG dugum araligi tablo disi");
    if ((uint64_t)dm.index_first + dm.index_count > h->dag_index_count) return E.fail("kume DAG indeks araligi tablo disi");
    if ((uint64_t)dm.child_first + dm.child_count > h->dag_child_count) return E.fail("kume DAG cocuk araligi tablo disi");
    if (dm.index_count % 3 != 0) return E.fail("kume DAG indeks sayisi 3'un kati degil");
    for (uint32_t n = 0; n < dm.node_count; n++) {
      const SceneBlobDagNode &nd = v.dag_nodes[dm.node_first + n];
      if ((uint64_t)nd.index_offset + nd.index_count > (uint64_t)dm.index_first + dm.index_count || nd.index_offset < dm.index_first)
        return E.fail("kume DAG dugum indeksi dilim disi");
      if (nd.index_count == 0 || nd.index_count % 3 != 0) return E.fail("kume DAG dugumu ucgensiz");
      if ((uint64_t)nd.child_offset + nd.child_count > (uint64_t)dm.child_first + dm.child_count) return E.fail("kume DAG cocuk araligi dilim disi");
      if (nd.child_count && nd.child_offset < dm.child_first) return E.fail("kume DAG cocuk araligi dilim disi");
      if (nd.parent_error < nd.error) return E.fail("kume DAG hatasi monoton degil (ust hata cocuktan kucuk)");
    }
    for (uint32_t c = 0; c < dm.child_count; c++) {
      const uint32_t ci = v.dag_children[dm.child_first + c];
      if (ci < dm.node_first || ci >= dm.node_first + dm.node_count) return E.fail("kume DAG cocuk baglantisi dilim disi");
    }
    for (uint32_t k = 0; k < dm.index_count; k++)
      if (v.dag_indices[dm.index_first + k] >= dm.vertex_count) return E.fail("kume DAG indeksi mesh vertex sayisini asiyor");
  }
  {
    uint32_t valid = 0;
    for (uint32_t i = 0; i < h->gi_probe_count; i++) {
      const SceneBlobGiProbe &pr = v.gi_probes[i];
      if (!(pr.flags & 1u)) valid++;
      if (!(pr.sun_vis >= 0.0f) || !(pr.sun_vis <= 1.0f)) return E.fail("GI sonda gunes gorunurlugu 0..1 disinda");
      for (uint32_t f = 0; f < 6; f++)
        for (uint32_t c = 0; c < 3; c++)
          if (!(pr.face[f][c] >= 0.0f) || !(pr.face[f][c] < 1.0e30f)) return E.fail("GI sonda isimasi negatif ya da sonlu degil");
    }
    if (h->gi_probe_count && valid != h->gi_valid) return E.fail("GI gecerli sonda sayisi tabloyla tutarsiz");
  }
  for (uint32_t i = 0; i < h->asset_count; i++)
    if (v.assets[i].path >= h->string_size || v.assets[i].path + v.assets[i].path_len >= h->string_size) return E.fail("kaynak yolu metin disi");
  for (uint32_t i = 0; i < h->entity_count; i++) {
    const SceneBlobEntity &e = v.entities[i];
    if (e.name >= h->string_size) return E.fail("varlik adi metin disi");
    if (e.draw >= (int32_t)h->draw_count || e.anim >= (int32_t)h->anim_count || e.light >= (int32_t)h->light_count || e.body >= (int32_t)h->body_count)
      return E.fail("varlik bilesen dizini tablo disi");
    if (e.draw >= 0 && v.draws[e.draw].entity != i) return E.fail("cizim tablosu varlikla tutarsiz");
    if (e.draw >= 0 && v.draws[e.draw].asset >= h->asset_count) return E.fail("cizim kaynak dizini tanimsiz");
    if (e.anim >= 0 && v.anims[e.anim].entity != i) return E.fail("animasyon tablosu varlikla tutarsiz");
    if (e.light >= 0 && v.lights[e.light].entity != i) return E.fail("isik tablosu varlikla tutarsiz");
    if (e.body >= 0 && v.bodies[e.body].entity != i) return E.fail("govde tablosu varlikla tutarsiz");
  }
  *out = v;
  return true;
}

SceneWorld SceneBlobView::world() const {
  SceneWorld w;
  w.sun_dir = get3(h->sun_dir); w.sun_diffuse = h->sun_diffuse;
  w.ambient = get3(h->ambient); w.shadow_radius = h->shadow_radius;
  w.shadow_center = get3(h->shadow_center); w.shadow_depth = h->shadow_depth;
  w.cam_target = get3(h->cam_target); w.cam_yaw = h->cam_yaw;
  w.cam_pitch = h->cam_pitch; w.cam_radius = h->cam_radius;
  return w;
}
Mat4 SceneBlobView::entity_matrix(uint32_t i) const {
  Mat4 m;
  std::memcpy(&m.m[0][0], entities[i].world, sizeof m.m);
  return m;
}

bool scene_blob_save(Arena &scratch, const SceneDesc &d, const char *path, SceneError *err) {
  return scene_blob_save_ex(scratch, d, nullptr, path, err);
}

bool scene_blob_save_ex(Arena &scratch, const SceneDesc &d, const SceneBlobExtras *x, const char *path, SceneError *err) {
  Err E{err};
  const size_t need = scene_blob_compile_ex(d, x, nullptr, 0);
  void *buf = scratch.alloc(need, kSceneBlobAlign);
  if (!buf) return E.fail("arena dolu");
  scene_blob_compile_ex(d, x, buf, need);
  FILE *f = std::fopen(path, "wb");
  if (!f) { if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "dosya yazilamadi: %s", path); } return false; }
  const bool ok = std::fwrite(buf, 1, need, f) == need;
  std::fclose(f);
  if (!ok && err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "yazma eksik: %s", path); }
  return ok;
}

bool scene_blob_load(Arena &arena, const char *path, SceneBlobView *out, SceneError *err) {
  Err E{err};
  *out = SceneBlobView{};
  FILE *f = std::fopen(path, "rb");
  if (!f) { if (err) { err->line = 0; std::snprintf(err->msg, sizeof err->msg, "dosya acilamadi: %s", path); } return false; }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (64 << 20)) { std::fclose(f); return E.fail("dosya boyutu gecersiz"); }
  void *buf = arena.alloc((size_t)sz, kSceneBlobAlign);
  if (!buf) { std::fclose(f); return E.fail("arena dolu"); }
  const size_t got = std::fread(buf, 1, (size_t)sz, f);
  std::fclose(f);
  if (got != (size_t)sz) return E.fail("okuma eksik");
  return scene_blob_open(buf, got, out, err);
}


// ===========================================================================
// SAHNE DERLEYICISI (content/scene_compile.hpp): yerlesik kume olcumu + navmesh
// bake. Blob'un kendisi saf bir donusumdur; buradaki adimlar DISK'e dokunur ve
// yalniz arac/editor tarafinda calisir — runtime bunlarin SONUCUNU okur.
// ===========================================================================
namespace {

// Olcum kaydinin onbellek anahtarina giren ayarlar. Tum alanlar 4 baytlik:
// dolgu yok, ayni ayar -> ayni baytlar (anahtar belirlenimli).
struct MeasureSettings {
  uint32_t record_version;
  uint32_t optimize, lods;
  float lod_error, clip_sample_rate;
  uint32_t vertex_size, skin_vertex_size;
  uint32_t max_meshes, max_images;
};
constexpr uint32_t kMeasureRecordVersion = 1; // kayit yerlesimi degisirse artir (eski onbellek dusar)

MeasureSettings measure_settings(const GltfLimits &lim) {
  MeasureSettings st{};
  st.record_version = kMeasureRecordVersion;
  st.optimize = lim.optimize ? 1u : 0u;
  st.lods = lim.lods ? 1u : 0u;
  st.lod_error = lim.lod_error;
  st.clip_sample_rate = lim.clip_sample_rate;
  st.vertex_size = (uint32_t)sizeof(renderer::GpuVertex);
  st.skin_vertex_size = (uint32_t)sizeof(renderer::GpuSkinnedVertex);
  st.max_meshes = lim.max_meshes;
  st.max_images = lim.max_images;
  return st;
}

// Mip zinciri dahil doku baytlari (renderer create_texture mip uretir).
uint32_t texture_bytes_with_mips(uint32_t w, uint32_t h) {
  uint64_t total = 0;
  while (true) {
    total += (uint64_t)w * h * 4u;
    if (w == 1 && h == 1) break;
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
  }
  return (uint32_t)total;
}

// Yuklenmis modelden GPU yerlesik baytlari + en buyuk tek gecici blok (staging).
void measure_model(const Model &m, SceneBlobResident *r) {
  uint64_t vtx = 0, idx = 0, tex = 0, clip = 0;
  uint32_t transient = 0;
  for (uint32_t i = 0; i < m.mesh_count; i++) {
    const ModelMesh &me = m.meshes[i];
    const uint64_t vb = (uint64_t)me.vertex_count * (me.skin >= 0 ? sizeof(renderer::GpuSkinnedVertex) : sizeof(renderer::GpuVertex));
    uint64_t ib = (uint64_t)me.index_count * 4u;
    for (uint32_t l = 0; l < kModelMaxLods; l++) ib += (uint64_t)me.lod_index_count[l] * 4u;
    vtx += vb;
    idx += ib;
    // Gecici (staging) blok TAMPON BASINA olculur: LOD indeks tamponlari ayri
    // yuklenir, toplamlari degil en buyugu tepe belirler.
    if (vb > transient) transient = (uint32_t)vb;
    if ((uint64_t)me.index_count * 4u > transient) transient = me.index_count * 4u;
    for (uint32_t l = 0; l < kModelMaxLods; l++)
      if ((uint64_t)me.lod_index_count[l] * 4u > transient) transient = me.lod_index_count[l] * 4u;
  }
  for (uint32_t i = 0; i < m.image_count; i++) {
    const uint32_t base = m.images[i].width * m.images[i].height * 4u;
    tex += texture_bytes_with_mips(m.images[i].width, m.images[i].height);
    if (base > transient) transient = base;
  }
  for (uint32_t i = 0; i < m.clip_count; i++)
    if (m.clips[i].clip) clip += m.clips[i].clip->total_bytes;
  r->gpu_vertex = (uint32_t)vtx;
  r->gpu_index = (uint32_t)idx;
  r->gpu_texture = (uint32_t)tex;
  r->gpu_clip = (uint32_t)clip; // bilgi: klip verisi CPU tarafinda (cpu_bytes'in ICINDE)
  r->transient = transient;
}

// Sessiz Recast baglami (log/zamanlama yok) — sim/navmesh.cpp ile ayni.
class QuietCtx : public rcContext {
public:
  QuietCtx() : rcContext(false) {}
};
} // namespace

uint32_t scene_nav_soup(const SceneDesc &d, float *verts, uint32_t max_verts, int *tris, uint32_t max_tris, uint32_t *out_verts) {
  uint32_t nv = 0, nt = 0;
  auto add_v = [&](Vec3 p) {
    verts[nv * 3] = p.x; verts[nv * 3 + 1] = p.y; verts[nv * 3 + 2] = p.z;
    return (int)nv++;
  };
  auto quad = [&](int a, int b, int c, int e) {
    tris[nt * 3] = a; tris[nt * 3 + 1] = b; tris[nt * 3 + 2] = c; nt++;
    tris[nt * 3] = a; tris[nt * 3 + 1] = c; tris[nt * 3 + 2] = e; nt++;
  };
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const SceneEntity &e = d.entities[i];
    if (!(e.components & kSceneBody) || e.dynamic || e.shape != SceneShape::Box) continue;
    if (nv + 8 > max_verts || nt + 12 > max_tris) break;
    const Mat4 m = scene_entity_world_matrix(d, i); // navmesh DUNYA uzayinda: cocuk zemin de sayilir
    auto corner = [&](float sx, float sy, float sz) {
      const Vec3 l{sx * e.half.x, sy * e.half.y, sz * e.half.z};
      return transform_point(m, l);
    };
    // Kose sirasi + sarim test_navmesh'teki kutu ile ayni: ust yuzun normali +y
    // (rcMarkWalkableTriangles egimi normalden okur).
    const int a = add_v(corner(-1, -1, -1)), b = add_v(corner(1, -1, -1)), c = add_v(corner(1, -1, 1)), dd = add_v(corner(-1, -1, 1));
    const int ee = add_v(corner(-1, 1, -1)), f = add_v(corner(1, 1, -1)), g = add_v(corner(1, 1, 1)), hh = add_v(corner(-1, 1, 1));
    quad(ee, hh, g, f); // ust
    quad(a, b, c, dd);  // alt
    quad(a, ee, f, b); quad(b, f, g, c); quad(c, g, hh, dd); quad(dd, hh, ee, a); // yanlar
  }
  if (out_verts) *out_verts = nv;
  return nt;
}

bool scene_navmesh_bake(Arena &arena, const float *verts, int nverts, const int *tris, int ntris, const sim::NavMeshBuildConfig &cfg,
                        void **out_data, uint32_t *out_size, uint32_t *out_polys, uint32_t *out_verts, char *err, size_t err_cap) {
  auto fail = [&](const char *what) {
    if (err && err_cap) std::snprintf(err, err_cap, "navmesh bake: %s", what);
    return false;
  };
  *out_data = nullptr;
  *out_size = 0;
  if (nverts < 3 || ntris < 1) return fail("ucgen corbasi bos (sahnede sabit kutu govde yok)");

  float bmin[3], bmax[3];
  rcCalcBounds(verts, nverts, bmin, bmax);
  rcConfig c;
  std::memset(&c, 0, sizeof c);
  c.cs = cfg.cell_size;
  c.ch = cfg.cell_height;
  c.walkableSlopeAngle = cfg.agent_max_slope_deg;
  c.walkableHeight = (int)std::ceil(cfg.agent_height / c.ch);
  c.walkableClimb = (int)std::floor(cfg.agent_max_climb / c.ch);
  c.walkableRadius = (int)std::ceil(cfg.agent_radius / c.cs);
  c.maxEdgeLen = (int)(cfg.edge_max_len / c.cs);
  c.maxSimplificationError = cfg.edge_max_error;
  c.minRegionArea = (int)(cfg.region_min_size * cfg.region_min_size);
  c.mergeRegionArea = (int)(cfg.region_merge_size * cfg.region_merge_size);
  c.maxVertsPerPoly = cfg.verts_per_poly;
  c.detailSampleDist = cfg.detail_sample_dist < 0.9f ? 0 : c.cs * cfg.detail_sample_dist;
  c.detailSampleMaxError = c.ch * cfg.detail_sample_max_error;
  rcVcopy(c.bmin, bmin);
  rcVcopy(c.bmax, bmax);
  rcCalcGridSize(c.bmin, c.bmax, c.cs, &c.width, &c.height);

  QuietCtx ctx;
  const char *why = "bilinmeyen adim";
  bool ok = false;
  unsigned char *data = nullptr;
  int data_size = 0;
  rcHeightfield *hf = rcAllocHeightfield();
  rcCompactHeightfield *chf = rcAllocCompactHeightfield();
  rcContourSet *cset = rcAllocContourSet();
  rcPolyMesh *pmesh = rcAllocPolyMesh();
  rcPolyMeshDetail *dmesh = rcAllocPolyMeshDetail();
  unsigned char *areas = static_cast<unsigned char *>(rcAlloc(ntris, RC_ALLOC_TEMP));
  do {
    if (!hf || !chf || !cset || !pmesh || !dmesh || !areas) { why = "Recast ayirma"; break; }
    if (!rcCreateHeightfield(&ctx, *hf, c.width, c.height, c.bmin, c.bmax, c.cs, c.ch)) { why = "yukseklik alani"; break; }
    std::memset(areas, 0, (size_t)ntris);
    rcMarkWalkableTriangles(&ctx, c.walkableSlopeAngle, verts, nverts, tris, ntris, areas);
    if (!rcRasterizeTriangles(&ctx, verts, nverts, tris, areas, ntris, *hf, c.walkableClimb)) { why = "rasterlestirme"; break; }
    rcFilterLowHangingWalkableObstacles(&ctx, c.walkableClimb, *hf);
    rcFilterLedgeSpans(&ctx, c.walkableHeight, c.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&ctx, c.walkableHeight, *hf);
    if (!rcBuildCompactHeightfield(&ctx, c.walkableHeight, c.walkableClimb, *hf, *chf)) { why = "sikisik alan"; break; }
    if (!rcErodeWalkableArea(&ctx, c.walkableRadius, *chf)) { why = "asindirma"; break; }
    if (!rcBuildDistanceField(&ctx, *chf)) { why = "uzaklik alani"; break; }
    if (!rcBuildRegions(&ctx, *chf, 0, c.minRegionArea, c.mergeRegionArea)) { why = "bolgeler"; break; }
    if (!rcBuildContours(&ctx, *chf, c.maxSimplificationError, c.maxEdgeLen, *cset)) { why = "konturlar"; break; }
    if (!rcBuildPolyMesh(&ctx, *cset, c.maxVertsPerPoly, *pmesh)) { why = "poligon agi"; break; }
    if (pmesh->npolys == 0) { why = "yurunebilir alan yok (kutular ajana gore kucuk ya da zemin yok)"; break; }
    if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, c.detailSampleDist, c.detailSampleMaxError, *dmesh)) { why = "ayrinti agi"; break; }
    for (int i = 0; i < pmesh->npolys; i++) {
      if (pmesh->areas[i] == RC_WALKABLE_AREA) pmesh->areas[i] = 0; // tek alan tipi
      pmesh->flags[i] = 1;                                          // yuruyulebilir
    }
    dtNavMeshCreateParams p;
    std::memset(&p, 0, sizeof p);
    p.verts = pmesh->verts;
    p.vertCount = pmesh->nverts;
    p.polys = pmesh->polys;
    p.polyAreas = pmesh->areas;
    p.polyFlags = pmesh->flags;
    p.polyCount = pmesh->npolys;
    p.nvp = pmesh->nvp;
    p.detailMeshes = dmesh->meshes;
    p.detailVerts = dmesh->verts;
    p.detailVertsCount = dmesh->nverts;
    p.detailTris = dmesh->tris;
    p.detailTriCount = dmesh->ntris;
    p.walkableHeight = cfg.agent_height;
    p.walkableRadius = cfg.agent_radius;
    p.walkableClimb = cfg.agent_max_climb;
    rcVcopy(p.bmin, pmesh->bmin);
    rcVcopy(p.bmax, pmesh->bmax);
    p.cs = c.cs;
    p.ch = c.ch;
    p.buildBvTree = true;
    if (!dtCreateNavMeshData(&p, &data, &data_size) || data_size <= 0) { why = "Detour verisi"; break; }
    if (out_polys) *out_polys = (uint32_t)pmesh->npolys;
    if (out_verts) *out_verts = (uint32_t)pmesh->nverts;
    ok = true;
  } while (false);
  rcFree(areas);
  rcFreePolyMeshDetail(dmesh);
  rcFreePolyMesh(pmesh);
  rcFreeContourSet(cset);
  rcFreeCompactHeightfield(chf);
  rcFreeHeightField(hf);
  if (!ok) {
    if (data) dtFree(data);
    return fail(why);
  }
  // Detour tamponu blob'a girecek: arenaya kopyala, malloc'u birak.
  void *copy = arena.alloc((size_t)data_size, kSceneBlobAlign);
  if (!copy) { dtFree(data); return fail("arena dolu"); }
  std::memcpy(copy, data, (size_t)data_size);
  dtFree(data);
  *out_data = copy;
  *out_size = (uint32_t)data_size;
  return true;
}

// ===========================================================================
// GI SONDA BAKE'i (content/gi.hpp): sahne geometrisinde CPU isin izlemesi ->
// sonda basina 6 yonlu ambient cube + dogrudan gunes gorunurlugu. Offline arac
// yolu (yavas olabilir); runtime BAKE YAPMAZ, SceneGi yalniz okur.
// ===========================================================================
namespace {

struct GiHit {
  float t = 0;
  Vec3 n{0, 1, 0};
  Vec3 albedo{0.5f, 0.5f, 0.5f};
};

float axis_of(Vec3 v, uint32_t a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
void set_axis(Vec3 &v, uint32_t a, float s) { (a == 0 ? v.x : (a == 1 ? v.y : v.z)) = s; }

// --- Ucgen BVH (orta-nokta bolmesi; ayirma yalniz arenadan) -----------------
struct GiBvhNode {
  Vec3 lo{0, 0, 0}, hi{0, 0, 0};
  uint32_t first = 0, count = 0, right = 0, pad = 0; // count 0 => ic dugum: first = sol, right = sag
};
struct GiBvh {
  const GiTri *tris = nullptr;
  const uint32_t *order = nullptr;
  const GiBvhNode *nodes = nullptr;
  uint32_t node_count = 0, tri_count = 0;
};

constexpr uint32_t kGiBvhLeaf = 4;
constexpr uint32_t kGiStackMax = 128;

bool gi_bvh_build(Arena &a, const GiTri *tris, uint32_t n, GiBvh *out) {
  *out = GiBvh{};
  if (!tris || n == 0) return true; // bos BVH gecerli (yalniz govde tikayicilari)
  uint32_t *order = a.alloc_array<uint32_t>(n);
  Vec3 *cent = a.alloc_array<Vec3>(n);
  GiBvhNode *nodes = a.alloc_array<GiBvhNode>(2 * (size_t)n + 1);
  if (!order || !cent || !nodes) return false;
  for (uint32_t i = 0; i < n; i++) {
    order[i] = i;
    cent[i] = (tris[i].a + tris[i].b + tris[i].c) * (1.0f / 3.0f);
  }
  struct Item {
    uint32_t node, first, count;
  };
  Item stack[kGiStackMax];
  uint32_t sp = 0, nc = 1;
  nodes[0] = GiBvhNode{};
  stack[sp++] = Item{0, 0, n};
  while (sp) {
    const Item it = stack[--sp];
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f}, clo = lo, chi = hi;
    for (uint32_t k = 0; k < it.count; k++) {
      const GiTri &t = tris[order[it.first + k]];
      lo = vmin(lo, vmin(t.a, vmin(t.b, t.c)));
      hi = vmax(hi, vmax(t.a, vmax(t.b, t.c)));
      clo = vmin(clo, cent[order[it.first + k]]);
      chi = vmax(chi, cent[order[it.first + k]]);
    }
    GiBvhNode &nd = nodes[it.node];
    nd.lo = lo;
    nd.hi = hi;
    if (it.count <= kGiBvhLeaf || sp + 2 > kGiStackMax || nc + 2 > 2 * n + 1) {
      nd.first = it.first;
      nd.count = it.count;
      continue;
    }
    const Vec3 ext = chi - clo;
    uint32_t axis = 0;
    if (ext.y > ext.x) axis = 1;
    if (axis_of(ext, 2) > axis_of(ext, axis)) axis = 2;
    const float mid = (axis_of(clo, axis) + axis_of(chi, axis)) * 0.5f;
    uint32_t i = it.first, j = it.first + it.count;
    while (i < j) {
      if (axis_of(cent[order[i]], axis) < mid) i++;
      else {
        j--;
        const uint32_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
    uint32_t left = i - it.first;
    if (left == 0 || left == it.count) left = it.count / 2; // yozlasmis bolme: sayiyi ikiye bol
    const uint32_t l = nc++, r = nc++;
    nodes[l] = GiBvhNode{};
    nodes[r] = GiBvhNode{};
    // Ic dugum: count = 0, first = SOL cocuk, right = SAG cocuk. Yigin tabanli
    // kurulumda cocuklar ebeveynin hemen ardinda DEGIL — indeks acikca durur.
    nodes[it.node].count = 0;
    nodes[it.node].first = l;
    nodes[it.node].right = r;
    stack[sp++] = Item{l, it.first, left};
    stack[sp++] = Item{r, it.first + left, it.count - left};
  }
  out->tris = tris;
  out->order = order;
  out->nodes = nodes;
  out->node_count = nc;
  out->tri_count = n;
  return true;
}

// Slab: t0 <= t1 ise kesisim var (t0 = giris; isin icerideyse 0).
bool gi_slab(Vec3 lo, Vec3 hi, Vec3 o, Vec3 inv, float tmax) {
  float t0 = 0.0f, t1 = tmax;
  for (uint32_t a = 0; a < 3; a++) {
    float x0 = (axis_of(lo, a) - axis_of(o, a)) * axis_of(inv, a);
    float x1 = (axis_of(hi, a) - axis_of(o, a)) * axis_of(inv, a);
    if (x0 > x1) { const float t = x0; x0 = x1; x1 = t; }
    if (x0 > t0) t0 = x0;
    if (x1 < t1) t1 = x1;
    if (t0 > t1) return false;
  }
  return true;
}

// Moller-Trumbore. Normal NORMALIZE EDILMEZ (cagiran cevirir/normalize eder).
bool gi_ray_tri(Vec3 o, Vec3 d, const GiTri &t, float tmin, float tmax, float *out_t, Vec3 *out_n) {
  const Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
  const Vec3 p = cross(d, e2);
  const float det = dot(e1, p);
  if (det > -1e-12f && det < 1e-12f) return false;
  const float inv = 1.0f / det;
  const Vec3 tv = o - t.a;
  const float u = dot(tv, p) * inv;
  if (u < 0.0f || u > 1.0f) return false;
  const Vec3 q = cross(tv, e1);
  const float v = dot(d, q) * inv;
  if (v < 0.0f || u + v > 1.0f) return false;
  const float tt = dot(e2, q) * inv;
  if (tt <= tmin || tt >= tmax) return false;
  *out_t = tt;
  *out_n = cross(e1, e2);
  return true;
}

bool gi_ray_obb(const GiOccluder &b, Vec3 o, Vec3 d, float tmin, float tmax, float *out_t, Vec3 *out_n) {
  const Quat iq = conjugate(b.rot);
  const Vec3 lo = rotate(iq, o - b.center), ld = rotate(iq, d);
  float t0 = tmin, t1 = tmax;
  uint32_t axis = 0;
  float sign = -1.0f;
  for (uint32_t a = 0; a < 3; a++) {
    const float dd = axis_of(ld, a), oo = axis_of(lo, a), h = axis_of(b.half, a);
    if (dd > -1e-12f && dd < 1e-12f) {
      if (oo < -h || oo > h) return false;
      continue;
    }
    const float inv = 1.0f / dd;
    float ta = (-h - oo) * inv, tb = (h - oo) * inv;
    float s = -1.0f;
    if (ta > tb) { const float t = ta; ta = tb; tb = t; s = 1.0f; }
    if (ta > t0) { t0 = ta; axis = a; sign = s; }
    if (tb < t1) t1 = tb;
    if (t0 > t1) return false;
  }
  if (t0 <= tmin || t0 >= tmax) return false;
  *out_t = t0;
  Vec3 ln{0, 0, 0};
  set_axis(ln, axis, sign);
  *out_n = rotate(b.rot, ln);
  return true;
}

bool gi_ray_sphere(Vec3 c, float r, Vec3 o, Vec3 d, float tmin, float tmax, float *out_t, Vec3 *out_n) {
  if (!(r > 0)) return false;
  const Vec3 oc = o - c;
  const float b = dot(oc, d), cc = dot(oc, oc) - r * r;
  const float disc = b * b - cc;
  if (disc < 0) return false;
  const float sq = std::sqrt(disc);
  float tt = -b - sq;
  if (tt <= tmin) tt = -b + sq;
  if (tt <= tmin || tt >= tmax) return false;
  *out_t = tt;
  *out_n = (oc + d * tt) * (1.0f / r);
  return true;
}

// Izleme baglami: sahne + BVH + isin sayaci (rapor icin).
struct GiWorld {
  const GiScene *s = nullptr;
  GiBvh bvh;
  const SceneGiOptions *opt = nullptr;
  uint64_t rays = 0;
};

// hit == nullptr: "herhangi bir vurus" (golge isini), ilk vurusta doner.
bool gi_trace(GiWorld &w, Vec3 o, Vec3 d, float tmax, GiHit *hit) {
  w.rays++;
  const float tmin = 1e-5f;
  float best = tmax;
  bool any = false;
  for (uint32_t i = 0; i < w.s->occluder_count; i++) {
    const GiOccluder &b = w.s->occluders[i];
    float t;
    Vec3 n;
    const bool h = b.kind == 1 ? gi_ray_sphere(b.center, b.radius, o, d, tmin, best, &t, &n)
                               : gi_ray_obb(b, o, d, tmin, best, &t, &n);
    if (!h) continue;
    if (!hit) return true;
    best = t;
    any = true;
    hit->t = t;
    hit->n = n;
    hit->albedo = b.albedo;
  }
  if (w.bvh.node_count) {
    const Vec3 inv{1.0f / (d.x != 0 ? d.x : 1e-30f), 1.0f / (d.y != 0 ? d.y : 1e-30f), 1.0f / (d.z != 0 ? d.z : 1e-30f)};
    uint32_t stack[kGiStackMax];
    uint32_t sp = 0;
    stack[sp++] = 0;
    while (sp) {
      const GiBvhNode &nd = w.bvh.nodes[stack[--sp]];
      if (!gi_slab(nd.lo, nd.hi, o, inv, best)) continue;
      if (nd.count) {
        for (uint32_t k = 0; k < nd.count; k++) {
          const GiTri &t = w.bvh.tris[w.bvh.order[nd.first + k]];
          float tt;
          Vec3 n;
          if (!gi_ray_tri(o, d, t, tmin, best, &tt, &n)) continue;
          if (!hit) return true;
          best = tt;
          any = true;
          hit->t = tt;
          hit->n = n;
          hit->albedo = t.albedo;
        }
      } else if (sp + 2 <= kGiStackMax) {
        stack[sp++] = nd.right;
        stack[sp++] = nd.first; // sol cocuk
      }
    }
  }
  if (any && hit) {
    hit->n = normalize(hit->n);
    if (dot(hit->n, d) > 0) hit->n = -hit->n; // normal her zaman isina BAKAR
  }
  return any;
}

// Nokta isigin bir noktadaki dogrudan katkisi (mesh.frag ile AYNI formul:
// pencereli ters-kare sonum + Lambert). vis_ray: gorunurluk isini atilsin mi.
Vec3 gi_point_lights(GiWorld &w, Vec3 p, Vec3 n, bool vis_ray) {
  Vec3 sum{0, 0, 0};
  for (uint32_t i = 0; i < w.s->light_count; i++) {
    const GiLight &l = w.s->lights[i];
    const Vec3 dv = l.pos - p;
    const float dist2 = dot(dv, dv);
    if (!(l.radius > 0)) continue;
    const float xr = dist2 / (l.radius * l.radius);
    float win = 1.0f - xr * xr;
    if (win <= 0) continue;
    if (win > 1) win = 1;
    const float att = win * win / (dist2 + 1.0f);
    const float dist = std::sqrt(dist2 > 1e-12f ? dist2 : 1e-12f);
    const Vec3 dh = dv * (1.0f / dist);
    const float nl = dot(n, dh);
    if (nl <= 0) continue;
    if (vis_ray && gi_trace(w, p, dh, dist, nullptr)) continue;
    sum += l.color * (l.intensity * att * nl);
  }
  return sum;
}

// Sicrama yuzeyinin verdigi isima (mesh.frag'in isik terimi x albedo):
// gokyuzu + golgeli gunes + nokta isiklar. Ikinci derece gorunurluk YOK
// (gokyuzu terimi bu noktada tikanmamis varsayilir) — tek sicrama yaklasimi.
Vec3 gi_surface_radiance(GiWorld &w, Vec3 x, Vec3 n, Vec3 albedo) {
  Vec3 e = w.s->ambient;
  if (w.s->sun_diffuse > 0) {
    const float nl = dot(n, w.s->sun_dir);
    if (nl > 0 && !gi_trace(w, x + n * w.opt->ray_bias, w.s->sun_dir, w.opt->max_distance, nullptr))
      e += w.s->sun_color * (w.s->sun_diffuse * nl);
  }
  if (w.opt->point_lights) e += gi_point_lights(w, x + n * w.opt->ray_bias, n, w.opt->shadow_point_lights);
  return albedo * e;
}

bool gi_inside(const GiOccluder &b, Vec3 p) {
  if (b.kind == 1) return length_sq(p - b.center) <= b.radius * b.radius;
  const Vec3 l = rotate(conjugate(b.rot), p - b.center);
  return std::fabs(l.x) <= b.half.x && std::fabs(l.y) <= b.half.y && std::fabs(l.z) <= b.half.z;
}

// Tek sondanin bake'i. Toplama sirasi SABIT (isin 0..N-1, sonra dogrudan
// terimler): ayni girdi -> ayni bayt.
void gi_bake_probe(GiWorld &w, Vec3 p, SceneBlobGiProbe *out) {
  const SceneGiOptions &opt = *w.opt;
  float e[6][3] = {};
  // Gunes gorunurlugu: siddetten BAGIMSIZ olculur (gunes kapaliyken de
  // gecerli bir sayi kalsin; kapinin kontrol kolu bunu okuyor).
  const float sun_vis = gi_trace(w, p, w.s->sun_dir, opt.max_distance, nullptr) ? 0.0f : 1.0f;
  // Kure ornekleri: Fibonacci spirali (parametre +Y ekseninde), tamamen
  // belirlenimli — tohum/rastgele sayi yok.
  const float ga = 2.39996322972865332f; // altin aci
  const float inv_n = 1.0f / (float)opt.rays;
  for (uint32_t i = 0; i < opt.rays; i++) {
    const float y = 1.0f - 2.0f * ((float)i + 0.5f) * inv_n;
    const float r2 = 1.0f - y * y;
    const float r = std::sqrt(r2 > 0 ? r2 : 0.0f);
    const float phi = ga * (float)i;
    const Vec3 d{r * std::cos(phi), y, r * std::sin(phi)};
    GiHit h;
    Vec3 L;
    if (gi_trace(w, p, d, opt.max_distance, &h)) {
      L = opt.bounces > 0 ? gi_surface_radiance(w, p + d * h.t, h.n, h.albedo) : Vec3{0, 0, 0};
    } else {
      L = w.s->ambient; // gokyuzu: her yonde esit isima
    }
    const uint32_t fx = d.x >= 0 ? 0u : 1u, fy = d.y >= 0 ? 2u : 3u, fz = d.z >= 0 ? 4u : 5u;
    const float cx = d.x >= 0 ? d.x : -d.x, cy = d.y >= 0 ? d.y : -d.y, cz = d.z >= 0 ? d.z : -d.z;
    e[fx][0] += L.x * cx; e[fx][1] += L.y * cx; e[fx][2] += L.z * cx;
    e[fy][0] += L.x * cy; e[fy][1] += L.y * cy; e[fy][2] += L.z * cy;
    e[fz][0] += L.x * cz; e[fz][1] += L.y * cz; e[fz][2] += L.z * cz;
  }
  // Monte Carlo normalizasyonu: E(n) = (4/N) * sum L_i * max(0, n . w_i).
  // Sabit L ile E(n) = L (birim sinamasi: gokyuzu = ambient -> sonda = ambient).
  const float k = 4.0f * inv_n;
  for (uint32_t f = 0; f < 6; f++)
    for (uint32_t c = 0; c < 3; c++) e[f][c] *= k;
  // Dogrudan gunes (istege bagli; gi_flags bit0 bunu soyler).
  if (opt.direct_sun && sun_vis > 0 && w.s->sun_diffuse > 0) {
    const Vec3 S = w.s->sun_color * (w.s->sun_diffuse * sun_vis);
    for (uint32_t f = 0; f < 6; f++) {
      const float c = dot(gi_face_dir(f), w.s->sun_dir);
      if (c <= 0) continue;
      e[f][0] += S.x * c; e[f][1] += S.y * c; e[f][2] += S.z * c;
    }
  }
  // Dogrudan nokta isiklar (yuz basina; gorunurluk isini istege bagli).
  if (opt.point_lights) {
    for (uint32_t f = 0; f < 6; f++) {
      const Vec3 c = gi_point_lights(w, p, gi_face_dir(f), opt.shadow_point_lights);
      e[f][0] += c.x; e[f][1] += c.y; e[f][2] += c.z;
    }
  }
  for (uint32_t f = 0; f < 6; f++)
    for (uint32_t c = 0; c < 3; c++) out->face[f][c] = e[f][c] > 0 ? e[f][c] : 0.0f;
  out->sun_vis = sun_vis;
  out->flags = 0;
}
} // namespace

uint32_t scene_gi_occluders(const SceneDesc &d, const SceneGiOptions &opt, GiOccluder *out, uint32_t max) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < d.entity_count && n < max; i++) {
    const SceneEntity &e = d.entities[i];
    if (!(e.components & kSceneBody) || e.dynamic) continue; // dinamik govde bake'e girmez
    GiOccluder o;
    const Mat4 wm = scene_entity_world_matrix(d, i);
    const Vec3 ws = scene_entity_world_scale(d, i);
    o.center = {wm.m[3][0], wm.m[3][1], wm.m[3][2]}; // scene_spawn_bodies ile ayni: govde DUNYA konumu
    o.rot = scene_entity_world_rotation(d, i);
    o.kind = e.shape == SceneShape::Sphere ? 1u : 0u;
    o.half = e.half * ws;
    o.radius = e.radius * ws.x;
    o.albedo = (e.components & kSceneModel) ? gi_srgb_to_linear(e.tint) : Vec3{opt.surface_albedo, opt.surface_albedo, opt.surface_albedo};
    out[n++] = o;
  }
  return n;
}

uint32_t scene_gi_model_tris(const SceneDesc &d, const SceneGiOptions &opt, const Model *const *models, uint32_t asset_count, GiTri *out,
                             uint32_t max) {
  (void)opt;
  if (!out) max = 0xFFFFFFFFu; // sayim gecisi: yazmadan yalniz ucgen say
  uint32_t n = 0;
  for (uint32_t i = 0; i < d.entity_count && n < max; i++) {
    const SceneEntity &e = d.entities[i];
    if (!(e.components & kSceneModel) || e.asset < 0 || (uint32_t)e.asset >= asset_count) continue;
    const Model *m = models[e.asset];
    if (!m) continue;
    const Mat4 em = scene_entity_world_matrix(d, i);
    const Vec3 tint = gi_srgb_to_linear(e.tint);
    for (uint32_t k = 0; k < m->instance_count && n < max; k++) {
      const ModelInstance &in = m->instances[k];
      if (in.mesh >= m->mesh_count) continue;
      const ModelMesh &me = m->meshes[in.mesh];
      if (me.skin >= 0) continue; // iskeletli mesh poz basina degisir: bake edilemez
      const Mat4 wm = em * in.world;
      Vec3 base{1, 1, 1};
      if (me.material >= 0 && (uint32_t)me.material < m->material_count)
        base = gi_srgb_to_linear(m->materials[me.material].base_color); // create_material ile ayni
      const Vec3 albedo = tint * base;
      for (uint32_t t = 0; t + 2 < me.index_count && n < max; t += 3) {
        if (!out) { n++; continue; }
        GiTri tri;
        tri.a = transform_point(wm, me.verts[me.indices[t]].pos);
        tri.b = transform_point(wm, me.verts[me.indices[t + 1]].pos);
        tri.c = transform_point(wm, me.verts[me.indices[t + 2]].pos);
        tri.albedo = albedo;
        out[n++] = tri;
      }
    }
  }
  return n;
}

uint32_t scene_gi_lights(const SceneDesc &d, GiLight *out, uint32_t max) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < d.entity_count && n < max; i++) {
    const SceneEntity &e = d.entities[i];
    if (!(e.components & kSceneLight)) continue;
    const Mat4 m = scene_entity_world_matrix(d, i);
    GiLight l;
    l.pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
    l.color = e.light_color;
    l.intensity = e.light_intensity;
    l.radius = e.light_radius;
    out[n++] = l;
  }
  return n;
}

void scene_gi_setup(const SceneDesc &d, GiScene *g) {
  g->sun_dir = normalize(d.sun_dir);
  if (length_sq(g->sun_dir) < 0.5f) g->sun_dir = {0, 1, 0};
  g->sun_color = {1, 1, 1};
  g->sun_diffuse = d.sun_diffuse;
  g->ambient = d.ambient; // set_light ambient'i donusturmez: zaten dogrusal
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const SceneBounds wb = scene_world_bounds(scene_entity_local_bounds(d.entities[i], nullptr), scene_entity_world_matrix(d, i));
    lo = vmin(lo, wb.lo);
    hi = vmax(hi, wb.hi);
  }
  if (d.entity_count == 0) { lo = {0, 0, 0}; hi = {0, 0, 0}; }
  g->bounds_lo = lo;
  g->bounds_hi = hi;
}

bool scene_gi_bake(Arena &arena, const GiScene &g, const SceneGiOptions &opt, SceneBlobExtras *x, SceneGiReport *rep) {
  SceneGiReport local;
  if (!rep) rep = &local;
  *rep = SceneGiReport{};
  if (!x) return false;
  if (opt.rays == 0 || opt.max_probes == 0 || !(opt.spacing > 0)) return false;
  const Vec3 mg{opt.margin, opt.margin, opt.margin};
  const Vec3 lo = g.bounds_lo - mg, hi = g.bounds_hi + mg;
  if (hi.x < lo.x || hi.y < lo.y || hi.z < lo.z) return false;
  // Izgara: adim, sonda tavanina sigana kadar buyutulur (belirlenimli dizi).
  float sp = opt.spacing;
  uint32_t dim[3] = {1, 1, 1};
  const uint32_t cap = opt.max_probes < kGiMaxProbes ? opt.max_probes : kGiMaxProbes;
  for (uint32_t guard = 0; guard < 64; guard++) {
    for (uint32_t a = 0; a < 3; a++) {
      const double ext = (double)(axis_of(hi, a) - axis_of(lo, a));
      double c = ext / (double)sp + 2.0;
      if (c > 1024.0) c = 1024.0;
      dim[a] = (uint32_t)c;
      if (dim[a] < 2) dim[a] = 2;
    }
    if ((uint64_t)dim[0] * dim[1] * dim[2] <= (uint64_t)cap) break;
    sp *= 1.5f;
  }
  const uint64_t count64 = (uint64_t)dim[0] * dim[1] * dim[2];
  if (count64 == 0 || count64 > (uint64_t)cap) return false;
  const uint32_t count = (uint32_t)count64;
  // Izgarayi hacmin ortasina yerlestir (kenarlar esit tasar).
  const Vec3 c = (lo + hi) * 0.5f;
  const Vec3 origin{c.x - (float)(dim[0] - 1) * sp * 0.5f, c.y - (float)(dim[1] - 1) * sp * 0.5f,
                    c.z - (float)(dim[2] - 1) * sp * 0.5f};

  SceneBlobGiProbe *probes = arena.alloc_array_zeroed<SceneBlobGiProbe>(count);
  if (!probes) return false;
  GiWorld w;
  w.s = &g;
  w.opt = &opt;
  if (!gi_bvh_build(arena, g.tris, g.tri_count, &w.bvh)) return false;

  const uint64_t t0 = platform::now_ns();
  uint32_t inside = 0;
  float min_luma = 1e30f, max_luma = 0;
  for (uint32_t zi = 0; zi < dim[2]; zi++)
    for (uint32_t yi = 0; yi < dim[1]; yi++)
      for (uint32_t xi = 0; xi < dim[0]; xi++) {
        const uint32_t idx = (zi * dim[1] + yi) * dim[0] + xi;
        const Vec3 p{origin.x + (float)xi * sp, origin.y + (float)yi * sp, origin.z + (float)zi * sp};
        bool solid = false;
        for (uint32_t i = 0; i < g.occluder_count && !solid; i++) solid = gi_inside(g.occluders[i], p);
        if (solid) {
          probes[idx] = SceneBlobGiProbe{};
          probes[idx].flags = 1u; // kati icinde: gecersiz, isin atilmaz
          inside++;
          continue;
        }
        gi_bake_probe(w, p, &probes[idx]);
        for (uint32_t f = 0; f < 6; f++) {
          const float lum = 0.2126f * probes[idx].face[f][0] + 0.7152f * probes[idx].face[f][1] + 0.0722f * probes[idx].face[f][2];
          if (lum < min_luma) min_luma = lum;
          if (lum > max_luma) max_luma = lum;
        }
      }
  const uint64_t t1 = platform::now_ns();

  x->gi_probes = probes;
  x->gi_probe_count = count;
  x->gi_dim[0] = dim[0];
  x->gi_dim[1] = dim[1];
  x->gi_dim[2] = dim[2];
  x->gi_origin[0] = origin.x;
  x->gi_origin[1] = origin.y;
  x->gi_origin[2] = origin.z;
  x->gi_spacing = sp;
  x->gi_rays = opt.rays;
  x->gi_bounces = opt.bounces;
  x->gi_valid = count - inside;
  x->gi_flags = 0;
  if (opt.direct_sun) x->gi_flags |= kGiDirectSun;
  if (opt.point_lights) x->gi_flags |= kGiPointLights;
  if (g.tri_count) x->gi_flags |= kGiModelTris;
  if (opt.bounces > 0) x->gi_flags |= kGiBounce;

  rep->probes = count;
  rep->probes_inside = inside;
  rep->dim[0] = dim[0];
  rep->dim[1] = dim[1];
  rep->dim[2] = dim[2];
  rep->spacing = sp;
  rep->origin[0] = origin.x;
  rep->origin[1] = origin.y;
  rep->origin[2] = origin.z;
  rep->occluders = g.occluder_count;
  rep->triangles = g.tri_count;
  rep->lights = g.light_count;
  rep->bvh_nodes = w.bvh.node_count;
  rep->rays = w.rays;
  rep->seconds = (double)(t1 - t0) * 1e-9;
  rep->hash = content_fnv1a(probes, sizeof(SceneBlobGiProbe) * (size_t)count);
  rep->min_luma = min_luma > 1e29f ? 0.0f : min_luma;
  rep->max_luma = max_luma;
  return true;
}

bool scene_compile(Arena &arena, const SceneDesc &d, const char *dir, const SceneCompileOptions &opt, SceneBlobExtras *out,
                   SceneCompileReport *rep) {
  SceneCompileReport local;
  if (!rep) rep = &local;
  *rep = SceneCompileReport{};
  *out = SceneBlobExtras{};

  // --- 1) Yerlesik kume: her kaynagi bir kez yukle, arenadan aldigi bayti OLC.
  if (opt.measure_resident && d.asset_count) {
    SceneBlobResident *res = arena.alloc_array_zeroed<SceneBlobResident>(d.asset_count);
    if (!res) return false;
    const GltfLimits limits{};
    const MeasureSettings st = measure_settings(limits);
    const size_t mark = arena.mark(); // olcum bellegi her kaynaktan sonra geri sarilir
    char path[1024];
    for (uint32_t i = 0; i < d.asset_count; i++) {
      res[i].asset = i;
      std::snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", d.assets[i]);
      const uint64_t key = opt.cache ? import_key(path, &st, sizeof st, kMeasureRecordVersion) : 0;
      ImportProduct prod;
      bool have = false;
      if (key && import_lookup(*opt.cache, key, ".olcum", &prod)) {
        SceneBlobResident rec{};
        FILE *f = std::fopen(prod.path, "rb");
        if (f) {
          have = std::fread(&rec, 1, sizeof rec, f) == sizeof rec;
          std::fclose(f);
        }
        if (have) { // ONBELLEK ISABETI: glTF hic acilmadi
          rec.asset = i;
          res[i] = rec;
          rep->cache_hits++;
        }
      }
      if (!have) {
        if (opt.cache) rep->cache_misses++;
        const size_t before = arena.used();
        Model m;
        if (gltf_load(arena, path, &m, limits)) {
          measure_model(m, &res[i]);
          res[i].cpu_bytes = (uint32_t)(arena.used() - before);
        } else {
          res[i].flags |= 1u; // acilamadi: sayilar 0, derleme surer
        }
        arena.reset_to(mark);
        if (key && !(res[i].flags & 1u)) {
          FILE *f = std::fopen(prod.path, "wb");
          if (f) {
            SceneBlobResident rec = res[i];
            rec.asset = 0; // kayit kaynaktan bagimsiz (anahtar dosyanin kendisi)
            const bool w = std::fwrite(&rec, 1, sizeof rec, f) == sizeof rec;
            std::fclose(f);
            if (w) import_store(*opt.cache, prod);
          }
        }
      }
      if (res[i].flags & 1u) {
        rep->assets_missing++;
        out->budget_flags |= 1u;
      } else {
        rep->assets_measured++;
        rep->resident_cpu += res[i].cpu_bytes;
        rep->resident_gpu += res[i].gpu_vertex + res[i].gpu_index + res[i].gpu_texture;
        if (res[i].transient > rep->peak_transient) rep->peak_transient = res[i].transient;
      }
    }
    rep->peak_bytes = rep->resident_cpu + rep->resident_gpu + rep->peak_transient;
    out->residents = res;
    out->resident_count = d.asset_count;
    out->resident_cpu = rep->resident_cpu;
    out->resident_gpu = rep->resident_gpu;
    out->peak_transient = rep->peak_transient;
    out->peak_bytes = rep->peak_bytes;
  }

  // --- 2) Kume (cluster) DAG bake: her kaynagin iskeletsiz mesh'leri.
  // Kaynaklar burada yeniden yuklenir ve arenada KALIR (DAG dizileri onlarin
  // vertex tamponuna degil kendi kopyalarina bakar, ama model bellegini geri
  // sarmak DAG'i da silerdi) — offline arac yolu, kabul.
  if (opt.build_cluster_dag && d.asset_count) {
    static_assert(kSceneDagMaxMeshes >= 8, "en az birkac mesh");
    ClusterDagBakeItem *items = arena.alloc_array_zeroed<ClusterDagBakeItem>(kSceneDagMaxMeshes);
    ClusterDag *dags = arena.alloc_array_zeroed<ClusterDag>(kSceneDagMaxMeshes);
    if (!items || !dags) return false;
    const ClusterDagOptions dopt = cluster_dag_preset(opt.dag_class);
    GltfLimits limits{};
    uint32_t n = 0;
    char path[1024];
    for (uint32_t i = 0; i < d.asset_count && n < kSceneDagMaxMeshes; i++) {
      std::snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", d.assets[i]);
      Model m;
      if (!gltf_load(arena, path, &m, limits)) continue;
      for (uint32_t k = 0; k < m.mesh_count && n < kSceneDagMaxMeshes; k++) {
        if (m.meshes[k].skin >= 0) continue; // iskeletli mesh'te DAG yok (poz basina degisir)
        dags[n] = ClusterDag{};
        if (!cluster_dag_build(arena, m.meshes[k], dopt, &dags[n])) continue;
        items[n].dag = &dags[n];
        items[n].asset = i;
        items[n].mesh = k;
        n++;
      }
    }
    if (n && cluster_dag_bake(arena, items, n, out)) {
      out->dag_device_class = (uint32_t)opt.dag_class;
      rep->dag_meshes = n;
      rep->dag_nodes = out->dag_node_count;
      rep->dag_indices = out->dag_index_count;
      rep->dag_children = out->dag_child_count;
      rep->dag_levels = out->dag_levels;
      rep->dag_bytes = (uint32_t)(sizeof(SceneBlobDagMesh) * n + sizeof(SceneBlobDagNode) * out->dag_node_count +
                                  sizeof(uint32_t) * (out->dag_index_count + out->dag_child_count));
      uint64_t h = kFnvSeed;
      for (uint32_t i = 0; i < n; i++) {
        const ClusterDag &g = *items[i].dag;
        h = content_fnv1a(&g.hash, sizeof g.hash, h);
        if (g.levels) rep->dag_top_tris += g.level_tris[g.levels - 1];
      }
      rep->dag_hash = h;
    }
  }

  // --- 3) Navmesh bake: sabit kutu govdeler -> ucgen corbasi -> Detour verisi.
  if (opt.bake_nav) {
    float *verts = arena.alloc_array<float>(kSceneNavMaxVerts * 3);
    int *tris = arena.alloc_array<int>(kSceneNavMaxTris * 3);
    if (!verts || !tris) return false;
    uint32_t nv = 0;
    const uint32_t nt = scene_nav_soup(d, verts, kSceneNavMaxVerts, tris, kSceneNavMaxTris, &nv);
    rep->nav_tris = nt;
    void *data = nullptr;
    uint32_t size = 0, polys = 0, nverts = 0;
    if (nt && scene_navmesh_bake(arena, verts, (int)nv, tris, (int)nt, opt.nav, &data, &size, &polys, &nverts, rep->nav_error,
                                 sizeof rep->nav_error)) {
      rep->nav_ok = true;
      rep->nav_polys = polys;
      rep->nav_verts = nverts;
      rep->nav_bytes = size;
      rep->nav_hash = content_fnv1a(data, size);
      out->nav_data = data;
      out->nav_size = size;
      out->nav_polys = polys;
      out->nav_verts = nverts;
      out->nav_tris = nt;
      out->nav_agent_radius = opt.nav.agent_radius;
      out->nav_agent_height = opt.nav.agent_height;
      out->nav_cell_size = opt.nav.cell_size;
      out->nav_agent_climb = opt.nav.agent_max_climb;
    } else if (!nt) {
      std::snprintf(rep->nav_error, sizeof rep->nav_error, "navmesh bake: sahnede sabit kutu govde yok (bake atlandi)");
    }
  }
  // --- 4) GI sonda bake'i: sahne geometrisinde CPU isin izlemesi (gi.hpp).
  // En sonda durur: model ucgenleri arenada KALIR (tikayici olarak bake
  // boyunca lazim), yani bu adimdan sonra geri sarilacak bir sey yok.
  if (opt.bake_gi) {
    GiScene g;
    scene_gi_setup(d, &g);
    GiOccluder *occ = arena.alloc_array<GiOccluder>(kGiMaxOccluders);
    GiLight *gl = arena.alloc_array<GiLight>(kGiMaxLights);
    if (!occ || !gl) return false;
    g.occluders = occ;
    g.occluder_count = scene_gi_occluders(d, opt.gi, occ, kGiMaxOccluders);
    g.lights = gl;
    g.light_count = scene_gi_lights(d, gl, kGiMaxLights);
    if (opt.gi_include_models && d.asset_count) {
      Model *loaded = arena.alloc_array_zeroed<Model>(d.asset_count);
      const Model **ptrs = arena.alloc_array_zeroed<const Model *>(d.asset_count);
      if (!loaded || !ptrs) return false;
      GltfLimits limits{};
      char path[1024];
      for (uint32_t i = 0; i < d.asset_count; i++) {
        std::snprintf(path, sizeof path, "%s/%s", dir && *dir ? dir : ".", d.assets[i]);
        Model m;
        if (!gltf_load(arena, path, &m, limits)) continue;
        loaded[i] = m;
        ptrs[i] = &loaded[i];
      }
      // Once SAY, sonra tam boyutta ayir: sabit ucgen tavani yok.
      const uint32_t nt = scene_gi_model_tris(d, opt.gi, ptrs, d.asset_count, nullptr, 0);
      if (nt) {
        GiTri *tris = arena.alloc_array<GiTri>(nt);
        if (!tris) return false;
        g.tris = tris;
        g.tri_count = scene_gi_model_tris(d, opt.gi, ptrs, d.asset_count, tris, nt);
      }
    }
    if (scene_gi_bake(arena, g, opt.gi, out, &rep->gi)) {
      rep->gi_ok = true;
      rep->gi_bytes = (uint32_t)(sizeof(SceneBlobGiProbe) * (size_t)rep->gi.probes);
    }
  }

  return true;
}

} // namespace tulpar::engine::content
