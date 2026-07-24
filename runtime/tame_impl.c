// Tame — Tulpar 2D game library, raylib-facing implementation layer.
//
// This TU is the ONLY one that includes raylib.h. The VMValue-facing
// bindings live in tame_bindings.cpp and call these flat-scalar functions
// through the prototypes below (kept in sync by hand — there is no shared
// header on purpose: raylib.h and the Tulpar runtime headers must never
// meet in one TU, because raylib's identifiers collide with windows.h
// (Rectangle, CloseWindow, ShowCursor, DrawText, LoadImage, ...) which the
// runtime headers pull in transitively on Windows.
//
// Color convention across the tame API: a single int64 packed as
// 0xRRGGBBAA (r<<24 | g<<16 | b<<8 | a). lib/tame.tpr exposes rgb()/rgba()
// and named color globals that produce this packing.

#include "raylib.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   // malloc/free (tm_beep dalga tamponu)
#include <math.h>     // sin (tm_beep sinüs sentezi)

// ---------------------------------------------------------------------------
// Window / loop
// ---------------------------------------------------------------------------

static int tame_window_ready = 0;

// --- Sanal dünya + kamera (Android) ----------------------------------------
// Android'de window(w,h) GERÇEKTE tam ekran açılır (InitWindow(0,0) → display
// çözünürlüğü) ve oyunun w×h dünyası Camera2D (zoom+offset) ile ortalanır.
// Neden: raylib'in kendi letterbox'ında bantlara çizim YAPILAMAZ (ortho
// projeksiyon dünya boyutunda) — kamera modelinde tüm ekran çizilebilir olur,
// dokunmatik kontroller gerçek ekran kenarına demirlenebilir (tm_view_*).
// Masaüstü/web'de kamera kapalıdır (pencere zaten w×h) → davranış birebir eski.
static int tame_world_w = 0, tame_world_h = 0;
static float tame_cam_zoom = 1.0f;
static float tame_cam_offx = 0.0f, tame_cam_offy = 0.0f;
static int tame_cam_on = 0;

static double tame_to_world_x(double x) {
  return tame_cam_on ? (x - (double)tame_cam_offx) / (double)tame_cam_zoom : x;
}
static double tame_to_world_y(double y) {
  return tame_cam_on ? (y - (double)tame_cam_offy) / (double)tame_cam_zoom : y;
}

static Color tame_color(int64_t packed) {
  Color c;
  c.r = (unsigned char)((packed >> 24) & 0xFF);
  c.g = (unsigned char)((packed >> 16) & 0xFF);
  c.b = (unsigned char)((packed >> 8) & 0xFF);
  c.a = (unsigned char)(packed & 0xFF);
  return c;
}

int tame_impl_window(int w, int h, const char *title) {
  if (tame_window_ready) return 1; // ikinci çağrı no-op / second call is a no-op
  SetTraceLogLevel(LOG_WARNING);   // oyun konsolunu raylib INFO spam'inden koru
  tame_world_w = w;
  tame_world_h = h;
#if defined(PLATFORM_ANDROID)
  // Tam ekran aç; w×h dünyası kamerayla ortalanır (yukarıdaki nota bak).
  InitWindow(0, 0, title ? title : "tame");
  if (!IsWindowReady()) return 0;
  {
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float zx = sw / (float)w;
    float zy = sh / (float)h;
    tame_cam_zoom = (zx < zy) ? zx : zy;
    tame_cam_offx = (sw - (float)w * tame_cam_zoom) * 0.5f;
    tame_cam_offy = (sh - (float)h * tame_cam_zoom) * 0.5f;
    tame_cam_on = 1;
  }
#else
  InitWindow(w, h, title ? title : "tame");
  if (!IsWindowReady()) return 0;
#endif
  SetTargetFPS(60); // varsayılan; tm_set_fps ile değiştirilebilir
  tame_window_ready = 1;
  return 1;
}

int tame_impl_running(void) {
  if (!tame_window_ready) return 0;
  return !WindowShouldClose();
}

// tame_impl.c'nin ilerisinde tanımlı registry temizliği (close çağırır).
static void tame_unload_all_resources(void);

void tame_impl_close(void) {
  if (!tame_window_ready) return;
  // GL kaynakları (texture/font) context kapanmadan, sesler aygıt
  // kapanmadan bırakılmalı — sıra: kaynaklar → ses aygıtı → pencere.
  tame_unload_all_resources();
  CloseWindow();
  tame_window_ready = 0;
}

void tame_impl_set_fps(int fps) { SetTargetFPS(fps); }

void tame_impl_begin(void) {
  BeginDrawing();
  if (tame_cam_on) {
    Camera2D cam = { 0 };
    cam.offset.x = tame_cam_offx;
    cam.offset.y = tame_cam_offy;
    cam.zoom = tame_cam_zoom;
    BeginMode2D(cam);   // bundan sonrası dünya koordinatı — eski kod değişmez
  }
}

