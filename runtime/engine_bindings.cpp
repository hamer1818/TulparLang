// URETILMIS DOSYA — engine/tools/gen_engine_bindings.py (SPEC tablosu). Elle duzenleme.
//
// Tulpar Engine kopru bindingleri: `import "engine"` eden programin cagirdigi
// aot_eng_*_ptr builtinleri (N-pointer VMValue ABI'si, aot_tm_* ile ayni) ->
// engine/bridge/engine_api.h'deki duz skaler teng_* C API'si. libtulpar_engine.a
// icinde yasar; link satirina yalniz backend->uses_engine isaretliyken girer.
#include "../src/vm/vm.hpp"
#include "../engine/bridge/engine_api.h"
#include <cstdint>
#include <cstring>

extern "C" ObjString *vm_alloc_string_aot(void *vm, const char *chars, int length);

namespace {
double tm_num(const VMValue *v) {
  if (!v) return 0.0;
  if (IS_INT(*v)) return (double)AS_INT(*v);
  if (IS_FLOAT(*v)) return AS_FLOAT(*v);
  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1.0 : 0.0;
  return 0.0;
}
int64_t tm_int(const VMValue *v) {
  if (!v) return 0;
  if (IS_INT(*v)) return AS_INT(*v);
  if (IS_FLOAT(*v)) return (int64_t)AS_FLOAT(*v);
  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1 : 0;
  return 0;
}
const char *tm_str(const VMValue *v) { return v && IS_STRING(*v) ? AS_STRING(*v)->chars : ""; }
VMValue tm_make_str(const char *s) {
  if (!s) s = "";
  ObjString *o = vm_alloc_string_aot(nullptr, s, (int)strlen(s));
  return VM_OBJ((Obj *)o);
}
} // namespace

