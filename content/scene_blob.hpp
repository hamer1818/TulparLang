// L6 CONTENT — Derlenmis sahne (.sahneb): PLAN §6 "sahne bir blob + kod".
// Yazar formati (.sahne metni, SceneDesc) OFFLINE derlenir; runtime metni
// yorumlamaz, blob'u oldugu gibi belleğe alir ve isaretci uzerinden okur:
// ayirma yok, ayristirma yok, turetilmis veri (dunya matrisi, kuaterniyon,
// olcekli govde boyutu, isik konumu) derleme aninda hesaplanmistir; bilesen
// tablolari (cizim / animasyon / isik / govde) varlik taramasi gerektirmez.
//
// Surum 2 (Faz 6): basliga icerik butcesi (yerlesik kume = kaynak basina CPU/GPU
// bayt, tepe bellek) ve BAKE EDILMIS NAVMESH bolumu eklendi. Runtime bake
// YAPMAZ: Recast bake'i sahne derleyicisinin isidir (scene_compile), runtime
// yalniz blob'daki Detour verisini sorgular (SceneNav).
//
// Surum 3 (Faz 9): KUME (cluster) DAG bolumu — mesh basina kume agaci, her
// dugumde sinir kuresi + normal konisi + MUTLAK hata; GPU cull + indirect'in
// okudugu veri. Bake sahne derleyicisinde (content/cluster_dag.hpp); LOD cut
// secimi runtime'in (cull pass) isidir, blob'da SECIM yok, yalniz hata durur.
//
// Surum 4 (Faz 6): GI SONDA bolumu — sahne icin isima (irradiance) sondalari
// DERLEME aninda CPU isin izlemesiyle hesaplanir (content/gi.hpp), runtime
// hesaplamaz: yalniz dunya konumundan okur (content::SceneGi). Gosterim 6
// yonlu ambient cube; NEDEN'i gi.hpp basliginda.
//
// Yerlesim: kucuk-endian, tum alanlar 4 baytlik (u32 / f32 / i32) — 8 baytlik
// hizalama sarti yok, ozet iki u32. Bolumler 16 bayt hizali, dolgu 0. Ayni
// SceneDesc -> ayni baytlar (deterministik; kapi test_scene'de).
// Butunluk: magic, surum, endian isareti, toplam boyut, FNV-1a 64 ozeti
// (ozet alanindan sonraki tum baytlar), tablo sinirlari, dizin tutarliligi.
#pragma once
#include <cstddef>
#include <cstdint>

#include "content/scene.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kSceneBlobMagic = 0x4E485354u;   // "TSHN" (LE)
constexpr uint32_t kSceneBlobVersion = 6; // v6: 6 yeni bilesen eklendi (particle, terrain, voxel, water, wind, character);
                                          // v5: ilkel geometri + malzeme;
constexpr uint32_t kSceneBlobEndian = 0x01020304u;
constexpr uint32_t kSceneBlobAlign = 16;
constexpr uint32_t kGiBlobMaxProbes = 32768; // GI sonda tablosu ust siniri (dosya formati)

struct SceneBlobHeader {
  uint32_t magic, version, total_size, endian;
  uint32_t hash_lo, hash_hi; // FNV-1a 64: [hash_end, total_size)
  uint32_t header_size, reserved0;
  // dunya
  float sun_dir[3], sun_diffuse;
  float ambient[3], shadow_radius;
  float shadow_center[3], shadow_depth;
  float cam_target[3], cam_yaw;
  float cam_pitch, cam_radius, reserved1, reserved2;
  // tablolar: sayi + bayt ofseti (blob basindan; 16 hizali)
  uint32_t asset_count, asset_offset;   // SceneBlobAsset[]
  uint32_t entity_count, entity_offset; // SceneBlobEntity[]
  uint32_t draw_count, draw_offset;     // SceneBlobDraw[]   (model bilesenli varliklar)
  uint32_t anim_count, anim_offset;     // SceneBlobAnim[]   (animasyon bilesenli)
  uint32_t light_count, light_offset;   // SceneBlobLight[]  (isik bilesenli)
  uint32_t body_count, body_offset;     // SceneBlobBody[]   (govde bilesenli)
  
