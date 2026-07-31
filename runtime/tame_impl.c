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
// 3D (Faz 0) — tek küresel Camera3D + temel primitifler.
//
// Camera3D/Vector3 struct'ları VMValue ABI'sinden GEÇMEZ; kamerayı C tarafında
// tutuyoruz (doku/font registry'siyle aynı felsefe — Tulpar yalnız düz skaler
// görür). tm3_begin/tm3_end 3D modunu mevcut begin/end (kare) İÇİNDE sarar:
// begin() → clear() → tm3_begin() → 3D çizim → tm3_end() → 2D HUD → end().
//
// NOT (Android): tame_impl_begin kamera-modunda BeginMode2D uygular (letterbox);
// 3D oyunda bu 2D dönüşüm istenmez — Faz 5'te 3D oyunlar için cam_on kapatılacak.
// Masaüstü/web'de cam_on=0 olduğundan Faz 0 PoC etkilenmez.
// ---------------------------------------------------------------------------

static Camera3D tame_cam3d = {0};
static int tame_cam3d_init = 0;

static void tame_cam3d_ensure(void) {
  if (tame_cam3d_init) return;
  tame_cam3d.position = (Vector3){0.0f, 4.0f, 8.0f};
  tame_cam3d.target = (Vector3){0.0f, 0.0f, 0.0f};
  tame_cam3d.up = (Vector3){0.0f, 1.0f, 0.0f};
  tame_cam3d.fovy = 45.0f;
  tame_cam3d.projection = CAMERA_PERSPECTIVE;
  tame_cam3d_init = 1;
}

// Işıklandırma durumu — asıl blok (shader kaynağı, ışık dizisi, API) aşağıda
// "Faz 4" başlığı altında; bu üçü burada çünkü tame_impl_begin3/end3 (hemen
// aşağıda) shader'ı bağlamak için görmek zorunda.
static Shader tame_light_shader = {0};
static int tame_light_ready = 0;   // shader derlendi mi
static int tame_lights_on = 0;     // ışıklandırma aktif mi

// Faz 4 bloğunda tanımlı — primitif çizimlerini ışık açıkken normal'i doğru
// olan birim mesh yoluna saptırmak için ileri bildirim.
// `shape`: 0=kutu 1=küre 2=silindir 3=düzlem. Işık kapalıysa 0 döner ve
// çağıran eski immediate-mode yolunu kullanır.
static int tame_lights_active(void);
static int tame_draw_lit(int shape, double x, double y, double z, double sx,
                         double sy, double sz, int64_t color);

// Kamerayı konumla: göz (px,py,pz), bakış hedefi (tx,ty,tz), dikey FOV derece.
void tame_impl_cam3(double px, double py, double pz, double tx, double ty,
                    double tz, double fov) {
  tame_cam3d_ensure();
  tame_cam3d.position = (Vector3){(float)px, (float)py, (float)pz};
  tame_cam3d.target = (Vector3){(float)tx, (float)ty, (float)tz};
  tame_cam3d.fovy = (float)fov;
}

void tame_impl_begin3(void) {
  tame_cam3d_ensure();
  BeginMode3D(tame_cam3d);
  if (tame_lights_active()) {
    // Specular hesabı kameranın dünya konumunu ister; kamera her kare
    // hareket edebildiği için burada güncelliyoruz.
    float view[3] = {tame_cam3d.position.x, tame_cam3d.position.y,
                     tame_cam3d.position.z};
    SetShaderValue(tame_light_shader,
                   tame_light_shader.locs[SHADER_LOC_VECTOR_VIEW], view,
                   SHADER_UNIFORM_VEC3);
    // rlgl'in anlık shader'ını değiştirir → DrawCube/DrawGrid gibi
    // immediate-mode çizimler ışık alır. (Modeller material.shader
    // kullandığından onlara ayrıca atanır — bkz. tame_model_apply_shader.)
    BeginShaderMode(tame_light_shader);
  }
}

void tame_impl_end3(void) {
  if (tame_lights_active()) EndShaderMode();
  EndMode3D();
}

void tame_impl_cube(double x, double y, double z, double w, double h, double d,
                    int64_t color) {
  if (tame_draw_lit(0, x, y, z, w, h, d, color)) return;
  DrawCube((Vector3){(float)x, (float)y, (float)z}, (float)w, (float)h,
           (float)d, tame_color(color));
}

