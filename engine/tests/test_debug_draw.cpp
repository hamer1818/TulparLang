// renderer/debug_draw.hpp: yasam dongusu KAPILARI. Gercek cizim bir Vulkan
// cihazi gerektirdigi icin burada test EDILMEZ (tests/test_renderer.cpp'nin
// kendi kurulumu var); burada test edilen sey, sarmalayicinin gecersiz
// kullanimda SESSIZCE bozulmadigi.
#include "renderer/debug_draw.hpp"

#include "renderer/renderer.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

ENGINE_TEST(debug_draw_rejects_invalid_setup) {
  // Baslatilmadan once hazir DEGIL.
  CHECK(!debug_draw_ready());

  // GECERSIZ mesh ile baslatma REDDEDILMELI (aksi halde her karede
  // gecersiz bir tutamacla cizim denenirdi).
  MeshHandle invalid{}; // varsayilan = gecersiz
  CHECK(!invalid.valid());
  CHECK(!debug_draw_init(invalid, 1024));
  CHECK(!debug_draw_ready());

  // Sifir butce de anlamsiz -> reddedilir.
  MeshHandle fake{};
  fake.id = 0; // gecerli sayilan bir tutamac (0xFFFFFFFF degil)
  CHECK(fake.valid());
  CHECK(!debug_draw_init(fake, 0));
  CHECK(!debug_draw_ready());
}

ENGINE_TEST(debug_draw_shutdown_is_safe_when_never_initialized) {
  // Cift kapatma / hic acilmadan kapatma COKMEMELI.
  CHECK(!debug_draw_ready());
  debug_draw_shutdown();
  debug_draw_shutdown();
  CHECK(!debug_draw_ready());
}

ENGINE_TEST(debug_draw_stats_start_at_zero) {
  const DebugDrawStats s = debug_draw_stats();
  CHECK(s.lines_drawn == 0);
  CHECK(s.points_drawn == 0);
  CHECK(s.dropped == 0);
}