// Müzik pompası tame_impl_end içinde — bkz. registry bölümü. Çalan her
// stream her karede beslenmezse ses birkaç saniyede susar; bunu kullanıcıya
// "her karede update_music çağır" kuralı olarak yıkmak yerine frame_end'in
// zaten her karede çağrıldığı gerçeğine yaslanıyoruz.
static void tame_pump_music(void);
static void tame_pump_sensor(void);   // ivmeölçer kuyruğu (Android; else no-op)

void tame_impl_end(void) {
  if (tame_cam_on) EndMode2D();
  EndDrawing();
  tame_pump_music();
  tame_pump_sensor();   // ivmeölçer kuyruğunu boşalt (Android; başka yerde no-op)
}

int tame_impl_fps(void) { return GetFPS(); }
double tame_impl_frame_time(void) { return (double)GetFrameTime(); }
double tame_impl_time(void) { return GetTime(); }
// Kamera modunda OYUN DÜNYASI boyutu döner (oyun kodu hep w×h'ye göre yazılır);
// gerçek ekranın dünya-uzayı kenarları için tm_view_* kullan.
int tame_impl_width(void) { return tame_cam_on ? tame_world_w : GetScreenWidth(); }
int tame_impl_height(void) { return tame_cam_on ? tame_world_h : GetScreenHeight(); }

// Görünür ekranın DÜNYA koordinatındaki kenarları. Masaüstü/web: 0..w, 0..h.
// Android (kamera): sol/üst negatif, sağ/alt w/h'den büyük olabilir — dokunmatik
// kontrolleri gerçek ekran kenarına demirlemek için.
double tame_impl_view_left(void)   { return tame_to_world_x(0.0); }
double tame_impl_view_top(void)    { return tame_to_world_y(0.0); }
double tame_impl_view_right(void)  { return tame_to_world_x((double)GetScreenWidth()); }
double tame_impl_view_bottom(void) { return tame_to_world_y((double)GetScreenHeight()); }

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void tame_impl_clear(int64_t color) { ClearBackground(tame_color(color)); }

void tame_impl_rect(double x, double y, double w, double h, int64_t color) {
  DrawRectangle((int)x, (int)y, (int)w, (int)h, tame_color(color));
}

void tame_impl_rect_lines(double x, double y, double w, double h,
                          int64_t color) {
  DrawRectangleLines((int)x, (int)y, (int)w, (int)h, tame_color(color));
}

void tame_impl_circle(double x, double y, double radius, int64_t color) {
  DrawCircle((int)x, (int)y, (float)radius, tame_color(color));
}

void tame_impl_line(double x1, double y1, double x2, double y2,
                    int64_t color) {
  DrawLine((int)x1, (int)y1, (int)x2, (int)y2, tame_color(color));
}

void tame_impl_pixel(double x, double y, int64_t color) {
  DrawPixel((int)x, (int)y, tame_color(color));
}

void tame_impl_text(const char *s, double x, double y, int size,
                    int64_t color) {
  DrawText(s ? s : "", (int)x, (int)y, size, tame_color(color));
}

// raylib DrawTriangle köşeleri saat yönünün TERSİNE ister; ters sırada
// üçgen hiç çizilmez (backface cull). Kullanıcıyı bu tuzaktan kurtarmak
// için işaretli alanla sarımı tespit edip gerekirse iki köşeyi takas eder.
void tame_impl_triangle(double x1, double y1, double x2, double y2, double x3,
                        double y3, int64_t color) {
  double signed_area = (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1);
  Vector2 a = {(float)x1, (float)y1};
  Vector2 b = {(float)x2, (float)y2};
  Vector2 c = {(float)x3, (float)y3};
  if (signed_area > 0) { // ekran koordinatlarında (y aşağı) saat yönü demek
    Vector2 tmp = b;
    b = c;
    c = tmp;
  }
  DrawTriangle(a, b, c, tame_color(color));
}

// ---------------------------------------------------------------------------
// Resource registries — textures / fonts / sounds / music.
//
// Tulpar sees resources as int handles (slot indexes), same model as the DB
// layer's descriptor-index handles: load_* scans for a free slot, draw/play
// validate the handle, unload frees the slot. -1 = load failed. Everything
// live is torn down in tame_impl_close() (GL resources need the context, so
// before CloseWindow; sounds before CloseAudioDevice).
// ---------------------------------------------------------------------------

#define TAME_MAX_TEXTURES 256
#define TAME_MAX_FONTS 64
#define TAME_MAX_SOUNDS 128
#define TAME_MAX_MUSIC 16

static Texture2D tame_textures[TAME_MAX_TEXTURES];
static int tame_texture_used[TAME_MAX_TEXTURES];
static Font tame_fonts[TAME_MAX_FONTS];
static int tame_font_used[TAME_MAX_FONTS];
static Sound tame_sounds[TAME_MAX_SOUNDS];
static int tame_sound_used[TAME_MAX_SOUNDS];
static Music tame_musics[TAME_MAX_MUSIC];
static int tame_music_used[TAME_MAX_MUSIC];