  // --- v6: 6 yeni bilesen (prosedurel ve arkaplan) ---
  uint32_t particle_count, particle_offset;
  uint32_t terrain_count, terrain_offset;
  uint32_t voxel_count, voxel_offset;
  uint32_t water_count, water_offset;
  uint32_t wind_count, wind_offset;
  uint32_t character_count, character_offset;

  uint32_t string_offset, string_size;  // NUL sonlu metinler (yollar, adlar)
  uint32_t reserved5, reserved6;
  float bounds_lo[3], reserved3;        // tum varliklarin dunya AABB'si (isaret kutusu dahil)
  float bounds_hi[3], reserved4;
  // --- v2: icerik butcesi (derleme aninda OLCULDU; runtime tahmin etmez) ---
  uint32_t resident_count, resident_offset; // SceneBlobResident[] (kaynak basina)
  uint32_t resident_cpu, resident_gpu;      // bayt: CPU (arena) / GPU (tampon + doku)
  uint32_t peak_transient, peak_bytes;      // en buyuk tek gecici blok / tepe = yerlesik + gecici
  uint32_t budget_flags, reserved7;         // bit0: en az bir kaynak olculemedi (sayilar eksik)
  // --- v2: navmesh (Recast bake, derleme aninda; 16 hizali bolum) ---
  uint32_t nav_offset, nav_size;            // Detour navmesh verisi (0 = bake yok)
  uint32_t nav_polys, nav_verts;
  uint32_t nav_tris, nav_flags;             // bake girdisi ucgen sayisi
  uint32_t nav_reserved0, nav_reserved1;
  float nav_agent_radius, nav_agent_height, nav_cell_size, nav_agent_climb;
  // --- v3: kume (cluster) DAG (offline bake; runtime yalniz okur) ---
  uint32_t dag_mesh_count, dag_mesh_offset;   // SceneBlobDagMesh[] (kaynak, mesh)
  uint32_t dag_node_count, dag_node_offset;   // SceneBlobDagNode[] (butun mesh'ler ardisik)
  uint32_t dag_index_count, dag_index_offset; // u32 mesh vertex indeksleri
  uint32_t dag_child_count, dag_child_offset; // u32 dugum dizinleri (global)
  uint32_t dag_levels, dag_device_class;      // en derin seviye sayisi; 0 dusuk / 1 orta / 2 yuksek
  uint32_t dag_reserved0, dag_reserved1;
  // --- v4: GI sondalari (offline bake; runtime yalniz okur) ---
  uint32_t gi_probe_count, gi_probe_offset; // SceneBlobGiProbe[] (esit arali izgara)
  uint32_t gi_dim[3], gi_flags;             // izgara boyutu (x,y,z); SceneGiFlagBits
  float gi_origin[3], gi_spacing;           // ILK sondanin merkezi + adim (dunya birimi)
  uint32_t gi_rays, gi_bounces;             // bake ayarlari (sonda basina isin, sicrama)
  uint32_t gi_valid, gi_reserved0;          // kati disinda kalan (gecerli) sonda sayisi
  uint32_t gi_reserved1, gi_reserved2;
};
// GI sondasi: 6 yonlu ambient cube + dogrudan gunes gorunurlugu.
// Yuz sirasi +X,-X,+Y,-Y,+Z,-Z; deger E(n)/pi, DOGRUSAL RGB (gi.hpp sozlesmesi).
struct SceneBlobGiProbe {
  float face[6][3];
  float sun_vis;  // 0..1 (dogrudan gunes isini tikali mi)
  uint32_t flags; // bit0: sonda KATI icinde -> gecersiz (interpolasyonda agirlik 0)
};
// Kaynak basina yerlesik kume: "bu sahneyi yuklemek kac bayt?" sorusunun
// derleme aninda OLCULMUS cevabi (tahmin degil: derleyici kaynagi bir kez
// yukler ve arenadan aldigi baytlari sayar).
struct SceneBlobResident {
  uint32_t asset;      // kaynak dizini
  uint32_t cpu_bytes;  // yuklemenin arenadan aldigi bayt (olculdu)
  uint32_t gpu_vertex, gpu_index, gpu_texture, gpu_clip; // GPU yerlesik (paketlenmis vertex, mip zinciri dahil)
  uint32_t transient;  // bu kaynagin en buyuk tek gecici blogu (staging)
  uint32_t flags;      // bit0: kaynak acilamadi (sayilar 0)
};
// Kume DAG dugumu: cull hacmi (sinir kuresi + normal konisi) + MUTLAK hata.
// Cizim kurali (runtime, cull pass): error <= t < parent_error. Ayni `group`
// degerine sahip kumeler AYNI parent_error'u tasir, yani birlikte gecerler —
// catlak ve popping ikisi de grup ici bolunmus gecisten dogar.
struct SceneBlobDagNode {
  float center[3], radius;        // sinir kuresi (frustum / occlusion cull)
  float cone_apex[3], cone_cutoff; // normal konisi: cos(aci/2) (backface cull)
  float cone_axis[3], error;      // bu kumeyi cizmenin hatasi (dunya birimi)
  float parent_error;             // ustunun hatasi (kok: sonsuz)
  uint32_t index_offset, index_count; // DAG indeks tablosunda (mesh vertex indeksi)
  uint32_t level;
  uint32_t child_offset, child_count; // DAG cocuk tablosunda (global dugum dizini)
  uint32_t group, reserved;
};
// Mesh basina DAG dilimi: tablolardaki araliklar.
struct SceneBlobDagMesh {
  uint32_t asset, mesh, levels, flags;
  uint32_t node_first, node_count;
  uint32_t index_first, index_count;
  uint32_t child_first, child_count;
  uint32_t vertex_count, reserved; // kaynak mesh vertex sayisi (indeks siniri)
};
struct SceneBlobAsset {
  uint32_t path;     // string ofseti (string_offset'e gore)
  uint32_t path_len; // NUL haric
  uint32_t reserved[2];
};
// SAHNE AGACI BURADA YOKTUR (Faz E2): `.sahne`'deki ebeveyn/cocuk iliskisi
// DERLEME aninda duzlestirilir — asagidaki alanlarin hepsi DUNYA uzayindadir
// (scene_entity_world_matrix / _rotation / _scale). Runtime zincir yurumez,
// blob'da bir `parent` alani tasimaya da gerek yoktur; bu yuzden kayit boyu ve
// blob surumu Faz E2'de DEGISMEDI. Kok varlikta dunya = yerel (bit-tam), yani
// hiyerarsisiz sahnelerin blob baytlari da degismedi.
struct SceneBlobEntity {
  float world[16];   // T*Rz*Ry*Rx*S zinciri, Mat4 yerlesimi (sutun-major), DUNYA
  float pos[3];      uint32_t components; // dunya matrisinin cevirisi
  float quat[4];     // scene_entity_world_rotation
  float scale[3];    uint32_t name; // dunya olcegi / string ofseti
  int32_t draw, anim, light, body;  // tablo dizinleri, -1 = bilesen yok
};
struct SceneBlobDraw {
  uint32_t entity;
  int32_t asset;
  int32_t primitive;
  float tint[3];
  float metallic, roughness, reflectance;
  float emissive[3];
  float emissive_strength;
};
struct SceneBlobAnim {
  uint32_t entity, clip;
  float phase, speed;
};
struct SceneBlobLight {
  uint32_t entity; float pos[3]; // dunya konumu (matris cevirisi)
  float color[3], intensity;
  // reserved[0]: SceneLightType (0 Nokta, 1 Yonlu), float olarak (blob boyutunu
  // BUYUTMEMEK icin -- yeni alan degil, var olan bosluk yeniden kullanildi).
  float radius, reserved[3];
};
struct SceneBlobBody {
  uint32_t entity, shape, dynamic, reserved0;
  float half[3], radius;        // OLCEKLI (yazar olcegi uygulanmis; sim'e giden deger)
  float pos[3], reserved1;
  float quat[4];
  float scale[3], reserved2;    // yazar olcegi (cizim matrisi icin)
};

