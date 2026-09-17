// L6 APP — Editor GORUNUM KAMERASI: gezinme artik bir SAF FONKSIYON.
//
// Neden ayri bir birim: kamera kodu editor_app.cpp'nin kare dongusunde,
// `in->mouse_down[1]` ve `prev_mx` gibi yerel degiskenlerin arasinda yasiyordu.
// Orada hicbir seyi olcemezsin — pencere acmadan kosturamazsin, "pan gercekten
// bakis yonune dik mi", "ucus kare hizindan bagimsiz mi", "yakinlasma carpimsal
// mi" sorularinin cevabi yoktu. Burada kamera bir VERI (EditorCamera) ve bir
// GECIS FONKSIYONU (camera_update): girdi -> yeni durum. Kapilar (tests/
// test_editor_camera.cpp) Vulkan'siz, analitik olcer.
//
// GEOMETRIK SOZLESME (motorun gelenegi, tahmin degil olculmus):
//  * Kamera bir YORUNGE (orbit) ile temsil edilir: hedef (target) + kure
//    koordinati (yaw, pitch, radius). goz = hedef + dir(yaw,pitch)*radius,
//    dir = (cos p sin y, sin p, cos p cos y). Ucus kipinde de AYNI temsil
//    kullanilir — yalniz donusun EKSENI degisir (gozde sabit, hedefte degil),
//    bu yuzden sag tusu birakinca yorunge kipine ZIPLAMADAN donulur: hedef
//    zaten kameranin `radius` kadar onundeki noktadir.
//  * Yukari YON DUNYA +Y'dir (Mat4::look_at ile ayni). pitch ±pitch_limit'e
//    (89.9 derece) kenetlenir: tam 90'da look_at'in cross(f,up)'i sifirlanir
//    ve gorunum matrisi sessizce BOZULUR (normalize(0) = 0 doner).
//  * Izdusum Vulkan gelenegindedir (NDC z in [0,1], y ASAGI) — Mat4::perspective
//    ve Mat4::ortho ikisi de oyle; burada yeni bir gelenek UYDURULMAZ.
//
// TUS/FARE TABLOSU (Unity / Unreal / Blender ortak paydasi):
//
//   YORUNGE (varsayilan)
//     Sag tus (RMB) surukle ............ yaw / pitch (hedef etrafinda dondur)
//     Orta tus (MMB) surukle ........... KAYDIR (pan): hedef sag/yukari
//                                        duzleminde gezer, adim `radius` ile
//                                        olceklidir (yakinken yavas, uzakken
//                                        hizli — her yakinlikta ayni "his")
//     Shift + sag tus surukle .......... KAYDIR (orta tusu olmayan fareler)
//     Alt + sag tus surukle ............ YAKINLIK (dolly) — asagi surukle uzaklas
//     Tekerlek ......................... YAKINLIK, CARPIMSAL: radius *= q^adim
//                                        (toplamsal olsaydi yakinken cok hizli,
//                                        uzakken sunger gibi olurdu)
//
//   UCUS (RMB basili tutulurken)
//     W / S ............................ ileri / geri (bakis yonu)
//     A / D ............................ sol / sag (kamera sagi)
//     Q / E ............................ asagi / yukari (DUNYA Y'si; ucusta
//                                        yukari tusu bakisa gore egilmez)
//     Shift ............................ hizli (speed * fast_mul)
//     Fare ............................. bakis (goz sabit, hedef yeniden turetilir)
//   Kip alani (EditorCamera::mode) Ucus'ken RMB tek basina bakisi ucus gibi
//   dondurur. Yorunge kipindeyken RMB + WASD'ye BASILIRSA gecici olarak ucusa
//   gecilir (Unreal gelenegi) ve tus birakilinca degil, SAG TUS birakilinca
//   yorungeye donulur (`flying` mandali) — yoksa W'yi birakan her karede
//   donus ekseni goz<->hedef arasinda gidip gelir ve goruntu titrer.
//
//   ODAK / EKSEN
//     camera_focus(b) .................. "F": kutuyu cerceveler (hedef = merkez,
//                                        radius = kusatan kureyi fov'a sigdiran
//                                        mesafe * kenar payi)
//     camera_align(axis) ............... Ust/On/Yan: gorus eksene oturur,
//                                        HEDEF ve RADIUS korunur
//
// Ctrl bilerek BOSTUR: editorde Ctrl+tik "secime ekle"dir, kameraya baglanirsa
// coklu secim yapilamaz.
//
// Ayirma yok, STL yok, sanal fonksiyon yok: EditorCamera POD'dur, kopyalanabilir
// (geri al/yinele ya da kamera yer imi isteyen biri kopyasini saklar).
#pragma once
#include <cstdint>

