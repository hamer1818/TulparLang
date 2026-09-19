#include "app/editor_camera.hpp"

#include <cmath>

namespace tulpar::engine::app {

namespace {
constexpr Vec3 kWorldUp{0, 1, 0};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Gozden hedefe DEGIL, hedeften goze birim yon (kure koordinati). Tum temsil
// bunun uzerine kurulu: goz = hedef + orbit_dir * radius.
Vec3 orbit_dir(float yaw, float pitch) {
  const float cp = std::cos(pitch);
  return {cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
}

// pitch'i kenetle. ⚠ Tam ±90 derecede look_at'in cross(f, up)'i sifir olur ve
// normalize(0) = 0 doner: gorunum matrisi SESSIZCE bozulur (satirlar sifirlanir,
// ekran tamamen bos cikar). 89.9 derece hem "duz asagi bakiyor" iddiasini
// tasiyacak kadar dik (forward.y = -0.9999985) hem de matrisi saglam tutar.
void clamp_pitch(EditorCamera &c) {
  const float lim = c.pitch_limit > 0.0f ? c.pitch_limit : 1.569051f;
  c.pitch = clampf(c.pitch, -lim, lim);
}

// yaw'i (-pi, pi]'ye sarar: saatlerce donduren biri float'i buyutup hassasiyet
// kaybetmesin (yaw 1e7 radyanken sin/cos'un gurultusu goze gorunur hale gelir).
void wrap_yaw(EditorCamera &c) {
  const float two_pi = 2.0f * kPi;
  if (c.yaw > kPi || c.yaw < -kPi) {
    c.yaw -= two_pi * std::floor((c.yaw + kPi) / two_pi);
  }
}

void clamp_radius(EditorCamera &c) {
  const float lo = c.min_radius > 0.0f ? c.min_radius : 0.01f;
  const float hi = c.max_radius > lo ? c.max_radius : lo;
  c.radius = clampf(c.radius, lo, hi);
}
} // namespace

Vec3 camera_eye(const EditorCamera &c) { return c.target + orbit_dir(c.yaw, c.pitch) * c.radius; }
Vec3 camera_forward(const EditorCamera &c) { return -orbit_dir(c.yaw, c.pitch); }
Vec3 camera_right(const EditorCamera &c) {
  const Vec3 r = cross(camera_forward(c), kWorldUp);
  // pitch kenetli oldugu icin buraya sifir vektor GELMEZ; yine de gelirse
  // (birisi pitch'i elle yazdiysa) +X'e duseriz, sessiz sifir vektore degil.
  return length_sq(r) > 1e-12f ? normalize(r) : Vec3{1, 0, 0};
}
Vec3 camera_up(const EditorCamera &c) { return cross(camera_right(c), camera_forward(c)); }
Mat4 camera_view(const EditorCamera &c) { return Mat4::look_at(camera_eye(c), c.target, kWorldUp); }

Mat4 camera_projection(const EditorCamera &c, float aspect, float znear, float zfar) {
  if (!(aspect > 0.0f)) aspect = 1.0f;
  if (c.proj == CameraProjection::Perspective) return Mat4::perspective(c.fov_y, aspect, znear, zfar);
  // Ayni yuksekligi goster: perspektifin `radius` mesafesindeki yari yuksekligi
  // radius*tan(fov/2). Kip degistirince olcek ZIPLAMAZ (kapi bunu olcer: aynı
  // mesafede ayni NDC, 2*radius'ta AYRISIRLAR — perspektifte kuculur).
  const float hh = c.radius * std::tan(c.fov_y * 0.5f);
  const float hw = hh * aspect;
  return Mat4::ortho(-hw, hw, -hh, hh, znear, zfar);
}

Mat4 camera_view_proj(const EditorCamera &c, float aspect, float znear, float zfar) {
  return camera_projection(c, aspect, znear, zfar) * camera_view(c);
}

void camera_ray(const EditorCamera &c, float aspect, float mx, float my, float fw, float fh, Vec3 *origin, Vec3 *dir) {
  if (!origin || !dir) return;
  if (!(fw > 0.0f) || !(fh > 0.0f)) { *origin = camera_eye(c); *dir = camera_forward(c); return; }
  if (!(aspect > 0.0f)) aspect = fw / fh;
  const Vec3 eye = camera_eye(c), f = camera_forward(c), r = camera_right(c), u = camera_up(c);
  if (c.proj == CameraProjection::Perspective) {
    const float th = std::tan(c.fov_y * 0.5f);
    const float nx = (2.0f * mx / fw - 1.0f) * th * aspect;
    const float ny = (1.0f - 2.0f * my / fh) * th;
    *origin = eye;
    *dir = normalize(f + r * nx + u * ny);
    return;
  }
  // Ortografik: yon HERKESTE ayni, kayan sey BASLANGIC noktasidir. Perspektif
  // formulunu burada kullanmak "ortoda secim hep ekranin ortasindan atiliyor"
  // hatasini verirdi (kenarda yanlis nesne secilir).
  const float hh = c.radius * std::tan(c.fov_y * 0.5f), hw = hh * aspect;
  const float nx = (2.0f * mx / fw - 1.0f) * hw;
  const float ny = (1.0f - 2.0f * my / fh) * hh;
  *origin = eye + r * nx + u * ny;
  *dir = f;
}

void camera_dolly(EditorCamera &c, float steps) {
  if (steps == 0.0f) return;
  // CARPIMSAL: her "tik" mesafeyi sabit bir ORANLA degistirir. Toplamsal
  // (radius -= k) olsaydi yakinken nesnenin icine dalar, uzaktayken hic
  // ilerlemezdi — eski editor kodu da bu yuzden pow() kullaniyordu.
  c.radius *= std::pow(c.dolly_rate > 0.0f ? c.dolly_rate : 0.9f, steps);
  clamp_radius(c);
}

void camera_focus(EditorCamera &c, const content::SceneBounds &b) {
  const Vec3 ctr = (b.lo + b.hi) * 0.5f;
  const Vec3 ext = (b.hi - b.lo) * 0.5f;
  float r = length(ext); // kusatan kurenin yaricapi (kose mesafesi)
  if (!(r > 1e-4f)) r = 0.5f; // dejenere kutu (nokta isik, bos varlik): makul bir taban
  c.target = ctr;
  const float half = clampf(c.fov_y * 0.5f, 0.05f, 1.5f);
  // Kureyi konisine TEGET yapan mesafe: d = r / sin(yari_aci). tan ile yapilirsa
  // (merkezi projelendirip yaricapi ekranda olcen yaklasim) genis fov'da kure
  // kenarlarindan tasar.
  const float margin = 1.25f;
  c.radius = r * margin / std::sin(half);
  clamp_radius(c);
}

void camera_focus_all(EditorCamera &c, const content::SceneBounds *b, uint32_t n) {
  if (!b || n == 0) return; // bos sahnede "F" kamerayi SICRATMAZ
  content::SceneBounds u = b[0];
  for (uint32_t i = 1; i < n; i++) {
    u.lo = vmin(u.lo, b[i].lo);
    u.hi = vmax(u.hi, b[i].hi);
  }
  camera_focus(c, u);
}

void camera_align(EditorCamera &c, CameraAxis a) {
  const float lim = c.pitch_limit > 0.0f ? c.pitch_limit : 1.569051f;
  switch (a) {
  case CameraAxis::PlusX:  c.yaw = kPi * 0.5f;  c.pitch = 0.0f; break;
  case CameraAxis::MinusX: c.yaw = -kPi * 0.5f; c.pitch = 0.0f; break;
  case CameraAxis::PlusZ:  c.yaw = 0.0f;        c.pitch = 0.0f; break;
  case CameraAxis::MinusZ: c.yaw = kPi;         c.pitch = 0.0f; break;
  // Y eksenlerinde yaw'a DOKUNULMAZ: asagi bakarken yaw gorusun ekrandaki
  // donusudur, sifirlamak kullanicinin pusulasini bir anda cevirir.
  case CameraAxis::PlusY:  c.pitch = lim; break;
  case CameraAxis::MinusY: c.pitch = -lim; break;
  default: return;
  }
  wrap_yaw(c);
}

void camera_update(EditorCamera &c, const CameraInput &in) {
  float dt = in.dt;
  if (!(dt > 0.0f)) dt = 0.0f;
  if (dt > 0.25f) dt = 0.25f; // takilan kare ucusu isinlamasin

  const bool wasd = in.key_w || in.key_a || in.key_s || in.key_d || in.key_q || in.key_e;
  // Ucus mandali: sag tus BIRAKILINCA duser. WASD birakilinca dusseydi, donus
  // ekseni (goz <-> hedef) her karede degisir ve goruntu titrerdi.
  if (!in.rmb) c.flying = false;
  else if (in.allow_keys && wasd && c.mode == CameraMode::Orbit) c.flying = true;
  const bool fly_now = in.rmb && in.allow_mouse && (c.mode == CameraMode::Fly || c.flying);

  const float sens = c.sensitivity;

  if (fly_now) {
    // --- Bakis: GOZ SABIT. Once gozu sakla, aci degis, hedefi yeniden turet;
    // boylece hedef daima kameranin `radius` kadar onundeki noktadir ve sag
    // tusu birakip yorungeye donmek goruntuyu ZIPLATMAZ (sozlesme).
    const Vec3 eye = camera_eye(c);
    c.yaw -= in.dx * sens;
    c.pitch += in.dy * sens;
    clamp_pitch(c);
    wrap_yaw(c);
    c.target = eye - orbit_dir(c.yaw, c.pitch) * c.radius;

    if (in.allow_keys && wasd && dt > 0.0f) {
      const Vec3 f = camera_forward(c), r = camera_right(c);
      Vec3 v{0, 0, 0};
      if (in.key_w) v += f;
      if (in.key_s) v -= f;
      if (in.key_d) v += r;
      if (in.key_a) v -= r;
      if (in.key_e) v += kWorldUp;
      if (in.key_q) v -= kWorldUp;
      if (length_sq(v) > 1e-12f) {
        // Normalize: kosegen (W+D) duz ileriden HIZLI olmasin.
        const float sp = c.speed * (in.shift ? c.fast_mul : 1.0f) * dt;
        const Vec3 step = normalize(v) * sp;
        // Goz ve hedef BIRLIKTE oteleniyor (temsilde hedefi oteleme yeter).
        c.target += step;
      }
    }
  } else if (in.allow_mouse && (in.rmb || in.mmb)) {
    const bool pan = in.mmb || (in.rmb && in.shift);
    const bool dolly = !in.mmb && in.rmb && in.alt;
    if (dolly) {
      camera_dolly(c, -in.dy * c.dolly_drag); // asagi surukle = uzaklas
    } else if (pan) {
      // Hedef, bakisa DIK duzlemde gezer (sag/yukari). Olcek `radius` ile
      // carpilir: yakinken kucuk, uzakken buyuk adim — "his" her yakinlikta ayni.
      const Vec3 r = camera_right(c), u = camera_up(c);
      const float k = c.pan_rate * c.radius;
      c.target -= r * (in.dx * k);
      c.target += u * (in.dy * k);
    } else {
      c.yaw -= in.dx * sens;
      c.pitch += in.dy * sens;
      clamp_pitch(c);
      wrap_yaw(c);
    }
  }

  if (in.allow_mouse && in.scroll != 0.0f) camera_dolly(c, in.scroll);
  clamp_radius(c);
  clamp_pitch(c);
}

} // namespace tulpar::engine::app