// --- v6 Yeni Bilesenler ---

struct SceneBlobParticle {
  uint32_t entity;
  float spawn_rate, lifetime_min, lifetime_max;
  float size_start, size_end;
  float velocity[3], jitter[3];
}; // 48 bayt

struct SceneBlobTerrain {
  uint32_t entity;
  float width, height, cell, amp, freq;
  int32_t octaves;
  uint32_t seed, reserved0, reserved1, reserved2, reserved3;
}; // 48 bayt

struct SceneBlobWater {
  uint32_t entity;
  float steepness, amplitude, wavelength;
  float direction[2];
  float reserved[2];
}; // 32 bayt

struct SceneBlobWind {
  uint32_t entity;
  float direction[2];
  float strength, gustiness, gust_freq;
  uint32_t seed;
  float reserved;
}; // 32 bayt

struct SceneBlobVoxel {
  uint32_t entity;
  uint32_t size_x, size_y, size_z;
  float cell;
  uint32_t reserved[3];
}; // 32 bayt

struct SceneBlobCharacter {
  uint32_t entity;
  float radius, height;
  float step_height;
}; // 16 bayt

// --------------------------
static_assert(sizeof(SceneBlobHeader) % kSceneBlobAlign == 0, "baslik 16 hizali");
static_assert(sizeof(SceneBlobResident) == 32, "yerlesik kaydi 32 bayt (dosya formati)");
static_assert(sizeof(SceneBlobDagNode) == 80 && sizeof(SceneBlobDagMesh) == 48, "DAG kayit boyutlari sabit (dosya formati)");
static_assert(sizeof(SceneBlobGiProbe) == 80, "GI sonda kaydi 80 bayt (dosya formati)");
static_assert(sizeof(SceneBlobEntity) == 128 && sizeof(SceneBlobLight) == 48 && sizeof(SceneBlobBody) == 80 &&
                  sizeof(SceneBlobAsset) == 16 && sizeof(SceneBlobDraw) == 52 && sizeof(SceneBlobAnim) == 16,
              "blob kayit boyutlari sabit (dosya formati)");
