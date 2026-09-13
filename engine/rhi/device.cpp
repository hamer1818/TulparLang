#include "rhi/device.hpp"

#include <cstdio>
#include <cstring>

namespace tulpar::engine::rhi {

// Dogrulama mesaji: hata SAYILIR (test 0 bekler), ilk 8'i basilir.
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                                     VkDebugUtilsMessageTypeFlagsEXT,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                     void *user) {
  Device *d = static_cast<Device *>(user);
  if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    d->count_validation_error();
    if (d->validation_errors() <= 8)
      std::fprintf(stderr, "[vulkan-validation] %s\n", data && data->pMessage ? data->pMessage : "?");
  }
  return VK_FALSE;
}

void Device::fail(const char *msg, VkResult r) {
  std::snprintf(err_, sizeof err_, "%s (%s)", msg, vk_result_str(r));
}

bool Device::init(Arena &, VkApi &api, const DeviceConfig &cfg) {
  api_ = &api;
  uint32_t loader_version = VK_API_VERSION_1_0;
  if (api.vkEnumerateInstanceVersion) api.vkEnumerateInstanceVersion(&loader_version);
  // Vulkan 1.1+ ister: GetPhysicalDeviceProperties2/Features2 cekirdekte.
  if (loader_version < VK_API_VERSION_1_1) {
    fail("Vulkan loader 1.1 altinda", VK_ERROR_INCOMPATIBLE_DRIVER);
    return false;
  }
  uint32_t want = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3
                  : loader_version >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;

  // MoltenVK: portability enumeration uzantisi + bayragi (yoksa cihaz gorunmez).
  const char *inst_exts[4];
  uint32_t inst_ext_n = 0;
  VkInstanceCreateFlags inst_flags = 0;
  bool have_debug_utils = false;
  {
    uint32_t n = 0;
    api.vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    VkExtensionProperties props[128];
    if (n > 128) n = 128;
    api.vkEnumerateInstanceExtensionProperties(nullptr, &n, props);
    for (uint32_t i = 0; i < n; i++) {
      if (std::strcmp(props[i].extensionName, "VK_KHR_portability_enumeration") == 0) {
        inst_exts[inst_ext_n++] = "VK_KHR_portability_enumeration";
        inst_flags |= 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
      } else if (std::strcmp(props[i].extensionName, "VK_EXT_debug_utils") == 0) {
        have_debug_utils = true;
      }
    }
  }
  // Dogrulama katmani: istenmis ve mevcutsa. Yoksa sessiz degil, caps'te false.
  const char *layers[1];
  uint32_t layer_n = 0;
  if (cfg.validation && api.vkEnumerateInstanceLayerProperties) {
    uint32_t n = 0;
    api.vkEnumerateInstanceLayerProperties(&n, nullptr);
    VkLayerProperties lp[64];
    if (n > 64) n = 64;
    api.vkEnumerateInstanceLayerProperties(&n, lp);
    for (uint32_t i = 0; i < n; i++)
      if (std::strcmp(lp[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
        layers[layer_n++] = "VK_LAYER_KHRONOS_validation";
        if (have_debug_utils) inst_exts[inst_ext_n++] = "VK_EXT_debug_utils";
        caps_.validation_layer = true;
      }
  }
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "tulpar-engine";
  app.pEngineName = "tulpar-engine";
  app.apiVersion = want;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.flags = inst_flags;
  ici.enabledExtensionCount = inst_ext_n;
  ici.ppEnabledExtensionNames = inst_exts;
  ici.enabledLayerCount = layer_n;
  ici.ppEnabledLayerNames = layers;
  VkResult r = api.vkCreateInstance(&ici, nullptr, &instance_);
  if (r != VK_SUCCESS) {
    fail("vkCreateInstance", r);
    return false;
  }
  vk_api_load_instance(api, instance_);
  if (caps_.validation_layer && have_debug_utils && api.vkCreateDebugUtilsMessengerEXT) {
    VkDebugUtilsMessengerCreateInfoEXT mci{};
    mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    mci.pfnUserCallback = debug_callback;
    mci.pUserData = this;
    api.vkCreateDebugUtilsMessengerEXT(instance_, &mci, nullptr, &messenger_);
  }
  if (!pick_physical(cfg)) return false;

  // Kuyruk ailesi: grafik + compute + transfer tek aile (mobilde tipik).
  uint32_t qn = 0;
  api.vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, nullptr);
  VkQueueFamilyProperties qp[16];
  if (qn > 16) qn = 16;
  api.vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, qp);
  queue_family_ = UINT32_MAX;
  for (uint32_t i = 0; i < qn; i++)
    if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
      queue_family_ = i;
      break;
    }
  if (queue_family_ == UINT32_MAX) {
    fail("grafik+compute kuyruk ailesi yok", VK_ERROR_INITIALIZATION_FAILED);
    return false;
  }
  caps_.timestamps = qp[queue_family_].timestampValidBits > 0 && caps_.timestamp_period_ns > 0;

  // Uzantilar (varsa ac).
  const char *dev_exts[8];
  uint32_t dev_ext_n = 0;
  if (caps_.ext_subpass_merge_feedback) dev_exts[dev_ext_n++] = "VK_EXT_subpass_merge_feedback";
  if (caps_.khr_portability_subset) dev_exts[dev_ext_n++] = "VK_KHR_portability_subset";
  // GPL: VK_KHR_pipeline_library + VK_EXT_graphics_pipeline_library + feature.
  VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gplf{};
  gplf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT;
  if (caps_.ext_graphics_pipeline_library) {
    VkPhysicalDeviceFeatures2 q{};
    q.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    q.pNext = &gplf;
    api.vkGetPhysicalDeviceFeatures2(phys_, &q);
    if (gplf.graphicsPipelineLibrary) {
      dev_exts[dev_ext_n++] = "VK_KHR_pipeline_library";
      dev_exts[dev_ext_n++] = "VK_EXT_graphics_pipeline_library";
      caps_.graphics_pipeline_library = true;
    }
  }

  // Feature zinciri: 1.2 (descriptorIndexing, timelineSemaphore, bufferDeviceAddress) [+ GPL].
  VkPhysicalDeviceVulkan12Features f12{};
  f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  if (caps_.graphics_pipeline_library) {
    gplf.graphicsPipelineLibrary = VK_TRUE;
    f12.pNext = &gplf;
  }
  f12.descriptorIndexing = caps_.descriptor_indexing;
  f12.runtimeDescriptorArray = caps_.descriptor_indexing;
  f12.shaderSampledImageArrayNonUniformIndexing = caps_.descriptor_indexing;
  f12.descriptorBindingPartiallyBound = caps_.descriptor_indexing;
  f12.timelineSemaphore = caps_.timeline_semaphore;
  f12.bufferDeviceAddress = caps_.buffer_device_address;
  VkPhysicalDeviceFeatures2 f2{};
  f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  f2.pNext = want >= VK_API_VERSION_1_2 ? &f12 : nullptr;
  f2.features.drawIndirectFirstInstance = VK_FALSE;

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queue_family_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = &f2;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = dev_ext_n;
  dci.ppEnabledExtensionNames = dev_exts;
  r = api.vkCreateDevice(phys_, &dci, nullptr, &device_);
  if (r != VK_SUCCESS) {
    fail("vkCreateDevice", r);
    return false;
  }
  vk_api_load_device(api, device_);
  api.vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  api.vkGetPhysicalDeviceMemoryProperties(phys_, &mem_props_);
  for (uint32_t i = 0; i < mem_props_.memoryTypeCount; i++)
    if (mem_props_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
      caps_.lazily_allocated_memory = true;

  VkCommandPoolCreateInfo cpi{};
  cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = queue_family_;
  r = api.vkCreateCommandPool(device_, &cpi, nullptr, &cmd_pool_);
  if (r != VK_SUCCESS) {
    fail("vkCreateCommandPool", r);
    return false;
  }
  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  api.vkCreateFence(device_, &fci, nullptr, &one_shot_fence_);
  return true;
}