static int tame_audio_ready = 0;

// Ses aygıtını ilk ihtiyaçta aç (load_sound/load_music). Başarısızlık
// (aygıt yok / headless) -1 handle olarak yüzeye çıkar, çökmez.
static int tame_ensure_audio(void) {
  if (!tame_audio_ready) {
    InitAudioDevice();
    tame_audio_ready = IsAudioDeviceReady();
    if (!tame_audio_ready)
      fprintf(stderr, "[tame] Ses aygiti acilamadi. / Audio device could "
                      "not be opened.\n");
  }
  return tame_audio_ready;
}

int tame_impl_load_texture(const char *path) {
  if (!tame_window_ready) {
    fprintf(stderr, "[tame] load_texture window()'dan once cagrilamaz. / "
                    "load_texture requires window() first.\n");
    return -1;
  }
  Texture2D t = LoadTexture(path ? path : "");
  if (t.id == 0) return -1;
  for (int i = 0; i < TAME_MAX_TEXTURES; i++) {
    if (!tame_texture_used[i]) {
      tame_textures[i] = t;
      tame_texture_used[i] = 1;
      return i;
    }
  }
  UnloadTexture(t);
  return -1;
}

static int tame_texture_ok(int h) {
  return h >= 0 && h < TAME_MAX_TEXTURES && tame_texture_used[h];
}

void tame_impl_draw_texture(int h, double x, double y) {
  if (tame_texture_ok(h))
    DrawTexture(tame_textures[h], (int)x, (int)y, WHITE);
}

// Not: raylib DrawTextureEx sırası (rotation, scale)'dir; tame API'si
// (scale, rotation) alır — oyun kodunda ölçek çok daha sık kullanılıyor.
void tame_impl_draw_texture_ex(int h, double x, double y, double scale,
                               double rotation) {
  if (tame_texture_ok(h)) {
    Vector2 pos = {(float)x, (float)y};
    DrawTextureEx(tame_textures[h], pos, (float)rotation, (float)scale,
                  WHITE);
  }
}

int tame_impl_texture_width(int h) {
  return tame_texture_ok(h) ? tame_textures[h].width : 0;
}

int tame_impl_texture_height(int h) {
  return tame_texture_ok(h) ? tame_textures[h].height : 0;
}

void tame_impl_unload_texture(int h) {
  if (tame_texture_ok(h)) {
    UnloadTexture(tame_textures[h]);
    tame_texture_used[h] = 0;
  }
}

int tame_impl_load_font(const char *path, int size) {
  if (!tame_window_ready) {
    fprintf(stderr, "[tame] load_font window()'dan once cagrilamaz. / "
                    "load_font requires window() first.\n");
    return -1;
  }
  if (size <= 0) size = 32;
  Font f = LoadFontEx(path ? path : "", size, NULL, 0);
  if (!IsFontValid(f) || f.texture.id == 0) return -1;
  // LoadFontEx başarısızlıkta default fontu döndürebilir — onu slot'a alma
  // (unload'u default fontu bozar).
  if (f.texture.id == GetFontDefault().texture.id) return -1;
  for (int i = 0; i < TAME_MAX_FONTS; i++) {
    if (!tame_font_used[i]) {
      tame_fonts[i] = f;
      tame_font_used[i] = 1;
      return i;
    }
  }
  UnloadFont(f);
  return -1;
}

void tame_impl_text_font(int fh, const char *s, double x, double y, int size,
                         int64_t color) {
  if (fh < 0 || fh >= TAME_MAX_FONTS || !tame_font_used[fh]) return;
  Vector2 pos = {(float)x, (float)y};
  // DrawText'in varsayılanıyla aynı boşluk oranı (fontSize/10).
  DrawTextEx(tame_fonts[fh], s ? s : "", pos, (float)size,
             (float)size / 10.0f, tame_color(color));
}

int tame_impl_measure_text(const char *s, int size) {
  return MeasureText(s ? s : "", size);
}

int tame_impl_load_sound(const char *path) {
  if (!tame_ensure_audio()) return -1;
  Sound s = LoadSound(path ? path : "");
  if (!IsSoundValid(s)) return -1;
  for (int i = 0; i < TAME_MAX_SOUNDS; i++) {
    if (!tame_sound_used[i]) {
      tame_sounds[i] = s;
      tame_sound_used[i] = 1;
      return i;
    }
  }
  UnloadSound(s);
  return -1;
}

static int tame_sound_ok(int h) {
  return h >= 0 && h < TAME_MAX_SOUNDS && tame_sound_used[h];
}

void tame_impl_play_sound(int h) {
  if (tame_sound_ok(h)) PlaySound(tame_sounds[h]);
}

void tame_impl_stop_sound(int h) {
  if (tame_sound_ok(h)) StopSound(tame_sounds[h]);
}

