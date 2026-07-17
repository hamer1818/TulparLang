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
void tame_impl_triangle(double x1, double y1, double x2, double y2, double x3,
                        double y3, int64_t color);
void tame_impl_screenshot(const char *path);
// Gamepad — buton/eksen adla ("A", "LB", "LX", ...; bkz. tame_impl.c eşleme)
int tame_impl_gamepad_available(int id);
const char *tame_impl_gamepad_name(int id);
int tame_impl_gamepad_down(int id, const char *btn);
int tame_impl_gamepad_pressed(int id, const char *btn);
double tame_impl_gamepad_axis(int id, const char *axis);
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

} // extern "C"