extern "C" {
VMValue aot_eng_init_ptr(VMValue *title, VMValue *w, VMValue *h) {
  return VM_BOOL(teng_init(tm_str(title), (int)tm_int(w), (int)tm_int(h)) != 0);
}
VMValue aot_eng_running_ptr(void) {
  return VM_BOOL(teng_running() != 0);
}
VMValue aot_eng_close_ptr(void) {
  teng_close();
  return VM_VOID();
}
VMValue aot_eng_shutdown_ptr(void) {
  teng_shutdown();
  return VM_VOID();
}
VMValue aot_eng_frame_begin_ptr(void) {
  return VM_BOOL(teng_frame_begin() != 0);
}
VMValue aot_eng_frame_end_ptr(void) {
  teng_frame_end();
  return VM_VOID();
}
VMValue aot_eng_dt_ptr(void) {
  return VM_FLOAT(teng_dt());
}
VMValue aot_eng_time_ptr(void) {
  return VM_FLOAT(teng_time());
}
VMValue aot_eng_frame_ptr(void) {
  return VM_INT((int64_t)teng_frame());
}
VMValue aot_eng_fps_ptr(void) {
  return VM_FLOAT(teng_fps());
}
VMValue aot_eng_width_ptr(void) {
  return VM_INT((int64_t)teng_width());
}
VMValue aot_eng_height_ptr(void) {
  return VM_INT((int64_t)teng_height());
}
VMValue aot_eng_headless_ptr(void) {
  return VM_BOOL(teng_headless() != 0);
}
VMValue aot_eng_set_headless_ptr(VMValue *frames, VMValue *out_ppm) {
  teng_set_headless((int)tm_int(frames), tm_str(out_ppm));
  return VM_VOID();
}
VMValue aot_eng_log_ptr(VMValue *msg) {
  teng_log(tm_str(msg));
  return VM_VOID();
}
VMValue aot_eng_log_level_ptr(VMValue *level) {
  teng_log_level((int)tm_int(level));
  return VM_VOID();
}
VMValue aot_eng_screenshot_ptr(VMValue *path) {
  return VM_BOOL(teng_screenshot(tm_str(path)) != 0);
}
VMValue aot_eng_gpu_name_ptr(void) {
  return tm_make_str(teng_gpu_name());
}
VMValue aot_eng_last_error_ptr(void) {
  return tm_make_str(teng_last_error());
}
VMValue aot_eng_error_count_ptr(void) {
  return VM_INT((int64_t)teng_error_count());
}
VMValue aot_eng_warning_count_ptr(void) {
  return VM_INT((int64_t)teng_warning_count());
}
VMValue aot_eng_bloom_ptr(VMValue *enable, VMValue *threshold, VMValue *intensity) {
  teng_bloom((int)tm_int(enable), tm_num(threshold), tm_num(intensity));
  return VM_VOID();
}
VMValue aot_eng_bloom_on_ptr(void) {
  return VM_BOOL(teng_bloom_on() != 0);
}
VMValue aot_eng_gravity_ptr(VMValue *gx, VMValue *gy, VMValue *gz) {
  teng_gravity(tm_num(gx), tm_num(gy), tm_num(gz));
  return VM_VOID();
}
VMValue aot_eng_sun_ptr(VMValue *dx, VMValue *dy, VMValue *dz, VMValue *diffuse) {
  teng_sun(tm_num(dx), tm_num(dy), tm_num(dz), tm_num(diffuse));
  return VM_VOID();
}
VMValue aot_eng_ambient_ptr(VMValue *color) {
  teng_ambient(tm_int(color));
  return VM_VOID();
}
VMValue aot_eng_shadow_volume_ptr(VMValue *cx, VMValue *cy, VMValue *cz, VMValue *radius, VMValue *depth) {
  teng_shadow_volume(tm_num(cx), tm_num(cy), tm_num(cz), tm_num(radius), tm_num(depth));
  return VM_VOID();
}
VMValue aot_eng_camera_ptr(VMValue *ex, VMValue *ey, VMValue *ez, VMValue *tx, VMValue *ty, VMValue *tz) {
  teng_camera(tm_num(ex), tm_num(ey), tm_num(ez), tm_num(tx), tm_num(ty), tm_num(tz));
  return VM_VOID();
}
VMValue aot_eng_camera_orbit_ptr(VMValue *tx, VMValue *ty, VMValue *tz, VMValue *yaw, VMValue *pitch, VMValue *radius) {
  teng_camera_orbit(tm_num(tx), tm_num(ty), tm_num(tz), tm_num(yaw), tm_num(pitch), tm_num(radius));
  return VM_VOID();
}
VMValue aot_eng_camera_x_ptr(void) {
  return VM_FLOAT(teng_camera_x());
}
VMValue aot_eng_camera_y_ptr(void) {
  return VM_FLOAT(teng_camera_y());
}
VMValue aot_eng_camera_z_ptr(void) {
  return VM_FLOAT(teng_camera_z());
}
VMValue aot_eng_scene_load_ptr(VMValue *path) {
  return VM_BOOL(teng_scene_load(tm_str(path)) != 0);
}
VMValue aot_eng_scene_count_ptr(void) {
  return VM_INT((int64_t)teng_scene_count());
}
VMValue aot_eng_scene_find_ptr(VMValue *name) {
  return VM_INT((int64_t)teng_scene_find(tm_str(name)));
}
VMValue aot_eng_scene_x_ptr(VMValue *i) {
  return VM_FLOAT(teng_scene_x((int)tm_int(i)));
}
VMValue aot_eng_scene_y_ptr(VMValue *i) {
  return VM_FLOAT(teng_scene_y((int)tm_int(i)));
}
VMValue aot_eng_scene_z_ptr(VMValue *i) {
  return VM_FLOAT(teng_scene_z((int)tm_int(i)));
}
VMValue aot_eng_scene_name_ptr(VMValue *i) {
  return tm_make_str(teng_scene_name((int)tm_int(i)));
}
VMValue aot_eng_scene_unload_ptr(void) {
  return VM_BOOL(teng_scene_unload() != 0);
}
VMValue aot_eng_scene_loaded_ptr(void) {
  return VM_BOOL(teng_scene_loaded() != 0);
}
VMValue aot_eng_scene_vx_ptr(VMValue *i) {
  return VM_FLOAT(teng_scene_vx((int)tm_int(i)));
}
VMValue aot_eng_scene_vy_ptr(VMValue *i) {
  return VM_FLOAT(teng_scene_vy((int)tm_int(i)));
}
VMValue aot_eng_scene_vz_ptr(VMValue *i) {
  return VM_FLOAT(teng_scene_vz((int)tm_int(i)));
}
VMValue aot_eng_scene_is_dynamic_ptr(VMValue *i) {
  return VM_BOOL(teng_scene_is_dynamic((int)tm_int(i)) != 0);
}
VMValue aot_eng_scene_set_velocity_ptr(VMValue *i, VMValue *vx, VMValue *vy, VMValue *vz) {
  teng_scene_set_velocity((int)tm_int(i), tm_num(vx), tm_num(vy), tm_num(vz));
  return VM_VOID();
}
VMValue aot_eng_scene_impulse_ptr(VMValue *i, VMValue *ix, VMValue *iy, VMValue *iz) {
  teng_scene_impulse((int)tm_int(i), tm_num(ix), tm_num(iy), tm_num(iz));
  return VM_VOID();
}
VMValue aot_eng_spawn_box_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *hx, VMValue *hy, VMValue *hz, VMValue *dynamic, VMValue *color) {
  return VM_INT((int64_t)teng_spawn_box(tm_num(x), tm_num(y), tm_num(z), tm_num(hx), tm_num(hy), tm_num(hz), (int)tm_int(dynamic), tm_int(color)));
}
VMValue aot_eng_spawn_sphere_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *radius, VMValue *dynamic, VMValue *color) {
  return VM_INT((int64_t)teng_spawn_sphere(tm_num(x), tm_num(y), tm_num(z), tm_num(radius), (int)tm_int(dynamic), tm_int(color)));
}
VMValue aot_eng_spawn_ground_ptr(VMValue *half_size, VMValue *color) {
  return VM_INT((int64_t)teng_spawn_ground(tm_num(half_size), tm_int(color)));
}
VMValue aot_eng_load_model_ptr(VMValue *path) {
  return VM_INT((int64_t)teng_load_model(tm_str(path)));
}
VMValue aot_eng_spawn_model_ptr(VMValue *asset, VMValue *x, VMValue *y, VMValue *z, VMValue *scale, VMValue *tint) {
  return VM_INT((int64_t)teng_spawn_model((int)tm_int(asset), tm_num(x), tm_num(y), tm_num(z), tm_num(scale), tm_int(tint)));
}
VMValue aot_eng_spawn_light_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *color, VMValue *intensity, VMValue *radius) {
  return VM_INT((int64_t)teng_spawn_light(tm_num(x), tm_num(y), tm_num(z), tm_int(color), tm_num(intensity), tm_num(radius)));
}
VMValue aot_eng_despawn_ptr(VMValue *id) {
  teng_despawn((int)tm_int(id));
  return VM_VOID();
}
VMValue aot_eng_alive_ptr(VMValue *id) {
  return VM_BOOL(teng_alive((int)tm_int(id)) != 0);
}
VMValue aot_eng_count_ptr(void) {
  return VM_INT((int64_t)teng_count());
}
VMValue aot_eng_x_ptr(VMValue *id) {
  return VM_FLOAT(teng_x((int)tm_int(id)));
}
VMValue aot_eng_y_ptr(VMValue *id) {
  return VM_FLOAT(teng_y((int)tm_int(id)));
}
VMValue aot_eng_z_ptr(VMValue *id) {
  return VM_FLOAT(teng_z((int)tm_int(id)));
}
VMValue aot_eng_set_pos_ptr(VMValue *id, VMValue *x, VMValue *y, VMValue *z) {
  teng_set_pos((int)tm_int(id), tm_num(x), tm_num(y), tm_num(z));
  return VM_VOID();
}
VMValue aot_eng_set_color_ptr(VMValue *id, VMValue *color) {
  teng_set_color((int)tm_int(id), tm_int(color));
  return VM_VOID();
}
VMValue aot_eng_set_scale_ptr(VMValue *id, VMValue *scale) {
  teng_set_scale((int)tm_int(id), tm_num(scale));
  return VM_VOID();
}
VMValue aot_eng_set_yaw_ptr(VMValue *id, VMValue *yaw_deg) {
  teng_set_yaw((int)tm_int(id), tm_num(yaw_deg));
  return VM_VOID();
}
VMValue aot_eng_vx_ptr(VMValue *id) {
  return VM_FLOAT(teng_vx((int)tm_int(id)));
}
VMValue aot_eng_vy_ptr(VMValue *id) {
  return VM_FLOAT(teng_vy((int)tm_int(id)));
}
VMValue aot_eng_vz_ptr(VMValue *id) {
  return VM_FLOAT(teng_vz((int)tm_int(id)));
}
VMValue aot_eng_set_velocity_ptr(VMValue *id, VMValue *vx, VMValue *vy, VMValue *vz) {
  teng_set_velocity((int)tm_int(id), tm_num(vx), tm_num(vy), tm_num(vz));
  return VM_VOID();
}
VMValue aot_eng_impulse_ptr(VMValue *id, VMValue *ix, VMValue *iy, VMValue *iz) {
  teng_impulse((int)tm_int(id), tm_num(ix), tm_num(iy), tm_num(iz));
  return VM_VOID();
}
VMValue aot_eng_is_dynamic_ptr(VMValue *id) {
  return VM_BOOL(teng_is_dynamic((int)tm_int(id)) != 0);
}
VMValue aot_eng_awake_ptr(VMValue *id) {
  return VM_BOOL(teng_awake((int)tm_int(id)) != 0);
}
VMValue aot_eng_model_clip_count_ptr(VMValue *asset) {
  return VM_INT((int64_t)teng_model_clip_count((int)tm_int(asset)));
}
VMValue aot_eng_model_clip_duration_ptr(VMValue *asset, VMValue *clip) {
  return VM_FLOAT(teng_model_clip_duration((int)tm_int(asset), (int)tm_int(clip)));
}
VMValue aot_eng_model_clip_name_ptr(VMValue *asset, VMValue *clip) {
  return tm_make_str(teng_model_clip_name((int)tm_int(asset), (int)tm_int(clip)));
}
VMValue aot_eng_set_anim_ptr(VMValue *id, VMValue *clip, VMValue *speed, VMValue *loop) {
  teng_set_anim((int)tm_int(id), (int)tm_int(clip), tm_num(speed), (int)tm_int(loop));
  return VM_VOID();
}
VMValue aot_eng_anim_time_ptr(VMValue *id) {
  return VM_FLOAT(teng_anim_time((int)tm_int(id)));
}
VMValue aot_eng_anim_done_ptr(VMValue *id) {
  return VM_BOOL(teng_anim_done((int)tm_int(id)) != 0);
}
VMValue aot_eng_key_down_ptr(VMValue *name) {
  return VM_BOOL(teng_key_down(tm_str(name)) != 0);
}
VMValue aot_eng_key_pressed_ptr(VMValue *name) {
  return VM_BOOL(teng_key_pressed(tm_str(name)) != 0);
}
VMValue aot_eng_touch_count_ptr(void) {
  return VM_INT((int64_t)teng_touch_count());
}
VMValue aot_eng_touch_x_ptr(VMValue *i) {
  return VM_FLOAT(teng_touch_x((int)tm_int(i)));
}
VMValue aot_eng_touch_y_ptr(VMValue *i) {
  return VM_FLOAT(teng_touch_y((int)tm_int(i)));
}
VMValue aot_eng_stick_x_ptr(void) {
  return VM_FLOAT(teng_stick_x());
}
VMValue aot_eng_stick_y_ptr(void) {
  return VM_FLOAT(teng_stick_y());
}
VMValue aot_eng_stick_action_ptr(void) {
  return VM_BOOL(teng_stick_action() != 0);
}
VMValue aot_eng_look_dx_ptr(void) {
  return VM_FLOAT(teng_look_dx());
}
VMValue aot_eng_mouse_x_ptr(void) {
  return VM_FLOAT(teng_mouse_x());
}
VMValue aot_eng_mouse_y_ptr(void) {
  return VM_FLOAT(teng_mouse_y());
}
VMValue aot_eng_mouse_down_ptr(VMValue *button) {
  return VM_BOOL(teng_mouse_down((int)tm_int(button)) != 0);
}
VMValue aot_eng_text_ptr(VMValue *s, VMValue *x, VMValue *y, VMValue *scale, VMValue *color) {
  teng_text(tm_str(s), tm_num(x), tm_num(y), tm_num(scale), tm_int(color));
  return VM_VOID();
}
VMValue aot_eng_rect_ptr(VMValue *x, VMValue *y, VMValue *w, VMValue *h, VMValue *color) {
  teng_rect(tm_num(x), tm_num(y), tm_num(w), tm_num(h), tm_int(color));
  return VM_VOID();
}
VMValue aot_eng_text_width_ptr(VMValue *s, VMValue *scale) {
  return VM_FLOAT(teng_text_width(tm_str(s), tm_num(scale)));
}
VMValue aot_eng_audio_open_ptr(VMValue *rate, VMValue *channels) {
  return VM_BOOL(teng_audio_open((int)tm_int(rate), (int)tm_int(channels)) != 0);
}
VMValue aot_eng_audio_close_ptr(void) {
  teng_audio_close();
  return VM_VOID();
}
VMValue aot_eng_audio_ok_ptr(void) {
  return VM_BOOL(teng_audio_ok() != 0);
}
VMValue aot_eng_audio_backend_ptr(void) {
  return tm_make_str(teng_audio_backend());
}
VMValue aot_eng_audio_load_ptr(VMValue *path) {
  return VM_INT((int64_t)teng_audio_load(tm_str(path)));
}
VMValue aot_eng_audio_tone_ptr(VMValue *hz, VMValue *seconds) {
  return VM_INT((int64_t)teng_audio_tone(tm_num(hz), tm_num(seconds)));
}
VMValue aot_eng_audio_play_ptr(VMValue *clip, VMValue *gain, VMValue *loop) {
  return VM_INT((int64_t)teng_audio_play((int)tm_int(clip), tm_num(gain), (int)tm_int(loop)));
}
VMValue aot_eng_audio_beep_ptr(VMValue *hz, VMValue *seconds, VMValue *gain) {
  return VM_INT((int64_t)teng_audio_beep(tm_num(hz), tm_num(seconds), tm_num(gain)));
}
VMValue aot_eng_audio_stop_ptr(VMValue *voice) {
  teng_audio_stop((int)tm_int(voice));
  return VM_VOID();
}
VMValue aot_eng_audio_stop_all_ptr(void) {
  teng_audio_stop_all();
  return VM_VOID();
}
VMValue aot_eng_audio_master_ptr(VMValue *gain) {
  teng_audio_master(tm_num(gain));
  return VM_VOID();
}
VMValue aot_eng_audio_playing_ptr(void) {
  return VM_INT((int64_t)teng_audio_playing());
}
VMValue aot_eng_audio_peak_ptr(void) {
  return VM_FLOAT(teng_audio_peak());
}
VMValue aot_eng_raycast_ptr(VMValue *ox, VMValue *oy, VMValue *oz, VMValue *dx, VMValue *dy, VMValue *dz, VMValue *max_dist, VMValue *skip_id) {
  return VM_FLOAT(teng_raycast(tm_num(ox), tm_num(oy), tm_num(oz), tm_num(dx), tm_num(dy), tm_num(dz), tm_num(max_dist), (int)tm_int(skip_id)));
}
VMValue aot_eng_ray_x_ptr(void) {
  return VM_FLOAT(teng_ray_x());
}
VMValue aot_eng_ray_y_ptr(void) {
  return VM_FLOAT(teng_ray_y());
}
VMValue aot_eng_ray_z_ptr(void) {
  return VM_FLOAT(teng_ray_z());
}
VMValue aot_eng_ray_nx_ptr(void) {
  return VM_FLOAT(teng_ray_nx());
}
VMValue aot_eng_ray_ny_ptr(void) {
  return VM_FLOAT(teng_ray_ny());
}
VMValue aot_eng_ray_nz_ptr(void) {
  return VM_FLOAT(teng_ray_nz());
}
VMValue aot_eng_ray_id_ptr(void) {
  return VM_INT((int64_t)teng_ray_id());
}
VMValue aot_eng_ray_scene_ptr(void) {
  return VM_INT((int64_t)teng_ray_scene());
}
VMValue aot_eng_overlap_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *radius, VMValue *skip_id) {
  return VM_INT((int64_t)teng_overlap(tm_num(x), tm_num(y), tm_num(z), tm_num(radius), (int)tm_int(skip_id)));
}
VMValue aot_eng_overlap_id_ptr(VMValue *i) {
  return VM_INT((int64_t)teng_overlap_id((int)tm_int(i)));
}
VMValue aot_eng_overlap_dist_ptr(VMValue *i) {
  return VM_FLOAT(teng_overlap_dist((int)tm_int(i)));
}
VMValue aot_eng_nearest_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *radius, VMValue *skip_id) {
  return VM_INT((int64_t)teng_nearest(tm_num(x), tm_num(y), tm_num(z), tm_num(radius), (int)tm_int(skip_id)));
}
VMValue aot_eng_collision_count_ptr(void) {
  return VM_INT((int64_t)teng_collision_count());
}
VMValue aot_eng_collision_dropped_ptr(void) {
  return VM_INT((int64_t)teng_collision_dropped());
}
VMValue aot_eng_collision_a_ptr(VMValue *i) {
  return VM_INT((int64_t)teng_collision_a((int)tm_int(i)));
}
VMValue aot_eng_collision_b_ptr(VMValue *i) {
  return VM_INT((int64_t)teng_collision_b((int)tm_int(i)));
}
VMValue aot_eng_collision_scene_a_ptr(VMValue *i) {
  return VM_INT((int64_t)teng_collision_scene_a((int)tm_int(i)));
}
VMValue aot_eng_collision_scene_b_ptr(VMValue *i) {
  return VM_INT((int64_t)teng_collision_scene_b((int)tm_int(i)));
}
VMValue aot_eng_collision_x_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_x((int)tm_int(i)));
}
VMValue aot_eng_collision_y_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_y((int)tm_int(i)));
}
VMValue aot_eng_collision_z_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_z((int)tm_int(i)));
}
VMValue aot_eng_collision_nx_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_nx((int)tm_int(i)));
}
VMValue aot_eng_collision_ny_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_ny((int)tm_int(i)));
}
VMValue aot_eng_collision_nz_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_nz((int)tm_int(i)));
}
VMValue aot_eng_collision_speed_ptr(VMValue *i) {
  return VM_FLOAT(teng_collision_speed((int)tm_int(i)));
}
VMValue aot_eng_nav_ok_ptr(void) {
  return VM_BOOL(teng_nav_ok() != 0);
}
VMValue aot_eng_nav_polys_ptr(void) {
  return VM_INT((int64_t)teng_nav_polys());
}
VMValue aot_eng_nav_path_ptr(VMValue *fx, VMValue *fy, VMValue *fz, VMValue *tx, VMValue *ty, VMValue *tz) {
  return VM_INT((int64_t)teng_nav_path(tm_num(fx), tm_num(fy), tm_num(fz), tm_num(tx), tm_num(ty), tm_num(tz)));
}
VMValue aot_eng_nav_partial_ptr(void) {
  return VM_BOOL(teng_nav_partial() != 0);
}
VMValue aot_eng_nav_x_ptr(VMValue *i) {
  return VM_FLOAT(teng_nav_x((int)tm_int(i)));
}
VMValue aot_eng_nav_y_ptr(VMValue *i) {
  return VM_FLOAT(teng_nav_y((int)tm_int(i)));
}
VMValue aot_eng_nav_z_ptr(VMValue *i) {
  return VM_FLOAT(teng_nav_z((int)tm_int(i)));
}
VMValue aot_eng_nav_nearest_ptr(VMValue *x, VMValue *y, VMValue *z) {
  return VM_BOOL(teng_nav_nearest(tm_num(x), tm_num(y), tm_num(z)) != 0);
}
VMValue aot_eng_nav_near_x_ptr(void) {
  return VM_FLOAT(teng_nav_near_x());
}
VMValue aot_eng_nav_near_y_ptr(void) {
  return VM_FLOAT(teng_nav_near_y());
}
VMValue aot_eng_nav_near_z_ptr(void) {
  return VM_FLOAT(teng_nav_near_z());
}
VMValue aot_eng_nav_raycast_ptr(VMValue *fx, VMValue *fy, VMValue *fz, VMValue *tx, VMValue *ty, VMValue *tz) {
  return VM_BOOL(teng_nav_raycast(tm_num(fx), tm_num(fy), tm_num(fz), tm_num(tx), tm_num(ty), tm_num(tz)) != 0);
}
VMValue aot_eng_nav_ray_t_ptr(void) {
  return VM_FLOAT(teng_nav_ray_t());
}
VMValue aot_eng_ui_begin_ptr(void) {
  teng_ui_begin();
  return VM_VOID();
}
VMValue aot_eng_ui_end_ptr(void) {
  teng_ui_end();
  return VM_VOID();
}
VMValue aot_eng_ui_theme_ptr(VMValue *panel, VMValue *text, VMValue *accent) {
  teng_ui_theme(tm_int(panel), tm_int(text), tm_int(accent));
  return VM_VOID();
}
VMValue aot_eng_ui_enable_ptr(VMValue *on) {
  teng_ui_enable((int)tm_int(on));
  return VM_VOID();
}
VMValue aot_eng_ui_panel_ptr(VMValue *x, VMValue *y, VMValue *w, VMValue *h, VMValue *color) {
  teng_ui_panel(tm_num(x), tm_num(y), tm_num(w), tm_num(h), tm_int(color));
  return VM_VOID();
}
VMValue aot_eng_ui_label_ptr(VMValue *s, VMValue *x, VMValue *y, VMValue *scale, VMValue *color) {
  teng_ui_label(tm_str(s), tm_num(x), tm_num(y), tm_num(scale), tm_int(color));
  return VM_VOID();
}
VMValue aot_eng_ui_button_ptr(VMValue *label, VMValue *x, VMValue *y, VMValue *w, VMValue *h) {
  return VM_BOOL(teng_ui_button(tm_str(label), tm_num(x), tm_num(y), tm_num(w), tm_num(h)) != 0);
}
VMValue aot_eng_ui_checkbox_ptr(VMValue *label, VMValue *x, VMValue *y, VMValue *w, VMValue *h, VMValue *value) {
  return VM_BOOL(teng_ui_checkbox(tm_str(label), tm_num(x), tm_num(y), tm_num(w), tm_num(h), (int)tm_int(value)) != 0);
}
VMValue aot_eng_ui_slider_ptr(VMValue *label, VMValue *x, VMValue *y, VMValue *w, VMValue *h, VMValue *value, VMValue *min_v, VMValue *max_v) {
  return VM_FLOAT(teng_ui_slider(tm_str(label), tm_num(x), tm_num(y), tm_num(w), tm_num(h), tm_num(value), tm_num(min_v), tm_num(max_v)));
}
VMValue aot_eng_ui_active_ptr(void) {
  return VM_BOOL(teng_ui_active() != 0);
}
VMValue aot_eng_ui_clicks_ptr(void) {
  return VM_INT((int64_t)teng_ui_clicks());
}
VMValue aot_eng_ui_test_click_ptr(VMValue *x, VMValue *y) {
  teng_ui_test_click(tm_num(x), tm_num(y));
  return VM_VOID();
}
VMValue aot_eng_save_open_ptr(VMValue *path) {
  return VM_BOOL(teng_save_open(tm_str(path)) != 0);
}
VMValue aot_eng_save_path_ptr(void) {
  return tm_make_str(teng_save_path());
}
VMValue aot_eng_save_set_ptr(VMValue *key, VMValue *value) {
  teng_save_set(tm_str(key), tm_num(value));
  return VM_VOID();
}
VMValue aot_eng_save_get_ptr(VMValue *key, VMValue *def) {
  return VM_FLOAT(teng_save_get(tm_str(key), tm_num(def)));
}
VMValue aot_eng_save_set_str_ptr(VMValue *key, VMValue *value) {
  teng_save_set_str(tm_str(key), tm_str(value));
  return VM_VOID();
}
VMValue aot_eng_save_get_str_ptr(VMValue *key, VMValue *def) {
  return tm_make_str(teng_save_get_str(tm_str(key), tm_str(def)));
}
VMValue aot_eng_save_has_ptr(VMValue *key) {
  return VM_BOOL(teng_save_has(tm_str(key)) != 0);
}
VMValue aot_eng_save_write_ptr(void) {
  return VM_BOOL(teng_save_write() != 0);
}
VMValue aot_eng_save_clear_ptr(void) {
  teng_save_clear();
  return VM_VOID();
}
VMValue aot_eng_save_count_ptr(void) {
  return VM_INT((int64_t)teng_save_count());
}
VMValue aot_eng_scene_watch_ptr(VMValue *on) {
  teng_scene_watch((int)tm_int(on));
  return VM_VOID();
}
VMValue aot_eng_scene_reloaded_ptr(void) {
  return VM_BOOL(teng_scene_reloaded() != 0);
}
VMValue aot_eng_scene_reload_count_ptr(void) {
  return VM_INT((int64_t)teng_scene_reload_count());
}
VMValue aot_eng_scene_path_ptr(void) {
  return tm_make_str(teng_scene_path());
}
VMValue aot_eng_file_copy_ptr(VMValue *src, VMValue *dst) {
  return VM_BOOL(teng_file_copy(tm_str(src), tm_str(dst)) != 0);
}
VMValue aot_eng_file_mtime_ptr(VMValue *path) {
  return VM_FLOAT(teng_file_mtime(tm_str(path)));
}
VMValue aot_eng_draw_count_ptr(void) {
  return VM_INT((int64_t)teng_draw_count());
}
VMValue aot_eng_body_count_ptr(void) {
  return VM_INT((int64_t)teng_body_count());
}
VMValue aot_eng_light_count_ptr(void) {
  return VM_INT((int64_t)teng_light_count());
}
VMValue aot_eng_frame_ms_ptr(void) {
  return VM_FLOAT(teng_frame_ms());
}
} // extern "C"
