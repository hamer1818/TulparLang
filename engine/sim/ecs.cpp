#include "sim/ecs.hpp"
#include <cstddef> // size_t — libc++ (macOS) gecisli getirmiyor

#include "platform/fatal.hpp"

namespace tulpar::engine::sim {

namespace {
ComponentRegistry::Info g_infos[kMaxComponents];
uint32_t g_info_count = 0;

uint64_t fnv1a(const void *p, size_t n, uint64_t h) {
  const uint8_t *b = static_cast<const uint8_t *>(p);
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 0x100000001b3ull;
  }
  return h;
}
} // namespace

ComponentId ComponentRegistry::add(const char *name, uint32_t size, uint32_t align) {
  ENGINE_ASSERT_MSG(g_info_count < kMaxComponents, "bilesen siniri (%u)", kMaxComponents);
  g_infos[g_info_count] = Info{name, size, align};
  return g_info_count++;
}
const ComponentRegistry::Info &ComponentRegistry::info(ComponentId id) {
  ENGINE_ASSERT(id < g_info_count);
  return g_infos[id];
}
uint32_t ComponentRegistry::count() { return g_info_count; }

bool World::init(Arena &arena, const WorldConfig &cfg) {
  arena_ = &arena;
  cfg_ = cfg;
  entities_ = arena.alloc_array_zeroed<EntityRec>(cfg.max_entities);
  free_entities_ = arena.alloc_array<uint32_t>(cfg.max_entities);
  archs_ = arena.alloc_array_zeroed<Archetype>(cfg.max_archetypes);
  chunks_ = arena.alloc_array_zeroed<Chunk>(cfg.max_chunks);
  if (!entities_ || !free_entities_ || !archs_ || !chunks_) return false;
  for (uint32_t i = 0; i < cfg.max_entities; i++) {
    free_entities_[i] = cfg.max_entities - 1 - i;
    entities_[i].gen = 1;
  }
  free_top_ = cfg.max_entities;
  // Chunk verisi: hepsi ONCEDEN (A2) — max_chunks * chunk_bytes.
  for (uint32_t i = 0; i < cfg.max_chunks; i++) {
    chunks_[i].data = static_cast<uint8_t *>(arena.alloc(cfg.chunk_bytes, 64));
    if (!chunks_[i].data) return false;
  }
  return true;
}

ComponentMask World::archetype_mask(const Archetype &a) const { return a.mask; }

Archetype *World::find_or_create_archetype(ComponentMask mask) {
  for (uint32_t i = 0; i < arch_count_; i++)
    if (archs_[i].mask == mask) return &archs_[i];
  if (arch_count_ >= cfg_.max_archetypes) return nullptr;
  Archetype &a = archs_[arch_count_];
  a.mask = mask;
  a.comp_count = 0;
  // Satir boyutu: entity + bilesenler (hizalama dahil); kapasite = chunk/satir.
  uint32_t row_bytes = sizeof(Entity);
  for (ComponentId id = 0; id < ComponentRegistry::count(); id++) {
    if (!(mask & mask_of(id))) continue;
    a.comps[a.comp_count++] = id;
    row_bytes += ComponentRegistry::info(id).size;
  }
  // Kapasiteyi bul: SoA sutunlar hizalanir, en kotu durum icin pay birak.
  uint32_t cap = (cfg_.chunk_bytes - 64 * (a.comp_count + 1)) / row_bytes;
  ENGINE_ASSERT_MSG(cap >= 1, "chunk (%u B) tek satiri bile almiyor", cfg_.chunk_bytes);
  a.row_capacity = cap;
  uint32_t off = 0;
  a.entities_offset = 0;
  off += (uint32_t)sizeof(Entity) * cap;
  for (uint32_t i = 0; i < a.comp_count; i++) {
    const ComponentRegistry::Info &inf = ComponentRegistry::info(a.comps[i]);
    off = (uint32_t)align_up(off, inf.align < 16 ? 16 : inf.align);
    a.offsets[i] = off;
    off += inf.size * cap;
  }
  ENGINE_ASSERT(off <= cfg_.chunk_bytes);
  arch_count_++;
  stats_.archetypes = arch_count_;
  return &a;
}

