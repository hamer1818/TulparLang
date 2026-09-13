// L1 CORE — Pool<T>: sabit boyutlu, serbest listeli, NESIL ETIKETLI slotmap.
// Ham isaretci degil Handle dagitilir; slot yeniden kullanilinca nesil artar
// ve eski handle sessizce yanlis nesneye degil, `get() == nullptr`a duser.
// (scene3d'nin `_e3gen[slot]*1048576+slot` deseninin C++ karsiligi.)
#pragma once
#include <cstdint>
#include <new>
#include <utility>

#include "core/memory/arena.hpp"
#include "platform/fatal.hpp"

namespace tulpar::engine {

struct Handle {
  uint64_t bits = 0; // ust 32: nesil (0 = gecersiz), alt 32: slot
  static Handle make(uint32_t index, uint32_t gen) {
    Handle h;
    h.bits = ((uint64_t)gen << 32) | index;
    return h;
  }
  static Handle invalid() { return Handle{}; }
  uint32_t index() const { return (uint32_t)(bits & 0xFFFFFFFFu); }
  uint32_t generation() const { return (uint32_t)(bits >> 32); }
  bool valid() const { return generation() != 0; }
  bool operator==(Handle o) const { return bits == o.bits; }
  bool operator!=(Handle o) const { return bits != o.bits; }
};

template <class T> class Pool {
public:
  bool init(Arena &arena, uint32_t capacity, const char *name) {
    name_ = name;
    capacity_ = capacity;
    slots_ = static_cast<T *>(arena.alloc(sizeof(T) * capacity, alignof(T) > 16 ? alignof(T) : 16));
    gens_ = arena.alloc_array_zeroed<uint32_t>(capacity);
    next_free_ = arena.alloc_array<uint32_t>(capacity);
    alive_ = arena.alloc_array_zeroed<uint8_t>(capacity);
    if (!slots_ || !gens_ || !next_free_ || !alive_) return false;
    for (uint32_t i = 0; i < capacity; i++) {
      gens_[i] = 1; // nesil 0 = gecersiz handle
      next_free_[i] = i + 1;
    }
    free_head_ = 0;
    live_count_ = 0;
    return true;
  }

  template <class... Args> Handle create(Args &&...args) {
    if (free_head_ >= capacity_) return Handle::invalid(); // dolu: cagiran karar verir
    uint32_t i = free_head_;
    free_head_ = next_free_[i];
    new (&slots_[i]) T(std::forward<Args>(args)...);
    alive_[i] = 1;
    live_count_++;
    return Handle::make(i, gens_[i]);
  }

  T *get(Handle h) {
    uint32_t i = h.index();
    if (!h.valid() || i >= capacity_ || !alive_[i] || gens_[i] != h.generation())
      return nullptr;
    return &slots_[i];
  }
  const T *get(Handle h) const { return const_cast<Pool *>(this)->get(h); }
  bool alive(Handle h) const { return get(h) != nullptr; }

  void destroy(Handle h) {
    T *p = get(h);
    ENGINE_ASSERT_MSG(p != nullptr, "pool '%s': bayat/gecersiz handle yok edilmeye calisildi", name_);
    p->~T();
    uint32_t i = h.index();
    alive_[i] = 0;
    gens_[i]++;
    if (gens_[i] == 0) gens_[i] = 1; // sarma: 0 gecersizdir
    next_free_[i] = free_head_;
    free_head_ = i;
    live_count_--;
  }

  uint32_t capacity() const { return capacity_; }
  uint32_t live_count() const { return live_count_; }

  // Canli slotlar uzerinde gezinti (sira slot sirasi).
  template <class F> void each(F &&f) {
    for (uint32_t i = 0; i < capacity_; i++)
      if (alive_[i]) f(Handle::make(i, gens_[i]), slots_[i]);
  }

private:
  const char *name_ = "?";
  T *slots_ = nullptr;
  uint32_t *gens_ = nullptr;
  uint32_t *next_free_ = nullptr;
  uint8_t *alive_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t free_head_ = 0;
  uint32_t live_count_ = 0;
};

} // namespace tulpar::engine
