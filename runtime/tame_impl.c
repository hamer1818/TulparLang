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
#include "raymath.h"   // MatrixOrtho/LookAt/Multiply — gölge ışık-uzayı matrisi
#include "rlgl.h"      // rlLoadFramebuffer/rlLoadTextureDepth — shadow map FBO

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
// "Ekran" ölçüsü ETKİN HEDEFİN ölçüsüdür. Render texture'a çizerken pencere
// boyutunu döndürmek, hedefe göre yerleşen her şeyi (sağa/alta yaslı HUD)
// dokunun dışına atıyordu — editörün sahne görünümü paneli tam olarak böyle
// bir hedef.
// (Tanımları dosyanın ilerisinde, render texture kaydının yanında.)
static int tame_target_w(void);
static int tame_target_h(void);

int tame_impl_width(void) { return tame_cam_on ? tame_world_w : tame_target_w(); }
int tame_impl_height(void) { return tame_cam_on ? tame_world_h : tame_target_h(); }

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
                         double sy, double sz, double yaw, int64_t color);
// Gölge açıkken sahne iki geçişte çizilir; bu ikisi o akışı sarar (tanımları
// Faz 4b bloğunda). tame_scene_begin 1 dönerse çizimler KAYDEDİLİYOR demektir
// ve tame_scene_end iki geçişi kendisi yapar.
static int tame_scene_begin(void);
static void tame_scene_end(void);
// Kayıt modunda mı? (kutu-tel/ızgara/çizgi/model çizimleri buna bakar)
static int tame_recording(void);
static void tame_dl_push(int kind, int handle, double x, double y, double z,
                         double sx, double sy, double sz, double yaw,
                         int64_t color);
// Model kayıt defteri aşağıda (doku registry'sinden sonra) — gölge geçişi
// modelleri çizmek için erişmek zorunda, bu yüzden erişimci ileri bildirimli.
static Model *tame_model_ptr(int h);
// Faz 5: gökyüzü kubbesi. begin3 (ve gölgeli yolda scene_end) kamerayı
// bağladıktan hemen sonra çağırır; tanımı birim mesh'lerin yanında.
static void tame_sky_draw(void);

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
  // Gölge açıksa çizimler kaydedilir ve space_end iki geçişte oynatır —
  // burada kamerayı/shader'ı bağlamayız.
  if (tame_scene_begin()) return;
  BeginMode3D(tame_cam3d);
  tame_sky_draw();   // sahnenin geri kalanından ÖNCE (derinliğe yazmaz)
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
  if (tame_recording()) { tame_scene_end(); return; }
  if (tame_lights_active()) EndShaderMode();
  EndMode3D();
}

void tame_impl_cube(double x, double y, double z, double w, double h, double d,
                    int64_t color) {
  if (tame_draw_lit(0, x, y, z, w, h, d, 0.0, color)) return;
  DrawCube((Vector3){(float)x, (float)y, (float)z}, (float)w, (float)h,
           (float)d, tame_color(color));
}

// Y ekseni etrafında DÖNMÜŞ kutu. `tm3_cube` dönme almıyordu; yaw'lı bir
// entity çarpışmada dönük (SAT/OBB) ama ekranda EKSEN HİZALI çiziliyordu.
// Yani görülen duvar ile çarpışan duvar aynı yerde değildi — oyuncu görünen
// duvarın içinden geçiyor gibi oluyordu (kullanıcı ekran görüntüsüyle
// gösterdi). Işıklı yol zaten DrawModelEx kullandığı için dönme bedava;
// ışıksız yedek yol rlgl matrisiyle döndürüyor.
void tame_impl_cube_rot(double x, double y, double z, double w, double h,
                        double d, double yaw, int64_t color) {
  if (tame_draw_lit(0, x, y, z, w, h, d, yaw, color)) return;
  rlPushMatrix();
  rlTranslatef((float)x, (float)y, (float)z);
  rlRotatef((float)yaw, 0.0f, 1.0f, 0.0f);
  DrawCube((Vector3){0.0f, 0.0f, 0.0f}, (float)w, (float)h, (float)d,
           tame_color(color));
  rlPopMatrix();
}

void tame_impl_cube_wires(double x, double y, double z, double w, double h,
                          double d, int64_t color) {
  if (tame_recording()) { tame_dl_push(5, -1, x, y, z, w, h, d, 0.0, color); return; }
  DrawCubeWires((Vector3){(float)x, (float)y, (float)z}, (float)w, (float)h,
                (float)d, tame_color(color));
}