void tame_impl_cube_wires(double x, double y, double z, double w, double h,
                          double d, int64_t color) {
  DrawCubeWires((Vector3){(float)x, (float)y, (float)z}, (float)w, (float)h,
                (float)d, tame_color(color));
}

void tame_impl_grid(int slices, double spacing) {
  DrawGrid(slices, (float)spacing);
}

// ---------------------------------------------------------------------------
// Faz 4 — ışıklandırma (yönlü + nokta ışık, Blinn-Phong).
//
// Shader GLSL kaynağı BURAYA GÖMÜLÜ (LoadShaderFromMemory) — .vs/.fs dosyası
// taşımıyoruz, böylece web/Android paketlerine ekstra asset girmiyor ve
// "oyunu kopyaladım, ışık gitti" sınıfı hata imkânsız. İki varyant var:
// masaüstü GL 3.3 (#version 330, in/out) ve GLES2 (#version 100,
// attribute/varying + gl_FragColor) — web ve Android GLES2 yolundan gider.
//
// DİKKAT — iki raylib gerçeği bu tasarımı zorunlu kıldı:
//  1) BeginShaderMode rlgl'in anlık shader'ını değiştirir, ama DrawMesh
//     material.shader kullanır → MODELLER BeginShaderMode'dan ETKİLENMEZ.
//     Bu yüzden modellerin materyaline shader'ı ayrıca atıyoruz.
//  2) DrawCube rlNormal3f üretir (ışık alır), ama DrawSphereEx/DrawCylinder
//     NORMAL ÜRETMEZ → immediate-mode küre/silindir yanlış gölgelenirdi.
//     Bu yüzden ışık AÇIKKEN primitifler, normal'i doğru olan cached birim
//     mesh'ler üzerinden DrawModelEx ile çizilir (aşağıdaki tame_unit_*).
//     Işık KAPALIYKEN eski immediate-mode yolu birebir korunur (hız + geriye
//     dönük uyum).
// ---------------------------------------------------------------------------

#define TAME_MAX_LIGHTS 4

typedef struct {
  int enabled;
  int type;          // 0 = yönlü (directional/güneş), 1 = nokta (point)
  Vector3 position;  // nokta ışık için konum, yönlü için YÖN
  Color color;
  int loc_enabled, loc_type, loc_pos, loc_color;
} TameLight;

// (tame_light_shader / tame_light_ready / tame_lights_on yukarıda, 3D
// bölümünün başında tanımlı — begin3/end3 onları görmek zorunda.)
static TameLight tame_lights[TAME_MAX_LIGHTS];
static int tame_loc_ambient = -1;
static float tame_ambient[4] = {0.18f, 0.18f, 0.22f, 1.0f};

// GLES2 (web/Android) mi, masaüstü GL3.3 mü? Derleyicinin KENDİ tanımladığı
// __EMSCRIPTEN__/__ANDROID__'i de sayıyoruz, sadece -DGRAPHICS_API_OPENGL_ES2'ye
// güvenmiyoruz: wasm/build_tame_web.sh tame_impl.c'yi (raylib'in aksine) o
// bayrak olmadan derliyordu ve web sessizce MASAÜSTÜ shader'ını seçip
// "'in' : storage qualifier supported in GLSL ES 3.00 and above only" ile
// derleme hatası veriyordu — sahne ışıksız çiziliyordu.
#if defined(GRAPHICS_API_OPENGL_ES2) || defined(PLATFORM_WEB) ||               \
    defined(PLATFORM_ANDROID) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
static const char *tame_light_vs =
    "#version 100                                \n"
    "attribute vec3 vertexPosition;              \n"
    "attribute vec2 vertexTexCoord;              \n"
    "attribute vec3 vertexNormal;                \n"
    "attribute vec4 vertexColor;                 \n"
    "uniform mat4 mvp;                           \n"
    "uniform mat4 matModel;                      \n"
    "uniform mat4 matNormal;                     \n"
    "varying vec3 fragPosition;                  \n"
    "varying vec2 fragTexCoord;                  \n"
    "varying vec4 fragColor;                     \n"
    "varying vec3 fragNormal;                    \n"
    "void main() {                               \n"
    "    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0)); \n"
    "    fragTexCoord = vertexTexCoord;          \n"
    "    fragColor = vertexColor;                \n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0))); \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                           \n";

