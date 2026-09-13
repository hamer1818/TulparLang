// L2 RHI — Vulkan yukleyici. Loader (libvulkan) DLOPEN ile: link zamani
// bagimlilik yok, loader yoksa RHI GORUNUR sekilde devre disi (ATLANDI),
// sessiz degil. Basliklar vendored (engine/third_party/vulkan), prototip yok;
// kullanilan her giris noktasi burada acikca listelenir — RHI'nin Vulkan
// yuzeyi bu tablodan ibarettir (plan L2: ince tut).
#pragma once
#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

namespace tulpar::engine::rhi {

struct VkApi {
  void *lib = nullptr;
  // global
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
  // instance
  PFN_vkDestroyInstance vkDestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
  PFN_vkCreateDevice vkCreateDevice = nullptr;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
  PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
  PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;   // dogrulama katmani varsa
  PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
  // device
  PFN_vkDestroyDevice vkDestroyDevice = nullptr;
  PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
  PFN_vkQueueSubmit vkQueueSubmit = nullptr;
  PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
  PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
  PFN_vkResetCommandPool vkResetCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
  PFN_vkCreateFence vkCreateFence = nullptr;
  PFN_vkDestroyFence vkDestroyFence = nullptr;
  PFN_vkWaitForFences vkWaitForFences = nullptr;
  PFN_vkResetFences vkResetFences = nullptr;
  PFN_vkAllocateMemory vkAllocateMemory = nullptr;
  PFN_vkFreeMemory vkFreeMemory = nullptr;
  PFN_vkMapMemory vkMapMemory = nullptr;
  PFN_vkUnmapMemory vkUnmapMemory = nullptr;
  PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges = nullptr;
  PFN_vkCreateBuffer vkCreateBuffer = nullptr;
  PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
  PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
  PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
  PFN_vkCreateImage vkCreateImage = nullptr;
  PFN_vkDestroyImage vkDestroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
  PFN_vkBindImageMemory vkBindImageMemory = nullptr;
  PFN_vkCreateImageView vkCreateImageView = nullptr;
  PFN_vkDestroyImageView vkDestroyImageView = nullptr;
  PFN_vkCreateRenderPass2 vkCreateRenderPass2 = nullptr;
  PFN_vkCreateRenderPass vkCreateRenderPass = nullptr;
  PFN_vkDestroyRenderPass vkDestroyRenderPass = nullptr;
  PFN_vkCreateFramebuffer vkCreateFramebuffer = nullptr;
  PFN_vkDestroyFramebuffer vkDestroyFramebuffer = nullptr;
  PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
  PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
  PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
  PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
  PFN_vkCreatePipelineCache vkCreatePipelineCache = nullptr;
  PFN_vkDestroyPipelineCache vkDestroyPipelineCache = nullptr;
  PFN_vkGetPipelineCacheData vkGetPipelineCacheData = nullptr;
  PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
  PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
  PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
  PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
  PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;
  PFN_vkCmdResetQueryPool vkCmdResetQueryPool = nullptr;
  PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = nullptr;
  PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
  PFN_vkCmdNextSubpass vkCmdNextSubpass = nullptr;
  PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
  PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
  PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
  PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
  PFN_vkCmdDraw vkCmdDraw = nullptr;
  PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
  PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
  PFN_vkCmdExecuteCommands vkCmdExecuteCommands = nullptr;
};

// libvulkan'i dlopen edip global + vkGetInstanceProcAddr'i yukler.
// Basarisizlikta false (loader yok) — cagiran GORUNUR atlar.
bool vk_api_load(VkApi &api);
// macOS: loader bulundu ama ICD'yi (MoltenVK json'u brew dizininde, loader'in
// aramadigi yerde) gormuyorsa MoltenVK'yi DOGRUDAN yukle (vkGetInstanceProcAddr
// disari verir, ICD gibi degil kutuphane gibi calisir). Basari: true.
bool vk_api_load_moltenvk_direct(VkApi &api);
bool vk_api_is_direct_moltenvk(const VkApi &api);
void vk_api_load_instance(VkApi &api, VkInstance instance);
void vk_api_load_device(VkApi &api, VkDevice device);
void vk_api_unload(VkApi &api);
const char *vk_result_str(VkResult r);

} // namespace tulpar::engine::rhi
