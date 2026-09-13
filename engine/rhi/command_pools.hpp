// L2 RHI — thread basina komut havuzu (plan L2: "command buffer'lar paralel
// kaydedilir"). Her worker'in (ve ana thread'in) kendi VkCommandPool'u var;
// tamponlar init'te ONCEDEN ayrilir (A2), kare basinda havuz sifirlanir
// (vkResetCommandPool: ucuz, tek cagri). Bir job kaydi `wait()` ile
// BOLUNMEMELI: bekleme sonrasi fiber baska thread'de devam eder ve baska
// bir havuza duser (yarissiz ama tampon yanlis havuzdan olur).
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::rhi {

class CommandPools {
public:
  // thread_count = worker sayisi + 1 (son yuva ana thread).
  bool init(Device &dev, Arena &arena, uint32_t thread_count, uint32_t secondaries_per_thread);
  void shutdown();
  // Kare basi: butun havuzlar sifirlanir (onceki karenin GPU isi bitmis olmali).
  void begin_frame();
  // Su anki thread'in yuvasindan bir ikincil tampon; havuz tukendiyse NULL
  // (kapasite init'te — sessiz buyume yok).
  VkCommandBuffer acquire_secondary();
  uint32_t slot_for_current_thread() const;
  uint32_t thread_count() const { return thread_count_; }
  uint32_t used_in_slot(uint32_t slot) const { return used_[slot]; }
  uint32_t total_used() const;

private:
  Device *dev_ = nullptr;
  uint32_t thread_count_ = 0;
  uint32_t per_thread_ = 0;
  VkCommandPool *pools_ = nullptr;
  VkCommandBuffer *buffers_ = nullptr; // [thread][i]
  uint32_t *used_ = nullptr;           // [thread]
};

} // namespace tulpar::engine::rhi
