#include "rhi/vk_api.hpp"

#include <dlfcn.h>
#include <stdlib.h>

namespace tulpar::engine::rhi {

namespace {
bool g_direct_moltenvk = false;
bool load_from(VkApi &api, const char *const *names, int n) {
  for (int i = 0; i < n; i++) {
    api.lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (api.lib) break;
  }
  if (!api.lib) return false;
  api.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(api.lib, "vkGetInstanceProcAddr");
  if (!api.vkGetInstanceProcAddr) {
    dlclose(api.lib);
    api.lib = nullptr;
    return false;
  }
#define G(name) api.name = (PFN_##name)api.vkGetInstanceProcAddr(nullptr, #name)
  G(vkEnumerateInstanceVersion);
  G(vkEnumerateInstanceExtensionProperties);
  G(vkEnumerateInstanceLayerProperties);
  G(vkCreateInstance);
#undef G
  return api.vkCreateInstance != nullptr;
}
} // namespace

bool vk_api_load_moltenvk_direct(VkApi &api) {
#if defined(__APPLE__)
  vk_api_unload(api);
  const char *names[] = {"libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib"};
  if (!load_from(api, names, 3)) return false;
  g_direct_moltenvk = true;
  return true;
#else
  (void)api;
  return false;
#endif
}

bool vk_api_is_direct_moltenvk(const VkApi &) { return g_direct_moltenvk; }

bool vk_api_load(VkApi &api) {
  if (api.lib) return true;
  // Pozitif kontrol: loader yokmus gibi davran — GORUNUR atlama yolu sinanir
  // (atlanan test sessizce yesil sayilmasin; ozet satiri "atlandi" gostermeli).
  if (const char *e = getenv("TULPAR_ENGINE_NO_VULKAN"); e && *e && *e != '0') return false;
  const char *names[] = {
#if defined(__APPLE__)
      // Once loader (brew vulkan-loader), sonra MoltenVK'nin kendisi (ICD
      // olarak degil dogrudan: vkGetInstanceProcAddr disari verir). dlopen
      // bare adi DYLD yolunda aramaz; brew dizinleri TAM yol.
      "libvulkan.1.dylib", "libvulkan.dylib",
      "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib",
      "libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib",
#else
      "libvulkan.so.1", "libvulkan.so",
#endif
  };
  g_direct_moltenvk = false;
  return load_from(api, names, (int)(sizeof names / sizeof names[0]));
}

void vk_api_load_instance(VkApi &api, VkInstance inst) {
#define G(name) api.name = (PFN_##name)api.vkGetInstanceProcAddr(inst, #name)
  G(vkDestroyInstance);
  G(vkEnumeratePhysicalDevices);
  G(vkGetPhysicalDeviceProperties2);
  G(vkGetPhysicalDeviceFeatures2);
  G(vkGetPhysicalDeviceQueueFamilyProperties);
  G(vkGetPhysicalDeviceMemoryProperties);
  G(vkGetPhysicalDeviceFormatProperties);
  G(vkEnumerateDeviceExtensionProperties);
  G(vkCreateDevice);
  G(vkGetDeviceProcAddr);
  G(vkCreateDebugUtilsMessengerEXT);
  G(vkDestroyDebugUtilsMessengerEXT);
  G(vkDestroySurfaceKHR); G(vkGetPhysicalDeviceSurfaceSupportKHR); G(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  G(vkGetPhysicalDeviceSurfaceFormatsKHR); G(vkGetPhysicalDeviceSurfacePresentModesKHR);
#undef G
}

void vk_api_load_device(VkApi &api, VkDevice dev) {
#define G(name) api.name = (PFN_##name)api.vkGetDeviceProcAddr(dev, #name)
  G(vkDestroyDevice); G(vkGetDeviceQueue); G(vkQueueSubmit); G(vkQueueWaitIdle); G(vkDeviceWaitIdle);
  G(vkCreateCommandPool); G(vkDestroyCommandPool); G(vkResetCommandPool); G(vkAllocateCommandBuffers);
  G(vkBeginCommandBuffer); G(vkEndCommandBuffer);
  G(vkCreateFence); G(vkDestroyFence); G(vkWaitForFences); G(vkResetFences);
  G(vkAllocateMemory); G(vkFreeMemory); G(vkMapMemory); G(vkUnmapMemory); G(vkInvalidateMappedMemoryRanges);
  G(vkCreateBuffer); G(vkDestroyBuffer); G(vkGetBufferMemoryRequirements); G(vkBindBufferMemory);
  G(vkCreateImage); G(vkDestroyImage); G(vkGetImageMemoryRequirements); G(vkBindImageMemory);
  G(vkCreateImageView); G(vkDestroyImageView);
  G(vkCreateSampler); G(vkDestroySampler); G(vkCmdSetDepthBias);
  G(vkCmdBlitImage); G(vkCmdCopyBufferToImage);
  G(vkCreateRenderPass2); G(vkCreateRenderPass); G(vkDestroyRenderPass);
  G(vkCreateFramebuffer); G(vkDestroyFramebuffer);
  G(vkCreateShaderModule); G(vkDestroyShaderModule);
  G(vkCreatePipelineLayout); G(vkDestroyPipelineLayout);
  G(vkCreatePipelineCache); G(vkDestroyPipelineCache); G(vkGetPipelineCacheData);
  G(vkCreateGraphicsPipelines); G(vkDestroyPipeline);
  G(vkCreateQueryPool); G(vkDestroyQueryPool); G(vkGetQueryPoolResults);
  G(vkCmdResetQueryPool); G(vkCmdWriteTimestamp);
  G(vkCmdBeginRenderPass); G(vkCmdNextSubpass); G(vkCmdEndRenderPass);
  G(vkCmdBindPipeline); G(vkCmdSetViewport); G(vkCmdSetScissor); G(vkCmdDraw);
  G(vkCmdPipelineBarrier); G(vkCmdCopyImageToBuffer); G(vkCmdExecuteCommands);
  G(vkCreateSwapchainKHR); G(vkDestroySwapchainKHR); G(vkGetSwapchainImagesKHR); G(vkAcquireNextImageKHR); G(vkQueuePresentKHR);
  G(vkCreateSemaphore); G(vkDestroySemaphore); G(vkResetCommandBuffer); G(vkFreeCommandBuffers);
  G(vkCmdBindVertexBuffers); G(vkCmdBindIndexBuffer); G(vkCmdDrawIndexed); G(vkCmdPushConstants); G(vkCmdCopyBuffer);
  G(vkFlushMappedMemoryRanges);
  G(vkCreateDescriptorSetLayout); G(vkDestroyDescriptorSetLayout); G(vkCreateDescriptorPool); G(vkDestroyDescriptorPool);
  G(vkAllocateDescriptorSets); G(vkUpdateDescriptorSets); G(vkCmdBindDescriptorSets);
#undef G
}

void vk_api_unload(VkApi &api) {
  if (api.lib) dlclose(api.lib);
  api = VkApi{};
  g_direct_moltenvk = false;
}

const char *vk_result_str(VkResult r) {
  switch (r) {
  case VK_SUCCESS: return "VK_SUCCESS";
  case VK_NOT_READY: return "VK_NOT_READY";
  case VK_TIMEOUT: return "VK_TIMEOUT";
  case VK_INCOMPLETE: return "VK_INCOMPLETE";
  case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
  default: return "VK_ERROR_?";
  }
}

} // namespace tulpar::engine::rhi