#include "content/scene.hpp"  // SceneBounds (odak cerceveleme)
#include "core/math/vec.hpp"

namespace tulpar::engine::app {

enum class CameraMode : uint8_t { Orbit, Fly };
enum class CameraProjection : uint8_t { Perspective, Ortho };
// Gorusun uzerine oturacagi dunya ekseni: PlusY = kamera YUKARIDA, ASAGI bakar
// ("Ust"). PlusZ = kamera +Z'de, -Z'ye bakar ("On").
enum class CameraAxis : uint8_t { PlusX, MinusX, PlusY, MinusY, PlusZ, MinusZ, Count };
// Gizmo eksen uzayi (ImGuizmo::WORLD / LOCAL karsiligi). Kamera bunu KULLANMAZ;
// burada durur cunku kaplamadaki cip bunu degistirir ve iki basligin da gordugu
// tek ortak yer burasi.
enum class GizmoSpace : uint8_t { World, Local };

struct EditorCamera {
  // --- Durum (kaydedilen/yuklenen kisim: sahne dosyasindaki cam_* alanlari) ---
  float yaw = 0.7f, pitch = 0.45f, radius = 26.0f;
  Vec3 target{0, 1.0f, -3.0f};
  CameraMode mode = CameraMode::Orbit;
  CameraProjection proj = CameraProjection::Perspective;

  // --- Ayar (kullanici tercihi; varsayilanlar editorun eski davranisiyla uyumlu) ---
  float fov_y = 0.897598f;     // ~51.4 derece = kPi/3.5 (editor_app'in degeri)
  float speed = 9.0f;          // ucus: birim/saniye
  float fast_mul = 4.0f;       // Shift carpani
  float sensitivity = 0.005f;  // radyan / mantiksal piksel (eski kod da 0.005)
  // Kaydirma: piksel basina dunya birimi, `radius` ile CARPILIR. Varsayilan
  // 2*tan(fov/2)/720 — yani ~720 piksel yuksek bir panelde ekran uzayinda
  // yaklasik bire bir ("tuttugun nokta parmagin altinda kalir").
  float pan_rate = 0.00134f;
  float dolly_rate = 0.9f;     // tekerlek: radius *= dolly_rate^adim
  float dolly_drag = 0.02f;    // Alt+RMB: piksel -> tekerlek adimi
  float min_radius = 0.25f, max_radius = 500.0f;
  float pitch_limit = 1.569051f; // 89.9 derece (bkz. sozlesme: 90 = bozuk matris)

