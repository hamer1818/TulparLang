#include "core/math/bvh.hpp"

#include <algorithm>
#include <limits>

namespace tulpar::engine {

bool Bvh::build(Arena &arena, const Aabb *item_bounds, uint32_t item_count, uint32_t leaf_threshold) {
  node_count_ = 0;
  if (item_count == 0) return true; // bos agac: empty()==true, raycast_closest HER ZAMAN false doner
  if (leaf_threshold == 0) leaf_threshold = 1;

  const uint32_t node_capacity = 2 * item_count; // guvenli ust sinir (tam ikili agac, L=1 en kotu durum)
  nodes_ = arena.alloc_array_zeroed<BvhNode>(node_capacity); // sifirlanmis: kullanilmayan alanlar HICBIR ZAMAN okunmaz ama garanti olsun
  item_order_ = arena.alloc_array<uint32_t>(item_count);
  scratch_ = arena.alloc_array<uint32_t>(item_count);
  prefix_area_ = arena.alloc_array<float>(item_count);
  suffix_area_ = arena.alloc_array<float>(item_count);
  if (!nodes_ || !item_order_ || !scratch_ || !prefix_area_ || !suffix_area_) return false;

  for (uint32_t i = 0; i < item_count; i++) item_order_[i] = i;
  build_recursive(item_bounds, item_order_, 0, item_count, leaf_threshold);
  return true;
}

uint32_t Bvh::build_recursive(const Aabb *item_bounds, uint32_t *order, uint32_t start, uint32_t end,
                               uint32_t leaf_threshold) {
  const uint32_t node_idx = node_count_++;

  Aabb bounds{};
  for (uint32_t i = start; i < end; i++) bounds = merge(bounds, item_bounds[order[i]]);
  nodes_[node_idx].bounds = bounds;

  const uint32_t count = end - start;
  if (count <= leaf_threshold) {
    nodes_[node_idx].left = UINT32_MAX;
    nodes_[node_idx].right = UINT32_MAX;
    nodes_[node_idx].first = start;
    nodes_[node_idx].count = count;
    return node_idx;
  }

  int best_axis = 0;
  uint32_t best_split = 0;
  find_best_sah_split(item_bounds, order, start, end, &best_axis, &best_split);

  // Kazanan ekseni NIHAI olarak order[] icine yaz (find_best_sah_split
  // yalniz MALIYET hesapladi, order[]'i DEGISTIRMEDI).
  for (uint32_t i = 0; i < count; i++) scratch_[i] = order[start + i];
  std::stable_sort(scratch_, scratch_ + count, [&](uint32_t a, uint32_t b) {
    const Vec3 ca = center(item_bounds[a]);
    const Vec3 cb = center(item_bounds[b]);
    const float va = best_axis == 0 ? ca.x : (best_axis == 1 ? ca.y : ca.z);
    const float vb = best_axis == 0 ? cb.x : (best_axis == 1 ? cb.y : cb.z);
    return va < vb;
  });
  for (uint32_t i = 0; i < count; i++) order[start + i] = scratch_[i];

  const uint32_t mid = start + best_split;
  const uint32_t left = build_recursive(item_bounds, order, start, mid, leaf_threshold);
  const uint32_t right = build_recursive(item_bounds, order, mid, end, leaf_threshold);
  nodes_[node_idx].left = left;
  nodes_[node_idx].right = right;
  return node_idx;
}

void Bvh::find_best_sah_split(const Aabb *item_bounds, const uint32_t *order, uint32_t start, uint32_t end,
                               int *out_axis, uint32_t *out_split) {
  const uint32_t count = end - start;
  float best_cost = std::numeric_limits<float>::max();
  int best_axis = 0;
  uint32_t best_split = 1;

  for (int axis = 0; axis < 3; axis++) {
    for (uint32_t i = 0; i < count; i++) scratch_[i] = order[start + i];
    std::stable_sort(scratch_, scratch_ + count, [&](uint32_t a, uint32_t b) {
      const Vec3 ca = center(item_bounds[a]);
      const Vec3 cb = center(item_bounds[b]);
      const float va = axis == 0 ? ca.x : (axis == 1 ? ca.y : ca.z);
      const float vb = axis == 0 ? cb.x : (axis == 1 ? cb.y : cb.z);
      return va < vb;
    });

    // prefix_area_[i]: scratch_[0..i] KAPSAYICI birlesimin yuzey alani.
    Aabb acc{};
    for (uint32_t i = 0; i < count; i++) {
      acc = merge(acc, item_bounds[scratch_[i]]);
      prefix_area_[i] = surface_area(acc);
    }
    // suffix_area_[i]: scratch_[i..count-1] KAPSAYICI birlesimin yuzey alani.
    acc = Aabb{};
    for (uint32_t i = count; i-- > 0;) {
      acc = merge(acc, item_bounds[scratch_[i]]);
      suffix_area_[i] = surface_area(acc);
    }
    // bolme i: sol = scratch_[0..i) (i oge), sag = scratch_[i..count) (count-i oge).
    for (uint32_t i = 1; i < count; i++) {
      const float cost = (float)i * prefix_area_[i - 1] + (float)(count - i) * suffix_area_[i];
      if (cost < best_cost) {
        best_cost = cost;
        best_axis = axis;
        best_split = i;
      }
    }
  }
  *out_axis = best_axis;
  *out_split = best_split;
}

void Bvh::raycast_node(const Aabb *item_bounds, uint32_t node_idx, Ray ray, uint32_t *best_item,
                        float *best_t) const {
  const BvhNode &node = nodes_[node_idx];
  float t;
  if (!intersect(ray, node.bounds, &t)) return;      // bu dugumun kutusuna hic girmiyor
  if (t > *best_t) return;                            // kutunun EN YAKIN noktasi bile mevcut en-iyiden UZAK -- BUDA

  if (node.left == UINT32_MAX) { // yaprak
    for (uint32_t i = 0; i < node.count; i++) {
      const uint32_t item = item_order_[node.first + i];
      float item_t;
      if (intersect(ray, item_bounds[item], &item_t) && item_t < *best_t) {
        *best_t = item_t;
        *best_item = item;
      }
    }
    return;
  }
  raycast_node(item_bounds, node.left, ray, best_item, best_t);
  raycast_node(item_bounds, node.right, ray, best_item, best_t);
}

bool Bvh::raycast_closest(const Aabb *item_bounds, Ray ray, uint32_t *out_item, float *out_t) const {
  if (node_count_ == 0) return false;
  float best_t = std::numeric_limits<float>::max();
  uint32_t best_item = UINT32_MAX;
  raycast_node(item_bounds, 0, ray, &best_item, &best_t);
  if (best_item == UINT32_MAX) return false;
  *out_item = best_item;
  *out_t = best_t;
  return true;
}

} // namespace tulpar::engine