void tame_impl_sound_volume(int h, double v) {
  if (tame_sound_ok(h)) SetSoundVolume(tame_sounds[h], (float)v);
}

int tame_impl_load_music(const char *path) {
  if (!tame_ensure_audio()) return -1;
  Music m = LoadMusicStream(path ? path : "");
  if (!IsMusicValid(m)) return -1;
  for (int i = 0; i < TAME_MAX_MUSIC; i++) {
    if (!tame_music_used[i]) {
      tame_musics[i] = m;
      tame_music_used[i] = 1;
      return i;
    }
  }
  UnloadMusicStream(m);
  return -1;
}

static int tame_music_ok(int h) {
  return h >= 0 && h < TAME_MAX_MUSIC && tame_music_used[h];
}

void tame_impl_play_music(int h) {
  if (tame_music_ok(h)) PlayMusicStream(tame_musics[h]);
}

void tame_impl_stop_music(int h) {
  if (tame_music_ok(h)) StopMusicStream(tame_musics[h]);
}

void tame_impl_music_volume(int h, double v) {
  if (tame_music_ok(h)) SetMusicVolume(tame_musics[h], (float)v);
}

// Ekran görüntüsünü PNG olarak kaydeder (çalışma dizinine göre yol).
// Hem oyuncu paylaşımı hem de otomatik görsel doğrulama için.
void tame_impl_screenshot(const char *path) {
  if (tame_window_ready) TakeScreenshot(path ? path : "tame_screenshot.png");
}

// Çalan müzik stream'lerini besle — tame_impl_end her kareden çağırır.
static void tame_pump_music(void) {
  if (!tame_audio_ready) return;
  for (int i = 0; i < TAME_MAX_MUSIC; i++) {
    if (tame_music_used[i] && IsMusicStreamPlaying(tame_musics[i]))
      UpdateMusicStream(tame_musics[i]);
  }
}

// close() sırasında canlı tüm kaynakları bırak (GL context / ses aygıtı
// hâlâ açıkken) ve ses aygıtını kapat.
static void tame_unload_all_resources(void) {
  for (int i = 0; i < TAME_MAX_TEXTURES; i++) {
    if (tame_texture_used[i]) {
      UnloadTexture(tame_textures[i]);
      tame_texture_used[i] = 0;
    }
  }
  for (int i = 0; i < TAME_MAX_FONTS; i++) {
    if (tame_font_used[i]) {
      UnloadFont(tame_fonts[i]);
      tame_font_used[i] = 0;
    }
  }
  for (int i = 0; i < TAME_MAX_SOUNDS; i++) {
    if (tame_sound_used[i]) {
      UnloadSound(tame_sounds[i]);
      tame_sound_used[i] = 0;
    }
  }
  for (int i = 0; i < TAME_MAX_MUSIC; i++) {
    if (tame_music_used[i]) {
      StopMusicStream(tame_musics[i]);
      UnloadMusicStream(tame_musics[i]);
      tame_music_used[i] = 0;
    }
  }
  if (tame_audio_ready) {
    CloseAudioDevice();
    tame_audio_ready = 0;
  }
}

// ---------------------------------------------------------------------------
// Input — keyboard
//
// Keys are addressed by name from Tulpar ("W", "SPACE", "LEFT", ...) so the
// language needs no key-constant table. Single letters/digits map directly;
// specials are matched case-insensitively by name.
// ---------------------------------------------------------------------------

