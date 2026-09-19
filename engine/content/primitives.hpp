// L6 CONTENT — prosedurel ilkel geometri tablosu.
//
// NEDEN VAR: PR #331 editore "Kapsul / Silindir / Koni / Dortgen / Simit"
// ekleme menusunu getirdi ve bu basligi DORT yerden include etti
// (content/scene_runtime.{hpp,cpp}, app/editor_app.cpp), ama dosyayi eklemeyi
// unuttu. Baslik bir HPP'den include edildigi icin hata bulasiciydi: onu
// dolayli goren her ceviri birimi de dusuyordu.
//
// Fikir: bu geometriler bir glTF kaynagindan GELMEZ, motorun kendi
// ureteclerinden (renderer::Renderer::sphere/capsule/...) uretilir. O yuzden
// varlik bir SceneEntity'de `asset = -1` ve `primitive = <yuva>` olarak durur.
#pragma once
#include <cstdint>

#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

// YUVA NUMARALARI EDITOR MENU KODLARIYLA AYNI. Bu bilerek secildi
// (app/editor_widgets.cpp `kCreate3D` tablosu ve app/editor_app.cpp'deki
// `case 20..24: e.primitive = kind;` dali): menude tiklanan kod dogrudan
// yuva indeksi oluyor, arada bir esleme tablosu tutulmuyor.
enum : int32_t {
  kPrimParticle = 0,  // SceneRuntime parcaciklari prims_[0] ile ciziyor
  kPrimPlane    = 8,
  kPrimCube     = 10,
  kPrimSphere   = 11,
  kPrimCapsule  = 20,
  kPrimCylinder = 21,
  kPrimCone     = 22,
  kPrimQuad     = 23,
  kPrimTorus    = 24,
};

// Tablo boyu = en buyuk yuva + 1. Cagiranlar `primitive` alanini bu sinira
// gore denetliyor (scene_runtime.cpp:300, editor_app.cpp:3026), o yuzden
// yeni bir ilkel eklenirse burasi da buyumeli.
constexpr uint32_t kPrimitiveSlotCount = 25;

// Tabloyu doldurur; YUKLEME ANINDA bir kez cagrilir, kare icinde degil.
// Kullanilmayan yuvalar gecersiz MeshHandle olarak kalir — cagiranlar zaten
// `valid()` ile bakiyor, yani bos yuva cizilmez.
void build_primitive_meshes(renderer::Renderer &r, renderer::MeshHandle *out);

} // namespace tulpar::engine::content
