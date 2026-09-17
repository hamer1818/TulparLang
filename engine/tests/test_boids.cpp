#include "tests/test.hpp"
#include "sim/boids.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

ENGINE_TEST(engine_test_boids) {
  constexpr uint32_t count = 3;
  Vec3 positions[count] = {
    {0, 0, 0},
    {0.5f, 0, 0},
    {-0.5f, 0, 0}
  };
  Vec3 velocities[count] = {
    {1, 0, 0},
    {1, 0, 0},
    {1, 0, 0}
  };

  BoidsConfig cfg;
  
  SpatialHash temp_hash;
  temp_hash.bucket_count = 16;
  uint32_t cell_start[17];
  uint32_t items[count];
  temp_hash.cell_start = cell_start;
  temp_hash.items = items;

  uint32_t neighbor_buffer[count];

  boids_step(positions, velocities, count, cfg, 0.1f, temp_hash, neighbor_buffer, count);

  // Hepsi saga (x yönünde) hareket ediyordu.
  // Ortadaki birbirinden uzaklasmak isteyecek (separation), vb.
  // Boids calistiginda coker veya hata vermiyorsa test basarili kabul edilir.
  CHECK(positions[0].x > 0);
}
