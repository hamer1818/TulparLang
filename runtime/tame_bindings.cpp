// Tame — Tulpar 2D game library, VMValue-facing binding layer.
//
// Implements the `aot_tm_*_ptr` builtins the AOT backend emits calls to when
// a program does `import "tame"`. This TU includes the Tulpar runtime value
// types but NOT raylib.h — the raylib-facing half lives in tame_impl.c and
// is reached through the flat-scalar prototypes below (two-TU split so
// raylib's identifiers never meet windows.h; see tame_impl.c header note).
//
// ABI: same N-pointer VMValue convention as every other `aot_*_ptr` builtin
// (see aot_ord_ptr in src/vm/runtime_bindings.cpp) — each Tulpar argument
// arrives as a VMValue*, the return is a VMValue. Numeric positions accept
// int OR float (games mix `x + dx` floats with literal ints freely), so all
// coordinate unpacking goes through tm_num().
//
// These objects ship in libtulpar_tame.a, which the AOT pipeline links only
// when the program imports "tame" — a hello-world binary stays free of any
// GL/window dependency. vm_make_* etc. resolve from libtulpar_runtime.a,
// which the link line places to the right of libtulpar_tame.a.

#include "../src/vm/vm.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

// --- tame_impl.c prototypes (kept in sync by hand; no shared header) -------
extern "C" {
int tame_impl_window(int w, int h, const char *title);
int tame_impl_running(void);
void tame_impl_close(void);
void tame_impl_set_fps(int fps);
void tame_impl_begin(void);
void tame_impl_end(void);
int tame_impl_fps(void);
double tame_impl_frame_time(void);
double tame_impl_time(void);
int tame_impl_width(void);
int tame_impl_height(void);
double tame_impl_view_left(void);
double tame_impl_view_right(void);
double tame_impl_view_top(void);
double tame_impl_view_bottom(void);
double tame_impl_accel_x(void);
double tame_impl_accel_y(void);
double tame_impl_accel_z(void);
int tame_impl_accel_available(void);
int tame_impl_active(void);
void tame_impl_beep(double freq, int ms);
void tame_impl_tone(double freq, int ms, double vol);
void tame_impl_clear(int64_t color);
void tame_impl_rect(double x, double y, double w, double h, int64_t color);
void tame_impl_rect_lines(double x, double y, double w, double h,
                          int64_t color);
void tame_impl_circle(double x, double y, double radius, int64_t color);
void tame_impl_line(double x1, double y1, double x2, double y2, int64_t color);
void tame_impl_pixel(double x, double y, int64_t color);
void tame_impl_text(const char *s, double x, double y, int size,
                    int64_t color);
int tame_impl_key_down(const char *k);
int tame_impl_key_pressed(const char *k);
int tame_impl_key_released(const char *k);
int tame_impl_mouse_x(void);
int tame_impl_mouse_y(void);
int tame_impl_mouse_down(int b);
int tame_impl_mouse_pressed(int b);
double tame_impl_mouse_wheel(void);
int tame_impl_touch_count(void);
int tame_impl_touch_x(int i);
int tame_impl_touch_y(int i);
// Faz 3-4: kaynak registry'leri (texture/font/ses/müzik) + ekran görüntüsü
int tame_impl_load_texture(const char *path);
void tame_impl_draw_texture(int h, double x, double y);
void tame_impl_draw_texture_ex(int h, double x, double y, double scale,
                               double rotation);
int tame_impl_texture_width(int h);
int tame_impl_texture_height(int h);
void tame_impl_unload_texture(int h);
int tame_impl_load_font(const char *path, int size);
void tame_impl_text_font(int fh, const char *s, double x, double y, int size,
                         int64_t color);
int tame_impl_measure_text(const char *s, int size);
int tame_impl_load_sound(const char *path);
void tame_impl_play_sound(int h);
void tame_impl_stop_sound(int h);
void tame_impl_sound_volume(int h, double v);
int tame_impl_load_music(const char *path);
void tame_impl_play_music(int h);
void tame_impl_stop_music(int h);
void tame_impl_music_volume(int h, double v);
void tame_impl_cam3(double px, double py, double pz, double tx, double ty,
                    double tz, double fov);
void tame_impl_begin3(void);
void tame_impl_end3(void);
void tame_impl_cube(double x, double y, double z, double w, double h, double d,
                    int64_t color);
void tame_impl_cube_wires(double x, double y, double z, double w, double h,
                          double d, int64_t color);
void tame_impl_grid(int slices, double spacing);
void tame_impl_sphere(double x, double y, double z, double r, int64_t color);
void tame_impl_sphere_wires(double x, double y, double z, double r, int seg,
                            int64_t color);
void tame_impl_cylinder(double x, double y, double z, double r, double h,
                        int64_t color);
void tame_impl_plane(double x, double y, double z, double sx, double sz,
                     int64_t color);
void tame_impl_line3(double x1, double y1, double z1, double x2, double y2,
                     double z2, int64_t color);
double tame_impl_pick_box(double mx, double my, double bx, double by, double bz,
                          double bw, double bh, double bd);
double tame_impl_pick_sphere(double mx, double my, double cx, double cy,
                             double cz, double r);
int tame_impl_lights(int enable);
void tame_impl_light_set(int idx, int type, double x, double y, double z,
                         int64_t color);
void tame_impl_light_off(int idx);
void tame_impl_ambient(int64_t color);
int tame_impl_shadows(int enable);
void tame_impl_shadow_area(double area);
int tame_impl_load_model(const char *path);
int tame_impl_gen(int kind, double a, double b, double c, double d);
void tame_impl_draw_model(int h, double x, double y, double z, double scale,
                          int64_t tint);
void tame_impl_draw_model_rot(int h, double x, double y, double z, double yaw,
                              double scale, int64_t tint);
void tame_impl_model_texture(int h, int tex_handle);
int tame_impl_model_anim_count(int h);
int tame_impl_anim_frames(int h, int idx);
void tame_impl_anim(int h, int idx, int frame);
void tame_impl_unload_model(int h);
void tame_impl_triangle(double x1, double y1, double x2, double y2, double x3,
                        double y3, int64_t color);
void tame_impl_screenshot(const char *path);
// Gamepad — buton/eksen adla ("A", "LB", "LX", ...; bkz. tame_impl.c eşleme)
int tame_impl_gamepad_available(int id);
const char *tame_impl_gamepad_name(int id);
int tame_impl_gamepad_down(int id, const char *btn);
int tame_impl_gamepad_pressed(int id, const char *btn);
double tame_impl_gamepad_axis(int id, const char *axis);
int tame_impl_save_data(const char *name, const char *text);
char *tame_impl_load_data(const char *name);
void tame_impl_text_free(char *p);
void tame_impl_vibrate(int ms);
}

