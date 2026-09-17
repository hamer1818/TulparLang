#include "sim/chaos_physics.hpp"

namespace tulpar::engine::sim {

namespace {
// Yonu bilinmeyen (iki eklem ust uste) dejenere durumda kullanilacak SABIT
// eksen -- rastgele degil, deterministik (motorun bit-es garantisiyle ayni
// cizgi: ayni girdi HER platformda ayni cikti).
constexpr float kDegenerateEps = 1e-6f;
Vec3 unit_or_default(Vec3 d) {
  const float r = length(d);
  return r > kDegenerateEps ? d * (1.0f / r) : Vec3{1.0f, 0.0f, 0.0f};
}
} // namespace

FabrikResult solve_fabrik(Vec3 *positions, const float *lengths, uint32_t count, Vec3 target,
                          float tolerance, uint32_t max_iterations) {
  FabrikResult res;
  if (!positions || !lengths || count < 2 || count > kFabrikMaxJoints) return res;

  const Vec3 root = positions[0]; // KOK SABIT: her yinelemenin sonunda buraya geri konur

  float total_reach = 0.0f;
  for (uint32_t i = 0; i + 1 < count; i++) total_reach += lengths[i];

  const Vec3 to_target = target - root;
  const float dist = length(to_target);

  // ULASILAMAZ hedef: FABRIK'in tanimli davranisi -- zincir hedefe DOGRU
  // tamamen duzlesir (her kemik kendi uzunlugunu KORUYARAK).
  if (dist > total_reach) {
    const Vec3 dir = unit_or_default(to_target);
    for (uint32_t i = 0; i + 1 < count; i++) positions[i + 1] = positions[i] + dir * lengths[i];
    return res; // reached=false, iterations=0
  }

  for (uint32_t iter = 0; iter < max_iterations; iter++) {
    if (length(positions[count - 1] - target) < tolerance) {
      res.reached = true;
      return res;
    }

    // ILERI gecis: uc eklem HEDEFE konur, geriye dogru her eklem bir
    // oncekinden TAM kemik uzunlugu mesafeye cekilir (kok kayar -- sonraki
    // gecis duzeltir).
    positions[count - 1] = target;
    for (uint32_t i = count - 1; i > 0; i--) {
      const Vec3 dir = unit_or_default(positions[i - 1] - positions[i]);
      positions[i - 1] = positions[i] + dir * lengths[i - 1];
    }

    // GERI gecis: kok yerine konur, ileri dogru ayni duzeltme -- bu iki
    // gecisin TEKRARI FABRIK'in tamami.
    positions[0] = root;
    for (uint32_t i = 0; i + 1 < count; i++) {
      const Vec3 dir = unit_or_default(positions[i + 1] - positions[i]);
      positions[i + 1] = positions[i] + dir * lengths[i];
    }
    res.iterations = iter + 1;
  }

  res.reached = length(positions[count - 1] - target) < tolerance;
  return res;
}

bool build_flow_field(FlowField &field, uint32_t goal_x, uint32_t goal_z) {
  if (!field.blocked || !field.cost || !field.flow || !field.queue) return false;
  if (field.width == 0 || field.height == 0) return false;
  if (goal_x >= field.width || goal_z >= field.height) return false;
  const uint32_t goal = field.index(goal_x, goal_z);
  if (field.blocked[goal] != 0) return false;

  const uint32_t n = field.width * field.height;
  for (uint32_t i = 0; i < n; i++) field.cost[i] = kFlowFieldBlocked;

  // (1) Integrasyon alani: hedeften GERIYE dogru genislik-oncelikli arama.
  // Tum adimlar ESIT maliyetli oldugu icin BFS == Dijkstra: bir hucreye ILK
  // varis ZATEN en kisadir, tekrar ziyaret GEREKMEZ.
  static constexpr int32_t kDx4[4] = {1, -1, 0, 0};
  static constexpr int32_t kDz4[4] = {0, 0, 1, -1};
  uint32_t head = 0, tail = 0;
  field.cost[goal] = 0;
  field.queue[tail++] = goal;
  while (head < tail) {
    const uint32_t cur = field.queue[head++];
    const uint32_t cx = cur % field.width, cz = cur / field.width;
    const uint16_t next_cost = (uint16_t)(field.cost[cur] + 1);
    if (next_cost >= kFlowFieldBlocked) continue; // tasma korumasi (cok buyuk izgara)
    for (int k = 0; k < 4; k++) {
      const int64_t nx = (int64_t)cx + kDx4[k], nz = (int64_t)cz + kDz4[k];
      if (nx < 0 || nz < 0 || nx >= (int64_t)field.width || nz >= (int64_t)field.height) continue;
      const uint32_t ni = field.index((uint32_t)nx, (uint32_t)nz);
      if (field.blocked[ni] != 0) continue;
      if (field.cost[ni] != kFlowFieldBlocked) continue; // ziyaret edildi
      field.cost[ni] = next_cost;
      field.queue[tail++] = ni;
    }
  }

  // (2) Akis alani: her hucreden EN DUSUK maliyetli komsuya birim yon.
  // 8 komsu (capraz hareket daha duzgun gorunur), ama KOSE KESME YASAK:
  // capraza ancak IKI ortogonal komsu da gecilebilirse gidilir (aksi halde
  // ajanlar iki duvarin arasindan sizardi -- klasik izgara-yol-bulma kurali).
  static constexpr int32_t kDx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
  static constexpr int32_t kDz8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  for (uint32_t z = 0; z < field.height; z++) {
    for (uint32_t x = 0; x < field.width; x++) {
      const uint32_t i = field.index(x, z);
      field.flow[i] = Vec3{0, 0, 0};
      if (field.blocked[i] != 0) continue;
      if (field.cost[i] == kFlowFieldBlocked) continue; // hedefe ulasilamiyor
      if (field.cost[i] == 0) continue;                 // HEDEFIN kendisi: yon yok

      uint16_t best_cost = field.cost[i];
      int32_t best_dx = 0, best_dz = 0;
      for (int k = 0; k < 8; k++) {
        const int64_t nx = (int64_t)x + kDx8[k], nz = (int64_t)z + kDz8[k];
        if (nx < 0 || nz < 0 || nx >= (int64_t)field.width || nz >= (int64_t)field.height) continue;
        const uint32_t ni = field.index((uint32_t)nx, (uint32_t)nz);
        if (field.blocked[ni] != 0) continue;
        if (field.cost[ni] >= best_cost) continue;
        if (kDx8[k] != 0 && kDz8[k] != 0) { // capraz: kose kesme kontrolu
          const uint32_t side_x = field.index((uint32_t)nx, z);
          const uint32_t side_z = field.index(x, (uint32_t)nz);
          if (field.blocked[side_x] != 0 || field.blocked[side_z] != 0) continue;
        }
        best_cost = field.cost[ni];
        best_dx = kDx8[k];
        best_dz = kDz8[k];
      }
      if (best_dx != 0 || best_dz != 0)
        field.flow[i] = unit_or_default(Vec3{(float)best_dx, 0.0f, (float)best_dz});
    }
  }
  return true;
}

Vec3 sample_flow(const FlowField &field, Vec3 world_pos) {
  if (!field.flow || field.cell_size <= 0.0f) return {0, 0, 0};
  const float fx = (world_pos.x - field.origin.x) / field.cell_size;
  const float fz = (world_pos.z - field.origin.z) / field.cell_size;
  if (fx < 0.0f || fz < 0.0f) return {0, 0, 0};
  const uint32_t x = (uint32_t)fx, z = (uint32_t)fz;
  if (x >= field.width || z >= field.height) return {0, 0, 0};
  return field.flow[field.index(x, z)];
}

} // namespace tulpar::engine::sim