static_assert(sizeof(SceneBlobParticle) == 48 && sizeof(SceneBlobTerrain) == 48 && sizeof(SceneBlobWater) == 32 &&
                  sizeof(SceneBlobWind) == 32 && sizeof(SceneBlobVoxel) == 32 && sizeof(SceneBlobCharacter) == 16,
              "yeni blob kayit boyutlari 16 hizali (dosya formati)");

// Acilmis blob: isaretciler blob'un icine bakar (kopya yok). Blob bellegi
// gorunumden uzun yasamali ve 16 hizali olmali.
struct SceneBlobView {
  const SceneBlobHeader *h = nullptr;
  const SceneBlobAsset *assets = nullptr;
  const SceneBlobEntity *entities = nullptr;
  const SceneBlobDraw *draws = nullptr;
  const SceneBlobAnim *anims = nullptr;
  const SceneBlobLight *lights = nullptr;
  const SceneBlobBody *bodies = nullptr;
  const SceneBlobParticle *particles = nullptr;
  const SceneBlobTerrain *terrains = nullptr;
  const SceneBlobWater *waters = nullptr;
  const SceneBlobWind *winds = nullptr;
  const SceneBlobVoxel *voxels = nullptr;
  const SceneBlobCharacter *characters = nullptr;
  const SceneBlobResident *residents = nullptr;
  const uint8_t *nav = nullptr; // bake edilmis Detour verisi (SALT OKUNUR; nav_size bayt)
  const SceneBlobDagMesh *dag_meshes = nullptr;
  const SceneBlobDagNode *dag_nodes = nullptr;
  const uint32_t *dag_indices = nullptr;
  const uint32_t *dag_children = nullptr;
  const SceneBlobGiProbe *gi_probes = nullptr;
  const char *strings = nullptr;
  const char *str(uint32_t off) const { return strings + off; }
  const char *asset_path(uint32_t i) const { return str(assets[i].path); }
  const char *entity_name(uint32_t i) const { return str(entities[i].name); }
  uint64_t hash() const { return ((uint64_t)h->hash_hi << 32) | h->hash_lo; }
  SceneWorld world() const;
  Mat4 entity_matrix(uint32_t i) const;
  uint32_t nav_size() const { return h->nav_size; }
  bool has_nav() const { return nav != nullptr && h->nav_size > 0; }
  bool has_dag() const { return dag_meshes != nullptr && h->dag_mesh_count > 0; }
  bool has_gi() const { return gi_probes != nullptr && h->gi_probe_count > 0; }
};

