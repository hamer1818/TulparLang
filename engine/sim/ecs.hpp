// L4 SIMULATION — Archetype ECS (SoA). Plan §1.5 "ADAPTE": archetype'lar
// runtime'da kurulmaz, derleyici uretir — o Tulpar alt kumesi gelene kadar
// (PLAN.md §11) C++ SoA ayni API'yi verir. Butun kapasiteler init'te
// arenadan (A2): entity tablosu, archetype tablosu, chunk havuzu. Kare
// icinde ayirma yok; kapasite dolunca create() gecersiz handle doner ve
// sayilir (sessiz degil).
//
// Yerlesim: archetype = ayni bilesen kumesine sahip entity'ler; her archetype
// sabit boyutlu chunk'lardan olusur, chunk icinde bilesen sutunlari SoA
// (sutun basina bitisik dizi). Silme: swap-remove (siralama korunmaz —
// belirlenimlilik icin sistemler sirayi degil VERIYI kullanir).
#pragma once
#include <cstdint>
#include <cstring>
#include <new>

#include "core/memory/arena.hpp"
#include "core/memory/pool.hpp"

namespace tulpar::engine::sim {

using Entity = Handle;
using ComponentId = uint32_t;
using ComponentMask = uint64_t;
constexpr uint32_t kMaxComponents = 64;
constexpr ComponentId kInvalidComponent = UINT32_MAX;

inline ComponentMask mask_of(ComponentId id) { return 1ull << id; }

// Global bilesen kaydi (tur -> id). Belirlenimli: kayit sirasi = id.
struct ComponentRegistry {
  struct Info {
    const char *name;
    uint32_t size;
    uint32_t align;
  };
  static ComponentId add(const char *name, uint32_t size, uint32_t align);
  static const Info &info(ComponentId id);
  static uint32_t count();
};

template <class T> struct ComponentTag {
  static ComponentId id;
};
template <class T> ComponentId ComponentTag<T>::id = kInvalidComponent;

// Tip icin id: ilk cagri kaydeder (ad verilmeli), sonrakiler doner.
template <class T> ComponentId component_id(const char *name = nullptr) {
  if (ComponentTag<T>::id == kInvalidComponent) {
    ENGINE_ASSERT_MSG(name != nullptr, "bilesen ilk kez kaydedilirken ad gerekli");
    ComponentTag<T>::id = ComponentRegistry::add(name, sizeof(T), alignof(T));
  }
  return ComponentTag<T>::id;
}

// --- Ic yapilar (World'den ONCE: Clang bagimli olmayan adlari sablon tanim
// aninda cozer, tam tip ister; GCC gecirdi, macOS clang gecirmedi — 2026-09-14).
struct Chunk {
  uint8_t *data = nullptr;   // chunk_bytes
  uint32_t count = 0;
  uint32_t capacity = 0;
  Chunk *next = nullptr;
};

struct Archetype {
  ComponentMask mask = 0;
  uint32_t comp_count = 0;
  ComponentId comps[kMaxComponents];
  uint32_t offsets[kMaxComponents]; // chunk icinde sutun baslangici
  uint32_t entities_offset = 0;     // Entity sutunu
  uint32_t row_capacity = 0;        // chunk basina satir
  Chunk *first = nullptr;
  Chunk *last = nullptr;
  uint32_t chunk_n = 0;
};

// Bir chunk'a SoA gorunum: sistemler bununla gezer.
struct ChunkView {
  uint32_t count = 0;
  const Entity *entities = nullptr;
  Archetype *arch = nullptr;
  Chunk *chunk = nullptr;
  void *column(ComponentId id) const {
    for (uint32_t i = 0; i < arch->comp_count; i++)
      if (arch->comps[i] == id) return chunk->data + arch->offsets[i];
    return nullptr;
  }
  template <class T> T *col() const { return static_cast<T *>(column(ComponentTag<T>::id)); }
};

struct WorldConfig {
  uint32_t max_entities = 4096;
  uint32_t max_archetypes = 64;
  uint32_t chunk_bytes = 16 * 1024;
  uint32_t max_chunks = 256;
};

struct WorldStats {
  uint32_t entities = 0;
  uint32_t archetypes = 0;
  uint32_t chunks_used = 0;
  uint32_t create_failed = 0; // kapasite (entity ya da chunk) yetmedi
};

class World {
public:
  bool init(Arena &arena, const WorldConfig &cfg);

  Entity create(ComponentMask mask);
  void destroy(Entity e);
  bool alive(Entity e) const;
  ComponentMask mask_of_entity(Entity e) const;

  // Bilesen isaretcisi: YAPISAL degisiklige (create/destroy) kadar gecerli.
  void *get(Entity e, ComponentId id);
  template <class T> T *get(Entity e) { return static_cast<T *>(get(e, ComponentTag<T>::id)); }

  // mask'in TAMAMINI iceren her archetype'in her chunk'i icin fn(ChunkView).
  template <class F> void each(ComponentMask mask, F &&fn) {
    for (uint32_t a = 0; a < arch_count_; a++) {
      Archetype &ar = archs_[a];
      if ((archetype_mask(ar) & mask) != mask) continue;
      for_each_chunk(ar, fn);
    }
  }
  const WorldStats &stats() const { return stats_; }

  // Belirlenimlilik: dunyanin icerik ozeti (replay testi icin). Sira
  // archetype/chunk/satir sirasi — ayni islem dizisi ayni ozeti verir.
  uint64_t content_hash() const;

private:
  struct EntityRec {
    uint32_t arch;   // archetype indeksi
    uint32_t chunk;  // archetype icindeki chunk indeksi
    uint32_t row;
    uint32_t gen;    // nesil (0 = hic kullanilmadi)
    uint8_t alive;
  };
  friend struct ChunkView;
  ComponentMask archetype_mask(const Archetype &a) const;
  Archetype *find_or_create_archetype(ComponentMask mask);
  Chunk *chunk_with_room(Archetype &a, uint32_t *chunk_index);
  template <class F> void for_each_chunk(Archetype &a, F &fn);

  Arena *arena_ = nullptr;
  WorldConfig cfg_{};
  EntityRec *entities_ = nullptr;
  uint32_t *free_entities_ = nullptr;
  uint32_t free_top_ = 0;
  Archetype *archs_ = nullptr;
  uint32_t arch_count_ = 0;
  Chunk *chunks_ = nullptr;   // havuz
  uint32_t chunk_count_ = 0;
  WorldStats stats_{};
};

template <class F> void World::for_each_chunk(Archetype &a, F &fn) {
  for (Chunk *c = a.first; c; c = c->next) {
    if (c->count == 0) continue;
    ChunkView v;
    v.count = c->count;
    v.entities = reinterpret_cast<const Entity *>(c->data + a.entities_offset);
    v.arch = &a;
    v.chunk = c;
    fn(v);
  }
}

} // namespace tulpar::engine::sim
