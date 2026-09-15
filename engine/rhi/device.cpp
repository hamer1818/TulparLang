#include "rhi/device.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tulpar::engine::rhi {

// Dogrulama mesaji: hata SAYILIR (test 0 bekler), ilk 8'i basilir. BestPractices
// uyarilari kimlik basina sayilir (Mali kapisi Arm kimliklerinde 0 bekler).
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                                     VkDebugUtilsMessageTypeFlagsEXT,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                     void *user) {
  Device *d = static_cast<Device *>(user);
  d->on_validation_message((uint32_t)sev, data ? data->pMessageIdName : nullptr, data ? data->pMessage : nullptr);
  return VK_FALSE;
}

void Device::on_validation_message(uint32_t sev, const char *id, const char *message) {
  if (!message) message = "?";
  if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    validation_errors_++;
    if (validation_errors_ <= 8) std::fprintf(stderr, "[vulkan-validation] %s\n", message);
    return;
  }
  if (id && std::strncmp(id, "BestPractices", 13) == 0) {
    const bool arm = std::strstr(id, "-Arm-") != nullptr;
    bp_warnings_++;
    if (arm) bp_arm_warnings_++;
    for (uint32_t i = 0; i < bp_id_n_; i++)
      if (std::strcmp(bp_ids_[i].name, id) == 0) { bp_ids_[i].count++; return; }
    if (bp_id_n_ < kBpIds) {
      BpId &b = bp_ids_[bp_id_n_++];
      std::snprintf(b.name, sizeof b.name, "%s", id);
      b.count = 1;
      b.arm = arm;
      if (bp_id_n_ <= 8) std::fprintf(stderr, "[best-practices%s] %.400s\n", arm ? "/Arm" : "", message);
    }
    return;
  }
  if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) validation_warnings_++;
}

uint32_t Device::best_practice_count(const char *id_substring) const {
  uint32_t n = 0;
  for (uint32_t i = 0; i < bp_id_n_; i++)
    if (std::strstr(bp_ids_[i].name, id_substring)) n += bp_ids_[i].count;
  return n;
}

namespace {
// Kalici PSO onbellek dosyasinin yolu. Sira: DeviceConfig -> TULPAR_ENGINE_PSO_CACHE
// -> $XDG_CACHE_HOME -> $HOME/.cache -> $TMPDIR (Android host bunu verir) -> KAPALI.
// "" ya da "0" acikca KAPATIR (yalniz bellek ici onbellek; disk yok).
// Dosya adi YALNIZ GPU'ya baglanir (satici + cihaz), surucu surumune DEGIL:
// surucu guncellenince ayni dosya acilir, basligi (driverVersion /
// pipelineCacheUUID) tutmaz, REDDEDILIR ve uzerine yenisi yazilir. Ada surum
// koymak her surucu guncellemesinde yetim bir dosya birakirdi.
void resolve_pso_path(char *out, size_t n, const DeviceConfig &cfg, const DeviceCaps &caps) {
  out[0] = 0;
  const char *want = cfg.pso_cache_path;
  if (!want) want = std::getenv("TULPAR_ENGINE_PSO_CACHE");
  if (want) {
    if (!*want || (want[0] == '0' && !want[1])) return; // kapali
    std::snprintf(out, n, "%s", want);
    return;
  }
  char home[400];
  const char *base = std::getenv("XDG_CACHE_HOME");
  if (!base || !*base) {
    const char *h = std::getenv("HOME");
    if (h && *h) {
      std::snprintf(home, sizeof home, "%s/.cache", h);
      base = home;
    }
  }
  if (!base || !*base) base = std::getenv("TMPDIR");
#if defined(__ANDROID__)
  // ANDROID: NativeActivity surecinde XDG_CACHE_HOME/HOME/TMPDIR'in ucu de
  // genelde TANIMSIZDIR. Yukaridaki zincir orada bosa cikiyor ve onbellek tam
  // olarak EN COK ISE YARADIGI platformda kapaniyordu — masaustunde her sey
  // yolunda gorundugu icin de fark edilmezdi. Motorun Android barindiricisi
  // acilirken APK varliklarini uygulamanin kendi dizinine cikarip oraya chdir
  // ediyor (yazilabilir, uygulamaya ozel, kaldirinca temizlenir), o yuzden
  // son care olarak calisma dizini dogru yer.
  if (!base || !*base) base = ".";
#endif
  if (!base || !*base) return; // yer yok -> kapali (sessiz degil: pso().path() bos)
  std::snprintf(out, n, "%s/tulpar_engine/pso_%04x_%04x.bin", base, caps.vendor_id, caps.device_id);
}
} // namespace

