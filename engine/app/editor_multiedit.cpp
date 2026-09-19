// L6 APP — editor_multiedit: coklu secimde alan duzenlemesini yaymak.
//
// URETILMIS TABLO: tools/scene_check.py bu tablonun SceneEntity'nin TUM
// alanlarini kapsadigini denetler; yeni alan eklenip tabloya girmezse derleme
// kirmizi olur. Tabloyu elle degil, kapinin istedigi alanlari ekleyerek guncelle.
#include "app/editor_multiedit.hpp"

#include <cstddef>
#include <cstring>

namespace tulpar::engine::app {

namespace {
struct Leaf {
  size_t off, size;
  const char *name;
};
using content::SceneEntity;
// offsetof(T, a.b) GCC/Clang/MSVC'de destekli (ic ice uye tanimlayicisi).
const Leaf kLeaves[] = {
    {offsetof(SceneEntity, pos.x), sizeof(float), "pos.x"},
    {offsetof(SceneEntity, pos.y), sizeof(float), "pos.y"},
    {offsetof(SceneEntity, pos.z), sizeof(float), "pos.z"},
    {offsetof(SceneEntity, rot_deg.x), sizeof(float), "rot_deg.x"},
    {offsetof(SceneEntity, rot_deg.y), sizeof(float), "rot_deg.y"},
    {offsetof(SceneEntity, rot_deg.z), sizeof(float), "rot_deg.z"},
    {offsetof(SceneEntity, scale.x), sizeof(float), "scale.x"},
    {offsetof(SceneEntity, scale.y), sizeof(float), "scale.y"},
    {offsetof(SceneEntity, scale.z), sizeof(float), "scale.z"},
    {offsetof(SceneEntity, asset), sizeof(int32_t), "asset"},
    {offsetof(SceneEntity, primitive), sizeof(int32_t), "primitive"},
    {offsetof(SceneEntity, tint.x), sizeof(float), "tint.x"},
    {offsetof(SceneEntity, tint.y), sizeof(float), "tint.y"},
    {offsetof(SceneEntity, tint.z), sizeof(float), "tint.z"},
    {offsetof(SceneEntity, metallic), sizeof(float), "metallic"},
    {offsetof(SceneEntity, roughness), sizeof(float), "roughness"},
    {offsetof(SceneEntity, reflectance), sizeof(float), "reflectance"},
    {offsetof(SceneEntity, emissive.x), sizeof(float), "emissive.x"},
    {offsetof(SceneEntity, emissive.y), sizeof(float), "emissive.y"},
    {offsetof(SceneEntity, emissive.z), sizeof(float), "emissive.z"},
    {offsetof(SceneEntity, emissive_strength), sizeof(float), "emissive_strength"},
    {offsetof(SceneEntity, clip), sizeof(uint32_t), "clip"},
    {offsetof(SceneEntity, phase), sizeof(float), "phase"},
    {offsetof(SceneEntity, speed), sizeof(float), "speed"},
    {offsetof(SceneEntity, light_color.x), sizeof(float), "light_color.x"},
    {offsetof(SceneEntity, light_color.y), sizeof(float), "light_color.y"},
    {offsetof(SceneEntity, light_color.z), sizeof(float), "light_color.z"},
    {offsetof(SceneEntity, light_intensity), sizeof(float), "light_intensity"},
    {offsetof(SceneEntity, light_radius), sizeof(float), "light_radius"},
    {offsetof(SceneEntity, light_type), sizeof(content::SceneLightType), "light_type"},
    {offsetof(SceneEntity, shape), sizeof(content::SceneShape), "shape"},
    {offsetof(SceneEntity, half.x), sizeof(float), "half.x"},
    {offsetof(SceneEntity, half.y), sizeof(float), "half.y"},
    {offsetof(SceneEntity, half.z), sizeof(float), "half.z"},
    {offsetof(SceneEntity, radius), sizeof(float), "radius"},
    {offsetof(SceneEntity, dynamic), sizeof(bool), "dynamic"},
    {offsetof(SceneEntity, cam_fov), sizeof(float), "cam_fov"},
    {offsetof(SceneEntity, cam_near), sizeof(float), "cam_near"},
    {offsetof(SceneEntity, cam_far), sizeof(float), "cam_far"},
    {offsetof(SceneEntity, audio_clip), sizeof(SceneEntity::audio_clip), "audio_clip"},
    {offsetof(SceneEntity, audio_volume), sizeof(float), "audio_volume"},
    {offsetof(SceneEntity, audio_pitch), sizeof(float), "audio_pitch"},
    {offsetof(SceneEntity, audio_loop), sizeof(bool), "audio_loop"},
    {offsetof(SceneEntity, audio_spatial), sizeof(bool), "audio_spatial"},
    {offsetof(SceneEntity, script_file), sizeof(SceneEntity::script_file), "script_file"},
    {offsetof(SceneEntity, script_enabled), sizeof(bool), "script_enabled"},
    {offsetof(SceneEntity, char_radius), sizeof(float), "char_radius"},
    {offsetof(SceneEntity, char_height), sizeof(float), "char_height"},
    {offsetof(SceneEntity, char_mass), sizeof(float), "char_mass"},
    {offsetof(SceneEntity, char_max_slope), sizeof(float), "char_max_slope"},
    {offsetof(SceneEntity, particle_spawn_rate), sizeof(float), "particle_spawn_rate"},
    {offsetof(SceneEntity, particle_lifetime_min), sizeof(float), "particle_lifetime_min"},
    {offsetof(SceneEntity, particle_lifetime_max), sizeof(float), "particle_lifetime_max"},
    {offsetof(SceneEntity, particle_size_start), sizeof(float), "particle_size_start"},
    {offsetof(SceneEntity, particle_size_end), sizeof(float), "particle_size_end"},
    {offsetof(SceneEntity, particle_velocity.x), sizeof(float), "particle_velocity.x"},
    {offsetof(SceneEntity, particle_velocity.y), sizeof(float), "particle_velocity.y"},
    {offsetof(SceneEntity, particle_velocity.z), sizeof(float), "particle_velocity.z"},
    {offsetof(SceneEntity, particle_jitter.x), sizeof(float), "particle_jitter.x"},
    {offsetof(SceneEntity, particle_jitter.y), sizeof(float), "particle_jitter.y"},
    {offsetof(SceneEntity, particle_jitter.z), sizeof(float), "particle_jitter.z"},
    {offsetof(SceneEntity, ai_target.x), sizeof(float), "ai_target.x"},
    {offsetof(SceneEntity, ai_target.y), sizeof(float), "ai_target.y"},
    {offsetof(SceneEntity, ai_target.z), sizeof(float), "ai_target.z"},
    {offsetof(SceneEntity, ai_speed), sizeof(float), "ai_speed"},
    {offsetof(SceneEntity, ai_turn_speed), sizeof(float), "ai_turn_speed"},
    {offsetof(SceneEntity, joint_target), sizeof(int32_t), "joint_target"},
    {offsetof(SceneEntity, joint_axis.x), sizeof(float), "joint_axis.x"},
    {offsetof(SceneEntity, joint_axis.y), sizeof(float), "joint_axis.y"},
    {offsetof(SceneEntity, joint_axis.z), sizeof(float), "joint_axis.z"},
    {offsetof(SceneEntity, joint_limit_min), sizeof(float), "joint_limit_min"},
    {offsetof(SceneEntity, joint_limit_max), sizeof(float), "joint_limit_max"},
    {offsetof(SceneEntity, joint_motor_speed), sizeof(float), "joint_motor_speed"},
    {offsetof(SceneEntity, terrain_width), sizeof(float), "terrain_width"},
    {offsetof(SceneEntity, terrain_height), sizeof(float), "terrain_height"},
    {offsetof(SceneEntity, terrain_cell), sizeof(float), "terrain_cell"},
    {offsetof(SceneEntity, terrain_amp), sizeof(float), "terrain_amp"},
    {offsetof(SceneEntity, terrain_freq), sizeof(float), "terrain_freq"},
    {offsetof(SceneEntity, terrain_octaves), sizeof(int32_t), "terrain_octaves"},
    {offsetof(SceneEntity, terrain_seed), sizeof(uint32_t), "terrain_seed"},
    {offsetof(SceneEntity, ref_probe_radius), sizeof(float), "ref_probe_radius"},
    {offsetof(SceneEntity, ref_probe_intensity), sizeof(float), "ref_probe_intensity"},
    {offsetof(SceneEntity, skybox_asset), sizeof(SceneEntity::skybox_asset), "skybox_asset"},
    {offsetof(SceneEntity, reverb_decay), sizeof(float), "reverb_decay"},
    {offsetof(SceneEntity, reverb_room_size), sizeof(float), "reverb_room_size"},
    {offsetof(SceneEntity, wave_length), sizeof(float), "wave_length"},
    {offsetof(SceneEntity, wave_amplitude), sizeof(float), "wave_amplitude"},
    {offsetof(SceneEntity, wave_steepness), sizeof(float), "wave_steepness"},
    {offsetof(SceneEntity, wave_speed), sizeof(float), "wave_speed"},
    {offsetof(SceneEntity, wave_direction.x), sizeof(float), "wave_direction.x"},
    {offsetof(SceneEntity, wave_direction.y), sizeof(float), "wave_direction.y"},
    {offsetof(SceneEntity, wind_direction.x), sizeof(float), "wind_direction.x"},
    {offsetof(SceneEntity, wind_direction.y), sizeof(float), "wind_direction.y"},
    {offsetof(SceneEntity, wind_strength), sizeof(float), "wind_strength"},
    {offsetof(SceneEntity, wind_gustiness), sizeof(float), "wind_gustiness"},
    {offsetof(SceneEntity, wind_gust_freq), sizeof(float), "wind_gust_freq"},
    {offsetof(SceneEntity, wind_seed), sizeof(uint32_t), "wind_seed"},
    {offsetof(SceneEntity, voxel_size_x), sizeof(uint32_t), "voxel_size_x"},
    {offsetof(SceneEntity, voxel_size_y), sizeof(uint32_t), "voxel_size_y"},
    {offsetof(SceneEntity, voxel_size_z), sizeof(uint32_t), "voxel_size_z"},
    {offsetof(SceneEntity, voxel_cell), sizeof(float), "voxel_cell"},
};
} // namespace

uint32_t multiedit_leaf_count() { return (uint32_t)(sizeof kLeaves / sizeof kLeaves[0]); }

bool multiedit_apply(const SceneEntity &before, const SceneEntity &after, SceneEntity *target) {
  const auto *b = reinterpret_cast<const unsigned char *>(&before);
  const auto *a = reinterpret_cast<const unsigned char *>(&after);
  auto *t = reinterpret_cast<unsigned char *>(target);
  bool changed = false;
  for (const Leaf &l : kLeaves) {
    if (std::memcmp(b + l.off, a + l.off, l.size) == 0) continue; // birincilde degismedi
    if (std::memcmp(t + l.off, a + l.off, l.size) == 0) continue; // hedefte zaten ayni
    std::memcpy(t + l.off, a + l.off, l.size);
    changed = true;
  }
  // Bit alanlari: EKLENEN ve KALDIRILAN bitler ayri uygulanir. Butun kelimeyi
  // kopyalamak hedefin KENDI bilesenlerini silerdi (Model'e Isik eklemek,
  // secimdeki Govdeli nesnenin Govdesini kaldirmamali).
  const uint32_t add_c = after.components & ~before.components, del_c = before.components & ~after.components;
  const uint32_t c = (target->components | add_c) & ~del_c;
  if (c != target->components) { target->components = c; changed = true; }
  const uint32_t add_f = after.flags & ~before.flags, del_f = before.flags & ~after.flags;
  const uint32_t f = (target->flags | add_f) & ~del_f;
  if (f != target->flags) { target->flags = f; changed = true; }
  return changed;
}

} // namespace tulpar::engine::app