  // --- Dahili mandal (cagiran dokunmaz) ---
  // RMB basiliyken ucusa gecildi mi. Bkz. tablo: WASD birakilinca DEGIL, sag
  // tus birakilinca sifirlanir.
  bool flying = false;
};

// Bir karelik girdi. dx/dy MANTIKSAL PIKSEL cinsinden fare deltasidir (birikimli
// konum degil), scroll BU KARENIN tekerlek deltasidir (InputState::scroll_y
// birikimlidir — cagiran farki alir).
//
// ⚠ allow_mouse icin `!ui.wants_mouse()` YETMEZ — hatta YANLISTIR. 3B artik bir
// ImGui PANELININ icinde yasiyor, yani fare goruntunun uzerindeyken
// io.WantCaptureMouse ZATEN true olur (fare bir ImGui penceresinin ustunde);
// o kosulla kamera goruntude HIC donmez. Dogru kosul "fare GORUNTU OGESININ
// ustunde ve kaplama/gizmo onu almamis" ya da "kamera suruklemesi zaten
// suruyor" (surukleyip panelin disina cikmak donusu kesmesin):
//   const bool cam_mouse = (view_hovered && !ores.consumed_mouse &&
//                           !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) || cam_dragging;
// allow_keys ise metin yazarken kapatilir: !ui.wants_text_input() (WASD bir ad
// alanina yaziliyorsa ucus baslamamali).
struct CameraInput {
  float dx = 0, dy = 0;
  float scroll = 0;
  bool lmb = false, mmb = false, rmb = false;
  bool shift = false, ctrl = false, alt = false;
  bool key_w = false, key_a = false, key_s = false, key_d = false, key_q = false, key_e = false;
  bool allow_mouse = true, allow_keys = true;
  float dt = 1.0f / 60.0f;
};

// Gecis fonksiyonu: c'yi girdiye gore ilerletir. Hicbir sey ayirmaz, hicbir
// global okumaz — ayni (c, in) ciftinden HER ZAMAN ayni sonuc cikar (kapi
// 30 fps / 120 fps esitligini bunun uzerine kurar).
void camera_update(EditorCamera &c, const CameraInput &in);

Vec3 camera_eye(const EditorCamera &c);
Vec3 camera_forward(const EditorCamera &c); // goz -> hedef, birim
Vec3 camera_right(const EditorCamera &c);   // ekran sagi, birim (dunya Y'sine dik)
Vec3 camera_up(const EditorCamera &c);      // ekran yukarisi, birim
Mat4 camera_view(const EditorCamera &c);
// aspect = genislik/yukseklik. Ortografik kip PERSPEKTIFIN `radius` mesafesinde
// gosterdigi YUKSEKLIGIN AYNISINI gosterir (yari yukseklik = radius*tan(fov/2)),
// yani kip degistirmek olcegi ZIPLATMAZ; yalniz uzaklik-perspektifi kalkar.
Mat4 camera_projection(const EditorCamera &c, float aspect, float znear, float zfar);
Mat4 camera_view_proj(const EditorCamera &c, float aspect, float znear, float zfar);

// Fare pikselinden dunya isini. Perspektifte gozden yelpazelenir, ortografikte
// PARALELDIR (baslangic noktasi goruntu duzleminde kayar) — secim iki kipte de
// dogru calissin diye. (fw, fh) goruntu olcusu, (mx, my) o goruntudeki piksel.
void camera_ray(const EditorCamera &c, float aspect, float mx, float my, float fw, float fh, Vec3 *origin, Vec3 *dir);

// Tekerlek/surukleme yakinligi: radius *= dolly_rate^steps, [min,max]'a kenetli.
void camera_dolly(EditorCamera &c, float steps);

// "F": kutuyu cerceveler. Hedef kutunun merkezi, radius kusatan kureyi dikey
// gorus konisine sigdiran mesafe (d = r/sin(fov/2)) * kenar payi.
// ⚠ Dikey fov'a gore hesaplanir: aspect >= 1 (enine panel) icin yatayda da
// sigar; cok dar/uzun bir panelde yanlarda tasabilir.
void camera_focus(EditorCamera &c, const content::SceneBounds &b);
// Hepsinin birlesimi. n == 0 ise kamera DEGISMEZ (bos sahnede "F" sicramaz).
void camera_focus_all(EditorCamera &c, const content::SceneBounds *b, uint32_t n);
// Ust/On/Yan: hedef ve radius korunur, yalniz yaw/pitch oturur. Y eksenlerinde
// yaw BILEREK korunur (pusula yonun kaybolmasin; asagi bakarken yaw gorusun
// ekrandaki donusudur).
void camera_align(EditorCamera &c, CameraAxis a);

} // namespace tulpar::engine::app