void Device::fail(const char *msg, VkResult r) {
  std::snprintf(err_, sizeof err_, "%s (%s)", msg, vk_result_str(r));
}

bool Device::init(Arena &arena_unused_, VkApi &api, const DeviceConfig &cfg) {
  (void)arena_unused_;
  return init_instance(api, cfg) && init_device(VK_NULL_HANDLE);
}

bool Device::init_instance(VkApi &api, const DeviceConfig &cfg) {
  api_ = &api;
  cfg_ = cfg;
  uint32_t loader_version = VK_API_VERSION_1_0;
  if (api.vkEnumerateInstanceVersion) api.vkEnumerateInstanceVersion(&loader_version);
  // Vulkan 1.1+ ister: GetPhysicalDeviceProperties2/Features2 cekirdekte.
  if (loader_version < VK_API_VERSION_1_1) {
    fail("Vulkan loader 1.1 altinda", VK_ERROR_INCOMPATIBLE_DRIVER);
    return false;
  }
  uint32_t want = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3
                  : loader_version >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;
  instance_version_ = want;

  // MoltenVK: portability enumeration uzantisi + bayragi (yoksa cihaz gorunmez).
  const char *inst_exts[12];
  uint32_t inst_ext_n = 0;
  VkInstanceCreateFlags inst_flags = 0;
  bool have_debug_utils = false;
  for (uint32_t i = 0; i < cfg.instance_extension_count && inst_ext_n < 8; i++)
    inst_exts[inst_ext_n++] = cfg.instance_extensions[i];
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
        // VK_EXT_debug_utils'i ICD vermeyebilir (Huawei/Android 10 yukleyicisi,
        // olculdu 2026-09-14: katman ETKIN ama mesaj yok = sahte yesil). Katmanin
        // kendi uzanti listesine de bak: katman bu uzantiyi kendisi saglar.
        if (!have_debug_utils && api.vkEnumerateInstanceExtensionProperties) {
          uint32_t m = 0;
          api.vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &m, nullptr);
          VkExtensionProperties lprops[32];
          if (m > 32) m = 32;
          api.vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &m, lprops);
          for (uint32_t k = 0; k < m; k++)
            if (std::strcmp(lprops[k].extensionName, "VK_EXT_debug_utils") == 0) have_debug_utils = true;
        }
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
  // Mali linter: katman ayarlari pNext ile (VK_EXT_layer_settings; uzantinin
  // etkin olmasi gerekmez, katman zinciri okur). Katman yoksa zincirlenmez.
  VkBool32 on = VK_TRUE;
  VkLayerSettingEXT settings[2] = {
      {"VK_LAYER_KHRONOS_validation", "validate_best_practices", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &on},
      {"VK_LAYER_KHRONOS_validation", "validate_best_practices_arm", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &on},
  };
  VkLayerSettingsCreateInfoEXT lsci{};
  lsci.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
  lsci.settingCount = 2;
  lsci.pSettings = settings;
  if (cfg.best_practices && caps_.validation_layer) {
    ici.pNext = &lsci;
    caps_.best_practices = true;
  }
  VkResult r = api.vkCreateInstance(&ici, nullptr, &instance_);
#if defined(__APPLE__)
  // Loader + MoltenVK ICD: VK_ERROR_INCOMPATIBLE_DRIVER (olculdu CI macOS
  // 2026-09-14, VK_ICD_FILENAMES verilmisken bile). Dogrudan MoltenVK calisiyor
  // (ilk kosumda cizdi). Yedek burada da: instance kurulamazsa dogrudan yukle,
  // bir kez bastan dene.
  if (r != VK_SUCCESS && !vk_api_is_direct_moltenvk(api)) {
    caps_ = DeviceCaps{};
    if (vk_api_load_moltenvk_direct(api)) return init_instance(api, cfg);
  }
#endif
  if (r != VK_SUCCESS) {
    fail("vkCreateInstance", r);
    return false;
  }
  vk_api_load_instance(api, instance_);
#if defined(__APPLE__)
  // Loader var ama ICD yok (brew MoltenVK json'u loader'in aramadigi dizinde):
  // olculdu CI macOS 2026-09-14, "Vulkan cihazi yok". MoltenVK'yi dogrudan
  // yukleyip bastan dene — bir kez.
  {
    uint32_t n = 0;
    api.vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0 && !vk_api_is_direct_moltenvk(api)) {
      api.vkDestroyInstance(instance_, nullptr);
      instance_ = VK_NULL_HANDLE;
      messenger_ = VK_NULL_HANDLE;
      caps_ = DeviceCaps{};
      if (!vk_api_load_moltenvk_direct(api)) {
        fail("loader cihaz gormuyor ve MoltenVK dogrudan yuklenemedi", VK_ERROR_INCOMPATIBLE_DRIVER);
        return false;
      }
      return init_instance(api, cfg);
    }
  }