// Derleyicinin urettigi EK bolumler (yerlesik kume + navmesh). Hepsi istege
// bagli: nullptr ile derlenen blob gecerlidir (sayilar 0, nav yok).
struct SceneBlobExtras {
  const SceneBlobResident *residents = nullptr;
  uint32_t resident_count = 0;
  uint32_t resident_cpu = 0, resident_gpu = 0, peak_transient = 0, peak_bytes = 0, budget_flags = 0;
  const void *nav_data = nullptr;
  uint32_t nav_size = 0, nav_polys = 0, nav_verts = 0, nav_tris = 0;
  float nav_agent_radius = 0, nav_agent_height = 0, nav_cell_size = 0, nav_agent_climb = 0;
  // v3: kume DAG (content/cluster_dag.hpp -> cluster_dag_bake)
  const SceneBlobDagMesh *dag_meshes = nullptr;
  const SceneBlobDagNode *dag_nodes = nullptr;
  const uint32_t *dag_indices = nullptr;
  const uint32_t *dag_children = nullptr;
  uint32_t dag_mesh_count = 0, dag_node_count = 0, dag_index_count = 0, dag_child_count = 0;
  uint32_t dag_levels = 0, dag_device_class = 0;
  // v4: GI sonda izgarasi (content/gi.hpp -> scene_gi_bake)
  const SceneBlobGiProbe *gi_probes = nullptr;
  uint32_t gi_probe_count = 0, gi_flags = 0, gi_rays = 0, gi_bounces = 0, gi_valid = 0;
  uint32_t gi_dim[3] = {0, 0, 0};
  float gi_origin[3] = {0, 0, 0};
  float gi_spacing = 0;
};

// SceneDesc -> blob. Donus: gereken boyut (snprintf gibi; cap yetmezse yazmaz).
size_t scene_blob_compile(const SceneDesc &d, void *buf, size_t cap);
size_t scene_blob_compile_ex(const SceneDesc &d, const SceneBlobExtras *x, void *buf, size_t cap);
// Butunluk denetimi + gorunum. Hata: err (satirsiz), false. data 16 hizali olmali.
bool scene_blob_open(const void *data, size_t size, SceneBlobView *out, SceneError *err);
// Dosya: derle + yaz / oku (arena, 16 hizali) + ac.
bool scene_blob_save(Arena &scratch, const SceneDesc &d, const char *path, SceneError *err);
bool scene_blob_save_ex(Arena &scratch, const SceneDesc &d, const SceneBlobExtras *x, const char *path, SceneError *err);
bool scene_blob_load(Arena &arena, const char *path, SceneBlobView *out, SceneError *err);
// ".sahne" -> ".sahneb" (uzanti yoksa ekler). Donus false: sigmadi.
bool scene_blob_path_for(const char *scene_path, char *out, size_t cap);
uint64_t scene_blob_fnv1a(const void *data, size_t n, uint64_t seed = 0xcbf29ce484222325ull);

} // namespace tulpar::engine::content