void tame_impl_grid(int slices, double spacing) {
  if (tame_recording()) { tame_dl_push(6, -1, 0, 0, 0, slices, spacing, 0, 0.0, 0); return; }
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

// --- Faz 5 durumu: doku + materyal ------------------------------------------
// "Geçerli materyal" bir DURUM'dur: tm3_texture/tm3_material çağrıldıktan sonra
// çizilen her 3B primitif onu kullanır (OpenGL'in klasik state machine'i gibi;
// oyun kodu her çizime 5 parametre daha yazmak zorunda kalmasın diye).
// Varsayılanlar eski davranışın birebir aynısı: doku yok, tile (1,1),
// shine 16, spec 1 — yani Faz 5 hiçbir mevcut sahnenin görüntüsünü değiştirmez.
static int tame_loc_texTile = -1;
static int tame_loc_matShine = -1;
static int tame_loc_matSpec = -1;
static int tame_cur_tex = -1;          // -1 = doku yok (düz renk)
static float tame_cur_tile[2] = {1.0f, 1.0f};
static float tame_cur_shine = 16.0f;
static float tame_cur_spec = 1.0f;
// Sis. Yoğunluk 0 = kapalı (shader'da exp(0)=1 → hiç karışım yok), o yüzden
// ayrı bir "açık mı" bayrağı yok. Renk gökyüzünün UFUK rengiyle aynı olmalı,
// yoksa uzaktaki cisimler gökyüzüne değil başka bir renge karışır ve sis
// "kirli cam" gibi görünür — bu yüzden fog(-1, d) sky()'nin ufkunu kullanır.
static int tame_loc_fogColor = -1;
static int tame_loc_fogDensity = -1;
static float tame_fog_color[4] = {0.75f, 0.84f, 0.93f, 1.0f};
static float tame_fog_density = 0.0f;

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
    "varying highp vec3 fragPosition;            \n"
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
    // Gölge haritası derinlik karşılaştırması mediump'ta (yaklaşık 10 bit
    // mantis) tamamen bozulur: cismin üstünde bir şeyler tutturur ama geniş
    // zemin düzleminde gölge hiç oluşmaz. Fragment shader'da highp GLES2'de
    // opsiyoneldir, bu yüzden GL_FRAGMENT_PRECISION_HIGH ile koşullu.
    "#ifdef GL_FRAGMENT_PRECISION_HIGH           \n"
    "precision highp float;                      \n"
    "#else                                       \n"
    "precision mediump float;                    \n"
    "#endif                                      \n"
    "varying highp vec3 fragPosition;            \n"
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
    "uniform sampler2D shadowMap;                \n"
    "uniform mat4 lightVP;                       \n"
    "uniform int  shadowOn;                      \n"
    "uniform float shadowTexel;                  \n"
    // Faz 5 — doku döşeme + materyal. texTile UV'yi çarpar (zeminde tek dokuyu
    // N kez tekrarlatmak için); matShine specular üssü, matSpec parlamanın
    // gücü. Varsayılanlar (1,1)/16/1 eski davranışın birebir aynısı.
    "uniform vec2  texTile;                      \n"
    "uniform float matShine;                     \n"
    "uniform float matSpec;                      \n"
    // Sis: yoğunluk 0 iken exp(0)=1 → hiç karışım olmaz, yani ayrı bir
    // "sis açık mı" bayrağına gerek yok.
    "uniform vec4  fogColor;                     \n"
    "uniform float fogDensity;                   \n"
    "float shadowFactor(vec3 n, vec3 l) {        \n"
    "    if (shadowOn == 0) return 1.0;          \n"
    "    vec4 lp = lightVP*vec4(fragPosition, 1.0); \n"
    "    vec3 proj = lp.xyz/lp.w*0.5 + 0.5;      \n"
    "    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0; \n"
    "    float bias = max(0.0025*(1.0 - dot(n, l)), 0.0006); \n"
    "    float lit = 0.0;                        \n"
    "    for (int u = -1; u <= 1; u++) {         \n"
    "        for (int v = -1; v <= 1; v++) {     \n"
    "            vec2 o = vec2(float(u), float(v))*shadowTexel; \n"
    "            highp vec4 pk = texture2D(shadowMap, proj.xy + o); \n"
    "            highp float d = dot(pk, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0)); \n"
    "            if (proj.z - bias <= d) lit += 1.0; \n"
    "        }                                   \n"
    "    }                                       \n"
    "    return 0.25 + 0.75*(lit/9.0);           \n"
    "}                                           \n"
    "void main() {                               \n"
    // fragColor = TEPE NOKTASI rengi. Stok raylib ışık shader'ı bunu vertex
    // shader'dan taşıyıp fragment'ta HİÇ kullanmıyordu; dokuyla aynı yerde
    // (yüzeyin albedosu olarak) çarpılması gerekiyor. Renk tamponu olmayan
    // mesh'lerde raylib öznitelik varsayılanını beyaz yapıyor, yani bu satır
    // mevcut hiçbir çizimi değiştirmiyor — yalnız renk YAZAN mesh'i etkiliyor
    // (bkz. arazi katman boyama).
    "    vec4 texelColor = texture2D(texture0, fragTexCoord*texTile)*fragColor; \n"
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
    "            float sh = 1.0;                 \n"
    "            if (lightsType[i] == 0) sh = shadowFactor(normal, light); \n"
    "            lightDot += lightsColor[i].rgb*NdotL*att*sh; \n"
    "            float specCo = 0.0;             \n"
    "            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), matShine); \n"
    "            specular += specCo*att*sh*matSpec; \n"
    "        }                                   \n"
    "    }                                       \n"
    "    vec4 finalColor = (texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0))); \n"
    "    finalColor += texelColor*(ambient)*colDiffuse; \n"
    // Üssel-kare sis: yakında hiç yok, uzakta hızlı doyuyor — düz doğrusal
    // sisin "her şey biraz soluk" görüntüsünü vermez.
    "    highp float fd = length(viewPos - fragPosition)*fogDensity; \n"
    "    float ff = clamp(exp(0.0 - fd*fd), 0.0, 1.0); \n"
    "    finalColor.rgb = mix(fogColor.rgb, finalColor.rgb, ff); \n"
    // ALFA'yı açıkça kur. Stok raylib ışık shader'ı opak varsayımıyla yazılmış:
    // yukarıdaki `colDiffuse + vec4(specular, 1.0)` terimi alfaya 1.0 EKLİYOR,
    // ambient terimi de üstüne bir pay koyuyor. Sonuçta 70/255 = 0.27'lik bir
    // tint alfası 1.54'e çıkıp 1.0'a kırpılıyordu — yani saydam çizmek
    // imkânsızdı (kamera röntgeni bu yüzden görünmüyordu). Doğru alfa yüzeyin
    // kendi alfası ile tint alfasının çarpımı; opak çizimlerde ikisi de 1
    // olduğu için mevcut hiçbir görüntü değişmiyor.
    "    finalColor.a = texelColor.a*colDiffuse.a; \n"
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
    "uniform sampler2D shadowMap;                \n"
    "uniform mat4 lightVP;                       \n"
    "uniform int  shadowOn;                      \n"
    "uniform float shadowTexel;                  \n"
    // Faz 5 — doku döşeme + materyal + sis (bkz. GLES varyantındaki notlar).
    "uniform vec2  texTile;                      \n"
    "uniform float matShine;                     \n"
    "uniform float matSpec;                      \n"
    "uniform vec4  fogColor;                     \n"
    "uniform float fogDensity;                   \n"
    "float shadowFactor(vec3 n, vec3 l) {        \n"
    "    if (shadowOn == 0) return 1.0;          \n"
    "    vec4 lp = lightVP*vec4(fragPosition, 1.0); \n"
    "    vec3 proj = lp.xyz/lp.w*0.5 + 0.5;      \n"
    "    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0; \n"
    "    float bias = max(0.0025*(1.0 - dot(n, l)), 0.0006); \n"
    "    float lit = 0.0;                        \n"
    "    for (int u = -1; u <= 1; u++) {         \n"
    "        for (int v = -1; v <= 1; v++) {     \n"
    "            vec2 o = vec2(float(u), float(v))*shadowTexel; \n"
    "            vec4 pk = texture(shadowMap, proj.xy + o); \n"
    "            float d = dot(pk, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0)); \n"
    "            if (proj.z - bias <= d) lit += 1.0; \n"
    "        }                                   \n"
    "    }                                       \n"
    "    return 0.25 + 0.75*(lit/9.0);           \n"
    "}                                           \n"
    "void main() {                               \n"
    // Tepe noktası rengi — gerekçe GLES varyantındaki yorumda.
    "    vec4 texelColor = texture(texture0, fragTexCoord*texTile)*fragColor; \n"
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
    "            float sh = 1.0;                 \n"
    "            if (lightsType[i] == 0) sh = shadowFactor(normal, light); \n"
    "            lightDot += lightsColor[i].rgb*NdotL*att*sh; \n"
    "            float specCo = 0.0;             \n"
    "            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), matShine); \n"
    "            specular += specCo*att*sh*matSpec; \n"
    "        }                                   \n"
    "    }                                       \n"
    "    finalColor = (texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0))); \n"
    "    finalColor += texelColor*(ambient)*colDiffuse; \n"
    // Üssel-kare sis (bkz. GLES varyantı).
    "    float fd = length(viewPos - fragPosition)*fogDensity; \n"
    "    float ff = clamp(exp(-fd*fd), 0.0, 1.0); \n"
    "    finalColor.rgb = mix(fogColor.rgb, finalColor.rgb, ff); \n"
    // Alfa — gerekçe GLES varyantındaki yorumda.
    "    finalColor.a = texelColor.a*colDiffuse.a; \n"
    "}                                           \n";
#endif

// --- Faz 5: gökyüzü ---------------------------------------------------------
// Kameraya duyarlı gradyan KUBBE — asset yok, cubemap yok, 6 resim yok.
// Kameranın konumunda duran BÜYÜK bir küre çizilir ve rengi bakış YÖNÜNE göre
// hesaplanır, yani yukarı bakınca zenit, aşağı bakınca ufuk rengi gelir (2B
// gradyan arka planın yapamadığı şey bu). Kürenin İÇİNDEYİZ, o yüzden ön yüz
// ayıklanır; derinliğe YAZMAZ, böylece kürenin yarıçapından uzaktaki cisimler
// de önünde çizilir.
#if defined(GRAPHICS_API_OPENGL_ES2) || defined(PLATFORM_WEB) ||               \
    defined(PLATFORM_ANDROID) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
static const char *tame_sky_vs =
    "#version 100                                \n"
    "attribute vec3 vertexPosition;              \n"
    "uniform mat4 mvp;                           \n"
    "varying vec3 vdir;                          \n"
    "void main() {                               \n"
    "    vdir = vertexPosition;                  \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                           \n";
static const char *tame_sky_fs =
    "#version 100                                \n"
    "precision mediump float;                    \n"
    "varying vec3 vdir;                          \n"
    "uniform vec4 skyTop;                        \n"
    "uniform vec4 skyBottom;                     \n"
    "uniform float starI;                        \n"
    "uniform float cloudI;                       \n"
    "uniform float cloudT;                       \n"
    // Yıldızlar PROSEDÜREL: bakış yönü bir ızgaraya yuvarlanıp hash'leniyor,
    // eşiği geçen hücre bir yıldız oluyor. Sıfır çizim çağrısı, sıfır asset ve
    // örtüşme kendiliğinden doğru — gökyüzü kubbesi zaten en arkada çiziliyor,
    // yani dağlar yıldızları örtüyor. 2B çizilseydi dağların ÖNÜNE düşerlerdi.
    // GLES2'de bit işlemi yok, o yüzden float hash.
    "float tmStarHash(vec3 p) {                  \n"
    "    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164)))*43758.5453); \n"
    "}                                           \n"
    // Bulutlar da PROSEDÜREL, yıldızlarla aynı gerekçeyle: sıfır çizim
    // çağrısı, sıfır asset ve örtüşme kendiliğinden doğru (kubbe en arkada,
    // dağlar bulutları örtüyor). 2B çizilseydi dağların ÖNÜNE düşerlerdi.
    //
    // Yön vektörü YATAY düzleme izdüşürülüyor (d.xz/d.y), yani bulutlar sabit
    // yükseklikte bir tabaka gibi görünüyor ve ufka doğru sıkışıyorlar —
    // düz d.xz kullanmak onları kubbeye yapıştırırdı.
    "float tmCloudHash(vec2 p) {                 \n"
    "    return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); \n"
    "}                                           \n"
    "float tmCloudNoise(vec2 p) {                \n"
    "    vec2 i = floor(p); vec2 f = fract(p);   \n"
    "    f = f*f*(3.0-2.0*f);                    \n"
    "    float a = tmCloudHash(i);               \n"
    "    float b = tmCloudHash(i+vec2(1.0,0.0)); \n"
    "    float c = tmCloudHash(i+vec2(0.0,1.0)); \n"
    "    float e = tmCloudHash(i+vec2(1.0,1.0)); \n"
    "    return mix(mix(a,b,f.x), mix(c,e,f.x), f.y); \n"
    "}                                           \n"
    "float tmCloudFbm(vec2 p) {                  \n"
    "    float v = 0.0; float amp = 0.5;         \n"
    "    for (int k = 0; k < 3; k++) {           \n"
    "        v += amp*tmCloudNoise(p);           \n"
    "        p *= 2.03; amp *= 0.5;              \n"
    "    }                                       \n"
    "    return v;                               \n"
    "}                                           \n"
    "void main() {                               \n"
    "    vec3 d = normalize(vdir);               \n"
    "    float t = clamp(d.y, 0.0, 1.0);         \n"
    "    vec4 col = mix(skyBottom, skyTop, pow(t, 0.55)); \n"
    "    if (starI > 0.001) {                    \n"
    "        float h = tmStarHash(floor(d*260.0)); \n"
    "        if (h > 0.9972) {                   \n"
    // Parlaklık hücreden hücreye değişsin, hepsi aynı beyazlıkta olmasın.
    "            float b = 0.35 + 0.65*fract(h*137.0); \n"
    // Ufka yakın yıldızlar sönük: gerçekte atmosfer yutar, ayrıca ufuk
    // çizgisinde biten yıldız alanı yapay görünür.
    "            float horiz = smoothstep(0.02, 0.30, d.y); \n"
    "            col.rgb += vec3(b*starI*horiz);  \n"
    "        }                                   \n"
    "    }                                       \n"
    "    if (cloudI > 0.001 && d.y > 0.02) {     \n"
    "        vec2 pl = d.xz/max(d.y, 0.05);      \n"
    "        float n = tmCloudFbm(pl*0.9 + vec2(cloudT*0.02, cloudT*0.01)); \n"
    "        float cov = smoothstep(0.62 - cloudI*0.35, 0.86 - cloudI*0.25, n); \n"
    "        float horiz = smoothstep(0.02, 0.35, d.y); \n"
    "        col.rgb = mix(col.rgb, vec3(1.0), cov*cloudI*horiz*0.85); \n"
    "    }                                       \n"
    "    gl_FragColor = col;                     \n"
    "}                                           \n";
#else
static const char *tame_sky_vs =
    "#version 330                                \n"
    "in vec3 vertexPosition;                     \n"
    "uniform mat4 mvp;                           \n"
    "out vec3 vdir;                              \n"
    "void main() {                               \n"
    "    vdir = vertexPosition;                  \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                           \n";
static const char *tame_sky_fs =
    "#version 330                                \n"
    "in vec3 vdir;                               \n"
    "uniform vec4 skyTop;                        \n"
    "uniform vec4 skyBottom;                     \n"
    "uniform float starI;                        \n"
    "uniform float cloudI;                       \n"
    "uniform float cloudT;                       \n"
    "out vec4 fc;                                \n"
    // Prosedürel yıldızlar — gerekçe GLES varyantındaki yorumda.
    "float tmStarHash(vec3 p) {                  \n"
    "    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164)))*43758.5453); \n"
    "}                                           \n"
    // Bulutlar da PROSEDÜREL, yıldızlarla aynı gerekçeyle: sıfır çizim
    // çağrısı, sıfır asset ve örtüşme kendiliğinden doğru (kubbe en arkada,
    // dağlar bulutları örtüyor). 2B çizilseydi dağların ÖNÜNE düşerlerdi.
    //
    // Yön vektörü YATAY düzleme izdüşürülüyor (d.xz/d.y), yani bulutlar sabit
    // yükseklikte bir tabaka gibi görünüyor ve ufka doğru sıkışıyorlar —
    // düz d.xz kullanmak onları kubbeye yapıştırırdı.
    "float tmCloudHash(vec2 p) {                 \n"
    "    return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); \n"
    "}                                           \n"
    "float tmCloudNoise(vec2 p) {                \n"
    "    vec2 i = floor(p); vec2 f = fract(p);   \n"
    "    f = f*f*(3.0-2.0*f);                    \n"
    "    float a = tmCloudHash(i);               \n"
    "    float b = tmCloudHash(i+vec2(1.0,0.0)); \n"
    "    float c = tmCloudHash(i+vec2(0.0,1.0)); \n"
    "    float e = tmCloudHash(i+vec2(1.0,1.0)); \n"
    "    return mix(mix(a,b,f.x), mix(c,e,f.x), f.y); \n"
    "}                                           \n"
    "float tmCloudFbm(vec2 p) {                  \n"
    "    float v = 0.0; float amp = 0.5;         \n"
    "    for (int k = 0; k < 3; k++) {           \n"
    "        v += amp*tmCloudNoise(p);           \n"
    "        p *= 2.03; amp *= 0.5;              \n"
    "    }                                       \n"
    "    return v;                               \n"
    "}                                           \n"
    "void main() {                               \n"
    "    vec3 d = normalize(vdir);               \n"
    "    float t = clamp(d.y, 0.0, 1.0);         \n"
    "    vec4 col = mix(skyBottom, skyTop, pow(t, 0.55)); \n"
    "    if (starI > 0.001) {                    \n"
    "        float h = tmStarHash(floor(d*260.0)); \n"
    "        if (h > 0.9972) {                   \n"
    "            float b = 0.35 + 0.65*fract(h*137.0); \n"
    "            float horiz = smoothstep(0.02, 0.30, d.y); \n"
    "            col.rgb += vec3(b*starI*horiz);  \n"
    "        }                                   \n"
    "    }                                       \n"
    "    if (cloudI > 0.001 && d.y > 0.02) {     \n"
    "        vec2 pl = d.xz/max(d.y, 0.05);      \n"
    "        float n = tmCloudFbm(pl*0.9 + vec2(cloudT*0.02, cloudT*0.01)); \n"
    "        float cov = smoothstep(0.62 - cloudI*0.35, 0.86 - cloudI*0.25, n); \n"
    "        float horiz = smoothstep(0.02, 0.35, d.y); \n"
    "        col.rgb = mix(col.rgb, vec3(1.0), cov*cloudI*horiz*0.85); \n"
    "    }                                       \n"
    "    fc = col;                               \n"
    "}                                           \n";
#endif

static Shader tame_sky_shader = {0};
static int tame_sky_ready = 0;
static int tame_sky_on = 0;
static int tame_loc_skyTop = -1;
static int tame_loc_skyBottom = -1;
static int tame_loc_starI = -1;
static int tame_loc_cloudI = -1;
static int tame_loc_cloudT = -1;
static float tame_cloud_intensity = 0.0f;
static float tame_star_intensity = 0.0f;
static float tame_sky_top[4] = {0.29f, 0.51f, 0.82f, 1.0f};
static float tame_sky_bottom[4] = {0.75f, 0.84f, 0.93f, 1.0f};

// Geçerli doku-döşeme/materyal durumunu GPU'ya gönder. Çizim başına değil,
// DEĞİŞTİĞİNDE çağrılır (tm3_texture/tm3_material) + shader derlenince bir kez.
static void tame_material_upload(void) {
  if (!tame_light_ready) return;
  SetShaderValue(tame_light_shader, tame_loc_texTile, tame_cur_tile,
                 SHADER_UNIFORM_VEC2);
  SetShaderValue(tame_light_shader, tame_loc_matShine, &tame_cur_shine,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(tame_light_shader, tame_loc_matSpec, &tame_cur_spec,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(tame_light_shader, tame_loc_fogColor, tame_fog_color,
                 SHADER_UNIFORM_VEC4);
  SetShaderValue(tame_light_shader, tame_loc_fogDensity, &tame_fog_density,
                 SHADER_UNIFORM_FLOAT);
}

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
  tame_loc_texTile = GetShaderLocation(tame_light_shader, "texTile");
  tame_loc_matShine = GetShaderLocation(tame_light_shader, "matShine");
  tame_loc_matSpec = GetShaderLocation(tame_light_shader, "matSpec");
  tame_loc_fogColor = GetShaderLocation(tame_light_shader, "fogColor");
  tame_loc_fogDensity = GetShaderLocation(tame_light_shader, "fogDensity");
  tame_light_ready = 1;
  tame_material_upload();
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

// Doku kaydı (tame_textures) bu noktanın ALTINDA tanımlı — buradan yalnız
// handle→Texture2D çevirisi lazım, o yüzden tame_model_ptr ile aynı
// ileri-bildirim desenini kullanıyoruz. Geçersiz handle'da .id == 0 döner.
static Texture2D tame_texture_get(int h);

// Birim mesh'lerin materyalindeki VARSAYILAN (1×1 beyaz) doku. no_texture3d
// sonrası buraya dönülür; yoksa "dokuyu kapat" diye bir şey olmazdı.
static Texture2D tame_white_tex;
static int tame_white_tex_saved = 0;

static Model tame_unit[4];
static int tame_unit_ready = 0;

static void tame_unit_ensure(void) {
  if (tame_unit_ready || !tame_window_ready) return;
  tame_unit[0] = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
  tame_unit[1] = LoadModelFromMesh(GenMeshSphere(0.5f, 18, 18));
  tame_unit[2] = LoadModelFromMesh(GenMeshCylinder(0.5f, 1.0f, 24));
  tame_unit[3] = LoadModelFromMesh(GenMeshPlane(1.0f, 1.0f, 1, 1));
  // Varsayılan (1×1 beyaz) dokuyu sakla — tm3_no_texture buna geri döner.
  tame_white_tex = tame_unit[0].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture;
  tame_white_tex_saved = 1;
  tame_unit_ready = 1;
}

// Modelin materyallerine bir shader bağla (BeginShaderMode modellere işlemez —
// DrawMesh material.shader kullanır). Gölge geçişinde derinlik shader'ı, ana
// geçişte ışık shader'ı bağlanır.
static void tame_model_set_shader(Model *m, Shader s) {
  if (!m) return;
  for (int i = 0; i < m->materialCount; i++) m->materials[i].shader = s;
}

static void tame_model_apply_shader(Model *m) {
  tame_model_set_shader(m, tame_light_shader);
}

// Bir çizimin materyalini bağla: doku + döşeme + parlaklık. Modeller
// material.shader üzerinden çizildiği için doku materyale, döşeme/parlaklık
// ise uniform'a yazılır.
static void tame_bind_material(Model *m, int tex, const float tile[2],
                               float shine, float spec) {
  if (m && tame_white_tex_saved) {
    Texture2D t = tame_texture_get(tex);
    m->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        (t.id != 0) ? t : tame_white_tex;
  }
  if (!tame_light_ready) return;
  SetShaderValue(tame_light_shader, tame_loc_texTile, tile, SHADER_UNIFORM_VEC2);
  SetShaderValue(tame_light_shader, tame_loc_matShine, &shine,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(tame_light_shader, tame_loc_matSpec, &spec,
                 SHADER_UNIFORM_FLOAT);
}

// --- Faz 5: gökyüzü kubbesi -------------------------------------------------
static int tame_sky_ensure(void) {
  if (tame_sky_ready) return 1;
  if (!tame_window_ready) return 0;
  tame_sky_shader = LoadShaderFromMemory(tame_sky_vs, tame_sky_fs);
  if (tame_sky_shader.id == 0) {
    fprintf(stderr, "[tame] Gokyuzu shader'i derlenemedi. / Sky shader failed "
                    "to compile.\n");
    return 0;
  }
  tame_loc_skyTop = GetShaderLocation(tame_sky_shader, "skyTop");
  tame_loc_skyBottom = GetShaderLocation(tame_sky_shader, "skyBottom");
  tame_loc_starI = GetShaderLocation(tame_sky_shader, "starI");
  tame_loc_cloudI = GetShaderLocation(tame_sky_shader, "cloudI");
  tame_loc_cloudT = GetShaderLocation(tame_sky_shader, "cloudT");
  tame_sky_ready = 1;
  return 1;
}

// Kamerayı merkez alan büyük küreyi çiz. BeginMode3D İÇİNDEN, sahnenin geri
// kalanından ÖNCE çağrılır.
static void tame_sky_draw(void) {
  if (!tame_sky_on || !tame_sky_ready) return;
  tame_unit_ensure();
  if (!tame_unit_ready) return;

  SetShaderValue(tame_sky_shader, tame_loc_skyTop, tame_sky_top,
                 SHADER_UNIFORM_VEC4);
  SetShaderValue(tame_sky_shader, tame_loc_skyBottom, tame_sky_bottom,
                 SHADER_UNIFORM_VEC4);
  SetShaderValue(tame_sky_shader, tame_loc_starI, &tame_star_intensity,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(tame_sky_shader, tame_loc_cloudI, &tame_cloud_intensity,
                 SHADER_UNIFORM_FLOAT);
  // Zaman uniform'u: bulutlar sürükleniyor. `GetTime()` kullanılıyor çünkü
  // gökyüzünün kendi zamanı yok ve oyunun dt'sini buraya taşımak gökyüzü
  // çizimini oyun döngüsüne bağlardı.
  float tame_cloud_t = (float)GetTime();
  SetShaderValue(tame_sky_shader, tame_loc_cloudT, &tame_cloud_t,
                 SHADER_UNIFORM_FLOAT);
  // Küre birim mesh'i gökyüzü için ödünç alınıyor; sonraki normal çizimde
  // tame_model_apply_shader/set_shader zaten ışık shader'ına geri alıyor.
  tame_model_set_shader(&tame_unit[1], tame_sky_shader);
  // Kürenin İÇİNDEYİZ → ön yüzleri ayıkla. Derinliğe YAZMA: yarıçapın (50
  // birim) ötesindeki cisimler yoksa gökyüzü tarafından kırpılırdı.
  rlDisableDepthMask();
  rlSetCullFace(RL_CULL_FACE_FRONT);
  DrawModelEx(tame_unit[1], tame_cam3d.position, (Vector3){0.0f, 1.0f, 0.0f},
              0.0f, (Vector3){100.0f, 100.0f, 100.0f}, WHITE);
  rlDrawRenderBatchActive();
  rlSetCullFace(RL_CULL_FACE_BACK);
  rlEnableDepthMask();
}

// ---------------------------------------------------------------------------
// Faz 4b — gölgeler (yönlü ışık için shadow mapping).
//
// Gölge, sahneyi İKİ KEZ çizmeyi gerektirir: (1) ışığın gözünden derinlik
// haritası, (2) normal geçiş + o haritayla "bu piksel gölgede mi" testi. Ama
// tame'in API'sinde çizimler space_begin/space_end ARASINDA anında yapılıyor —
// ikinci kez çizecek bir şey kalmıyor.
//
// Çözüm: GÖLGE AÇIKKEN çizimler anında yapılmaz, bir listeye kaydedilir ve
// space_end iki geçiş halinde oynatır. GÖLGE KAPALIYKEN tek satırı bile
// değişmez — eski anında-çizim yolu aynen kalır (sıfır maliyet, sıfır risk).
//
// Yalnız YÖNLÜ ışık (güneş, slot 0) gölge üretir: nokta ışık gölgesi cube-map
// ister, mobilde maliyeti oyun başına anlamsız. Zaten "nesne yerde duruyor"
// hissini veren de güneş gölgesidir.
// ---------------------------------------------------------------------------

#define TAME_MAX_DRAWCMD 2048

typedef struct {
  int kind;      // 0-3 primitif (birim mesh), 4 model, 5 kutu-tel, 6 ızgara, 7 çizgi
  int handle;    // kind 4 için model handle
  float x, y, z;
  float sx, sy, sz;
  float yaw;
  int64_t color;
  // Faz 5: materyal DURUMU çizim anında dondurulur. Kayıt sırasında değişip
  // oynatma sırasında farklı olabileceği için komutla birlikte saklanır —
  // yoksa bir karedeki tüm cisimler son ayarlanan dokuyla çizilirdi.
  int tex;
  float tile[2];
  float shine, spec;
} TameDrawCmd;

static TameDrawCmd tame_dl[TAME_MAX_DRAWCMD];
static int tame_dl_n = 0;
static int tame_dl_recording = 0;

// Etkin render hedefi. Gölge geçişi kendi framebuffer'ına geçip işi bitince
// ESKİ hedefe dönmek zorunda; "ekrana dön" varsayımı editörde kırılıyordu
// (aşağıya bak).
static int tame_cur_rt = -1;
static void tame_restore_target(void);

static unsigned int tame_shadow_fbo = 0;
static unsigned int tame_shadow_tex = 0;    // derinlik renderbuffer'ı (z-testi; örneklenmez)
static unsigned int tame_shadow_color = 0;  // ASIL gölge haritası: derinlik RGBA8'e paketli
static int tame_shadow_res = 1024;
static int tame_shadows_on = 0;
static int tame_shadow_ready = 0;
static float tame_shadow_area = 22.0f;  // ışık ortografik yarı-genişliği
static Shader tame_depth_shader = {0};
static int tame_depth_ready = 0;
static Matrix tame_light_vp;
static int tame_loc_lightVP = -1, tame_loc_shadowMap = -1;
static int tame_loc_shadowOn = -1, tame_loc_shadowTexel = -1;

// Derinlik-only shader: sadece pozisyonu dönüştürür, renk yazmaz. GLES2 bir
// fragment shader zorunlu kıldığı için boş bir tane veriyoruz.
#if defined(GRAPHICS_API_OPENGL_ES2) || defined(PLATFORM_WEB) ||               \
    defined(PLATFORM_ANDROID) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
static const char *tame_depth_vs =
    "#version 100                                \n"
    "attribute vec3 vertexPosition;              \n"
    "uniform mat4 mvp;                           \n"
    "void main() { gl_Position = mvp*vec4(vertexPosition, 1.0); } \n";
static const char *tame_depth_fs =
    "#version 100                                \n"
    "precision highp float;                      \n"
    "void main() {                               \n"
    "    highp float d = gl_FragCoord.z;         \n"
    "    highp vec4 c = vec4(d, fract(d*255.0), fract(d*65025.0), fract(d*16581375.0)); \n"
    "    gl_FragColor = c - c.gbaa*vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0); \n"
    "}                                           \n";
#else
static const char *tame_depth_vs =
    "#version 330                                \n"
    "in vec3 vertexPosition;                     \n"
    "uniform mat4 mvp;                           \n"
    "void main() { gl_Position = mvp*vec4(vertexPosition, 1.0); } \n";
static const char *tame_depth_fs =
    "#version 330                                \n"
    "out vec4 fc;                                \n"
    "void main() {                               \n"
    "    float d = gl_FragCoord.z;               \n"
    "    vec4 c = vec4(d, fract(d*255.0), fract(d*65025.0), fract(d*16581375.0)); \n"
    "    fc = c - c.gbaa*vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0); \n"
    "}                                           \n";
#endif

// Shadow map FBO'sunu kur. rlLoadTextureDepth, derinlik DOKUSU desteklenmiyorsa
// sessizce renderbuffer'a düşer — o örneklenebilir değildir, yani gölge sessizce
// çalışmazdı. Bu yüzden framebuffer bütünlüğünü DOĞRULUYOR ve başarısızsa
// gölgeyi kapatıp açık bir mesaj basıyoruz (ışıklandırma çalışmaya devam eder).
static int tame_shadow_ensure(void) {
  if (tame_shadow_ready) return 1;
  if (!tame_window_ready) return 0;
  if (!tame_light_ensure()) return 0;

  if (!tame_depth_ready) {
    tame_depth_shader = LoadShaderFromMemory(tame_depth_vs, tame_depth_fs);
    if (tame_depth_shader.id == 0) {
      fprintf(stderr, "[tame] Golge derinlik shader'i derlenemedi. / Shadow "
                      "depth shader failed to compile.\n");
      return 0;
    }
    tame_depth_ready = 1;
  }

  tame_shadow_fbo = rlLoadFramebuffer();
  if (tame_shadow_fbo == 0) return 0;
  rlEnableFramebuffer(tame_shadow_fbo);
  // Gölge haritası bir RENK dokusudur, derinlik dokusu DEĞİL: derinliği
  // shader'da RGBA8'e paketliyoruz. Sebep — GLES2'de derinlik dokusu yolu
  // kırılgan: Android emülatörü OES_depth_texture'ı DESTEKLEDİĞİ HALDE
  // framebuffer "incomplete attachment" veriyordu (raylib boyutlu iç format
  // kullanıyor, katı GLES2'de internalformat == format olmalı). Renk-dokusu
  // yöntemi masaüstü/web/Android'de aynı şekilde çalışır ve "donanım
  // desteklemiyor olabilir" uyarısına gerek bırakmaz.
  tame_shadow_color = rlLoadTexture(NULL, tame_shadow_res, tame_shadow_res,
                                    RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
  if (tame_shadow_color == 0) { rlUnloadFramebuffer(tame_shadow_fbo); tame_shadow_fbo = 0; return 0; }
  // NEAREST ŞART: paketlenmiş derinlik interpolasyona gelmez — bilinear
  // örnekleme iki komşu paketin ortalamasını alıp anlamsız bir derinlik üretir
  // (gölge tamamen kaybolur). CLAMP da şart, kenarda tekrarlamasın.
  rlTextureParameters(tame_shadow_color, RL_TEXTURE_MIN_FILTER,
                      RL_TEXTURE_FILTER_NEAREST);
  rlTextureParameters(tame_shadow_color, RL_TEXTURE_MAG_FILTER,
                      RL_TEXTURE_FILTER_NEAREST);
  rlTextureParameters(tame_shadow_color, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
  rlTextureParameters(tame_shadow_color, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
  rlFramebufferAttach(tame_shadow_fbo, tame_shadow_color,
                      RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  // Z-testi için derinlik RENDERBUFFER'ı (örneklenmeyecek, sadece derinlik
  // sıralaması yapacak) — her GLES2 uygulamasında güvenli.
  tame_shadow_tex = rlLoadTextureDepth(tame_shadow_res, tame_shadow_res, true);
  rlFramebufferAttach(tame_shadow_fbo, tame_shadow_tex, RL_ATTACHMENT_DEPTH,
                      RL_ATTACHMENT_RENDERBUFFER, 0);
  int ok = rlFramebufferComplete(tame_shadow_fbo);
  tame_restore_target();
  if (!ok) {
    fprintf(stderr,
            "[tame] Golge haritasi framebuffer'i olusturulamadi; golgeler "
            "kapatildi, isik calisiyor. / Shadow map framebuffer incomplete; "
            "shadows disabled, lighting still on.\n");
    rlUnloadFramebuffer(tame_shadow_fbo);
    tame_shadow_fbo = 0; tame_shadow_tex = 0; tame_shadow_color = 0;
    return 0;
  }
  tame_loc_lightVP = GetShaderLocation(tame_light_shader, "lightVP");
  tame_loc_shadowMap = GetShaderLocation(tame_light_shader, "shadowMap");
  tame_loc_shadowOn = GetShaderLocation(tame_light_shader, "shadowOn");
  tame_loc_shadowTexel = GetShaderLocation(tame_light_shader, "shadowTexel");
  tame_shadow_ready = 1;
  return 1;
}

int tame_impl_shadows(int enable) {
  if (!enable) {
    tame_shadows_on = 0;
    if (tame_light_ready) {
      int off = 0;
      SetShaderValue(tame_light_shader, tame_loc_shadowOn, &off,
                     SHADER_UNIFORM_INT);
    }
    return 1;
  }
  if (!tame_shadow_ensure()) return 0;
  tame_shadows_on = 1;
  return 1;
}

// Gölge GERÇEKTEN aktif mi? (istendi + FBO kurulabildi). Oyun kodu buna
// bakarak kullanıcıya dürüst bilgi gösterebilir — tm3_shadows(1) çağırmış
// olmak gölgenin çalıştığı anlamına gelmez.
int tame_impl_shadows_active(void) {
  return tame_shadows_on && tame_shadow_ready;
}

// Gölgenin kapsadığı alanın yarı-genişliği (dünya birimi). Küçük = keskin ama
// dar; büyük = geniş ama kaba. Kamera hedefinin çevresini kapsar.
void tame_impl_shadow_area(double area) {
  if (area > 0.5) tame_shadow_area = (float)area;
}

static void tame_dl_push(int kind, int handle, double x, double y, double z,
                         double sx, double sy, double sz, double yaw,
                         int64_t color) {
  if (tame_dl_n >= TAME_MAX_DRAWCMD) return;   // taşarsa sessizce kırp
  TameDrawCmd *c = &tame_dl[tame_dl_n++];
  c->kind = kind;
  c->handle = handle;
  c->x = (float)x; c->y = (float)y; c->z = (float)z;
  c->sx = (float)sx; c->sy = (float)sy; c->sz = (float)sz;
  c->yaw = (float)yaw;
  c->color = color;
  c->tex = tame_cur_tex;
  c->tile[0] = tame_cur_tile[0];
  c->tile[1] = tame_cur_tile[1];
  c->shine = tame_cur_shine;
  c->spec = tame_cur_spec;
}

// Kaydedilmiş listeyi çiz. `depth_pass` ise yalnız gölge DÜŞÜREN cisimler
// (primitifler + modeller) çizilir; ızgara/çizgi/tel-çerçeve atlanır.
static void tame_dl_replay(int depth_pass) {
  Shader use = depth_pass ? tame_depth_shader : tame_light_shader;
  for (int i = 0; i < tame_dl_n; i++) {
    TameDrawCmd *c = &tame_dl[i];
    if (c->kind >= 0 && c->kind <= 3) {
      tame_unit_ensure();
      if (!tame_unit_ready) continue;
      tame_model_set_shader(&tame_unit[c->kind], use);
      // Derinlik geçişinde materyalin bir anlamı yok (renk yazılmıyor).
      if (!depth_pass)
        tame_bind_material(&tame_unit[c->kind], c->tex, c->tile, c->shine,
                           c->spec);
      DrawModelEx(tame_unit[c->kind], (Vector3){c->x, c->y, c->z},
                  (Vector3){0.0f, 1.0f, 0.0f}, c->yaw,
                  (Vector3){c->sx, c->sy, c->sz}, tame_color(c->color));
    } else if (c->kind == 4) {
      Model *m = tame_model_ptr(c->handle);
      if (!m) continue;
      tame_model_set_shader(m, use);
      // Modeller KENDİ dokularını korur (GLB materyalini ezmek yıkıcı olurdu;
      // model dokusu tm3_model_texture ile ayarlanır) — yalnız döşeme/parlaklık
      // uygulanır, o yüzden model NULL geçiliyor.
      if (!depth_pass)
        tame_bind_material(NULL, -1, c->tile, c->shine, c->spec);
      DrawModelEx(*m, (Vector3){c->x, c->y, c->z},
                  (Vector3){0.0f, 1.0f, 0.0f}, c->yaw,
                  (Vector3){c->sx, c->sy, c->sz}, tame_color(c->color));
    } else if (!depth_pass) {
      if (c->kind == 5) {
        DrawCubeWires((Vector3){c->x, c->y, c->z}, c->sx, c->sy, c->sz,
                      tame_color(c->color));
      } else if (c->kind == 6) {
        DrawGrid((int)c->sx, c->sy);
      } else if (c->kind == 7) {
        DrawLine3D((Vector3){c->x, c->y, c->z}, (Vector3){c->sx, c->sy, c->sz},
                   tame_color(c->color));
      }
    }
  }
}

// Güneşin (slot 0, yönlü) gözünden ortografik view-projection matrisi. Kamera
// hedefinin çevresini kapsar → gölge her zaman oyuncunun olduğu yerde nettir.
static Matrix tame_light_matrix(void) {
  Vector3 dir = {-0.6f, -1.0f, -0.4f};
  for (int i = 0; i < TAME_MAX_LIGHTS; i++) {
    if (tame_lights[i].enabled && tame_lights[i].type == 0) {
      dir = tame_lights[i].position;
      break;
    }
  }
  float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (len < 0.0001f) { dir = (Vector3){-0.6f, -1.0f, -0.4f}; len = 1.0f; }
  dir.x /= len; dir.y /= len; dir.z /= len;

  Vector3 center = tame_cam3d.target;
  float back = tame_shadow_area * 2.0f;
  Vector3 eye = {center.x - dir.x * back, center.y - dir.y * back,
                 center.z - dir.z * back};
  Matrix view = MatrixLookAt(eye, center, (Vector3){0.0f, 1.0f, 0.0f});
  float a = tame_shadow_area;
  Matrix proj = MatrixOrtho(-a, a, -a, a, 0.05, back * 2.5f);
  return MatrixMultiply(view, proj);
}

static int tame_recording(void) { return tame_dl_recording; }

// Gölge açık ve hazırsa kayıt modunu başlat (1 döner). Aksi halde 0 → çağıran
// eski anında-çizim yolunu kullanır.
static int tame_scene_begin(void) {
  if (!tame_shadows_on || !tame_shadow_ready || !tame_lights_active()) return 0;
  tame_dl_n = 0;
  tame_dl_recording = 1;
  return 1;
}

// İki geçiş: (1) ışığın gözünden derinlik haritası, (2) kameradan normal
// geçiş + gölge testi.
static void tame_scene_end(void) {
  tame_dl_recording = 0;

  // --- 1. geçiş: shadow map -------------------------------------------------
  // Derinlik geçişi rlgl'in projeksiyon/modelview matrislerini ezer. BeginMode3D
  // projeksiyonu push'layıp EndMode3D pop'ladığı için, geri konmazsa pop bizim
  // ezdiğimiz matrisi geri yükler ve space_end'DEN SONRA çizilen 2D HUD yanlış
  // projeksiyonla (ekran dışına) çizilir — ilk denemede yazılar kaybolmuştu.
  Matrix saved_proj = rlGetMatrixProjection();
  Matrix saved_mv = rlGetMatrixModelview();

  tame_light_vp = tame_light_matrix();
  rlEnableFramebuffer(tame_shadow_fbo);
  rlViewport(0, 0, tame_shadow_res, tame_shadow_res);
  // Casteri olmayan bölgeler "en uzak" (derinlik 1.0 = beyaz paket) kalmalı,
  // yoksa boş alanlar gölgeli sayılır.
  rlClearColor(255, 255, 255, 255);
  rlClearScreenBuffers();
  rlEnableDepthTest();
  // HARMANLAMA KAPALI OLMALI: raylib alpha blending'i varsayılan açık tutar,
  // ama biz derinliği RGBA'ya PAKETLİYORUZ — alfa kanalı da veri taşıyor ve
  // rastgele bir opaklık gibi yorumlanıyor. Açık bırakınca her fragment beyaz
  // zeminle harmanlanıp shadow map benekli çıkıyor (gölge de noktalı/kayıp).
  rlDisableColorBlend();
  rlSetMatrixProjection(MatrixIdentity());
  rlSetMatrixModelview(tame_light_vp);
  // Ön yüzleri ayıklamak "shadow acne"yi (yüzeyin kendini gölgelemesi)
  // belirgin şekilde azaltır; bias ile birlikte kullanılıyor.
  rlSetCullFace(RL_CULL_FACE_FRONT);
  tame_dl_replay(1);
  rlSetCullFace(RL_CULL_FACE_BACK);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();

  // ÖNCEKİ hedefe dön — "ekrana dön" DEĞİL. Gölge geçişi bir render texture'ın
  // içinden çağrılabiliyor (editörün sahne görünümü paneli böyle çiziliyor) ve
  // rlDisableFramebuffer() orada varsayılan framebuffer'a, yani EKRANA
  // dönüyordu. Sonuç sinsiydi: gölge geçişinden SONRAKİ her şey ekrana
  // çiziliyor, editörün dokusunda yalnız temizleme rengi kalıyordu — panelde
  // koyu mavi bir dikdörtgen, sahne ise panellerin arkasında.
  tame_restore_target();
  rlSetMatrixProjection(saved_proj);
  rlSetMatrixModelview(saved_mv);

  // --- 2. geçiş: kamera + gölgeli aydınlatma --------------------------------
  int on = 1;
  float texel = 1.0f / (float)tame_shadow_res;
  SetShaderValue(tame_light_shader, tame_loc_shadowOn, &on, SHADER_UNIFORM_INT);
  SetShaderValue(tame_light_shader, tame_loc_shadowTexel, &texel,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValueMatrix(tame_light_shader, tame_loc_lightVP, tame_light_vp);
  // Gölge haritasını serbest bir doku birimine bağla (0/1 raylib'in kendi
  // texture0/specular'ı için ayrılmış).
  rlEnableShader(tame_light_shader.id);
  rlActiveTextureSlot(10);
  rlEnableTexture(tame_shadow_color);
  int slot = 10;
  rlSetUniform(tame_loc_shadowMap, &slot, SHADER_UNIFORM_INT, 1);

  BeginMode3D(tame_cam3d);
  tame_sky_draw();   // gölgeli yolda da sahneden ÖNCE (bkz. begin3)
  float view[3] = {tame_cam3d.position.x, tame_cam3d.position.y,
                   tame_cam3d.position.z};
  SetShaderValue(tame_light_shader,
                 tame_light_shader.locs[SHADER_LOC_VECTOR_VIEW], view,
                 SHADER_UNIFORM_VEC3);
  BeginShaderMode(tame_light_shader);
  tame_dl_replay(0);
  EndShaderMode();
  EndMode3D();

// Teşhis: -DTAME_SHADOW_DEBUG ile derlenirse gölge haritasını ekranın sol
// üstünde gösterir. Gölge yanlış göründüğünde "hangi aşama bozuk?" sorusunu
// tahminle değil bakarak çözer — harita benekliyse derinlik geçişi, düzgün
// ama gölge yoksa örnekleme/projeksiyon hatalıdır. (Alfa harmanlama hatası
// tam olarak böyle bulundu.)
#ifdef TAME_SHADOW_DEBUG
  {
    Texture2D dbg = {0};
    dbg.id = tame_shadow_color;
    dbg.width = tame_shadow_res;
    dbg.height = tame_shadow_res;
    dbg.mipmaps = 1;
    dbg.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    Rectangle src = {0, 0, (float)tame_shadow_res, (float)tame_shadow_res};
    Rectangle dst = {8, 76, 200, 200};
    DrawTexturePro(dbg, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
    DrawRectangleLines(8, 76, 200, 200, RED);
  }
#endif
}

static int tame_draw_lit(int shape, double x, double y, double z, double sx,
                         double sy, double sz, double yaw, int64_t color) {
  // Gölge kaydı önceliklidir: liste modundaysak hiçbir şey çizmeyip kaydet.
  if (tame_dl_recording) {
    tame_dl_push(shape, -1, x, y, z, sx, sy, sz, yaw, color);
    return 1;
  }
  if (!tame_lights_active()) return 0;
  tame_unit_ensure();
  if (!tame_unit_ready || shape < 0 || shape > 3) return 0;
  tame_model_apply_shader(&tame_unit[shape]);
  tame_bind_material(&tame_unit[shape], tame_cur_tex, tame_cur_tile,
                     tame_cur_shine, tame_cur_spec);
  DrawModelEx(tame_unit[shape], (Vector3){(float)x, (float)y, (float)z},
              (Vector3){0.0f, 1.0f, 0.0f}, (float)yaw,
              (Vector3){(float)sx, (float)sy, (float)sz}, tame_color(color));
  return 1;
}

// --- Faz 1 primitifleri -----------------------------------------------------

void tame_impl_sphere(double x, double y, double z, double r, int64_t color) {
  // Birim küre r=0.5 → ölçek = çap.
  if (tame_draw_lit(1, x, y, z, r * 2.0, r * 2.0, r * 2.0, 0.0, color)) return;
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
  if (tame_draw_lit(2, x, y, z, r * 2.0, h, r * 2.0, 0.0, color)) return;
  DrawCylinder((Vector3){(float)x, (float)y, (float)z}, (float)r, (float)r,
               (float)h, 20, tame_color(color));
}

void tame_impl_plane(double x, double y, double z, double sx, double sz,
                     int64_t color) {
  // Birim düzlem 1×1 (XZ) → ölçek doğrudan boyut; y ölçeği anlamsız (1).
  if (tame_draw_lit(3, x, y, z, sx, 1.0, sz, 0.0, color)) return;
  DrawPlane((Vector3){(float)x, (float)y, (float)z},
            (Vector2){(float)sx, (float)sz}, tame_color(color));
}

void tame_impl_line3(double x1, double y1, double z1, double x2, double y2,
                     double z2, int64_t color) {
  if (tame_recording()) { tame_dl_push(7, -1, x1, y1, z1, x2, y2, z2, 0.0, color); return; }
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

// ---- Render texture (sahne görünümü paneli) --------------------------------
// Editörün 3B görünümü TAM EKRAN değil, panellerin arasındaki dikdörtgen —
// Unity/Godot düzeni bu. Sahneyi doğrudan çizip kırpmak (scissor) yetmez:
// kamera izdüşümü hâlâ pencerenin en-boy oranını kullanır, yani görüntü ezik
// ve merkezi kaymış çıkar. Render texture'a çizmek en-boy oranını da
// düzeltiyor, çünkü raylib izdüşümü etkin hedefin boyutundan türetiyor.
#define TAME_MAX_RT 4
static RenderTexture2D tame_rts[TAME_MAX_RT];
static int tame_rt_used[TAME_MAX_RT];

// ---- Kırpma (scissor) ------------------------------------------------------
// Bir panelin İÇİNE kaydırılabilir içerik çizmek için: dikdörtgenin dışına
// düşen pikseller yazılmıyor. Alternatif her widget çağrısından önce elle sınır
// denetimi yapmaktı — her yeni widget o borcu büyütürdü ve yarı görünür
// satırlar yine kırpılamazdı.
void tame_impl_scissor(int x, int y, int w, int h) {
  if (w < 0) w = 0;
  if (h < 0) h = 0;
  BeginScissorMode(x, y, w, h);
}
void tame_impl_scissor_end(void) { EndScissorMode(); }

int tame_impl_rt_new(int w, int h) {
  if (!tame_window_ready) return -1;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  for (int i = 0; i < TAME_MAX_RT; i++) {
    if (!tame_rt_used[i]) {
      tame_rts[i] = LoadRenderTexture(w, h);
      if (tame_rts[i].texture.id == 0) return -1;
      SetTextureFilter(tame_rts[i].texture, TEXTURE_FILTER_POINT);
      tame_rt_used[i] = 1;
      return i;
    }
  }
  return -1;
}

void tame_impl_rt_free(int h) {
  if (h < 0 || h >= TAME_MAX_RT || !tame_rt_used[h]) return;
  UnloadRenderTexture(tame_rts[h]);
  tame_rt_used[h] = 0;
}

int tame_impl_rt_w(int h) {
  if (h < 0 || h >= TAME_MAX_RT || !tame_rt_used[h]) return 0;
  return tame_rts[h].texture.width;
}
int tame_impl_rt_h(int h) {
  if (h < 0 || h >= TAME_MAX_RT || !tame_rt_used[h]) return 0;
  return tame_rts[h].texture.height;
}

void tame_impl_rt_begin(int h) {
  if (h < 0 || h >= TAME_MAX_RT || !tame_rt_used[h]) return;
  BeginTextureMode(tame_rts[h]);
  tame_cur_rt = h;
}
void tame_impl_rt_end(void) {
  EndTextureMode();
  tame_cur_rt = -1;
}

static int tame_target_w(void) {
  if (tame_cur_rt >= 0 && tame_cur_rt < TAME_MAX_RT && tame_rt_used[tame_cur_rt]) {
    return tame_rts[tame_cur_rt].texture.width;
  }
  return GetScreenWidth();
}
static int tame_target_h(void) {
  if (tame_cur_rt >= 0 && tame_cur_rt < TAME_MAX_RT && tame_rt_used[tame_cur_rt]) {
    return tame_rts[tame_cur_rt].texture.height;
  }
  return GetScreenHeight();
}

// Etkin hedefi geri bağla. Gölge geçişi gibi kendi framebuffer'ına geçen
// kodlar bunu çağırmalı; "ekrana dön" varsayımı render texture içindeyken
// yanlış.
static void tame_restore_target(void) {
  if (tame_cur_rt >= 0 && tame_cur_rt < TAME_MAX_RT && tame_rt_used[tame_cur_rt]) {
    rlEnableFramebuffer(tame_rts[tame_cur_rt].id);
    rlViewport(0, 0, tame_rts[tame_cur_rt].texture.width,
               tame_rts[tame_cur_rt].texture.height);
    return;
  }
  rlDisableFramebuffer();
  rlViewport(0, 0, GetScreenWidth(), GetScreenHeight());
}

// Render texture'lar OpenGL'de baş aşağı; kaynak dikdörtgenin yüksekliği
// negatif verilerek çevriliyor (raylib'in standart yolu).
void tame_impl_rt_draw(int h, int x, int y) {
  if (h < 0 || h >= TAME_MAX_RT || !tame_rt_used[h]) return;
  Texture2D t = tame_rts[h].texture;
  Rectangle src = {0.0f, 0.0f, (float)t.width, -(float)t.height};
  Vector2 pos = {(float)x, (float)y};
  DrawTextureRec(t, src, pos, WHITE);
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

// Prosedürel damalı doku — dosyasız. Motorun geri kalanı (gömülü shader'lar,
// GenMesh primitifleri) gibi "asset gerektirmeden çalışsın" çizgisinde:
// doku desteği varken kimse basit bir zemin karosu için PNG taşımak zorunda
// kalmasın. Normal doku handle'ı döner, 2B'de de kullanılabilir.
int tame_impl_checker(int w, int h, int cells, int64_t c1, int64_t c2) {
  if (!tame_window_ready) {
    fprintf(stderr, "[tame] checker window()'dan once cagrilamaz. / "
                    "checker requires window() first.\n");
    return -1;
  }
  if (w < 2) w = 2;
  if (h < 2) h = 2;
  if (cells < 1) cells = 1;
  Image img = GenImageChecked(w, h, w / cells, h / cells, tame_color(c1),
                              tame_color(c2));
  Texture2D t = LoadTextureFromImage(img);
  UnloadImage(img);
  if (t.id == 0) return -1;
  // Döşerken karo sınırlarında dikiş olmasın diye tekrarlı sarma + yumuşatma.
  SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
  SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
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

// 3B materyal yolunun ileri-bildirdiği erişimci (bkz. tame_bind_material).
static Texture2D tame_texture_get(int h) {
  if (tame_texture_ok(h)) return tame_textures[h];
  Texture2D none = {0};
  return none;
}

// --- Faz 5 API'si: doku / materyal / gökyüzü --------------------------------

// Sonraki 3B primitiflerin dokusunu ayarla. tex < 0 → dokusuz (düz renk).
// tile, dokunun yüzey boyunca kaç kez tekrarlanacağı — zeminde tek bir 64×64
// karo dokusunu 20×20 döşemek için tile_u=tile_v=20 ver.
void tame_impl_texture3(int tex, double tile_u, double tile_v) {
  tame_cur_tex = tame_texture_ok(tex) ? tex : -1;
  tame_cur_tile[0] = (tile_u > 0.0) ? (float)tile_u : 1.0f;
  tame_cur_tile[1] = (tile_v > 0.0) ? (float)tile_v : 1.0f;
  // Anında-mod çizimleri (ızgara/tel/çizgi) uniform'ları doğrudan okur.
  tame_material_upload();
}

// Yüzey parlaklığı. shine = specular üssü (büyük = küçük ve keskin parlama,
// "cilalı"; küçük = geniş ve yayvan, "mat"). spec = parlamanın gücü, 0 =
// tamamen mat. Varsayılan 16 / 1.0 — Faz 5 öncesi davranışın aynısı.
void tame_impl_material3(double shine, double spec) {
  tame_cur_shine = (shine > 0.05) ? (float)shine : 0.05f;
  tame_cur_spec = (spec >= 0.0) ? (float)spec : 0.0f;
  tame_material_upload();
}

// Gradyan gökyüzünü aç. top = zenit (tepe), bottom = ufuk rengi.
int tame_impl_sky(int64_t top, int64_t bottom) {
  if (!tame_sky_ensure()) return 0;
  Color t = tame_color(top), b = tame_color(bottom);
  tame_sky_top[0] = (float)t.r / 255.0f;
  tame_sky_top[1] = (float)t.g / 255.0f;
  tame_sky_top[2] = (float)t.b / 255.0f;
  tame_sky_top[3] = 1.0f;
  tame_sky_bottom[0] = (float)b.r / 255.0f;
  tame_sky_bottom[1] = (float)b.g / 255.0f;
  tame_sky_bottom[2] = (float)b.b / 255.0f;
  tame_sky_bottom[3] = 1.0f;
  tame_sky_on = 1;
  return 1;
}

void tame_impl_sky_off(void) { tame_sky_on = 0; }

// Yıldız yoğunluğu: 0 kapalı, 1 tam. Gökyüzü kubbesi çizilirken uniform olarak
// gidiyor — yıldızlar gökyüzünün bir parçası, ayrı bir cisim değil.
// Bulut kapsaması. 0 = açık gökyüzü, 1 = tamamen kapalı. Yıldızlarla aynı
// yol: tek bir float, gökyüzü kubbesinin shader'ına gidiyor — bulutlar
// gökyüzünün parçası, ayrı bir cisim değil.
void tame_impl_sky_clouds(double coverage) {
  float v = (float)coverage;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  tame_cloud_intensity = v;
}

void tame_impl_sky_stars(double intensity) {
  float v = (float)intensity;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  tame_star_intensity = v;
}

// Mesafe sisi. density 0 = kapalı. color < 0 → gökyüzünün UFUK rengi kullanılır
// (doğru olan bu: sis, uzaktaki cismi arkasındaki gökyüzüne karıştırmalı).
//
// Sis ışık shader'ında hesaplanıyor, yani ışık kapalıyken (lights_off) sis de
// çizilmez — gölgeyle aynı bağımlılık.
void tame_impl_fog(int64_t color, double density) {
  if (color < 0) {
    for (int i = 0; i < 4; i++) tame_fog_color[i] = tame_sky_bottom[i];
  } else {
    Color c = tame_color(color);
    tame_fog_color[0] = (float)c.r / 255.0f;
    tame_fog_color[1] = (float)c.g / 255.0f;
    tame_fog_color[2] = (float)c.b / 255.0f;
    tame_fog_color[3] = 1.0f;
  }
  tame_fog_density = (density > 0.0) ? (float)density : 0.0f;
  tame_material_upload();
}

// --- Faz 8: billboard + dünya→ekran izdüşümü --------------------------------
//
// Billboard, HER ZAMAN kameraya dönük duran bir dörtgendir. Parçacık, kıvılcım,
// duman, 3B etiket gibi şeylerin tek çizim yolu budur: normal bir kutu/küre
// kameradan yana bakınca incelir, parçacık ise her açıdan aynı görünmelidir.
//
// Dokusuz (düz renkli) billboard da istiyoruz — parçacık için tipik durum bu ve
// kullanıcıyı "önce bir doku yükle" adımına zorlamak anlamsız. raylib
// DrawBillboard bir Texture2D şart koştuğu için içeride 1×1 beyaz bir doku
// tutuyoruz; tint ile boyanınca düz renkli kare oluyor.
static Texture2D tame_white_tex = {0};
static int tame_white_tex_ready = 0;

static Texture2D tame_white_texture(void) {
  if (!tame_white_tex_ready) {
    Image img = GenImageColor(1, 1, WHITE);
    tame_white_tex = LoadTextureFromImage(img);
    UnloadImage(img);
    tame_white_tex_ready = 1;
  }
  return tame_white_tex;
}

// Kameraya dönük dörtgen. tex < 0 (ya da geçersiz) → düz renk.
// Işıklandırma UYGULANMAZ: parçacıklar ışık kaynağıdır, gölgelenmeleri yanlış
// olurdu — bu yüzden tame_draw_lit yolundan bilerek geçmiyor.
void tame_impl_billboard(int tex, double x, double y, double z, double size,
                         int64_t color) {
  Texture2D t = tame_texture_ok(tex) ? tame_textures[tex] : tame_white_texture();
  DrawBillboard(tame_cam3d, t, (Vector3){(float)x, (float)y, (float)z},
                (float)size, tame_color(color));
}

// Dünya noktasının EKRAN koordinatı. 3B can barı, isim etiketi ve hasar sayısı
// gibi şeyler aslında 2B çizimdir — yalnız konumları 3B'de bir cisme bağlıdır.
// Billboard bunları veremez (yazı tipi/metin 3B dörtgene sığmaz), izdüşüm verir:
// entity'nin tepesini ekrana çevir, oraya normal text()/rect() çiz.
// X ve Y ayrı builtin: VMValue ABI'sinden iki değer birden dönmek zahmetli.
double tame_impl_screen_x(double x, double y, double z) {
  Vector2 p = GetWorldToScreen((Vector3){(float)x, (float)y, (float)z},
                               tame_cam3d);
  return (double)p.x;
}

double tame_impl_screen_y(double x, double y, double z) {
  Vector2 p = GetWorldToScreen((Vector3){(float)x, (float)y, (float)z},
                               tame_cam3d);
  return (double)p.y;
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

// Gölge geçişinin kullandığı erişimci (bkz. yukarıdaki ileri bildirim):
// geçersiz handle'da NULL.
static Model *tame_model_ptr(int h) {
  return tame_model_ok(h) ? &tame_models[h].model : NULL;
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

// --- Faz 10: gerçek arazi (heightmap) ---------------------------------------
//
// Rampa (Faz 9) dünyayı düz düzlemden kurtardı ama sınırlıydı: kama biçimli
// entity'ler, kademeli çizim. Arazi gerçek çözüm — bir yükseklik haritasından
// üretilmiş TEK mesh, her (x,z) için sürekli yükseklik.
//
// Mesh, model kayıt defterine NORMAL bir model olarak giriyor. Bunun sebebi
// önemli: çizim/gölge/ışık/kayıt (gölge geçişi için display list) yollarının
// hepsi model handle'ı üzerinden çalışıyor, dolayısıyla arazi bedavaya gölge
// alıyor ve ışıklanıyor. Ayrı bir Model tutsaydık üçünü de elden bağlamak
// gerekirdi.
//
// Yükseklik örneklemesi GenMeshHeightmap'in ÜÇGENLEMESİNİ birebir taklit
// ediyor (düz bilineer DEĞİL): mesh her hücreyi köşegenden iki üçgene bölüyor
// ve düz bilineer o köşegende mesh'ten sapar — oyuncu görünürde zeminin biraz
// altına gömülür ya da üstünde yüzer. Fizik ile görselin uyuşması buna bağlı.
static float *tame_terr_h = NULL;   // gri değerler (0..255), satır-major
static int    tame_terr_mx = 0;     // yükseklik haritası çözünürlüğü
static int    tame_terr_mz = 0;
static float  tame_terr_sx = 0.0f;  // dünya boyutları
static float  tame_terr_sy = 0.0f;
static float  tame_terr_sz = 0.0f;
static float  tame_terr_base = 0.0f; // taban Y (dünya)
static int    tame_terr_ready = 0;

void tame_impl_terrain_off(void) {
  if (tame_terr_h) { free(tame_terr_h); tame_terr_h = NULL; }
  tame_terr_ready = 0;
}

// Görüntüden gri değerleri sakla + mesh üret + model kaydına koy.
//
// Yükseklik verisi ile MESH kasten ayrı: gri değerleri çıkarmak saf CPU işi,
// mesh üretmek ise GPU'ya yükleme yapıyor (GenMeshHeightmap sonunda
// UploadMesh çağırıyor). Pencere yoksa yükseklik verisini yine de saklıyoruz
// ve -1 dönüyoruz — böylece arazi FİZİĞİ pencere açmadan (headless testte)
// sürülebiliyor, yalnız çizim devre dışı kalıyor.
// Katman boyaması aşağıda tanımlı (yükseklik sorgusuna dayanıyor, o da bu
// fonksiyonun kurduğu duruma) — ileri bildirim.
static void tame_terrain_paint_mesh(Mesh *mesh);

static int tame_terrain_build(Image img, double sx, double sy, double sz,
                              double base) {
  Color *px = LoadImageColors(img);
  if (!px) { UnloadImage(img); return -1; }
  int mx = img.width, mz = img.height;
  float *hs = (float *)malloc((size_t)mx * (size_t)mz * sizeof(float));
  if (!hs) { UnloadImageColors(px); UnloadImage(img); return -1; }
  for (int i = 0; i < mx * mz; i++) {
    // GenMeshHeightmap'in GRAY_VALUE'su ile AYNI: (r+g+b)/3
    hs[i] = ((float)px[i].r + (float)px[i].g + (float)px[i].b) / 3.0f;
  }
  UnloadImageColors(px);

  // Yükseklik verisi her durumda saklanır (fizik penceresiz de çalışır).
  tame_impl_terrain_off();          // önceki araziyi bırak
  tame_terr_h = hs;
  tame_terr_mx = mx; tame_terr_mz = mz;
  tame_terr_sx = (float)sx; tame_terr_sy = (float)sy; tame_terr_sz = (float)sz;
  tame_terr_base = (float)base;
  tame_terr_ready = 1;

  // Mesh yalnız pencere varken — GPU'ya yükleme gerekiyor.
  if (!tame_window_ready) { UnloadImage(img); return -1; }
  int slot = tame_model_slot();
  if (slot < 0) { UnloadImage(img); return -1; }
  Mesh mesh = GenMeshHeightmap(img, (Vector3){(float)sx, (float)sy, (float)sz});
  UnloadImage(img);
  // Katman boyaması LoadModelFromMesh'ten ÖNCE: Mesh bir DEĞER yapısı, model
  // onun kopyasını saklıyor — sonra boyasak yerel kopyayı boyamış olurduk.
  tame_terrain_paint_mesh(&mesh);
  Model m = LoadModelFromMesh(mesh);
  tame_models[slot].model = m;
  tame_models[slot].anims = NULL;
  tame_models[slot].anim_count = 0;
  tame_models[slot].used = 1;
  return slot;
}

// Perlin gürültüsünden prosedürel arazi — asset dosyası gerekmez.
int tame_impl_terrain_gen(int res, double sx, double sy, double sz, double base,
                          double scale, int seed) {
  if (res < 2) res = 2;
  if (res > 512) res = 512;         // 512² = 261k tepe noktası; üstü anlamsız
  if (scale <= 0.0) scale = 4.0;
  Image img = GenImagePerlinNoise(res, res, seed, seed, (float)scale);
  return tame_terrain_build(img, sx, sy, sz, base);
}

// Dosyadan yükseklik haritası (gri tonlamalı görüntü).
int tame_impl_terrain_load(const char *path, double sx, double sy, double sz,
                           double base) {
  if (!path || !*path) return -1;
  Image img = LoadImage(path);
  if (img.data == NULL) return -1;
  return tame_terrain_build(img, sx, sy, sz, base);
}

// (x,z) dünya noktasında arazi yüzeyinin Y'si. Arazi yoksa ya da nokta ayak
// izinin dışındaysa taban Y döner (yani düz zemin gibi davranır).
double tame_impl_terrain_height(double wx, double wz) {
  if (!tame_terr_ready || tame_terr_mx < 2 || tame_terr_mz < 2)
    return tame_terr_base;
  // Arazi dünyada ORTALI: yerel = dünya + yarı-boy.
  float lx = (float)wx + tame_terr_sx * 0.5f;
  float lz = (float)wz + tame_terr_sz * 0.5f;
  if (lx < 0.0f || lz < 0.0f || lx > tame_terr_sx || lz > tame_terr_sz)
    return tame_terr_base;
  // Hücre koordinatlarına çevir (mesh: x*sx/(mx-1), z*sz/(mz-1)).
  float cx = lx * (float)(tame_terr_mx - 1) / tame_terr_sx;
  float cz = lz * (float)(tame_terr_mz - 1) / tame_terr_sz;
  int x0 = (int)cx, z0 = (int)cz;
  if (x0 >= tame_terr_mx - 1) x0 = tame_terr_mx - 2;
  if (z0 >= tame_terr_mz - 1) z0 = tame_terr_mz - 2;
  float fx = cx - (float)x0;
  float fz = cz - (float)z0;
  float h00 = tame_terr_h[x0 + z0 * tame_terr_mx];
  float h10 = tame_terr_h[(x0 + 1) + z0 * tame_terr_mx];
  float h01 = tame_terr_h[x0 + (z0 + 1) * tame_terr_mx];
  float h11 = tame_terr_h[(x0 + 1) + (z0 + 1) * tame_terr_mx];
  // GenMeshHeightmap hücreyi (0,1)-(1,0) köşegeninden bölüyor:
  //   üçgen 1 = (0,0),(0,1),(1,0)  → fx + fz <= 1
  //   üçgen 2 = (1,0),(0,1),(1,1)  → fx + fz >  1
  float g;
  if (fx + fz <= 1.0f) g = h00 + (h10 - h00) * fx + (h01 - h00) * fz;
  else g = h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);
  return (double)(tame_terr_base + g * tame_terr_sy / 255.0f);
}

// --- Arazi katman boyama ----------------------------------------------------
// Arazi tek renkti: kırk birimlik bir dünyanın tamamı aynı yeşil. Oysa
// yükseklik de eğim de zaten elimizde — fizik ikisini de kullanıyor. Katman
// boyama yeni VERİ istemiyor, o veriyi mesh'in tepe noktalarına RENK olarak
// yazmayı istiyor.
//
// Neden vertex color, neden shader değil: arazi sıradan bir model olarak
// kalıyor, yani doku/ışık/gölge/sis yollarının hiçbiri değişmiyor ve ayrı bir
// materyal yönetmek gerekmiyor. (Bunun görünür olması için ışık shader'ında
// texelColor'ın fragColor ile çarpılması gerekti — stok raylib ışık shader'ı
// tepe rengini fragment'a taşıyıp kullanmıyordu.)
static int   tame_terr_lay_on    = 0;
static Color tame_terr_lay_low   = { 96, 132,  88, 255};   // çim
static Color tame_terr_lay_mid   = {124, 116,  82, 255};   // toprak
static Color tame_terr_lay_high  = {238, 242, 250, 255};   // kar
static Color tame_terr_lay_rock  = {112, 110, 106, 255};   // kaya (eğime göre)
static float tame_terr_lay_midy  = 3.0f;    // dünya Y: buradan sonra toprak
static float tame_terr_lay_highy = 7.0f;    // dünya Y: buradan sonra kar
static float tame_terr_lay_slope = 42.0f;   // derece: bundan dik yüzey kaya

// Yüzey normalinin Y bileşeni (1 = düz, 0'a yaklaştıkça dik). Merkezi
// farklarla — scene3d'nin Tulpar tarafındaki _terrain_normal3'üyle aynı
// yöntem, aynı sonuç.
static float tame_terrain_up(double wx, double wz) {
  const double e = 0.5;
  double hl = tame_impl_terrain_height(wx - e, wz);
  double hr = tame_impl_terrain_height(wx + e, wz);
  double hd = tame_impl_terrain_height(wx, wz - e);
  double hu = tame_impl_terrain_height(wx, wz + e);
  double dx = (hr - hl) / (2.0 * e);
  double dz = (hu - hd) / (2.0 * e);
  return (float)(1.0 / sqrt(dx * dx + 1.0 + dz * dz));
}

// Hangi katman: 0 çim, 1 toprak, 2 kar, 3 kaya. KESKİN sınıflandırma —
// oyun mantığı için (ayak sesi, hız, "karda mısın?"). Mesh boyaması ise
// aşağıda YUMUŞAK geçiş kullanıyor: göz gradyan ister, oyun kesin cevap.
int tame_impl_terrain_layer(double wx, double wz) {
  if (!tame_terr_ready) return 0;
  float up = tame_terrain_up(wx, wz);
  if (up < cosf(tame_terr_lay_slope * (float)(3.14159265358979 / 180.0)))
    return 3;
  double h = tame_impl_terrain_height(wx, wz);
  if (h >= (double)tame_terr_lay_highy) return 2;
  if (h >= (double)tame_terr_lay_midy) return 1;
  return 0;
}

static Color tame_terr_mix(Color a, Color b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  Color c;
  c.r = (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t);
  c.g = (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t);
  c.b = (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t);
  c.a = 255;
  return c;
}

// Mesh için renk: katman sınırlarında YUMUŞAK geçiş. Keskin sınır, arazide
// çizilmiş bir kontur gibi görünür — doğada öyle bir çizgi yok.
static Color tame_terrain_color_at(double wx, double wz, double h) {
  float band = (tame_terr_lay_highy - tame_terr_lay_midy) * 0.28f;
  if (band < 0.05f) band = 0.05f;
  Color c;
  double m = (double)tame_terr_lay_midy;
  double t = (double)tame_terr_lay_highy;
  if (h <= m - band)      c = tame_terr_lay_low;
  else if (h < m + band)  c = tame_terr_mix(tame_terr_lay_low, tame_terr_lay_mid,
                                            (float)((h - (m - band)) / (2.0 * band)));
  else if (h <= t - band) c = tame_terr_lay_mid;
  else if (h < t + band)  c = tame_terr_mix(tame_terr_lay_mid, tame_terr_lay_high,
                                            (float)((h - (t - band)) / (2.0 * band)));
  else                    c = tame_terr_lay_high;
  // Eğim kayayı ÜSTE bindirir: dik yüzeyde çim/kar tutmaz. Bu da bir bant
  // üzerinden karışıyor, yoksa yamaçta keskin bir kaya lekesi oluşurdu.
  float up = tame_terrain_up(wx, wz);
  float sc = cosf(tame_terr_lay_slope * (float)(3.14159265358979 / 180.0));
  if (up < sc + 0.06f) {
    float k = (sc + 0.06f - up) / 0.12f;
    c = tame_terr_mix(c, tame_terr_lay_rock, k);
  }
  return c;
}

// Mesh'e renk tamponu ekle. GenMeshHeightmap zaten yükleme yaptığı için VAO
// var; yalnız RENK VBO'sunu ekliyoruz. Mesh'i kendimiz üretmiyoruz ki
// üçgenleme raylib'inkiyle birebir kalsın — tm3_terrain_height o
// üçgenlemeyi taklit ediyor, ayrışırlarsa fizik görselden kayar.
static void tame_terrain_paint_mesh(Mesh *mesh) {
  if (!tame_terr_lay_on || mesh->vertexCount <= 0 || mesh->vertices == NULL)
    return;
  int vc = mesh->vertexCount;
  unsigned char *cols = (unsigned char *)malloc((size_t)vc * 4);
  if (!cols) return;
  for (int i = 0; i < vc; i++) {
    // Mesh yerel uzayda 0..sx / 0..sz; dünyada yarı-boy kadar geri kaydırılıp
    // çiziliyor (bkz. scene3d _s3_render). Yerel Y'ye taban eklenince dünya Y.
    float lx = mesh->vertices[i * 3 + 0];
    float ly = mesh->vertices[i * 3 + 1];
    float lz = mesh->vertices[i * 3 + 2];
    double wx = (double)lx - (double)tame_terr_sx * 0.5;
    double wz = (double)lz - (double)tame_terr_sz * 0.5;
    Color c = tame_terrain_color_at(wx, wz, (double)ly + (double)tame_terr_base);
    cols[i * 4 + 0] = c.r; cols[i * 4 + 1] = c.g;
    cols[i * 4 + 2] = c.b; cols[i * 4 + 3] = 255;
  }
  mesh->colors = cols;
  if (mesh->vaoId > 0) {
    rlEnableVertexArray(mesh->vaoId);
    mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR] =
        rlLoadVertexBuffer(cols, vc * 4 * (int)sizeof(unsigned char), false);
    rlSetVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, 4,
                         RL_UNSIGNED_BYTE, true, 0, 0);
    rlEnableVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR);
    rlDisableVertexArray();
  }
}

// Katmanları ayarla ve AÇ. Mesh üretim zamanında okunduğu için araziden ÖNCE
// çağrılmalı; sonra çağrılırsa bir sonraki arazi üretiminde geçerli olur.
void tame_impl_terrain_layers(int64_t low, int64_t mid, int64_t high,
                              int64_t rock, double mid_y, double high_y,
                              double rock_slope_deg) {
  tame_terr_lay_low = tame_color(low);
  tame_terr_lay_mid = tame_color(mid);
  tame_terr_lay_high = tame_color(high);
  tame_terr_lay_rock = tame_color(rock);
  tame_terr_lay_midy = (float)mid_y;
  tame_terr_lay_highy = (float)high_y;
  tame_terr_lay_slope = (float)rock_slope_deg;
  tame_terr_lay_on = 1;
}

void tame_impl_terrain_layers_off(void) { tame_terr_lay_on = 0; }

void tame_impl_draw_model(int h, double x, double y, double z, double scale,
                          int64_t tint) {
  if (!tame_model_ok(h)) return;
  if (tame_recording()) { tame_dl_push(4, h, x, y, z, scale, scale, scale, 0.0, tint); return; }
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
  if (tame_recording()) { tame_dl_push(4, h, x, y, z, scale, scale, scale, yaw, tint); return; }
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

// Yüklenmiş fontla metnin genişliği. Harf aralığı `tame_impl_text_font` ile
// AYNI olmak zorunda — ölçüm ve çizim ayrışırsa yerleşim sessizce kayar
// (editörün bütün sütun genişlikleri ölçülen metne dayanıyor).
int tame_impl_font_width(int fh, const char *s, int size) {
  if (fh < 0 || fh >= TAME_MAX_FONTS || !tame_font_used[fh]) return 0;
  Vector2 m = MeasureTextEx(tame_fonts[fh], s ? s : "", (float)size,
                            (float)size / 10.0f);
  return (int)m.x;
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
    // Materyaldeki kullanıcı dokusunu geri al: doku kaydı birazdan zaten
    // UnloadTexture edecek, birim mesh'te asılı kalmasın.
    for (int i = 0; i < 4; i++) {
      if (tame_white_tex_saved)
        tame_unit[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
            tame_white_tex;
      UnloadModel(tame_unit[i]);
    }
    tame_unit_ready = 0;
    tame_white_tex_saved = 0;
  }
  if (tame_sky_ready) {
    UnloadShader(tame_sky_shader);
    tame_sky_ready = 0;
    tame_sky_on = 0;
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
  if (tame_shadow_ready) {
    rlUnloadFramebuffer(tame_shadow_fbo);   // bağlı dokuları da bırakır
    tame_shadow_fbo = 0; tame_shadow_tex = 0; tame_shadow_color = 0;
    tame_shadow_ready = 0; tame_shadows_on = 0;
  }
  if (tame_depth_ready) { UnloadShader(tame_depth_shader); tame_depth_ready = 0; }
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

// Ham raylib tuş kodu ile aynı sorgular. Tulpar tarafı tuşları ADLA
// adresliyor, ama scene3d gibi kütüphaneler kendi sabitlerini (K_W = 87,
// K_LEFT = 263 …) tutuyor — sayı verilince ad tablosuna düşmek yerine kodu
// doğrudan kullanıyoruz.
// Metin genişliği: editörün yerleşimi buna bağlı (panel sütunları, düğme
// kutuları). Elle "karakter sayısı * kabaca yarım punto" tahmini kullanmak
// yazı tipi değişince sessizce bozulurdu.
int tame_impl_text_width(const char *s, int size) {
  if (!s) return 0;
  return MeasureText(s, size);
}

// Klavyeden gelen sıradaki UNICODE karakter (yoksa 0). IsKeyPressed tuş
// KODU verir, yani düzen/shift bilmez — metin alanı için gereken bu.
int tame_impl_char_pressed(void) { return GetCharPressed(); }

int tame_impl_key_down_code(int key) { return key ? IsKeyDown(key) : 0; }
int tame_impl_key_pressed_code(int key) { return key ? IsKeyPressed(key) : 0; }
int tame_impl_key_released_code(int key) { return key ? IsKeyReleased(key) : 0; }

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

// Fare DELTA'sı (bu kare içinde ne kadar hareket etti). İmleç kilitliyken
// mouse_x/mouse_y sabit kalır (işaretçi ekranın ortasına çakılıdır) — o modda
// bakış için tek girdi kaynağı budur. Android'in sanal-dünya kamerası açıksa
// delta da dünya ölçeğine çevrilir, böylece mouse_x/y ile aynı birimde olur.
double tame_impl_mouse_dx(void) {
  double d = (double)GetMouseDelta().x;
  return tame_cam_on ? d / (double)tame_cam_zoom : d;
}
double tame_impl_mouse_dy(void) {
  double d = (double)GetMouseDelta().y;
  return tame_cam_on ? d / (double)tame_cam_zoom : d;
}

// İmleci kilitle/serbest bırak (FPS bakışı). Kilitliyken imleç gizlenir ve
// pencereden çıkamaz; web'de bu tarayıcının Pointer Lock'ıdır (kullanıcı
// jesti gerekir — tarayıcı ilk tıklamada verir). Pencere yoksa sessiz no-op:
// headless testte DisableCursor() GLFW handle'ı NULL'ken çağrılmamalı.
static int tame_cursor_locked = 0;

void tame_impl_cursor_lock(int on) {
  if (!IsWindowReady()) return;
  if (on) {
    DisableCursor();
    tame_cursor_locked = 1;
  } else {
    EnableCursor();
    tame_cursor_locked = 0;
  }
}

int tame_impl_cursor_locked(void) { return tame_cursor_locked; }

// raylib InitWindow'da çıkış tuşunu ESC'ye kuruyor: ESC'ye basılınca
// WindowShouldClose() true döner ve oyun döngüsü biter. Bu, ESC'yi Tulpar
// tarafında YAKALANAMAZ yapıyor — duraklat menüsü kurmanın önündeki tek engel
// buydu. 0 (KEY_NULL) geçilince raylib'in kestirmesi kapanır ve ESC sıradan bir
// tuş olur; `key_pressed(K_ESC)` ile okunabilir.
void tame_impl_exit_key(int key) {
  if (!IsWindowReady()) return;
  SetExitKey(key);
}

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

// ANDROID'DE pencere hazır olmadan çağrılamaz. Orada raylib'in dosya yolu
// göreli adlar için AAssetManager'a düşüyor (LoadFileText → android_fopen →
// AAssetManager_open) ve asset manager ancak aktivite ayağa kalkınca, yani
// InitWindow sırasında kuruluyor. Öncesinde çağrılırsa NULL mutex üzerinde
// SIGSEGV — emülatörde birebir bu görüldü: oyun `kayit_ac3d()`yi üst düzeyde,
// `oyna3d()`den ÖNCE çağırdığı anda çöküyordu. Çökmek yerine "veri yok" demek
// doğru davranış; scene3d zaten pencereden sonra yeniden okuyor.
//
// Guard SADECE Android'de: masaüstünde raylib düz fopen kullanıyor, dosya
// erişiminin pencereyle ilgisi yok. Guard'ı her platforma koymak motorun
// headless test edilebilirliğini kırardı (arazi fiziğinde bilerek korunan
// özellik) — nitekim ilk denemede kalıcılık testleri tam bu yüzden düştü.
#if defined(PLATFORM_ANDROID) || defined(__ANDROID__)
#  define TAME_DATA_NEEDS_WINDOW 1
#else
#  define TAME_DATA_NEEDS_WINDOW 0
#endif

int tame_impl_save_data(const char *name, const char *text) {
  if (!name || !name[0] || !text) return 0;
#if TAME_DATA_NEEDS_WINDOW
  if (!IsWindowReady()) return 0;
#endif
  return SaveFileText(name, (char *)text) ? 1 : 0;
}

// Dönen tampon LoadFileText'in ayırdığı bellektir; binding kopyaladıktan
// sonra tame_impl_text_free ile bırakır. Dosya yoksa NULL döner.
char *tame_impl_load_data(const char *name) {
  if (!name || !name[0]) return NULL;
#if TAME_DATA_NEEDS_WINDOW
  if (!IsWindowReady()) return NULL;
#endif
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