static const char *tame_light_fs =
    "#version 100                                \n"
    "precision mediump float;                    \n"
    "varying vec3 fragPosition;                  \n"
    "varying vec2 fragTexCoord;                  \n"
    "varying vec4 fragColor;                     \n"
    "varying vec3 fragNormal;                    \n"
    "uniform sampler2D texture0;                 \n"
    "uniform vec4 colDiffuse;                    \n"
    "uniform vec4 ambient;                       \n"
    "uniform vec3 viewPos;                       \n"
    "uniform int  lightsEnabled[4];              \n"
    "uniform int  lightsType[4];                 \n"
    "uniform vec3 lightsPosition[4];             \n"
    "uniform vec4 lightsColor[4];                \n"
    "void main() {                               \n"
    "    vec4 texelColor = texture2D(texture0, fragTexCoord); \n"
    "    vec3 lightDot = vec3(0.0);              \n"
    "    vec3 normal = normalize(fragNormal);    \n"
    "    vec3 viewD = normalize(viewPos - fragPosition); \n"
    "    vec3 specular = vec3(0.0);              \n"
    "    for (int i = 0; i < 4; i++) {           \n"
    "        if (lightsEnabled[i] == 1) {        \n"
    "            vec3 light = vec3(0.0);         \n"
    "            float att = 1.0;                \n"
    "            if (lightsType[i] == 0) {       \n"
    "                light = -normalize(lightsPosition[i]); \n"
    "            } else {                        \n"
    "                vec3 d = lightsPosition[i] - fragPosition; \n"
    "                float dist = length(d);     \n"
    "                light = d/max(dist, 0.0001);\n"
    "                att = 1.0/(1.0 + 0.14*dist + 0.07*dist*dist); \n"
    "            }                               \n"
    "            float NdotL = max(dot(normal, light), 0.0); \n"
    "            lightDot += lightsColor[i].rgb*NdotL*att; \n"
    "            float specCo = 0.0;             \n"
    "            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0); \n"
    "            specular += specCo*att;         \n"
    "        }                                   \n"
    "    }                                       \n"
    "    vec4 finalColor = (texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0))); \n"
    "    finalColor += texelColor*(ambient)*colDiffuse; \n"
    "    gl_FragColor = finalColor;           \n"
    "}                                           \n";
#else
static const char *tame_light_vs =
    "#version 330                                \n"
    "in vec3 vertexPosition;                     \n"
    "in vec2 vertexTexCoord;                     \n"
    "in vec3 vertexNormal;                       \n"
    "in vec4 vertexColor;                        \n"
    "uniform mat4 mvp;                           \n"
    "uniform mat4 matModel;                      \n"
    "uniform mat4 matNormal;                     \n"
    "out vec3 fragPosition;                      \n"
    "out vec2 fragTexCoord;                      \n"
    "out vec4 fragColor;                         \n"
    "out vec3 fragNormal;                        \n"
    "void main() {                               \n"
    "    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0)); \n"
    "    fragTexCoord = vertexTexCoord;          \n"
    "    fragColor = vertexColor;                \n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0))); \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                           \n";

