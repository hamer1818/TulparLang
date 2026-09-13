#include "core/memory/arena.hpp"

#include "platform/fatal.hpp"
#include "platform/memory.hpp"

#include <cstring>

namespace tulpar::engine {

#if defined(ENGINE_MEM_CANARY)
namespace {
constexpr uint32_t kHeaderMagic = 0xA11C0DEDu;
constexpr uint64_t kCanary = 0xC0FFEEC0FFEE5AFEull;
struct BlockHeader {
  uint32_t magic;
  uint32_t size;        // kullanici boyutu
  uint32_t prev_header; // onceki blogun basligi (offset), yoksa UINT32_MAX
  uint32_t pad;
};
static_assert(sizeof(BlockHeader) == 16, "baslik 16 bayt olmali");
} // namespace
#endif

void Arena::init(void *base, size_t capacity, const char *name,
                 OverflowPolicy policy) {
  base_ = static_cast<uint8_t *>(base);
  capacity_ = capacity;
  used_ = 0;
  name_ = name ? name : "?";
  policy_ = policy;
  stats_ = MemoryStats{};
  stats_.capacity = capacity;
#if defined(ENGINE_MEM_CANARY)
  last_header_ = UINT32_MAX;
#endif
}

void *Arena::alloc(size_t size, size_t align) {
  ENGINE_ASSERT_MSG(base_ != nullptr, "arena '%s' init edilmedi", name_);
  ENGINE_ASSERT_MSG((align & (align - 1)) == 0, "arena '%s': hizalama 2^n olmali",
                    name_);
#if defined(ENGINE_MEM_CANARY)
  // [pad][BlockHeader][blok (align'li)][kanarya 8B]
  size_t hdr_at = align_up(used_ + sizeof(BlockHeader), align) - sizeof(BlockHeader);
  size_t blk_at = hdr_at + sizeof(BlockHeader);
  size_t end = blk_at + size + sizeof(uint64_t);
#else
  size_t blk_at = align_up(used_, align);
  size_t end = blk_at + size;
#endif
  if (end > capacity_ || end < used_) {
    stats_.overflow_count++;
    if (policy_ == OverflowPolicy::Fatal)
      platform::fatal("arena '%s' TASTI: istek %zu B (hizalama %zu), kullanim "
                      "%zu / %zu B. Kapasite build'de hesaplanmali (A2).",
                      name_, size, align, used_, capacity_);
    return nullptr;
  }
#if defined(ENGINE_MEM_CANARY)
  BlockHeader *h = reinterpret_cast<BlockHeader *>(base_ + hdr_at);
  h->magic = kHeaderMagic;
  h->size = (uint32_t)size;
  h->prev_header = last_header_;
  h->pad = 0;
  std::memcpy(base_ + blk_at + size, &kCanary, sizeof(kCanary));
  last_header_ = (uint32_t)hdr_at;
#endif
  used_ = end;
  stats_.used = used_;
  if (used_ > stats_.peak) stats_.peak = used_;
  stats_.alloc_count++;
  return base_ + blk_at;
}

void *Arena::alloc_zeroed(size_t size, size_t align) {
  void *p = alloc(size, align);
  if (p) std::memset(p, 0, size);
  return p;
}

bool Arena::carve(Arena &child, size_t capacity, const char *name,
                  OverflowPolicy policy) {
  void *p = alloc(capacity, platform::os_page_size() < 64 ? 64 : 64);
  if (!p) return false;
  child.init(p, capacity, name, policy);
  return true;
}

void Arena::reset_to(size_t m) {
  ENGINE_ASSERT_MSG(m <= used_, "arena '%s': isaret (%zu) kullanimin (%zu) otesinde",
                    name_, m, used_);
  used_ = m;
  stats_.used = used_;
  stats_.reset_count++;
#if defined(ENGINE_MEM_CANARY)
  // Isaretin gerisindeki basliklara geri sar.
  while (last_header_ != UINT32_MAX && last_header_ >= m) {
    const BlockHeader *h = reinterpret_cast<const BlockHeader *>(base_ + last_header_);
    last_header_ = h->prev_header;
  }
#endif
}

bool Arena::check() const {
#if defined(ENGINE_MEM_CANARY)
  uint32_t off = last_header_;
  while (off != UINT32_MAX) {
    const BlockHeader *h = reinterpret_cast<const BlockHeader *>(base_ + off);
    if (h->magic != kHeaderMagic) return false;
    uint64_t c;
    std::memcpy(&c, base_ + off + sizeof(BlockHeader) + h->size, sizeof(c));
    if (c != kCanary) return false;
    off = h->prev_header;
  }
#endif
  return true;
}

bool SystemArena::reserve(size_t capacity, const char *name) {
  size_t page = platform::os_page_size();
  os_bytes_ = align_up(capacity, page);
  os_base_ = platform::os_reserve(os_bytes_);
  if (!os_base_) {
    os_bytes_ = 0;
    return false;
  }
  init(os_base_, capacity, name, OverflowPolicy::Fatal);
  return true;
}

void SystemArena::release() {
  if (os_base_) platform::os_release(os_base_, os_bytes_);
  os_base_ = nullptr;
  os_bytes_ = 0;
  init(nullptr, 0, name(), OverflowPolicy::Fatal);
}

SystemArena::~SystemArena() { release(); }

} // namespace tulpar::engine