// Tulpar runtime'ının string allocator'ı (libtulpar_runtime.a'dan çözülür;
// AOT codegen de aynı sembolü kullanır → extern "C", unmangled).
extern "C" ObjString *vm_alloc_string_aot(void *vm, const char *chars,
                                          int length);

// --- VMValue unpack helpers -------------------------------------------------

static double tm_num(const VMValue *v) {
  if (!v) return 0.0;
  if (IS_INT(*v)) return (double)AS_INT(*v);
  if (IS_FLOAT(*v)) return AS_FLOAT(*v);
  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1.0 : 0.0;
  return 0.0;
}

static int64_t tm_int(const VMValue *v) {
  if (!v) return 0;
  if (IS_INT(*v)) return AS_INT(*v);
  if (IS_FLOAT(*v)) return (int64_t)AS_FLOAT(*v);
  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1 : 0;
  return 0;
}

static const char *tm_str(const VMValue *v) {
  if (v && IS_STRING(*v)) return AS_STRING(*v)->chars;
  return "";
}

// --- Builtins ---------------------------------------------------------------

extern "C" {

// tm_window(w, h, title) -> bool : pencereyi açar; başarısızsa (örn. display
// yok) stderr'e iki dilli bir açıklama basar ve false döner.
VMValue aot_tm_window_ptr(VMValue *w, VMValue *h, VMValue *title) {
  int ok = tame_impl_window((int)tm_int(w), (int)tm_int(h), tm_str(title));
  if (!ok) {
    fprintf(stderr,
            "[tame] Pencere acilamadi — grafik ortami (DISPLAY) bulunamadi "
            "olabilir. / Window could not be opened — no graphics "
            "environment (DISPLAY)?\n");
  }
  return VM_BOOL(ok);
}

VMValue aot_tm_running_ptr(void) { return VM_BOOL(tame_impl_running()); }

VMValue aot_tm_close_ptr(void) {
  tame_impl_close();
  return VM_VOID();
}

VMValue aot_tm_set_fps_ptr(VMValue *fps) {
  tame_impl_set_fps((int)tm_int(fps));
  return VM_VOID();
}

VMValue aot_tm_begin_ptr(void) {
  tame_impl_begin();
  return VM_VOID();
}

VMValue aot_tm_end_ptr(void) {
  tame_impl_end();
  return VM_VOID();
}

VMValue aot_tm_fps_ptr(void) { return VM_INT(tame_impl_fps()); }
VMValue aot_tm_frame_time_ptr(void) {
  return VM_FLOAT(tame_impl_frame_time());
}
VMValue aot_tm_time_ptr(void) { return VM_FLOAT(tame_impl_time()); }
VMValue aot_tm_width_ptr(void) { return VM_INT(tame_impl_width()); }
VMValue aot_tm_height_ptr(void) { return VM_INT(tame_impl_height()); }

// Görünür ekranın dünya-koordinatlı kenarları (Android'de bantlar dahil).
VMValue aot_tm_view_left_ptr(void)   { return VM_FLOAT(tame_impl_view_left()); }
VMValue aot_tm_view_right_ptr(void)  { return VM_FLOAT(tame_impl_view_right()); }
VMValue aot_tm_view_top_ptr(void)    { return VM_FLOAT(tame_impl_view_top()); }
VMValue aot_tm_view_bottom_ptr(void) { return VM_FLOAT(tame_impl_view_bottom()); }

VMValue aot_tm_clear_ptr(VMValue *color) {
  tame_impl_clear(tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_rect_ptr(VMValue *x, VMValue *y, VMValue *w, VMValue *h,
                        VMValue *color) {
  tame_impl_rect(tm_num(x), tm_num(y), tm_num(w), tm_num(h), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_rect_lines_ptr(VMValue *x, VMValue *y, VMValue *w, VMValue *h,
                              VMValue *color) {
  tame_impl_rect_lines(tm_num(x), tm_num(y), tm_num(w), tm_num(h),
                       tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_circle_ptr(VMValue *x, VMValue *y, VMValue *radius,
                          VMValue *color) {
  tame_impl_circle(tm_num(x), tm_num(y), tm_num(radius), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_line_ptr(VMValue *x1, VMValue *y1, VMValue *x2, VMValue *y2,
                        VMValue *color) {
  tame_impl_line(tm_num(x1), tm_num(y1), tm_num(x2), tm_num(y2),
                 tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_pixel_ptr(VMValue *x, VMValue *y, VMValue *color) {
  tame_impl_pixel(tm_num(x), tm_num(y), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_text_ptr(VMValue *s, VMValue *x, VMValue *y, VMValue *size,
                        VMValue *color) {
  tame_impl_text(tm_str(s), tm_num(x), tm_num(y), (int)tm_int(size),
                 tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_key_down_ptr(VMValue *k) {
  return VM_BOOL(tame_impl_key_down(tm_str(k)));
}

VMValue aot_tm_key_pressed_ptr(VMValue *k) {
  return VM_BOOL(tame_impl_key_pressed(tm_str(k)));
}

VMValue aot_tm_key_released_ptr(VMValue *k) {
  return VM_BOOL(tame_impl_key_released(tm_str(k)));
}

VMValue aot_tm_mouse_x_ptr(void) { return VM_INT(tame_impl_mouse_x()); }
VMValue aot_tm_mouse_y_ptr(void) { return VM_INT(tame_impl_mouse_y()); }

VMValue aot_tm_mouse_down_ptr(VMValue *b) {
  return VM_BOOL(tame_impl_mouse_down((int)tm_int(b)));
}

VMValue aot_tm_mouse_pressed_ptr(VMValue *b) {
  return VM_BOOL(tame_impl_mouse_pressed((int)tm_int(b)));
}

VMValue aot_tm_mouse_wheel_ptr(void) {
  return VM_FLOAT(tame_impl_mouse_wheel());
}

VMValue aot_tm_touch_count_ptr(void) {
  return VM_INT(tame_impl_touch_count());
}
VMValue aot_tm_touch_x_ptr(VMValue *i) {
  return VM_INT(tame_impl_touch_x((int)tm_int(i)));
}
VMValue aot_tm_touch_y_ptr(VMValue *i) {
  return VM_INT(tame_impl_touch_y((int)tm_int(i)));
}

// --- Faz 3: texture / font -------------------------------------------------

VMValue aot_tm_load_texture_ptr(VMValue *path) {
  return VM_INT(tame_impl_load_texture(tm_str(path)));
}

VMValue aot_tm_draw_texture_ptr(VMValue *tex, VMValue *x, VMValue *y) {
  tame_impl_draw_texture((int)tm_int(tex), tm_num(x), tm_num(y));
  return VM_VOID();
}

VMValue aot_tm_draw_texture_ex_ptr(VMValue *tex, VMValue *x, VMValue *y,
                                   VMValue *scale, VMValue *rotation) {
  tame_impl_draw_texture_ex((int)tm_int(tex), tm_num(x), tm_num(y),
                            tm_num(scale), tm_num(rotation));
  return VM_VOID();
}

VMValue aot_tm_texture_width_ptr(VMValue *tex) {
  return VM_INT(tame_impl_texture_width((int)tm_int(tex)));
}

VMValue aot_tm_texture_height_ptr(VMValue *tex) {
  return VM_INT(tame_impl_texture_height((int)tm_int(tex)));
}

VMValue aot_tm_unload_texture_ptr(VMValue *tex) {
  tame_impl_unload_texture((int)tm_int(tex));
  return VM_VOID();
}

VMValue aot_tm_load_font_ptr(VMValue *path, VMValue *size) {
  return VM_INT(tame_impl_load_font(tm_str(path), (int)tm_int(size)));
}

VMValue aot_tm_text_font_ptr(VMValue *font, VMValue *s, VMValue *x,
                             VMValue *y, VMValue *size, VMValue *color) {
  tame_impl_text_font((int)tm_int(font), tm_str(s), tm_num(x), tm_num(y),
                      (int)tm_int(size), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_measure_text_ptr(VMValue *s, VMValue *size) {
  return VM_INT(tame_impl_measure_text(tm_str(s), (int)tm_int(size)));
}

// --- Faz 4: ses / müzik ------------------------------------------------------

VMValue aot_tm_load_sound_ptr(VMValue *path) {
  return VM_INT(tame_impl_load_sound(tm_str(path)));
}

VMValue aot_tm_play_sound_ptr(VMValue *snd) {
  tame_impl_play_sound((int)tm_int(snd));
  return VM_VOID();
}

VMValue aot_tm_stop_sound_ptr(VMValue *snd) {
  tame_impl_stop_sound((int)tm_int(snd));
  return VM_VOID();
}

VMValue aot_tm_sound_volume_ptr(VMValue *snd, VMValue *vol) {
  tame_impl_sound_volume((int)tm_int(snd), tm_num(vol));
  return VM_VOID();
}

VMValue aot_tm_load_music_ptr(VMValue *path) {
  return VM_INT(tame_impl_load_music(tm_str(path)));
}

VMValue aot_tm_play_music_ptr(VMValue *mus) {
  tame_impl_play_music((int)tm_int(mus));
  return VM_VOID();
}

VMValue aot_tm_stop_music_ptr(VMValue *mus) {
  tame_impl_stop_music((int)tm_int(mus));
  return VM_VOID();
}

VMValue aot_tm_music_volume_ptr(VMValue *mus, VMValue *vol) {
  tame_impl_music_volume((int)tm_int(mus), tm_num(vol));
  return VM_VOID();
}

// --- Ek çizim / araç ---------------------------------------------------------

VMValue aot_tm_triangle_ptr(VMValue *x1, VMValue *y1, VMValue *x2,
                            VMValue *y2, VMValue *x3, VMValue *y3,
                            VMValue *color) {
  tame_impl_triangle(tm_num(x1), tm_num(y1), tm_num(x2), tm_num(y2),
                     tm_num(x3), tm_num(y3), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm_screenshot_ptr(VMValue *path) {
  tame_impl_screenshot(tm_str(path));
  return VM_VOID();
}

// --- 3D (Faz 0) --------------------------------------------------------------

VMValue aot_tm3_camera_ptr(VMValue *px, VMValue *py, VMValue *pz, VMValue *tx,
                           VMValue *ty, VMValue *tz, VMValue *fov) {
  tame_impl_cam3(tm_num(px), tm_num(py), tm_num(pz), tm_num(tx), tm_num(ty),
                 tm_num(tz), tm_num(fov));
  return VM_VOID();
}

VMValue aot_tm3_begin_ptr(void) {
  tame_impl_begin3();
  return VM_VOID();
}

VMValue aot_tm3_end_ptr(void) {
  tame_impl_end3();
  return VM_VOID();
}

VMValue aot_tm3_cube_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *w,
                         VMValue *h, VMValue *d, VMValue *color) {
  tame_impl_cube(tm_num(x), tm_num(y), tm_num(z), tm_num(w), tm_num(h),
                 tm_num(d), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_cube_wires_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *w,
                               VMValue *h, VMValue *d, VMValue *color) {
  tame_impl_cube_wires(tm_num(x), tm_num(y), tm_num(z), tm_num(w), tm_num(h),
                       tm_num(d), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_grid_ptr(VMValue *slices, VMValue *spacing) {
  tame_impl_grid((int)tm_int(slices), tm_num(spacing));
  return VM_VOID();
}

// --- 3D (Faz 1) — primitifler + raycast --------------------------------------

VMValue aot_tm3_sphere_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *r,
                           VMValue *color) {
  tame_impl_sphere(tm_num(x), tm_num(y), tm_num(z), tm_num(r), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_sphere_wires_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *r,
                                 VMValue *seg, VMValue *color) {
  tame_impl_sphere_wires(tm_num(x), tm_num(y), tm_num(z), tm_num(r),
                         (int)tm_int(seg), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_cylinder_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *r,
                             VMValue *h, VMValue *color) {
  tame_impl_cylinder(tm_num(x), tm_num(y), tm_num(z), tm_num(r), tm_num(h),
                     tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_plane_ptr(VMValue *x, VMValue *y, VMValue *z, VMValue *sx,
                          VMValue *sz, VMValue *color) {
  tame_impl_plane(tm_num(x), tm_num(y), tm_num(z), tm_num(sx), tm_num(sz),
                  tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_line_ptr(VMValue *x1, VMValue *y1, VMValue *z1, VMValue *x2,
                         VMValue *y2, VMValue *z2, VMValue *color) {
  tame_impl_line3(tm_num(x1), tm_num(y1), tm_num(z1), tm_num(x2), tm_num(y2),
                  tm_num(z2), tm_int(color));
  return VM_VOID();
}

// Ekran (mx,my)'den kamera ışını ile kutu/küreye tıklama testi. Vurursa çarpışma
// mesafesini (>=0), ıskalarsa -1 döner. Float döner.
VMValue aot_tm3_pick_box_ptr(VMValue *mx, VMValue *my, VMValue *bx, VMValue *by,
                             VMValue *bz, VMValue *bw, VMValue *bh,
                             VMValue *bd) {
  return VM_FLOAT(tame_impl_pick_box(tm_num(mx), tm_num(my), tm_num(bx),
                                     tm_num(by), tm_num(bz), tm_num(bw),
                                     tm_num(bh), tm_num(bd)));
}

VMValue aot_tm3_pick_sphere_ptr(VMValue *mx, VMValue *my, VMValue *cx,
                                VMValue *cy, VMValue *cz, VMValue *r) {
  return VM_FLOAT(tame_impl_pick_sphere(tm_num(mx), tm_num(my), tm_num(cx),
                                        tm_num(cy), tm_num(cz), tm_num(r)));
}

// --- 3D (Faz 4) — ışıklandırma ----------------------------------------------

VMValue aot_tm3_lights_ptr(VMValue *enable) {
  return VM_BOOL(tame_impl_lights((int)tm_int(enable)));
}

VMValue aot_tm3_light_set_ptr(VMValue *idx, VMValue *type, VMValue *x,
                              VMValue *y, VMValue *z, VMValue *color) {
  tame_impl_light_set((int)tm_int(idx), (int)tm_int(type), tm_num(x), tm_num(y),
                      tm_num(z), tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_light_off_ptr(VMValue *idx) {
  tame_impl_light_off((int)tm_int(idx));
  return VM_VOID();
}

VMValue aot_tm3_ambient_ptr(VMValue *color) {
  tame_impl_ambient(tm_int(color));
  return VM_VOID();
}

VMValue aot_tm3_shadows_ptr(VMValue *enable) {
  return VM_BOOL(tame_impl_shadows((int)tm_int(enable)));
}

VMValue aot_tm3_shadow_area_ptr(VMValue *area) {
  tame_impl_shadow_area(tm_num(area));
  return VM_VOID();
}

// --- 3D (Faz 2) — model yükleme / üretme / çizim / animasyon -----------------

VMValue aot_tm3_load_model_ptr(VMValue *path) {
  return VM_INT(tame_impl_load_model(tm_str(path)));
}

VMValue aot_tm3_gen_ptr(VMValue *kind, VMValue *a, VMValue *b, VMValue *c,
                        VMValue *d) {
  return VM_INT(tame_impl_gen((int)tm_int(kind), tm_num(a), tm_num(b), tm_num(c),
                              tm_num(d)));
}

VMValue aot_tm3_draw_model_ptr(VMValue *h, VMValue *x, VMValue *y, VMValue *z,
                               VMValue *scale, VMValue *tint) {
  tame_impl_draw_model((int)tm_int(h), tm_num(x), tm_num(y), tm_num(z),
                       tm_num(scale), tm_int(tint));
  return VM_VOID();
}

VMValue aot_tm3_draw_model_rot_ptr(VMValue *h, VMValue *x, VMValue *y,
                                   VMValue *z, VMValue *yaw, VMValue *scale,
                                   VMValue *tint) {
  tame_impl_draw_model_rot((int)tm_int(h), tm_num(x), tm_num(y), tm_num(z),
                           tm_num(yaw), tm_num(scale), tm_int(tint));
  return VM_VOID();
}

VMValue aot_tm3_model_texture_ptr(VMValue *h, VMValue *tex) {
  tame_impl_model_texture((int)tm_int(h), (int)tm_int(tex));
  return VM_VOID();
}

VMValue aot_tm3_anim_count_ptr(VMValue *h) {
  return VM_INT(tame_impl_model_anim_count((int)tm_int(h)));
}

VMValue aot_tm3_anim_frames_ptr(VMValue *h, VMValue *idx) {
  return VM_INT(tame_impl_anim_frames((int)tm_int(h), (int)tm_int(idx)));
}

VMValue aot_tm3_anim_ptr(VMValue *h, VMValue *idx, VMValue *frame) {
  tame_impl_anim((int)tm_int(h), (int)tm_int(idx), (int)tm_int(frame));
  return VM_VOID();
}

VMValue aot_tm3_unload_model_ptr(VMValue *h) {
  tame_impl_unload_model((int)tm_int(h));
  return VM_VOID();
}

// --- Gamepad ------------------------------------------------------------------

VMValue aot_tm_gamepad_available_ptr(VMValue *id) {
  return VM_BOOL(tame_impl_gamepad_available((int)tm_int(id)));
}

VMValue aot_tm_gamepad_name_ptr(VMValue *id) {
  const char *n = tame_impl_gamepad_name((int)tm_int(id));
  ObjString *s = vm_alloc_string_aot(nullptr, n, (int)strlen(n));
  return VM_OBJ((Obj *)s);
}

VMValue aot_tm_gamepad_down_ptr(VMValue *id, VMValue *btn) {
  return VM_BOOL(tame_impl_gamepad_down((int)tm_int(id), tm_str(btn)));
}

VMValue aot_tm_gamepad_pressed_ptr(VMValue *id, VMValue *btn) {
  return VM_BOOL(tame_impl_gamepad_pressed((int)tm_int(id), tm_str(btn)));
}

VMValue aot_tm_gamepad_axis_ptr(VMValue *id, VMValue *axis) {
  return VM_FLOAT(tame_impl_gamepad_axis((int)tm_int(id), tm_str(axis)));
}

// --- Kalıcı kayıt + titreşim --------------------------------------------------

VMValue aot_tm_save_data_ptr(VMValue *name, VMValue *text) {
  return VM_BOOL(tame_impl_save_data(tm_str(name), tm_str(text)));
}

// Dosya yoksa "" döner; LoadFileText tamponu kopya sonrası bırakılır.
VMValue aot_tm_load_data_ptr(VMValue *name) {
  char *t = tame_impl_load_data(tm_str(name));
  const char *c = t ? t : "";
  ObjString *s = vm_alloc_string_aot(nullptr, c, (int)strlen(c));
  if (t) tame_impl_text_free(t);
  return VM_OBJ((Obj *)s);
}

VMValue aot_tm_vibrate_ptr(VMValue *ms) {
  tame_impl_vibrate((int)tm_int(ms));
  return VM_VOID();
}

VMValue aot_tm_accel_x_ptr(void) { return VM_FLOAT(tame_impl_accel_x()); }
VMValue aot_tm_accel_y_ptr(void) { return VM_FLOAT(tame_impl_accel_y()); }
VMValue aot_tm_accel_z_ptr(void) { return VM_FLOAT(tame_impl_accel_z()); }
VMValue aot_tm_accel_available_ptr(void) {
  return VM_BOOL(tame_impl_accel_available() != 0);
}
VMValue aot_tm_active_ptr(void) { return VM_BOOL(tame_impl_active() != 0); }
VMValue aot_tm_beep_ptr(VMValue *freq, VMValue *ms) {
  tame_impl_beep(tm_num(freq), (int)tm_int(ms));
  return VM_VOID();
}
VMValue aot_tm_tone_ptr(VMValue *freq, VMValue *ms, VMValue *vol) {
  tame_impl_tone(tm_num(freq), (int)tm_int(ms), tm_num(vol));
  return VM_VOID();
}

} // extern "C"
