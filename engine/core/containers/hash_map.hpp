// L1 CORE — HashMap<K,V>: acik adresleme (dogrusal sondalama), sabit
// kapasite (arenadan, init'te), buyumez; %75 doluluk sinirinda insert false
// doner. Silme geri-kaydirma ile (mezar tasi yok). K == ile kiyaslanir,
// hash fonksiyonu sablon parametresi (varsayilan: tamsayi karistirici).
#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "core/memory/arena.hpp"
#include "platform/fatal.hpp"

namespace tulpar::engine {

struct IntHash {
  uint64_t operator()(uint64_t k) const {
    k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ull; k ^= k >> 33;
    return k;
  }
};

template <class K, class V, class Hash = IntHash> class HashMap {
public:
  // capacity 2^n'e yukari yuvarlanir; kullanilabilir eleman ~ %75'i.
  bool init(Arena &arena, size_t capacity) {
    size_t c = 16;
    while (c < capacity) c <<= 1;
    cap_ = c;
    mask_ = c - 1;
    keys_ = arena.alloc_array<K>(c);
    vals_ = arena.alloc_array<V>(c);
    used_ = arena.alloc_array_zeroed<uint8_t>(c);
    n_ = 0;
    return keys_ && vals_ && used_;
  }
  size_t size() const { return n_; }
  size_t capacity() const { return cap_; }
  size_t limit() const { return cap_ - cap_ / 4; }

  bool insert(const K &k, const V &v) {
    size_t i = hash_(k) & mask_;
    for (;;) {
      if (!used_[i]) {
        if (n_ >= limit()) return false; // dolu: cagiran karar verir
        new (&keys_[i]) K(k);
        new (&vals_[i]) V(v);
        used_[i] = 1;
        n_++;
        return true;
      }
      if (keys_[i] == k) { vals_[i] = v; return true; }
      i = (i + 1) & mask_;
    }
  }
  V *find(const K &k) {
    size_t i = hash_(k) & mask_;
    while (used_[i]) {
      if (keys_[i] == k) return &vals_[i];
      i = (i + 1) & mask_;
    }
    return nullptr;
  }
  const V *find(const K &k) const { return const_cast<HashMap *>(this)->find(k); }
  bool contains(const K &k) const { return find(k) != nullptr; }
  bool erase(const K &k) {
    size_t i = hash_(k) & mask_;
    while (used_[i]) {
      if (keys_[i] == k) {
        keys_[i].~K();
        vals_[i].~V();
        used_[i] = 0;
        n_--;
        // geri kaydirma: sonraki kumeyi kapat
        size_t j = (i + 1) & mask_;
        while (used_[j]) {
          size_t home = hash_(keys_[j]) & mask_;
          // j'deki eleman i'ye tasinabilir mi? (home, i'nin "gerisinde" mi)
          bool move = ((i <= j) ? (home <= i || home > j) : (home <= i && home > j));
          if (move) {
            new (&keys_[i]) K(std::move(keys_[j]));
            new (&vals_[i]) V(std::move(vals_[j]));
            keys_[j].~K();
            vals_[j].~V();
            used_[i] = 1;
            used_[j] = 0;
            i = j;
          }
          j = (j + 1) & mask_;
        }
        return true;
      }
      i = (i + 1) & mask_;
    }
    return false;
  }
  template <class F> void each(F &&f) {
    for (size_t i = 0; i < cap_; i++)
      if (used_[i]) f(keys_[i], vals_[i]);
  }

private:
  Hash hash_;
  K *keys_ = nullptr;
  V *vals_ = nullptr;
  uint8_t *used_ = nullptr;
  size_t cap_ = 0, mask_ = 0, n_ = 0;
};

} // namespace tulpar::engine