bool Device::pick_physical(const DeviceConfig &cfg) {
  VkApi &api = *api_;
  uint32_t n = 0;
  api.vkEnumeratePhysicalDevices(instance_, &n, nullptr);
  if (n == 0) {
    fail("fiziksel Vulkan cihazi yok (ICD?)", VK_ERROR_INCOMPATIBLE_DRIVER);
    return false;
  }
  VkPhysicalDevice devs[8];
  if (n > 8) n = 8;
  api.vkEnumeratePhysicalDevices(instance_, &n, devs);
  int best = -1, best_score = -1;
  bool want_cpu = cfg.prefer && std::strcmp(cfg.prefer, "cpu") == 0;
  for (uint32_t i = 0; i < n; i++) {
    VkPhysicalDeviceProperties2 p2{};
    p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    api.vkGetPhysicalDeviceProperties2(devs[i], &p2);
    int score = 0;
    switch (p2.properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = want_cpu ? 1 : 4; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = want_cpu ? 1 : 3; break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 2; break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: score = want_cpu ? 5 : 1; break;
    default: score = 0;
    }
    if (p2.properties.apiVersion < VK_API_VERSION_1_1) score = -1;
    if (score > best_score) {
      best_score = score;
      best = (int)i;
    }
  }
  if (best < 0) {
    fail("Vulkan 1.1 destekleyen cihaz yok", VK_ERROR_INCOMPATIBLE_DRIVER);
    return false;
  }
  phys_ = devs[best];

  VkPhysicalDeviceProperties2 p2{};
  p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  api.vkGetPhysicalDeviceProperties2(phys_, &p2);
  std::snprintf(caps_.device_name, sizeof caps_.device_name, "%s", p2.properties.deviceName);
  caps_.api_version = p2.properties.apiVersion;
  caps_.driver_version = p2.properties.driverVersion;
  caps_.vendor_id = p2.properties.vendorID;
  caps_.device_id = p2.properties.deviceID;
  caps_.device_type = p2.properties.deviceType;
  caps_.timestamp_period_ns = p2.properties.limits.timestampPeriod;

  VkPhysicalDeviceVulkan12Features f12{};
  f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  VkPhysicalDeviceFeatures2 f2{};
  f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  if (caps_.api_version >= VK_API_VERSION_1_2) f2.pNext = &f12;
  api.vkGetPhysicalDeviceFeatures2(phys_, &f2);
  caps_.descriptor_indexing = f12.descriptorIndexing && f12.runtimeDescriptorArray;
  caps_.timeline_semaphore = f12.timelineSemaphore;
  caps_.buffer_device_address = f12.bufferDeviceAddress;
  caps_.draw_indirect = true; // cekirdek 1.0: vkCmdDrawIndexedIndirect

  bool has_pipeline_library = false;
  uint32_t en = 0;
  api.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, nullptr);
  VkExtensionProperties ext[512];
  if (en > 512) en = 512;
  api.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, ext);
  for (uint32_t i = 0; i < en; i++) {
    const char *e = ext[i].extensionName;
    if (!std::strcmp(e, "VK_EXT_subpass_merge_feedback")) caps_.ext_subpass_merge_feedback = true;
    else if (!std::strcmp(e, "VK_EXT_graphics_pipeline_library")) caps_.ext_graphics_pipeline_library = true;
    else if (!std::strcmp(e, "VK_KHR_pipeline_library")) has_pipeline_library = true;
    else if (!std::strcmp(e, "VK_EXT_host_image_copy")) caps_.ext_host_image_copy = true;
    else if (!std::strcmp(e, "VK_KHR_fragment_shading_rate")) caps_.khr_fragment_shading_rate = true;
    else if (!std::strcmp(e, "VK_KHR_portability_subset")) caps_.khr_portability_subset = true;
  }
  if (!has_pipeline_library) caps_.ext_graphics_pipeline_library = false; // ikisi birlikte gerekir
  if (cfg.require_mandatory) {
    if (!caps_.descriptor_indexing) { fail("zorunlu: descriptorIndexing yok", VK_ERROR_FEATURE_NOT_PRESENT); return false; }
    if (!caps_.timeline_semaphore) { fail("zorunlu: timelineSemaphore yok", VK_ERROR_FEATURE_NOT_PRESENT); return false; }
    if (!caps_.buffer_device_address) { fail("zorunlu: bufferDeviceAddress yok", VK_ERROR_FEATURE_NOT_PRESENT); return false; }
  }
  return true;
}

