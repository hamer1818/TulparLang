// L6 CONTENT — primitives: SceneEntity::primitive icin prosedurel mesh tablosu.
//
// Neden ayri dosya: bu tablo ONCE yalniz SceneRuntime::init icindeydi, yani
// ilkel geometriler DERLENMIS sahnede cizilip EDITORDE cizilmiyordu. Editor
// `primitive` alanini hic tanimadigi icin menuden eklenen Kapsul/Silindir/
// Koni/Dortgen/Simit gorunmez varliklar olarak duruyordu. Tabloyu kopyalamak
// iki ayri dogruluk kaynagi yaratirdi (birinde slot eklenir otekinde
// unutulur); bu yuzden TEK yer burasi, iki cagiran da buradan kurar.
//
// Slot numaralari SceneEntity::primitive degerleridir ve editor "Olustur"
// menusunun kodlariyla AYNIDIR (kCreate3D: Kup=10, Kure=11, Kapsul=20...).
// Bu kasitli: menu kodu dogrudan primitive alanina yazilabilsin diye.
#pragma once
#include <cstdint>

#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

// primitive alaninin gecerli araligi; prims_ dizilerinin boyu da budur.
constexpr uint32_t kPrimitiveSlotCount = 32;

// Dolu slotlarin isimleri. Cagiran ham sayi yazmasin diye: parcacik cizimi
// bir zamanlar prims_[0] kullaniyordu ve slot 0 hic doldurulmadigi icin
// parcaciklar hic cizilmiyordu -- sessizce, cunku 0 da gecerli bir indeks.
constexpr uint32_t kPrimitivePlane    = 8;
constexpr uint32_t kPrimitiveCube     = 10;
constexpr uint32_t kPrimitiveSphere   = 11;
constexpr uint32_t kPrimitiveCapsule  = 20;
constexpr uint32_t kPrimitiveCylinder = 21;
constexpr uint32_t kPrimitiveCone     = 22;
constexpr uint32_t kPrimitiveQuad     = 23;
constexpr uint32_t kPrimitiveTorus    = 24;

// out[kPrimitiveSlotCount] tablosunu kurar. Dolu slotlar:
//   8 Duzlem · 10 Kup · 11 Kure · 20 Kapsul · 21 Silindir · 22 Koni
//   23 Dortgen · 24 Simit
// Geri kalan slotlar gecersiz (MeshHandle::valid() == false) kalir; cagiran
// cizmeden ONCE valid() sormalidir.
//
// Uretici tavani asarsa o slot kurulmaz (bozuk mesh cizmektense eksik ciz).
// Donus: kurulan mesh sayisi.
uint32_t build_primitive_meshes(renderer::Renderer &r, renderer::MeshHandle *out);

} // namespace tulpar::engine::content
