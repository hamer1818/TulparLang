#include "sim/quantum_gen.hpp"

namespace tulpar::engine::sim {

namespace {

uint32_t popcount32(uint32_t v) {
  // Kernighan: her adim EN SAGDAKI 1 bitini siler -- derleyici icsel
  // fonksiyonuna (popcnt) bagimli DEGIL, her platformda AYNI sonuc.
  uint32_t n = 0;
  while (v) { v &= v - 1; n++; }
  return n;
}

uint32_t lowest_bit_index(uint32_t v) {
  uint32_t i = 0;
  while ((v & 1u) == 0u) { v >>= 1; i++; }
  return i;
}

// dir yonundeki komsunun koordinati; izgara disi ise false.
bool neighbor_of(const WfcGrid &g, uint32_t x, uint32_t y, WfcDir dir, uint32_t *nx, uint32_t *ny) {
  switch (dir) {
    case WfcDir::kRight: if (x + 1 >= g.width) return false;  *nx = x + 1; *ny = y; return true;
    case WfcDir::kLeft:  if (x == 0) return false;            *nx = x - 1; *ny = y; return true;
    case WfcDir::kDown:  if (y + 1 >= g.height) return false; *nx = x;     *ny = y + 1; return true;
    case WfcDir::kUp:    if (y == 0) return false;            *nx = x;     *ny = y - 1; return true;
  }
  return false;
}

WfcDir opposite(WfcDir d) {
  switch (d) {
    case WfcDir::kRight: return WfcDir::kLeft;
    case WfcDir::kLeft:  return WfcDir::kRight;
    case WfcDir::kDown:  return WfcDir::kUp;
    case WfcDir::kUp:    return WfcDir::kDown;
  }
  return WfcDir::kRight;
}

} // namespace

bool wfc_rules_symmetric(const WfcRules &rules) {
  if (rules.tile_count == 0 || rules.tile_count > kWfcMaxTiles) return false;
  for (uint32_t d = 0; d < 4; d++) {
    const uint32_t od = (uint32_t)opposite((WfcDir)d);
    for (uint32_t a = 0; a < rules.tile_count; a++) {
      for (uint32_t b = 0; b < rules.tile_count; b++) {
        const bool a_allows_b = (rules.allowed[d][a] & (1u << b)) != 0;
        const bool b_allows_a = (rules.allowed[od][b] & (1u << a)) != 0;
        if (a_allows_b != b_allows_a) return false;
      }
    }
  }
  return true;
}

uint32_t wfc_tile_at(const WfcGrid &grid, uint32_t x, uint32_t y) {
  if (!grid.cells || x >= grid.width || y >= grid.height) return UINT32_MAX;
  const uint32_t mask = grid.cells[grid.index(x, y)];
  if (popcount32(mask) != 1) return UINT32_MAX; // henuz cokmemis ya da celiski
  return lowest_bit_index(mask);
}

WfcResult wfc_solve(WfcGrid &grid, const WfcRules &rules, Rng &rng) {
  WfcResult res;
  if (!grid.cells || !grid.stack || grid.width == 0 || grid.height == 0) return res;
  if (rules.tile_count == 0 || rules.tile_count > kWfcMaxTiles) return res;

  const uint32_t n = grid.width * grid.height;
  const uint32_t full = rules.tile_count >= 32 ? 0xFFFFFFFFu : ((1u << rules.tile_count) - 1u);

  // Baslangic: her hucre TUM doselerin ust ustelenmesi.
  for (uint32_t i = 0; i < n; i++) grid.cells[i] = full;

  for (;;) {
    // (1) GOZLEM: en dusuk "entropi" (en az olasilik, ama >1) hucreyi sec.
    // Klasik WFC sezgisi: en kisitli yerden ilerle -> celiski olasiligi azalir.
    uint32_t best = UINT32_MAX;
    uint32_t best_options = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t opts = popcount32(grid.cells[i]);
      if (opts == 0) { res.contradiction = true; return res; }
      if (opts == 1) continue; // zaten cokmus
      if (opts < best_options) { best_options = opts; best = i; }
    }
    if (best == UINT32_MAX) { // cokmemis hucre kalmadi
      res.solved = true;
      return res;
    }

    // (2) COKUS: olasiliklardan birini DETERMINISTIK rng ile sec.
    const uint32_t mask = grid.cells[best];
    const uint32_t pick = (uint32_t)rng.next_range(0, (int32_t)best_options);
    uint32_t seen = 0, chosen = 0;
    for (uint32_t t = 0; t < rules.tile_count; t++) {
      if ((mask & (1u << t)) == 0u) continue;
      if (seen == pick) { chosen = t; break; }
      seen++;
    }
    grid.cells[best] = 1u << chosen;
    res.collapsed++;

    // (3) YAYILIM: degisen hucreden komsulara kisit yay (yigin tabanli,
    // ozyineleme YOK -- derin izgarada yigin tasmasi olmaz).
    uint32_t top = 0;
    grid.stack[top++] = best;
    while (top > 0) {
      const uint32_t cur = grid.stack[--top];
      const uint32_t cx = cur % grid.width, cy = cur / grid.width;
      const uint32_t cur_mask = grid.cells[cur];

      for (uint32_t d = 0; d < 4; d++) {
        uint32_t nx = 0, ny = 0;
        if (!neighbor_of(grid, cx, cy, (WfcDir)d, &nx, &ny)) continue;
        const uint32_t ni = grid.index(nx, ny);

        // Bu hucrenin KALAN olasiliklarina gore, komsuda IZIN VERILEN
        // doselerin birlesimi.
        uint32_t permitted = 0;
        for (uint32_t t = 0; t < rules.tile_count; t++) {
          if ((cur_mask & (1u << t)) == 0u) continue;
          permitted |= rules.allowed[d][t];
        }

        const uint32_t before = grid.cells[ni];
        const uint32_t after = before & permitted;
        if (after == before) continue; // degisiklik yok -> yaymaya gerek yok
        if (after == 0) { res.contradiction = true; return res; }
        grid.cells[ni] = after;
        if (top < n) grid.stack[top++] = ni;
      }
    }
  }
}

} // namespace tulpar::engine::sim