static const char *tame_light_fs =
    "#version 330                                \n"
    "in vec3 fragPosition;                       \n"
    "in vec2 fragTexCoord;                       \n"
    "in vec4 fragColor;                          \n"
    "in vec3 fragNormal;                         \n"
    "uniform sampler2D texture0;                 \n"
    "uniform vec4 colDiffuse;                    \n"
    "out vec4 finalColor;                        \n"
    "uniform vec4 ambient;                       \n"
    "uniform vec3 viewPos;                       \n"
    "uniform int  lightsEnabled[4];              \n"
    "uniform int  lightsType[4];                 \n"
    "uniform vec3 lightsPosition[4];             \n"
    "uniform vec4 lightsColor[4];                \n"
    "void main() {                               \n"
    "    vec4 texelColor = texture(texture0, fragTexCoord); \n"
    "    vec3 lightDot = vec3(0.0);              \n"
    "    vec3 normal = normalize(fragNormal);    \n"
    "    vec3 viewD = normalize(viewPos - fragPosition); \n"
    "    vec3 specular = vec3(0.0);              \n"
    "    for (int i = 0; i < 4; i++) {           \n"
    "        if (lightsEnabled[i] == 1) {        \n"
    "            vec3 light = vec3(0.0);         \n"
    "            float att = 1.0;                \n"
    "            if (lightsType[i] == 0) {       \n"
    "                light = -normalize(lightsPosition[i]); \n"
    "            } else {                        \n"
    "                vec3 d = lightsPosition[i] - fragPosition; \n"
    "                float dist = length(d);     \n"
    "                light = d/max(dist, 0.0001);\n"
    "                att = 1.0/(1.0 + 0.14*dist + 0.07*dist*dist); \n"
    "            }                               \n"
    "            float NdotL = max(dot(normal, light), 0.0); \n"
    "            lightDot += lightsColor[i].rgb*NdotL*att; \n"
    "            float specCo = 0.0;             \n"
    "            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0); \n"
    "            specular += specCo*att;         \n"
    "        }                                   \n"
    "    }                                       \n"
    "    finalColor = (texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0))); \n"
    "    finalColor += texelColor*(ambient)*colDiffuse; \n"

    "}                                           \n";
#endif

// Işık slot'unun uniform konumlarını (indeksli dizi elemanları) çöz.
static void tame_light_resolve_locs(int i) {
  char buf[64];
  snprintf(buf, sizeof(buf), "lightsEnabled[%i]", i);
  tame_lights[i].loc_enabled = GetShaderLocation(tame_light_shader, buf);
  snprintf(buf, sizeof(buf), "lightsType[%i]", i);
  tame_lights[i].loc_type = GetShaderLocation(tame_light_shader, buf);
  snprintf(buf, sizeof(buf), "lightsPosition[%i]", i);
  tame_lights[i].loc_pos = GetShaderLocation(tame_light_shader, buf);
  snprintf(buf, sizeof(buf), "lightsColor[%i]", i);
  tame_lights[i].loc_color = GetShaderLocation(tame_light_shader, buf);
}

// Bir ışık slot'unun tüm uniform'larını GPU'ya gönder.
static void tame_light_upload(int i) {
  if (!tame_light_ready) return;
  int en = tame_lights[i].enabled;
  int ty = tame_lights[i].type;
  SetShaderValue(tame_light_shader, tame_lights[i].loc_enabled, &en,
                 SHADER_UNIFORM_INT);
  SetShaderValue(tame_light_shader, tame_lights[i].loc_type, &ty,
                 SHADER_UNIFORM_INT);
  float pos[3] = {tame_lights[i].position.x, tame_lights[i].position.y,
                  tame_lights[i].position.z};
  SetShaderValue(tame_light_shader, tame_lights[i].loc_pos, pos,
                 SHADER_UNIFORM_VEC3);
  float col[4] = {(float)tame_lights[i].color.r / 255.0f,
                  (float)tame_lights[i].color.g / 255.0f,
                  (float)tame_lights[i].color.b / 255.0f,
                  (float)tame_lights[i].color.a / 255.0f};
  SetShaderValue(tame_light_shader, tame_lights[i].loc_color, col,
                 SHADER_UNIFORM_VEC4);
}

// Shader'ı ilk ihtiyaçta derle (GL context şart → window()'dan sonra).
static int tame_light_ensure(void) {
  if (tame_light_ready) return 1;
  if (!tame_window_ready) return 0;
  tame_light_shader = LoadShaderFromMemory(tame_light_vs, tame_light_fs);
  if (tame_light_shader.id == 0) {
    fprintf(stderr, "[tame] Isik shader'i derlenemedi. / Lighting shader "
                    "failed to compile.\n");
    return 0;
  }
  tame_light_shader.locs[SHADER_LOC_VECTOR_VIEW] =
      GetShaderLocation(tame_light_shader, "viewPos");
  tame_loc_ambient = GetShaderLocation(tame_light_shader, "ambient");
  tame_light_ready = 1;
  for (int i = 0; i < TAME_MAX_LIGHTS; i++) {
    tame_light_resolve_locs(i);
    tame_light_upload(i);
  }
  SetShaderValue(tame_light_shader, tame_loc_ambient, tame_ambient,
                 SHADER_UNIFORM_VEC4);
  return 1;
}