#endif
  if (caps_.validation_layer && have_debug_utils && api.vkCreateDebugUtilsMessengerEXT) {
    VkDebugUtilsMessengerCreateInfoEXT mci{};
    mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT; // BestPractices bazen INFO
    mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // BestPractices = performans turu
    mci.pfnUserCallback = debug_callback;
    mci.pUserData = this;
    api.vkCreateDebugUtilsMessengerEXT(instance_, &mci, nullptr, &messenger_);
  }
  caps_.debug_messenger = messenger_ != VK_NULL_HANDLE;
  if (caps_.validation_layer && !caps_.debug_messenger)
    std::fprintf(stderr, "[device] dogrulama katmani etkin ama VK_EXT_debug_utils/messenger yok: mesajlar GORUNMEZ\n");
  return true;
}

bool Device::init_device(VkSurfaceKHR surface) {
  VkApi &api = *api_;
  const DeviceConfig &cfg = cfg_;
  surface_ = surface;
  if (!pick_physical(cfg, surface)) return false;

  // Kuyruk ailesi: grafik + compute (+ sunum, yuzey varsa) tek aile.
  uint32_t qn = 0;
  api.vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, nullptr);
  VkQueueFamilyProperties qp[16];
  if (qn > 16) qn = 16;
  api.vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, qp);
  queue_family_ = UINT32_MAX;
  for (uint32_t i = 0; i < qn; i++) {
    if (!(qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !(qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
    if (surface) {
      VkBool32 present = VK_FALSE;
      api.vkGetPhysicalDeviceSurfaceSupportKHR(phys_, i, surface, &present);
      if (!present) continue;
    }
    queue_family_ = i;
    break;
  }
  if (queue_family_ == UINT32_MAX) {
    fail("grafik+compute kuyruk ailesi yok", VK_ERROR_INITIALIZATION_FAILED);
    return false;
  }
  caps_.timestamps = qp[queue_family_].timestampValidBits > 0 && caps_.timestamp_period_ns > 0;

  // Uzantilar (varsa ac).
  const char *dev_exts[12 + DeviceCaps::kMaxOptionalExtensions];
  uint32_t dev_ext_n = 0;
  for (uint32_t i = 0; i < cfg_.optional_device_extension_count && i < DeviceCaps::kMaxOptionalExtensions; i++)
    if (caps_.optional_extension_enabled[i]) dev_exts[dev_ext_n++] = cfg_.optional_device_extensions[i];
  if (surface) {
    if (!has_swapchain_ext_) { fail("VK_KHR_swapchain yok", VK_ERROR_EXTENSION_NOT_PRESENT); return false; }
    dev_exts[dev_ext_n++] = "VK_KHR_swapchain";
  }
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

  // Feature zinciri: 1.2 cekirdek (descriptorIndexing, timelineSemaphore, BDA) ya da
  // 1.1 cihazda uzanti bicimleri (ayni yapilar, EXT/KHR takma adlari) [+ GPL].
  const bool core12 = instance_version_ >= VK_API_VERSION_1_2 && caps_.api_version >= VK_API_VERSION_1_2;
  VkPhysicalDeviceVulkan12Features f12{};
  f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  VkPhysicalDeviceDescriptorIndexingFeatures fdi{};
  fdi.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
  VkPhysicalDeviceTimelineSemaphoreFeatures fts{};
  fts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceBufferDeviceAddressFeatures fbda{};
  fbda.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  void *chain = nullptr;
  if (caps_.graphics_pipeline_library) {
    gplf.graphicsPipelineLibrary = VK_TRUE;
    gplf.pNext = chain;
    chain = &gplf;
  }
  if (core12) {
    f12.descriptorIndexing = caps_.descriptor_indexing;
    f12.runtimeDescriptorArray = caps_.descriptor_indexing;
    f12.shaderSampledImageArrayNonUniformIndexing = caps_.descriptor_indexing;
    f12.descriptorBindingPartiallyBound = caps_.descriptor_indexing;
    f12.timelineSemaphore = caps_.timeline_semaphore;
    f12.bufferDeviceAddress = caps_.buffer_device_address;
    f12.pNext = chain;
    chain = &f12;
  } else {
    if (ext_descriptor_indexing_ && caps_.descriptor_indexing) {
      dev_exts[dev_ext_n++] = "VK_EXT_descriptor_indexing";
      fdi.runtimeDescriptorArray = VK_TRUE;
      fdi.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
      fdi.descriptorBindingPartiallyBound = VK_TRUE;
      fdi.pNext = chain;
      chain = &fdi;
    }
    if (ext_timeline_semaphore_ && caps_.timeline_semaphore) {
      dev_exts[dev_ext_n++] = "VK_KHR_timeline_semaphore";
      fts.timelineSemaphore = VK_TRUE;
      fts.pNext = chain;
      chain = &fts;
    }
    if (ext_buffer_device_address_ && caps_.buffer_device_address) {
      dev_exts[dev_ext_n++] = "VK_KHR_buffer_device_address";
      fbda.bufferDeviceAddress = VK_TRUE;
      fbda.pNext = chain;
      chain = &fbda;
    }
  }
  VkPhysicalDeviceFeatures2 f2{};
  f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  f2.pNext = chain;
  f2.features.drawIndirectFirstInstance = VK_FALSE;
  f2.features.textureCompressionASTC_LDR = caps_.texture_compression_astc_ldr ? VK_TRUE : VK_FALSE;

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
  VkResult r = api.vkCreateDevice(phys_, &dci, nullptr, &device_);
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

  // Kalici PSO onbellegi + VkApi kancasi. Kanca vk_api_load_device'tan SONRA
  // takilir: o cagri tabloyu bastan doldurur ve kancayi silerdi (Tuzaklar 8an).
  {
    char pso_path[512];
    resolve_pso_path(pso_path, sizeof pso_path, cfg_, caps_);
    PsoDeviceId id;
    id.vendor_id = caps_.vendor_id;
    id.device_id = caps_.device_id;
    id.driver_version = caps_.driver_version;
    id.api_version = caps_.api_version;
    std::memcpy(id.cache_uuid, caps_.pipeline_cache_uuid, VK_UUID_SIZE);
    pso_.init(api, device_, id, pso_path);
    pso_install_hook(api, device_, &pso_);
  }
  return true;
}

bool Device::pick_physical(const DeviceConfig &cfg, VkSurfaceKHR surface) {
  (void)surface;
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
  std::memcpy(caps_.pipeline_cache_uuid, p2.properties.pipelineCacheUUID, VK_UUID_SIZE);

  bool has_pipeline_library = false;
  ext_descriptor_indexing_ = ext_timeline_semaphore_ = ext_buffer_device_address_ = false;
  uint32_t en = 0;
  api.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, nullptr);
  VkExtensionProperties ext[512];
  if (en > 512) en = 512;
  api.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, ext);
  for (uint32_t i = 0; i < en; i++) {
    const char *e = ext[i].extensionName;
    if (!std::strcmp(e, "VK_EXT_descriptor_indexing")) ext_descriptor_indexing_ = true;
    else if (!std::strcmp(e, "VK_KHR_timeline_semaphore")) ext_timeline_semaphore_ = true;
    else if (!std::strcmp(e, "VK_KHR_buffer_device_address")) ext_buffer_device_address_ = true;
    else if (!std::strcmp(e, "VK_EXT_subpass_merge_feedback")) caps_.ext_subpass_merge_feedback = true;
    else if (!std::strcmp(e, "VK_EXT_graphics_pipeline_library")) caps_.ext_graphics_pipeline_library = true;
    else if (!std::strcmp(e, "VK_KHR_pipeline_library")) has_pipeline_library = true;
    else if (!std::strcmp(e, "VK_EXT_host_image_copy")) caps_.ext_host_image_copy = true;
    else if (!std::strcmp(e, "VK_KHR_fragment_shading_rate")) caps_.khr_fragment_shading_rate = true;
    else if (!std::strcmp(e, "VK_KHR_portability_subset")) caps_.khr_portability_subset = true;
    else if (!std::strcmp(e, "VK_KHR_swapchain")) has_swapchain_ext_ = true;
    for (uint32_t k = 0; k < cfg_.optional_device_extension_count && k < DeviceCaps::kMaxOptionalExtensions; k++)
      if (!std::strcmp(e, cfg_.optional_device_extensions[k])) caps_.optional_extension_enabled[k] = true;
  }
  if (!has_pipeline_library) caps_.ext_graphics_pipeline_library = false; // ikisi birlikte gerekir

  // Feature sorgusu: 1.2 cekirdek yapisi ya da (1.1 cihazda) uzanti yapilari.
  VkPhysicalDeviceVulkan12Features f12{};
  f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  VkPhysicalDeviceDescriptorIndexingFeatures fdi{};
  fdi.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
  VkPhysicalDeviceTimelineSemaphoreFeatures fts{};
  fts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceBufferDeviceAddressFeatures fbda{};
  fbda.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  VkPhysicalDeviceFeatures2 f2{};
  f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  void *chain = nullptr;
  const bool core12 = instance_version_ >= VK_API_VERSION_1_2 && caps_.api_version >= VK_API_VERSION_1_2;
  if (core12) chain = &f12;
  else {
    if (ext_descriptor_indexing_) { fdi.pNext = chain; chain = &fdi; }
    if (ext_timeline_semaphore_) { fts.pNext = chain; chain = &fts; }
    if (ext_buffer_device_address_) { fbda.pNext = chain; chain = &fbda; }
  }
  f2.pNext = chain;
  api.vkGetPhysicalDeviceFeatures2(phys_, &f2);
  caps_.texture_compression_astc_ldr = f2.features.textureCompressionASTC_LDR == VK_TRUE;
  if (core12) {
    caps_.descriptor_indexing = f12.descriptorIndexing && f12.runtimeDescriptorArray;
    caps_.timeline_semaphore = f12.timelineSemaphore;
    caps_.buffer_device_address = f12.bufferDeviceAddress;
  } else {
    caps_.descriptor_indexing = ext_descriptor_indexing_ && fdi.runtimeDescriptorArray;
    caps_.timeline_semaphore = ext_timeline_semaphore_ && fts.timelineSemaphore;
    caps_.buffer_device_address = ext_buffer_device_address_ && fbda.bufferDeviceAddress;
  }
  caps_.draw_indirect = true; // cekirdek 1.0: vkCmdDrawIndexedIndirect
  caps_.missing_mandatory[0] = 0;
  size_t mm = 0;
  auto miss = [&](const char *name) {
    int w = std::snprintf(caps_.missing_mandatory + mm, sizeof caps_.missing_mandatory - mm, "%s%s", mm ? "," : "", name);
    if (w > 0) mm += (size_t)w;
  };
  if (!caps_.descriptor_indexing) miss("descriptorIndexing");
  if (!caps_.timeline_semaphore) miss("timelineSemaphore");
  if (!caps_.buffer_device_address) miss("bufferDeviceAddress");
  if (cfg.require_mandatory && mm) {
    fail("zorunlu feature eksik (caps.missing_mandatory)", VK_ERROR_FEATURE_NOT_PRESENT);
    return false;
  }
  return true;
}

