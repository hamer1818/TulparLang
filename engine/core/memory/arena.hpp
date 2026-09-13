// L1 CORE — Arena (dogrusal ayirici). Tasarim Aksiyomu A2: kare icinde
// allocation yok. Dort arena tipi (SystemArena / SceneArena / FrameArena /
// Pool) hepsi bunun ustune; hicbiri BUYUMEZ, kapasite verilir, dolarsa bu bir
// icerik hatasidir (build'de hesaplanmali) ve GORUNUR olur: overflow sayilir,
// hard modda fatal.
//
// Kanarya: ENGINE_MEM_CANARY tanimliysa her blogun onune baslik, arkasina
// 8 baytlik kanarya yazilir; `check()` geriye dogru yuruyup tasmayi bulur.
// Testlerde ACIK. Ship'te kapali (sifir ek yuk).
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine {

struct MemoryStats {
  size_t used = 0;
  size_t peak = 0;
  size_t capacity = 0;
  uint32_t alloc_count = 0;
  uint32_t overflow_count = 0;
  uint32_t reset_count = 0;
};

enum class OverflowPolicy : uint8_t {
  ReturnNull, // alloc nullptr doner, sayac artar (cagiran karar verir)
  Fatal       // aninda fatal (varsayilan: sessiz bozulma yasak)
};

inline size_t align_up(size_t v, size_t a) { return (v + (a - 1)) & ~(a - 1); }

class Arena {
public:
  static constexpr size_t kDefaultAlign = 16;

  Arena() = default;
  Arena(const Arena &) = delete;
  Arena &operator=(const Arena &) = delete;

  // `base` cagiranin: OS'tan (SystemArena) ya da ust arenadan (alt arena).
  void init(void *base, size_t capacity, const char *name,
            OverflowPolicy policy = OverflowPolicy::Fatal);

  void *alloc(size_t size, size_t align = kDefaultAlign);
  void *alloc_zeroed(size_t size, size_t align = kDefaultAlign);

  template <class T> T *alloc_array(size_t n) {
    return static_cast<T *>(alloc(sizeof(T) * n, alignof(T) > kDefaultAlign
                                                      ? alignof(T)
                                                      : kDefaultAlign));
  }
  template <class T> T *alloc_array_zeroed(size_t n) {
    return static_cast<T *>(
        alloc_zeroed(sizeof(T) * n,
                     alignof(T) > kDefaultAlign ? alignof(T) : kDefaultAlign));
  }

  // Alt arena: bu arenadan `capacity` kadar yer ayirip `child`i ona kurar.
  bool carve(Arena &child, size_t capacity, const char *name,
             OverflowPolicy policy = OverflowPolicy::Fatal);

  // Isaret/geri sar: gecici ayirmalar icin (FrameArena her kare reset()).
  size_t mark() const { return used_; }
  void reset_to(size_t m);
  void reset() { reset_to(0); }

  const MemoryStats &stats() const { return stats_; }
  const char *name() const { return name_; }
  size_t capacity() const { return capacity_; }
  size_t used() const { return used_; }
  size_t remaining() const { return capacity_ - used_; }
  bool contains(const void *p) const {
    const uint8_t *q = static_cast<const uint8_t *>(p);
    return q >= base_ && q < base_ + capacity_;
  }

  // Kanarya modunda: butun bloklar saglam mi? (kapaliyken hep true)
  bool check() const;

private:
  uint8_t *base_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
  const char *name_ = "?";
  OverflowPolicy policy_ = OverflowPolicy::Fatal;
  MemoryStats stats_{};
#if defined(ENGINE_MEM_CANARY)
  uint32_t last_header_ = UINT32_MAX; // son blogun basligi (offset)
#endif
};

// Roller: tipler ayni, adlar niyet. Plan §3/L1'deki dort tip.
//   SystemArena — uygulama omru, boot'ta tek seferlik OS rezervi.
//   SceneArena  — sahne omru, sahne degisince komple reset.
//   FrameArena  — 1 kare, her kare reset; free CAGRILMAZ.
class SystemArena : public Arena {
public:
  // OS'tan `capacity` rezerve eder. Yalniz acilista.
  bool reserve(size_t capacity, const char *name = "system");
  void release();
  ~SystemArena();
private:
  void *os_base_ = nullptr;
  size_t os_bytes_ = 0;
};

class SceneArena : public Arena {};

class FrameArena : public Arena {
public:
  void begin_frame() {
    reset();
    frame_++;
  }
  uint64_t frame() const { return frame_; }
private:
  uint64_t frame_ = 0;
};

} // namespace tulpar::engine