Chunk *World::chunk_with_room(Archetype &a, uint32_t *chunk_index) {
  uint32_t idx = 0;
  for (Chunk *c = a.first; c; c = c->next, idx++)
    if (c->count < c->capacity) {
      *chunk_index = idx;
      return c;
    }
  if (chunk_count_ >= cfg_.max_chunks) return nullptr;
  Chunk *c = &chunks_[chunk_count_++];
  c->count = 0;
  c->capacity = a.row_capacity;
  c->next = nullptr;
  if (a.last) a.last->next = c;
  else a.first = c;
  a.last = c;
  *chunk_index = a.chunk_n++;
  stats_.chunks_used = chunk_count_;
  return c;
}

Entity World::create(ComponentMask mask) {
  if (free_top_ == 0) {
    stats_.create_failed++;
    return Entity::invalid();
  }
  Archetype *a = find_or_create_archetype(mask);
  uint32_t ci = 0;
  Chunk *c = a ? chunk_with_room(*a, &ci) : nullptr;
  if (!c) {
    stats_.create_failed++;
    return Entity::invalid();
  }
  uint32_t idx = free_entities_[--free_top_];
  EntityRec &r = entities_[idx];
  r.arch = (uint32_t)(a - archs_);
  r.chunk = ci;
  r.row = c->count++;
  r.alive = 1;
  Entity e = Entity::make(idx, r.gen);
  reinterpret_cast<Entity *>(c->data + a->entities_offset)[r.row] = e;
  for (uint32_t i = 0; i < a->comp_count; i++) {
    const ComponentRegistry::Info &inf = ComponentRegistry::info(a->comps[i]);
    std::memset(c->data + a->offsets[i] + (size_t)r.row * inf.size, 0, inf.size);
  }
  stats_.entities++;
  return e;
}

bool World::alive(Entity e) const {
  uint32_t i = e.index();
  return e.valid() && i < cfg_.max_entities && entities_[i].alive && entities_[i].gen == e.generation();
}

ComponentMask World::mask_of_entity(Entity e) const {
  if (!alive(e)) return 0;
  return archs_[entities_[e.index()].arch].mask;
}

void World::destroy(Entity e) {
  if (!alive(e)) return;
  EntityRec &r = entities_[e.index()];
  Archetype &a = archs_[r.arch];
  Chunk *c = a.first;
  for (uint32_t k = 0; k < r.chunk; k++) c = c->next;
  uint32_t last = c->count - 1;
  if (r.row != last) {
    // swap-remove: son satiri bu satira tasi, onun kaydini guncelle
    Entity moved = reinterpret_cast<Entity *>(c->data + a.entities_offset)[last];
    reinterpret_cast<Entity *>(c->data + a.entities_offset)[r.row] = moved;
    for (uint32_t i = 0; i < a.comp_count; i++) {
      uint32_t sz = ComponentRegistry::info(a.comps[i]).size;
      std::memcpy(c->data + a.offsets[i] + (size_t)r.row * sz, c->data + a.offsets[i] + (size_t)last * sz, sz);
    }
    entities_[moved.index()].row = r.row;
  }
  c->count--;
  r.alive = 0;
  r.gen++;
  if (r.gen == 0) r.gen = 1;
  free_entities_[free_top_++] = e.index();
  stats_.entities--;
}

void *World::get(Entity e, ComponentId id) {
  if (!alive(e)) return nullptr;
  EntityRec &r = entities_[e.index()];
  Archetype &a = archs_[r.arch];
  Chunk *c = a.first;
  for (uint32_t k = 0; k < r.chunk; k++) c = c->next;
  for (uint32_t i = 0; i < a.comp_count; i++)
    if (a.comps[i] == id) return c->data + a.offsets[i] + (size_t)r.row * ComponentRegistry::info(id).size;
  return nullptr;
}