static int tame_key_from_name(const char *name) {
  if (!name || !name[0]) return 0;
  // Tek karakter: harf/rakam doğrudan eşlenir (raylib keycode == ASCII upper)
  if (!name[1]) {
    char c = name[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (int)c;
    if (c == ' ') return KEY_SPACE;
    return 0;
  }
  // Adlı tuşlar — büyük/küçük harf duyarsız
  char up[24];
  size_t n = strlen(name);
  if (n >= sizeof(up)) return 0;
  for (size_t i = 0; i <= n; i++) {
    char c = name[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    up[i] = c;
  }
  if (!strcmp(up, "SPACE")) return KEY_SPACE;
  if (!strcmp(up, "ENTER") || !strcmp(up, "RETURN")) return KEY_ENTER;
  if (!strcmp(up, "ESC") || !strcmp(up, "ESCAPE")) return KEY_ESCAPE;
  // Android donanım GERİ tuşu (raylib bunu OS'a bırakmayıp KEY_BACK olarak
  // teslim eder — rcore_android "eat BACK_BUTTON"). Masaüstünde hiç basılmaz.
  if (!strcmp(up, "BACK") || !strcmp(up, "GERI")) return KEY_BACK;
  if (!strcmp(up, "TAB")) return KEY_TAB;
  if (!strcmp(up, "BACKSPACE")) return KEY_BACKSPACE;
  if (!strcmp(up, "DELETE")) return KEY_DELETE;
  if (!strcmp(up, "LEFT")) return KEY_LEFT;
  if (!strcmp(up, "RIGHT")) return KEY_RIGHT;
  if (!strcmp(up, "UP")) return KEY_UP;
  if (!strcmp(up, "DOWN")) return KEY_DOWN;
  if (!strcmp(up, "SHIFT") || !strcmp(up, "LSHIFT")) return KEY_LEFT_SHIFT;
  if (!strcmp(up, "RSHIFT")) return KEY_RIGHT_SHIFT;
  if (!strcmp(up, "CTRL") || !strcmp(up, "LCTRL")) return KEY_LEFT_CONTROL;
  if (!strcmp(up, "RCTRL")) return KEY_RIGHT_CONTROL;
  if (!strcmp(up, "ALT") || !strcmp(up, "LALT")) return KEY_LEFT_ALT;
  if (!strcmp(up, "RALT")) return KEY_RIGHT_ALT;
  if (!strcmp(up, "HOME")) return KEY_HOME;
  if (!strcmp(up, "END")) return KEY_END;
  if (!strcmp(up, "PAGEUP")) return KEY_PAGE_UP;
  if (!strcmp(up, "PAGEDOWN")) return KEY_PAGE_DOWN;
  if (up[0] == 'F' && up[1] >= '1' && up[1] <= '9') {
    int fn = up[1] - '0';
    if (up[2] >= '0' && up[2] <= '9' && up[3] == '\0')
      fn = fn * 10 + (up[2] - '0');
    else if (up[2] != '\0')
      return 0;
    if (fn >= 1 && fn <= 12) return KEY_F1 + (fn - 1);
  }
  return 0;
}

int tame_impl_key_down(const char *k) {
  int key = tame_key_from_name(k);
  return key ? IsKeyDown(key) : 0;
}

int tame_impl_key_pressed(const char *k) {
  int key = tame_key_from_name(k);
  return key ? IsKeyPressed(key) : 0;
}

int tame_impl_key_released(const char *k) {
  int key = tame_key_from_name(k);
  return key ? IsKeyReleased(key) : 0;
}

// ---------------------------------------------------------------------------
// Input — gamepad. Klavye gibi adla erişilir: buton "A"/"B"/"X"/"Y" (Xbox)
// veya "CROSS"/"CIRCLE"/"SQUARE"/"TRIANGLE" (PS), yönler "UP"/"DOWN"/...,
// omuzlar "LB"/"RB" ("L1"/"R1"), tetikler "LT"/"RT" ("L2"/"R2"),
// "START"/"SELECT"/"GUIDE", çubuk basmaları "L3"/"R3". Eksenler
// "LX"/"LY"/"RX"/"RY"/"LT"/"RT" (-1..1; tetikler 0..1). id = 0'dan başlar.
// ---------------------------------------------------------------------------

static int tame_gamepad_button_from_name(const char *name) {
  if (!name || !name[0]) return -1;
  char up[16];
  size_t n = strlen(name);
  if (n >= sizeof(up)) return -1;
  for (size_t i = 0; i <= n; i++) {
    char c = name[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    up[i] = c;
  }
  if (!strcmp(up, "A") || !strcmp(up, "CROSS"))
    return GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
  if (!strcmp(up, "B") || !strcmp(up, "CIRCLE"))
    return GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
  if (!strcmp(up, "X") || !strcmp(up, "SQUARE"))
    return GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
  if (!strcmp(up, "Y") || !strcmp(up, "TRIANGLE"))
    return GAMEPAD_BUTTON_RIGHT_FACE_UP;
  if (!strcmp(up, "UP")) return GAMEPAD_BUTTON_LEFT_FACE_UP;
  if (!strcmp(up, "DOWN")) return GAMEPAD_BUTTON_LEFT_FACE_DOWN;
  if (!strcmp(up, "LEFT")) return GAMEPAD_BUTTON_LEFT_FACE_LEFT;
  if (!strcmp(up, "RIGHT")) return GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
  if (!strcmp(up, "LB") || !strcmp(up, "L1"))
    return GAMEPAD_BUTTON_LEFT_TRIGGER_1;
  if (!strcmp(up, "RB") || !strcmp(up, "R1"))
    return GAMEPAD_BUTTON_RIGHT_TRIGGER_1;
  if (!strcmp(up, "LT") || !strcmp(up, "L2"))
    return GAMEPAD_BUTTON_LEFT_TRIGGER_2;
  if (!strcmp(up, "RT") || !strcmp(up, "R2"))
    return GAMEPAD_BUTTON_RIGHT_TRIGGER_2;
  if (!strcmp(up, "SELECT") || !strcmp(up, "BACK"))
    return GAMEPAD_BUTTON_MIDDLE_LEFT;
  if (!strcmp(up, "GUIDE") || !strcmp(up, "HOME"))
    return GAMEPAD_BUTTON_MIDDLE;
  if (!strcmp(up, "START")) return GAMEPAD_BUTTON_MIDDLE_RIGHT;
  if (!strcmp(up, "L3") || !strcmp(up, "LSTICK"))
    return GAMEPAD_BUTTON_LEFT_THUMB;
  if (!strcmp(up, "R3") || !strcmp(up, "RSTICK"))
    return GAMEPAD_BUTTON_RIGHT_THUMB;
  return -1;
}

static int tame_gamepad_axis_from_name(const char *name) {
  if (!name || !name[0]) return -1;
  char up[16];
  size_t n = strlen(name);
  if (n >= sizeof(up)) return -1;
  for (size_t i = 0; i <= n; i++) {
    char c = name[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    up[i] = c;
  }
  if (!strcmp(up, "LX") || !strcmp(up, "LEFT_X")) return GAMEPAD_AXIS_LEFT_X;
  if (!strcmp(up, "LY") || !strcmp(up, "LEFT_Y")) return GAMEPAD_AXIS_LEFT_Y;
  if (!strcmp(up, "RX") || !strcmp(up, "RIGHT_X")) return GAMEPAD_AXIS_RIGHT_X;
  if (!strcmp(up, "RY") || !strcmp(up, "RIGHT_Y")) return GAMEPAD_AXIS_RIGHT_Y;
  if (!strcmp(up, "LT") || !strcmp(up, "LEFT_TRIGGER"))
    return GAMEPAD_AXIS_LEFT_TRIGGER;
  if (!strcmp(up, "RT") || !strcmp(up, "RIGHT_TRIGGER"))
    return GAMEPAD_AXIS_RIGHT_TRIGGER;
  return -1;
}

int tame_impl_gamepad_available(int id) {
  return tame_window_ready ? IsGamepadAvailable(id) : 0;
}

const char *tame_impl_gamepad_name(int id) {
  if (!tame_window_ready || !IsGamepadAvailable(id)) return "";
  const char *n = GetGamepadName(id);
  return n ? n : "";
}

int tame_impl_gamepad_down(int id, const char *btn) {
  int b = tame_gamepad_button_from_name(btn);
  return (b >= 0) ? IsGamepadButtonDown(id, b) : 0;
}

int tame_impl_gamepad_pressed(int id, const char *btn) {
  int b = tame_gamepad_button_from_name(btn);
  return (b >= 0) ? IsGamepadButtonPressed(id, b) : 0;
}

double tame_impl_gamepad_axis(int id, const char *axis) {
  int a = tame_gamepad_axis_from_name(axis);
  return (a >= 0) ? (double)GetGamepadAxisMovement(id, a) : 0.0;
}

// ---------------------------------------------------------------------------
// Input — mouse. Button: 0=sol/left, 1=sağ/right, 2=orta/middle (raylib'le aynı)
// ---------------------------------------------------------------------------

int tame_impl_mouse_x(void) { return (int)tame_to_world_x((double)GetMouseX()); }
int tame_impl_mouse_y(void) { return (int)tame_to_world_y((double)GetMouseY()); }
int tame_impl_mouse_down(int b) { return IsMouseButtonDown(b); }
int tame_impl_mouse_pressed(int b) { return IsMouseButtonPressed(b); }
double tame_impl_mouse_wheel(void) { return (double)GetMouseWheelMove(); }

// Input — touch (mobil). raylib masaüstünde tek dokunuşu fareye maplediği
// için mouse_* zaten çalışır; bu API çok-parmak (multi-touch) ve açık parmak
// konumu verir. Index geçersizse (0,0) döner.
int tame_impl_touch_count(void) { return GetTouchPointCount(); }

// ---------------------------------------------------------------------------
// İvmeölçer (accelerometer) — telefonu eğerek kontrol. Android'de NDK
// ASensorManager ile (JNI YOK); değerler m/s^2, cihaz doğal yönüne göre
// x=sağ, y=yukarı, z=ekrandan dışarı. Masaüstü/web: 0 (nötr) — "egim" şeması
// klavyeye düşer. tame_pump_sensor her kare (frame_end) kuyruğu boşaltır.
// ---------------------------------------------------------------------------
static double tame_ax = 0.0, tame_ay = 0.0, tame_az = 0.0;
static int tame_accel_on = 0;   // 0=denenmedi, 1=aktif, -1=yok/başarısız

#if defined(PLATFORM_ANDROID)
#include <android/sensor.h>
#include <android/looper.h>
static ASensorManager    *tame_sensor_mgr = NULL;
static const ASensor     *tame_sensor_acc = NULL;
static ASensorEventQueue *tame_sensor_q   = NULL;

// Kuyruk CALLBACK'i: raylib zaten her kare ALooper_pollOnce çağırır; sensör
// fd'si hazır olunca looper bu callback'i OTOMATİK çalıştırır (getEvents
// zamanlamasını raylib'in poll döngüsüne bağlamak yerine). Son okumayı saklar.
// Dönüş 1 = beslemeyi sürdür.
static int tame_sensor_cb(int fd, int events, void *data) {
  (void)fd; (void)events; (void)data;
  if (!tame_sensor_q) return 1;
  ASensorEvent ev;
  while (ASensorEventQueue_getEvents(tame_sensor_q, &ev, 1) > 0) {
    tame_ax = (double)ev.acceleration.x;
    tame_ay = (double)ev.acceleration.y;
    tame_az = (double)ev.acceleration.z;
  }
  return 1;
}

static void tame_sensor_init(void) {
  if (tame_accel_on != 0) return;
  tame_accel_on = -1;                       // aksi kanıtlanana dek "yok"
#if __ANDROID_API__ >= 26
  tame_sensor_mgr = ASensorManager_getInstanceForPackage("dev.tulparlang.game");
#else
  tame_sensor_mgr = ASensorManager_getInstance();
#endif
  if (!tame_sensor_mgr) return;
  tame_sensor_acc = ASensorManager_getDefaultSensor(
      tame_sensor_mgr, ASENSOR_TYPE_ACCELEROMETER);
  if (!tame_sensor_acc) return;
  ALooper *looper = ALooper_forThread();
  if (!looper) looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  if (!looper) return;
  // Callback'li kuyruk: ident yerine callback → pollOnce olayı bize iletir.
  tame_sensor_q = ASensorManager_createEventQueue(
      tame_sensor_mgr, looper, 3 /*ident*/, tame_sensor_cb, NULL);
  if (!tame_sensor_q) return;
  ASensorEventQueue_enableSensor(tame_sensor_q, tame_sensor_acc);
  ASensorEventQueue_setEventRate(tame_sensor_q, tame_sensor_acc,
                                 (1000L * 1000L) / 60);   // ~60 Hz (mikro-sn)
  tame_accel_on = 1;
}

static void tame_pump_sensor(void) {
  // Init (ilk kare). Drenaj callback'te — burada bir şey yapmaya gerek yok;
  // yine de callback bir sebeple çalışmazsa güvenlik ağı olarak kuyruğu çek.
  if (tame_accel_on == 0) tame_sensor_init();
  if (tame_accel_on != 1 || !tame_sensor_q) return;
  ASensorEvent ev;
  while (ASensorEventQueue_getEvents(tame_sensor_q, &ev, 1) > 0) {
    tame_ax = (double)ev.acceleration.x;
    tame_ay = (double)ev.acceleration.y;
    tame_az = (double)ev.acceleration.z;
  }
}
#else
static void tame_pump_sensor(void) {}
#endif

double tame_impl_accel_x(void) {
#if defined(PLATFORM_ANDROID)
  if (tame_accel_on == 0) tame_sensor_init();
#endif
  return tame_ax;
}
double tame_impl_accel_y(void) {
#if defined(PLATFORM_ANDROID)
  if (tame_accel_on == 0) tame_sensor_init();
#endif
  return tame_ay;
}
double tame_impl_accel_z(void) { return tame_az; }
int tame_impl_accel_available(void) {
#if defined(PLATFORM_ANDROID)
  if (tame_accel_on == 0) tame_sensor_init();
  return tame_accel_on == 1 ? 1 : 0;
#else
  return 0;
#endif
}

// Uygulama önde/odakta mı (arka plana atılınca 0). G2: oyunlar buna göre
// "PAUSED" gösterebilir; motor zaten döngüyü Android'de otomatik dondurur.
int tame_impl_active(void) {
  if (!tame_window_ready) return 0;
  return IsWindowFocused() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// tm_beep(frekans_hz, sure_ms) — dosyasız ses efekti. Anlık bir sinüs dalgası
// üretip çalar; kısa oyun sesleri (zıpla/vur/topla) için asset taşımadan.
// Çalan Sound'lar bir havuzda tutulur (round-robin) — PlaySound async olduğu
// için ses bitene dek veri canlı kalmalı; slot yeniden kullanılınca eski
// Sound boşaltılır. Masaüstü/web/Android'de aynı (saf raylib audio).
// ---------------------------------------------------------------------------
#define TAME_BEEP_POOL 8
static Sound tame_beep_pool[TAME_BEEP_POOL];
static int   tame_beep_used[TAME_BEEP_POOL];
static int   tame_beep_next = 0;
static int   tame_beep_audio_ready = 0;

void tame_impl_beep(double freq, int ms) {
  if (freq <= 0.0 || ms <= 0) return;
  if (!tame_beep_audio_ready) {
    if (!IsAudioDeviceReady()) InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    tame_beep_audio_ready = 1;
  }
  int rate = 22050;
  if (ms > 3000) ms = 3000;                 // makul tavan
  unsigned int n = (unsigned int)((long)rate * ms / 1000);
  if (n < 1) return;
  short *pcm = (short *)malloc((size_t)n * sizeof(short));
  if (!pcm) return;
  double step = 6.28318530718 * freq / (double)rate;
  unsigned int atk = n / 20; if (atk < 1) atk = 1;   // ~%5 attack/decay (tık önle)
  for (unsigned int i = 0; i < n; i++) {
    double env = 1.0;
    if (i < atk) env = (double)i / (double)atk;
    else if (i > n - atk) env = (double)(n - i) / (double)atk;
    double s = sin(step * (double)i) * env * 0.35;   // 0.35 → kulak dostu düzey
    pcm[i] = (short)(s * 32767.0);
  }
  Wave w;
  w.frameCount = n;
  w.sampleRate = (unsigned int)rate;
  w.sampleSize = 16;
  w.channels = 1;
  w.data = pcm;
  Sound snd = LoadSoundFromWave(w);
  free(pcm);
  int slot = tame_beep_next;
  tame_beep_next = (tame_beep_next + 1) % TAME_BEEP_POOL;
  if (tame_beep_used[slot]) UnloadSound(tame_beep_pool[slot]);  // önceki biti/kesildi
  tame_beep_pool[slot] = snd;
  tame_beep_used[slot] = 1;
  PlaySound(snd);
}
// Kamera modunda display pikselini dünya koordinatına çeviririz (screen==display
// olduğundan raylib'in kendi ölçeklemesi kimliktir; dönüşüm tek elde — burada).
int tame_impl_touch_x(int i) { return (int)tame_to_world_x((double)GetTouchPosition(i).x); }
int tame_impl_touch_y(int i) { return (int)tame_to_world_y((double)GetTouchPosition(i).y); }

// ---------------------------------------------------------------------------
// Kalıcı kayıt (save/load) — raylib SaveFileText/LoadFileText üzerinden.
// Android'de raylib fopen'ı yönlendirir: yazma internalDataPath'e gider,
// okuma önce APK assets/ sonra internal storage'a bakar (utils.c
// android_fopen). Masaüstünde çalışma dizinine yazar/okur — yani AYNI oyun
// kodu her platformda kalıcı kayıt alır. İçerik düz metindir (skor için
// toString/toInt yeter); ad dosya adıdır ("skor" gibi, yol verme).
// ---------------------------------------------------------------------------

int tame_impl_save_data(const char *name, const char *text) {
  if (!name || !name[0] || !text) return 0;
  return SaveFileText(name, (char *)text) ? 1 : 0;
}

// Dönen tampon LoadFileText'in ayırdığı bellektir; binding kopyaladıktan
// sonra tame_impl_text_free ile bırakır. Dosya yoksa NULL döner.
char *tame_impl_load_data(const char *name) {
  if (!name || !name[0]) return NULL;
  return LoadFileText(name);
}

void tame_impl_text_free(char *p) {
  if (p) UnloadFileText(p);
}

// ---------------------------------------------------------------------------
// Titreşim (haptik) — yalnız Android'de gerçek iş yapar. NativeActivity'nin
// Java tarafına JNI ile geçip Context.getSystemService("vibrator") →
// Vibrator.vibrate(ms) çağrılır. AndroidManifest'e VIBRATE izni driver
// tarafından her zaman yazılır (normal izin, kurulum anında verilir).
// Masaüstü/web: sessiz no-op — oyun kodu platform ayrımı yapmaz.
// ---------------------------------------------------------------------------

#if defined(PLATFORM_ANDROID)
#include <jni.h>
struct android_app;              // native_app_glue tanımı (yalnız işaretçi)
struct android_app *GetAndroidApp(void);   // rcore_android.c (raylib)
// native_app_glue'nun android_app yapısından yalnız activity alanına
// ihtiyacımız var; başlığı çekmemek için ofsetle değil resmi başlıkla gidelim:
#include <android_native_app_glue.h>

void tame_impl_vibrate(int ms) {
  if (ms <= 0) return;
  struct android_app *app = GetAndroidApp();
  if (!app || !app->activity || !app->activity->vm) return;
  JavaVM *vm = app->activity->vm;
  JNIEnv *env = NULL;
  if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || !env) return;
  // Not: DetachCurrentThread çağırmıyoruz — render thread'i tekrar tekrar
  // attach/detach etmek pahalı; attach idempotenttir, thread çıkışında VM
  // temizler.
  jobject activity = app->activity->clazz;
  jclass ctx = (*env)->GetObjectClass(env, activity);
  jmethodID gss = (*env)->GetMethodID(
      env, ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  jstring svc = (*env)->NewStringUTF(env, "vibrator");
  jobject vib = gss ? (*env)->CallObjectMethod(env, activity, gss, svc) : NULL;
  if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
  if (vib) {
    jclass vibc = (*env)->GetObjectClass(env, vib);
    jmethodID vibrate = (*env)->GetMethodID(env, vibc, "vibrate", "(J)V");
    if (vibrate) (*env)->CallVoidMethod(env, vib, vibrate, (jlong)ms);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, vibc);
    (*env)->DeleteLocalRef(env, vib);
  }
  (*env)->DeleteLocalRef(env, svc);
  (*env)->DeleteLocalRef(env, ctx);
}
#else
void tame_impl_vibrate(int ms) { (void)ms; }
#endif
