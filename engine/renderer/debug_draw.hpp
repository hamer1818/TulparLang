// L3 RENDERER — Hata ayiklama cizimi (tek karelik cizgi/kure/kutu/ok/eksen).
//
// **SIFIRDAN YAZILMADI.** Cekirdek, glampert/debug-draw kutuphanesidir
// (github.com/glampert/debug-draw, **KAMU MALI** -- lisans kisiti YOK,
// 541 yildiz, alaninin fiili standardi). third_party/debug_draw/ altinda
// DEGISTIRILMEDEN duruyor. Bu dosya yalnizca iki sey yapar:
//   1) kutuphaneyi Tulpar'in kurallarina gore YAPILANDIRIR,
//   2) onun `dd::RenderInterface` arayuzunu bizim mevcut cizim yoluna baglar.
//
// **Neden bu kutuphane:** olculebilir sebeplerle, tahminle degil --
//   * `throw`/`try`/`catch` sayisi: 0  -> -fno-exceptions ile uyumlu
//   * `dynamic_cast`/`typeid` sayisi: 0 -> -fno-rtti ile uyumlu
//   * Tamponlar SABIT boyutlu (DEBUG_DRAW_MAX_LINES vb. makrolarla)
//   * `DD_MALLOC` YALNIZ initialize()/shutdown()'da cagriliyor (font
//     acma + baglam) -> KARE ICI TAHSIS SIFIR, AllocGate kapisiyla uyumlu
//   * `DEBUG_DRAW_OVERFLOWED` override edilebilir -> tampon dolunca
//     SESSIZCE kirpmak yerine bizim raporlama yolumuza baglanir
// Bedava gelen sekiller: line, point, box, aabb, sphere, capsule, cone,
// arrow, circle, plane, cross, frustum, axisTriad, tangentBasis,
// vertexNormal, xzSquareGrid. Bunlarin matematigi kanitlanmis; biz yalniz
// piksele cevirme kismini yaziyoruz.
//
// **Cizim yolu -- BILINCLI bir odun:** cizgiler, YENI bir Vulkan cizgi boru
// hatti yerine MEVCUT kup mesh'iyle (ince, uzun kutular olarak) ciziliyor.
// Sebep: bu ortamda derleyici ve cihaz yok; yazilacak yeni bir boru hatti
// TEK SATIRI BILE dogrulanamaz. Bu yol ise motorun ZATEN calisan
// `Renderer::draw()` yolunu kullanir. Bedeli: cizgi basina bir cizim
// cagrisi (toplu degil). Bu yuzden varsayilan butce DUSUK tutuldu.
// Gercek bir toplu cizgi boru hatti sonraki adim -- ve `dd::RenderInterface`
// tam da onun takilacagi yer (kutuphaneyi degistirmeden backend degisir).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

class Renderer;
struct MeshHandle;

// Cizim butcesi: `Renderer`in kare basina cizim listesi sinirli
// (RendererConfig::max_draws). Hata ayiklama cizimi oyunun cizimlerini
// TASIRMAMALI; bu yuzden ayri, DUSUK bir tavan var. Asilirsa sayilir ve
// raporlanir -- sessizce kaybolmaz.
struct DebugDrawStats {
  uint32_t lines_drawn = 0;
  uint32_t points_drawn = 0;
  uint32_t dropped = 0; // butce asimi (sessiz degil)
};

// Motor acilisinda BIR KEZ. `cube` cizgi/nokta govdesi olarak kullanilir
// (birim kup, yari-boyut 0.5 -- Renderer::cube() sozlesmesi).
// false: zaten baslatilmis ya da gecersiz mesh.
bool debug_draw_init(MeshHandle cube, uint32_t max_lines_per_frame = 2048);
void debug_draw_shutdown();
bool debug_draw_ready();

// Kare basina: begin_frame() SONRASI cagirilir; o kareye kadar biriken
// TUM dd::* komutlarini `r` uzerine cizer ve tamponu bosaltir.
// line_thickness: dunya birimi (cizgiler kutu olarak cizildigi icin
// kalinlik gerekir; tipik 0.02 - 0.05).
void debug_draw_flush(Renderer &r, float line_thickness = 0.03f);

DebugDrawStats debug_draw_stats();

} // namespace tulpar::engine::renderer