int tame_impl_lights(int enable) {
  if (enable) {
    if (!tame_light_ensure()) return 0;
    tame_lights_on = 1;
    // Hiç ışık tanımlanmadıysa makul bir güneş ver — "ışığı açtım, ekran
    // simsiyah" tuzağına düşülmesin.
    int any = 0;
    for (int i = 0; i < TAME_MAX_LIGHTS; i++) any |= tame_lights[i].enabled;
    if (!any) {
      tame_lights[0].enabled = 1;
      tame_lights[0].type = 0;
      tame_lights[0].position = (Vector3){-0.6f, -1.0f, -0.4f};
      tame_lights[0].color = (Color){255, 244, 214, 255};
      tame_light_upload(0);
    }
  } else {
    tame_lights_on = 0;
  }
  return 1;
}

void tame_impl_light_set(int idx, int type, double x, double y, double z,
                         int64_t color) {
  if (idx < 0 || idx >= TAME_MAX_LIGHTS) return;
  if (!tame_light_ensure()) return;
  tame_lights[idx].enabled = 1;
  tame_lights[idx].type = (type == 1) ? 1 : 0;
  tame_lights[idx].position = (Vector3){(float)x, (float)y, (float)z};
  tame_lights[idx].color = tame_color(color);
  tame_light_upload(idx);
}

void tame_impl_light_off(int idx) {
  if (idx < 0 || idx >= TAME_MAX_LIGHTS) return;
  tame_lights[idx].enabled = 0;
  tame_light_upload(idx);
}

void tame_impl_ambient(int64_t color) {
  Color c = tame_color(color);
  tame_ambient[0] = (float)c.r / 255.0f;
  tame_ambient[1] = (float)c.g / 255.0f;
  tame_ambient[2] = (float)c.b / 255.0f;
  tame_ambient[3] = 1.0f;
  if (tame_light_ensure())
    SetShaderValue(tame_light_shader, tame_loc_ambient, tame_ambient,
                   SHADER_UNIFORM_VEC4);
}

static int tame_lights_active(void) {
  return tame_lights_on && tame_light_ready;
}

// --- Işıklı primitif yolu (birim mesh önbelleği) ----------------------------
// DrawSphereEx/DrawCylinder normal üretmediği için ışık altında yanlış
// gölgelenirdi. Işık açıkken bunun yerine GenMesh* ile üretilmiş (normal'i
// doğru) BİRİM mesh'leri DrawModelEx ile ölçekleyerek çiziyoruz. Birim
// seçimleri: kutu 1×1×1, küre r=0.5, silindir r=0.5 h=1 (tabanı y=0'da),
// düzlem 1×1 (XZ). Böylece ölçek doğrudan istenen boyut olur.
//
// Düzgün-olmayan (non-uniform) ölçekte normaller bozulmaz: shader matNormal
// (transpose-inverse model matrisi) kullanıyor, rlgl bunu kendisi kuruyor.

static Model tame_unit[4];
static int tame_unit_ready = 0;

static void tame_unit_ensure(void) {
  if (tame_unit_ready || !tame_window_ready) return;
  tame_unit[0] = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
  tame_unit[1] = LoadModelFromMesh(GenMeshSphere(0.5f, 18, 18));
  tame_unit[2] = LoadModelFromMesh(GenMeshCylinder(0.5f, 1.0f, 24));
  tame_unit[3] = LoadModelFromMesh(GenMeshPlane(1.0f, 1.0f, 1, 1));
  tame_unit_ready = 1;
}

// Modelin materyallerine ışık shader'ını bağla (BeginShaderMode modellere
// işlemez — DrawMesh material.shader kullanır).
static void tame_model_apply_shader(Model *m) {
  if (!m) return;
  for (int i = 0; i < m->materialCount; i++)
    m->materials[i].shader = tame_light_shader;
}

