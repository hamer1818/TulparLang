#include "renderer/indirect_cull.hpp"

namespace tulpar::engine::renderer {

bool cull_and_select_lod(const Frustum &frustum, Vec3 camera_pos,
                          const CullInstance *instances, uint32_t count,
                          const LodLevel *lods, uint32_t lod_count,
                          IndexedIndirectCommand *out_commands, CullStats *out_stats) {
  if (lod_count == 0) return false;
  *out_stats = CullStats{};

  for (uint32_t i = 0; i < count; i++) {
    const CullInstance &inst = instances[i];
    IndexedIndirectCommand &cmd = out_commands[i];
    cmd = IndexedIndirectCommand{};
    cmd.first_instance = i; // hangi instance donusumunun kullanilacagini belirler -- gorunurlukten BAGIMSIZ

    if (!intersects(frustum, Sphere{inst.pos, inst.bounding_radius})) {
      cmd.instance_count = 0; // kirpildi -- diger alanlar ONEMSIZ (cizilmeyecek)
      continue;
    }

    cmd.instance_count = 1;
    out_stats->draw_count++;

    // Mesafeye gore LOD: ilk esigi GECEN (mesafe < esik) seviye secilir --
    // cull.comp'taki `for (...) if (distance < lods[i].distance) { ...; break; }`
    // ile BIREBIR ayni sira/kirma mantigi. Hicbiri gecmezse son (en kaba) seviye.
    uint32_t lod_level = lod_count - 1;
    const float dist = length(inst.pos - camera_pos);
    for (uint32_t l = 0; l < lod_count - 1; l++) {
      if (dist < lods[l].distance) {
        lod_level = l;
        break;
      }
    }
    cmd.first_index = lods[lod_level].first_index;
    cmd.index_count = lods[lod_level].index_count;
    if (lod_level < kMaxCullLodLevels) out_stats->lod_count[lod_level]++;
  }
  return true;
}

} // namespace tulpar::engine::renderer