uint64_t World::content_hash() const {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint32_t ai = 0; ai < arch_count_; ai++) {
    const Archetype &a = archs_[ai];
    h = fnv1a(&a.mask, sizeof a.mask, h);
    for (Chunk *c = a.first; c; c = c->next) {
      h = fnv1a(&c->count, sizeof c->count, h);
      h = fnv1a(c->data + a.entities_offset, sizeof(Entity) * c->count, h);
      for (uint32_t i = 0; i < a.comp_count; i++) {
        uint32_t sz = ComponentRegistry::info(a.comps[i]).size;
        h = fnv1a(c->data + a.offsets[i], (size_t)sz * c->count, h);
      }
    }
  }
  return h;
}

uint64_t World::snapshot_bytes() const {
  return (uint64_t)sizeof(EntityRec) * cfg_.max_entities + (uint64_t)sizeof(uint32_t) * cfg_.max_entities +
         sizeof(free_top_) + (uint64_t)sizeof(Archetype) * cfg_.max_archetypes + sizeof(arch_count_) +
         (uint64_t)sizeof(Chunk) * cfg_.max_chunks + sizeof(chunk_count_) +
         (uint64_t)cfg_.max_chunks * cfg_.chunk_bytes + sizeof(stats_);
}

void World::snapshot(void *dst) const {
  uint8_t *p = static_cast<uint8_t *>(dst);
  auto put = [&](const void *src, size_t n) { std::memcpy(p, src, n); p += n; };
  put(entities_, sizeof(EntityRec) * (size_t)cfg_.max_entities);
  put(free_entities_, sizeof(uint32_t) * (size_t)cfg_.max_entities);
  put(&free_top_, sizeof(free_top_));
  put(archs_, sizeof(Archetype) * (size_t)cfg_.max_archetypes);
  put(&arch_count_, sizeof(arch_count_));
  put(chunks_, sizeof(Chunk) * (size_t)cfg_.max_chunks);
  put(&chunk_count_, sizeof(chunk_count_));
  // Chunk struct'lari (yukarida) kopyalandi ama gercek satir verisi ayri
  // arena bloklarinda yasar (chunks_[i].data) -- bunlar da tek tek yazilir.
  for (uint32_t i = 0; i < cfg_.max_chunks; i++) put(chunks_[i].data, cfg_.chunk_bytes);
  put(&stats_, sizeof(stats_));
}

void World::restore(const void *src) {
  const uint8_t *p = static_cast<const uint8_t *>(src);
  auto get = [&](void *dst, size_t n) { std::memcpy(dst, p, n); p += n; };
  get(entities_, sizeof(EntityRec) * (size_t)cfg_.max_entities);
  get(free_entities_, sizeof(uint32_t) * (size_t)cfg_.max_entities);
  get(&free_top_, sizeof(free_top_));
  get(archs_, sizeof(Archetype) * (size_t)cfg_.max_archetypes);
  get(&arch_count_, sizeof(arch_count_));
  get(chunks_, sizeof(Chunk) * (size_t)cfg_.max_chunks);
  get(&chunk_count_, sizeof(chunk_count_));
  // chunks_[i].data'nin KENDISI (isaretci degeri) yukarida chunks_ ile
  // birlikte geri yazildi -- ama arena adresleri hicbir zaman degismedigi
  // icin bu her zaman AYNI degerdir; asagida yalniz o adreslerin ICERIGI
  // (gercek satir baytlari) geri yaziliyor.
  for (uint32_t i = 0; i < cfg_.max_chunks; i++) get(chunks_[i].data, cfg_.chunk_bytes);
  get(&stats_, sizeof(stats_));
}

} // namespace tulpar::engine::sim
