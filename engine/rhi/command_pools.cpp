#include "rhi/command_pools.hpp"

#include "core/jobs/job_system.hpp"

namespace tulpar::engine::rhi {

bool CommandPools::init(Device &dev, Arena &arena, uint32_t thread_count, uint32_t per_thread) {
  dev_ = &dev;
  thread_count_ = thread_count;
  per_thread_ = per_thread;
  pools_ = arena.alloc_array_zeroed<VkCommandPool>(thread_count);
  buffers_ = arena.alloc_array_zeroed<VkCommandBuffer>((size_t)thread_count * per_thread);
  used_ = arena.alloc_array_zeroed<uint32_t>(thread_count);
  if (!pools_ || !buffers_ || !used_) return false;
  VkApi &a = dev.api();
  for (uint32_t t = 0; t < thread_count; t++) {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // kisa omurlu, havuz sifirlamali
    ci.queueFamilyIndex = dev.queue_family();
    if (a.vkCreateCommandPool(dev.handle(), &ci, nullptr, &pools_[t]) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pools_[t];
    ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    ai.commandBufferCount = per_thread;
    if (a.vkAllocateCommandBuffers(dev.handle(), &ai, &buffers_[(size_t)t * per_thread]) != VK_SUCCESS)
      return false;
  }
  return true;
}

void CommandPools::shutdown() {
  if (!dev_) return;
  for (uint32_t t = 0; t < thread_count_; t++)
    if (pools_[t]) dev_->api().vkDestroyCommandPool(dev_->handle(), pools_[t], nullptr);
  dev_ = nullptr;
}

void CommandPools::begin_frame() {
  for (uint32_t t = 0; t < thread_count_; t++) {
    if (used_[t]) dev_->api().vkResetCommandPool(dev_->handle(), pools_[t], 0);
    used_[t] = 0;
  }
}

uint32_t CommandPools::slot_for_current_thread() const {
  uint32_t w = JobSystem::current_worker();
  return w == UINT32_MAX ? thread_count_ - 1 : w;
}

VkCommandBuffer CommandPools::acquire_secondary() {
  uint32_t t = slot_for_current_thread();
  if (t >= thread_count_ || used_[t] >= per_thread_) return VK_NULL_HANDLE;
  return buffers_[(size_t)t * per_thread_ + used_[t]++];
}

uint32_t CommandPools::total_used() const {
  uint32_t n = 0;
  for (uint32_t t = 0; t < thread_count_; t++) n += used_[t];
  return n;
}

} // namespace tulpar::engine::rhi