static int tame_draw_lit(int shape, double x, double y, double z, double sx,
                         double sy, double sz, int64_t color) {
  if (!tame_lights_active()) return 0;
  tame_unit_ensure();
  if (!tame_unit_ready || shape < 0 || shape > 3) return 0;
  tame_model_apply_shader(&tame_unit[shape]);
  DrawModelEx(tame_unit[shape], (Vector3){(float)x, (float)y, (float)z},
              (Vector3){0.0f, 1.0f, 0.0f}, 0.0f,
              (Vector3){(float)sx, (float)sy, (float)sz}, tame_color(color));
  return 1;
}

// --- Faz 1 primitifleri -----------------------------------------------------

void tame_impl_sphere(double x, double y, double z, double r, int64_t color) {
  // Birim küre r=0.5 → ölçek = çap.
  if (tame_draw_lit(1, x, y, z, r * 2.0, r * 2.0, r * 2.0, color)) return;
  DrawSphere((Vector3){(float)x, (float)y, (float)z}, (float)r,
             tame_color(color));
}

void tame_impl_sphere_wires(double x, double y, double z, double r, int seg,
                            int64_t color) {
  if (seg < 3) seg = 3;
  DrawSphereWires((Vector3){(float)x, (float)y, (float)z}, (float)r, seg, seg,
                  tame_color(color));
}

void tame_impl_cylinder(double x, double y, double z, double r, double h,
                        int64_t color) {
  // Taban (x,y,z)'de duran, dikey silindir; radiusTop==radiusBottom==r.
  // Birim silindir r=0.5, h=1 ve tabanı y=0'da → ölçek (çap, yükseklik, çap).
  if (tame_draw_lit(2, x, y, z, r * 2.0, h, r * 2.0, color)) return;
  DrawCylinder((Vector3){(float)x, (float)y, (float)z}, (float)r, (float)r,
               (float)h, 20, tame_color(color));
}

void tame_impl_plane(double x, double y, double z, double sx, double sz,
                     int64_t color) {
  // Birim düzlem 1×1 (XZ) → ölçek doğrudan boyut; y ölçeği anlamsız (1).
  if (tame_draw_lit(3, x, y, z, sx, 1.0, sz, color)) return;
  DrawPlane((Vector3){(float)x, (float)y, (float)z},
            (Vector2){(float)sx, (float)sz}, tame_color(color));
}

void tame_impl_line3(double x1, double y1, double z1, double x2, double y2,
                     double z2, int64_t color) {
  DrawLine3D((Vector3){(float)x1, (float)y1, (float)z1},
             (Vector3){(float)x2, (float)y2, (float)z2}, tame_color(color));
}

// --- Faz 1 raycast (ekran → dünya, fare/dokunuş ile tıklama-seçim) -----------
// Global Camera3D'yi kullanır. Vurursa çarpışma MESAFESİNİ döner (dünya birimi,
// >= 0), ıskalarsa -1. Oyun kodu "en küçük pozitif mesafe = tıklanan nesne"
// mantığıyla seçim yapar.

double tame_impl_pick_box(double mx, double my, double bx, double by, double bz,
                          double bw, double bh, double bd) {
  tame_cam3d_ensure();
  Ray ray = GetScreenToWorldRay((Vector2){(float)mx, (float)my}, tame_cam3d);
  BoundingBox box;
  box.min = (Vector3){(float)(bx - bw * 0.5), (float)(by - bh * 0.5),
                      (float)(bz - bd * 0.5)};
  box.max = (Vector3){(float)(bx + bw * 0.5), (float)(by + bh * 0.5),
                      (float)(bz + bd * 0.5)};
  RayCollision hit = GetRayCollisionBox(ray, box);
  return hit.hit ? (double)hit.distance : -1.0;
}

