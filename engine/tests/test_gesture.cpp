// platform/gesture.hpp: TAP ve PINCH tanimayi somut sayilarla dogrular.
#include "core/math/vec.hpp"
#include "platform/gesture.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::platform;

ENGINE_TEST(gesture_quick_still_touch_produces_tap) {
  GestureRecognizer gr; // varsayilan: 0.3s / 16px
  TouchState s;

  s.begin(0, 100, 100);
  GestureEvents e1 = gr.update(s, 0.0);
  CHECK(e1.count == 0); // parmak hala basili, henuz jest yok

  s.end(0);
  GestureEvents e2 = gr.update(s, 0.03); // 30ms sonra kalkti, hic hareket etmedi
  CHECK(e2.count == 1);
  CHECK(e2.events[0].type == GestureType::kTap);
  CHECK(e2.events[0].x == 100.0f && e2.events[0].y == 100.0f);
}

ENGINE_TEST(gesture_excessive_movement_permanently_breaks_tap) {
  GestureRecognizer gr(0.3f, 16.0f);
  TouchState s;

  s.begin(0, 0, 0);
  gr.update(s, 0.0);
  s.move(0, 50, 0); // 50px >> 16px esik
  gr.update(s, 0.01); // bu karede esik asilir (moved_too_far=true, KALICI)
  s.end(0);
  GestureEvents e = gr.update(s, 0.02); // hizli kalkis, ama artik tap adayi degil
  CHECK(e.count == 0);
}

ENGINE_TEST(gesture_excessive_duration_breaks_tap) {
  GestureRecognizer gr(0.3f, 16.0f);
  TouchState s;

  s.begin(0, 10, 10);
  gr.update(s, 0.0);
  s.end(0);
  GestureEvents e = gr.update(s, 1.0); // 1s > 0.3s max
  CHECK(e.count == 0);
}

ENGINE_TEST(gesture_two_finger_pinch_full_lifecycle) {
  GestureRecognizer gr;
  TouchState s;

  // Iki parmak ayni anda basar, aralarinda 100px.
  s.begin(0, 0, 0);
  s.begin(1, 100, 0);
  GestureEvents e1 = gr.update(s, 0.0);
  CHECK(e1.count == 1);
  CHECK(e1.events[0].type == GestureType::kPinchBegin);
  CHECK(e1.events[0].x == 50.0f && e1.events[0].y == 0.0f); // orta nokta

  // Parmaklar birbirinden uzaklasir: 100px -> 200px (TAM 2x buyutme).
  s.move(0, -50, 0);
  s.move(1, 150, 0);
  GestureEvents e2 = gr.update(s, 0.1);
  CHECK(e2.count == 1);
  CHECK(e2.events[0].type == GestureType::kPinchUpdate);
  CHECK(nearly_equal(e2.events[0].scale, 2.0f, 1e-4f));
  CHECK(nearly_equal(e2.events[0].rotation_rad, 0.0f, 1e-4f)); // ayni eksende, donus yok

  // Bir parmak kalkar: pinch biter.
  s.end(0);
  GestureEvents e3 = gr.update(s, 0.2);
  CHECK(e3.count == 1);
  CHECK(e3.events[0].type == GestureType::kPinchEnd);

  // Kalkan parmak icin TAP OLUSMAMALI (pinch'e katilmisti, cok hareket etmisti).
  for (uint32_t i = 0; i < e3.count; i++) CHECK(e3.events[i].type != GestureType::kTap);
}

ENGINE_TEST(gesture_third_finger_prevents_pinch_start) {
  GestureRecognizer gr;
  TouchState s;
  s.begin(0, 0, 0);
  s.begin(1, 100, 0);
  s.begin(2, 200, 0); // 3 parmak: pinch BASLAMAMALI
  GestureEvents e = gr.update(s, 0.0);
  CHECK(e.count == 0);
}