void Device::replace_surface(VkSurfaceKHR s) {
  if (surface_ && api_ && api_->vkDestroySurfaceKHR) api_->vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = s;
}

int Device::find_memory_type(uint32_t mask, VkMemoryPropertyFlags flags) const {
  for (uint32_t i = 0; i < mem_props_.memoryTypeCount; i++)
    if ((mask & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & flags) == flags) return (int)i;
  return -1;
}

bool Device::allocate_dedicated(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags, bool lazily_ok,
                                MemoryAlloc *out) {
  int type = -1;
  if (lazily_ok) type = find_memory_type(req.memoryTypeBits, flags | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
  if (type < 0) type = find_memory_type(req.memoryTypeBits, flags);
  if (type < 0) {
    fail("uygun bellek turu yok (dedicated)", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return false;
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = (uint32_t)type;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkResult r = api_->vkAllocateMemory(device_, &mai, nullptr, &mem);
  if (r != VK_SUCCESS) {
    fail("vkAllocateMemory (dedicated)", r);
    return false;
  }
  out->memory = mem;
  out->offset = 0;
  out->size = req.size;
  out->mapped = nullptr;
  dedicated_count_++;
  return true;
}

void Device::free_dedicated(MemoryAlloc *a) {
  if (!a || !a->memory) return;
  api_->vkFreeMemory(device_, a->memory, nullptr);
  a->memory = VK_NULL_HANDLE;
  a->offset = a->size = 0;
  a->mapped = nullptr;
  if (dedicated_count_) dedicated_count_--;
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
    // Onbellek cihazdan ONCE: vkGetPipelineCacheData canli cihaz ister.
    pso_.shutdown(true);
    pso_remove_hook(api, device_);
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
    if (surface_ && api.vkDestroySurfaceKHR) api.vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (messenger_ && api.vkDestroyDebugUtilsMessengerEXT) api.vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
    api.vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

} // namespace tulpar::engine::rhi