int Device::find_memory_type(uint32_t mask, VkMemoryPropertyFlags flags) const {
  for (uint32_t i = 0; i < mem_props_.memoryTypeCount; i++)
    if ((mask & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & flags) == flags) return (int)i;
  return -1;
}

bool Device::allocate(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags, bool lazily_ok,
                      MemoryAlloc *out) {
  int type = -1;
  if (lazily_ok) type = find_memory_type(req.memoryTypeBits, flags | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
  if (type < 0) type = find_memory_type(req.memoryTypeBits, flags);
  if (type < 0) {
    fail("uygun bellek turu yok", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return false;
  }
  bool host_visible = (mem_props_.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
  // Var olan blokta yer var mi?
  for (uint32_t i = 0; i < block_count_; i++) {
    MemoryBlock &b = blocks_[i];
    if (b.type_index != (uint32_t)type) continue;
    VkDeviceSize off = (b.used + req.alignment - 1) / req.alignment * req.alignment;
    if (off + req.size <= b.size) {
      out->memory = b.memory;
      out->offset = off;
      out->size = req.size;
      out->mapped = b.mapped ? (char *)b.mapped + off : nullptr;
      b.used = off + req.size;
      return true;
    }
  }
  if (block_count_ >= kMaxBlocks) {
    fail("bellek blok siniri (kMaxBlocks) — kapasite build'de hesaplanmali (A2)", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return false;
  }
  MemoryBlock &b = blocks_[block_count_];
  b.type_index = (uint32_t)type;
  b.size = req.size > kBlockSize ? req.size : kBlockSize;
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = b.size;
  mai.memoryTypeIndex = (uint32_t)type;
  VkResult r = api_->vkAllocateMemory(device_, &mai, nullptr, &b.memory);
  if (r != VK_SUCCESS) {
    fail("vkAllocateMemory", r);
    return false;
  }
  if (host_visible) api_->vkMapMemory(device_, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
  b.used = req.size;
  block_count_++;
  out->memory = b.memory;
  out->offset = 0;
  out->size = req.size;
  out->mapped = b.mapped;
  return true;
}

VkCommandBuffer Device::begin_one_shot() {
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = cmd_pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  if (api_->vkAllocateCommandBuffers(device_, &ai, &cb) != VK_SUCCESS) return VK_NULL_HANDLE;
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  api_->vkBeginCommandBuffer(cb, &bi);
  return cb;
}

bool Device::end_one_shot_and_wait(VkCommandBuffer cb, uint64_t timeout_ns) {
  api_->vkEndCommandBuffer(cb);
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  api_->vkResetFences(device_, 1, &one_shot_fence_);
  VkResult r = api_->vkQueueSubmit(queue_, 1, &si, one_shot_fence_);
  if (r != VK_SUCCESS) {
    fail("vkQueueSubmit", r);
    return false;
  }
  r = api_->vkWaitForFences(device_, 1, &one_shot_fence_, VK_TRUE, timeout_ns);
  if (r != VK_SUCCESS) {
    fail("vkWaitForFences", r);
    return false;
  }
  return true;
}

void Device::shutdown() {
  if (!api_) return;
  VkApi &api = *api_;
  if (device_) {
    api.vkDeviceWaitIdle(device_);
    if (one_shot_fence_) api.vkDestroyFence(device_, one_shot_fence_, nullptr);
    if (cmd_pool_) api.vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    for (uint32_t i = 0; i < block_count_; i++) {
      if (blocks_[i].mapped) api.vkUnmapMemory(device_, blocks_[i].memory);
      api.vkFreeMemory(device_, blocks_[i].memory, nullptr);
    }
    block_count_ = 0;
    api.vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (instance_) {
    if (messenger_ && api.vkDestroyDebugUtilsMessengerEXT) api.vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
    api.vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

} // namespace tulpar::engine::rhi