double tame_impl_pick_sphere(double mx, double my, double cx, double cy,
                             double cz, double r) {
  tame_cam3d_ensure();
  Ray ray = GetScreenToWorldRay((Vector2){(float)mx, (float)my}, tame_cam3d);
  RayCollision hit = GetRayCollisionSphere(
      ray, (Vector3){(float)cx, (float)cy, (float)cz}, (float)r);
  return hit.hit ? (double)hit.distance : -1.0;
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

// ---------------------------------------------------------------------------
// Faz 2 — 3D model registry (yükle/üret + çiz + iskelet animasyonu).
//
// Doku/font registry'siyle aynı handle deseni (bu yüzden texture registry'sinden
// SONRA gelir — tame_textures/tame_texture_ok'a erişir). Bir handle bir Model +
// o modelin dosyasından yüklenen animasyon dizisini tutar (GLB/IQM animasyonu
// gömer). GenMesh* üreteçleri de LoadModelFromMesh ile aynı registry'ye model
// üretir → dosyasız (prosedürel) ve dosyalı modeller tek çizim yolunu paylaşır.
// ---------------------------------------------------------------------------

#define TAME_MAX_MODELS 128

typedef struct {
  Model model;
  ModelAnimation *anims;
  int anim_count;
  int used;
} TameModel;

static TameModel tame_models[TAME_MAX_MODELS];

static int tame_model_slot(void) {
  for (int i = 0; i < TAME_MAX_MODELS; i++)
    if (!tame_models[i].used) return i;
  return -1;
}

static int tame_model_ok(int h) {
  return h >= 0 && h < TAME_MAX_MODELS && tame_models[h].used;
}

// Dosyadan model yükle + (varsa) gömülü animasyonları da yükle. -1 = başarısız.
int tame_impl_load_model(const char *path) {
  if (!tame_window_ready) {
    fprintf(stderr, "[tame] load_model window()'dan once cagrilamaz. / "
                    "load_model requires window() first.\n");
    return -1;
  }
  int slot = tame_model_slot();
  if (slot < 0) return -1;
  Model m = LoadModel(path ? path : "");
  if (m.meshCount == 0) {
    UnloadModel(m);
    return -1;
  }
  int ac = 0;
  ModelAnimation *anims = LoadModelAnimations(path ? path : "", &ac);
  tame_models[slot].model = m;
  tame_models[slot].anims = anims;
  tame_models[slot].anim_count = ac;
  tame_models[slot].used = 1;
  return slot;
}

// Prosedürel mesh üret → model handle. kind ile şekil seçilir (bkz. lib/tame.tpr
// gen_* sarmalayıcıları). Dosya gerekmez.
int tame_impl_gen(int kind, double a, double b, double c, double d) {
  if (!tame_window_ready) return -1;
  int slot = tame_model_slot();
  if (slot < 0) return -1;
  Mesh mesh;
  int ib = (int)b, ic = (int)c, id = (int)d;
  if (ib < 1) ib = 1;
  if (ic < 3) ic = 3;
  if (id < 3) id = 3;
  switch (kind) {
    case 0: mesh = GenMeshCube((float)a, (float)b, (float)c); break;
    case 1: mesh = GenMeshSphere((float)a, ib, ic); break;
    case 2: mesh = GenMeshPlane((float)a, (float)b, ic, id); break;
    case 3: mesh = GenMeshCylinder((float)a, (float)b, ic); break;
    case 4: mesh = GenMeshTorus((float)a, (float)b, ic, id); break;
    case 5: mesh = GenMeshCone((float)a, (float)b, ic); break;
    case 6: mesh = GenMeshKnot((float)a, (float)b, ic, id); break;
    default: mesh = GenMeshCube((float)a, (float)b, (float)c); break;
  }
  Model m = LoadModelFromMesh(mesh);
  tame_models[slot].model = m;
  tame_models[slot].anims = NULL;
  tame_models[slot].anim_count = 0;
  tame_models[slot].used = 1;
  return slot;
}

void tame_impl_draw_model(int h, double x, double y, double z, double scale,
                          int64_t tint) {
  if (!tame_model_ok(h)) return;
  // Işık açıksa materyale shader'ı bağla (BeginShaderMode modellere işlemez).
  // Çizim anında yapıyoruz ki ışık oyun ortasında açılıp kapanabilsin.
  if (tame_lights_active()) tame_model_apply_shader(&tame_models[h].model);
  DrawModel(tame_models[h].model, (Vector3){(float)x, (float)y, (float)z},
            (float)scale, tame_color(tint));
}

// Y ekseni etrafında yaw (derece) ile döndürerek çiz — karakter/araç için tipik.
void tame_impl_draw_model_rot(int h, double x, double y, double z, double yaw,
                              double scale, int64_t tint) {
  if (!tame_model_ok(h)) return;
  if (tame_lights_active()) tame_model_apply_shader(&tame_models[h].model);
  DrawModelEx(tame_models[h].model, (Vector3){(float)x, (float)y, (float)z},
              (Vector3){0.0f, 1.0f, 0.0f}, (float)yaw,
              (Vector3){(float)scale, (float)scale, (float)scale},
              tame_color(tint));
}

// Yüklü bir dokuyu (tm_load_texture handle'ı) modelin ilk materyaline bağla.
void tame_impl_model_texture(int h, int tex_handle) {
  if (!tame_model_ok(h)) return;
  if (tame_models[h].model.materialCount < 1) return;
  if (!tame_texture_ok(tex_handle)) return;
  SetMaterialTexture(&tame_models[h].model.materials[0], MATERIAL_MAP_DIFFUSE,
                     tame_textures[tex_handle]);
}

int tame_impl_model_anim_count(int h) {
  return tame_model_ok(h) ? tame_models[h].anim_count : 0;
}

int tame_impl_anim_frames(int h, int idx) {
  if (!tame_model_ok(h) || idx < 0 || idx >= tame_models[h].anim_count) return 0;
  return tame_models[h].anims[idx].frameCount;
}

// Animasyon pozunu uygula (CPU skinning). frame'i oyun kodu her karede artırır
// ve frame-sayısına göre mod'lar.
void tame_impl_anim(int h, int idx, int frame) {
  if (!tame_model_ok(h) || idx < 0 || idx >= tame_models[h].anim_count) return;
  UpdateModelAnimation(tame_models[h].model, tame_models[h].anims[idx], frame);
}

void tame_impl_unload_model(int h) {
  if (!tame_model_ok(h)) return;
  if (tame_models[h].anims)
    UnloadModelAnimations(tame_models[h].anims, tame_models[h].anim_count);
  UnloadModel(tame_models[h].model);
  tame_models[h].anims = NULL;
  tame_models[h].anim_count = 0;
  tame_models[h].used = 0;
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
  // Işık altyapısı: birim mesh'ler + shader. Modellerin materyalleri bu
  // shader'a işaret ediyor olabilir, o yüzden ONLARDAN ÖNCE değil, önce
  // birim mesh'leri bırak, shader'ı en sonda kaldır.
  if (tame_unit_ready) {
    for (int i = 0; i < 4; i++) UnloadModel(tame_unit[i]);
    tame_unit_ready = 0;
  }
  for (int i = 0; i < TAME_MAX_MODELS; i++) {
    if (tame_models[i].used) {
      if (tame_models[i].anims)
        UnloadModelAnimations(tame_models[i].anims, tame_models[i].anim_count);
      UnloadModel(tame_models[i].model);
      tame_models[i].anims = NULL;
      tame_models[i].anim_count = 0;
      tame_models[i].used = 0;
    }
  }
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
  // Işık shader'ı en sonda — yukarıdaki modellerin materyalleri buna
  // işaret ediyordu, önce onların bırakılması gerekiyordu.
  if (tame_light_ready) {
    UnloadShader(tame_light_shader);
    tame_light_ready = 0;
    tame_lights_on = 0;
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

// tm_tone(freq, ms, vol) — beep'in ses-seviyeli kardeşi. vol 0..1, temel 0.35
// genliği ölçekler; arka plan müziği notaları SFX'i bastırmasın diye kısık
// (ör. 0.3) çalınır. tm_beep = tone(freq, ms, 1.0).
void tame_impl_tone(double freq, int ms, double vol) {
  if (freq <= 0.0 || ms <= 0) return;
  if (vol <= 0.0) return;
  if (vol > 1.0) vol = 1.0;
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
  double amp = 0.35 * vol;                            // temel düzey × ses seviyesi
  for (unsigned int i = 0; i < n; i++) {
    double env = 1.0;
    if (i < atk) env = (double)i / (double)atk;
    else if (i > n - atk) env = (double)(n - i) / (double)atk;
    double s = sin(step * (double)i) * env * amp;
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

// tm_beep(freq, ms) — tam-seviye ton (geriye dönük uyum: eski SFX çağrıları).
void tame_impl_beep(double freq, int ms) { tame_impl_tone(freq, ms, 1.0); }
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
