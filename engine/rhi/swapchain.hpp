// L2 RHI — Swapchain + kare senkronu (masaustu pencere; Android yuzeyi de
// ayni sinif, yuzey Kotlin host'tan). Render pass: depth prepass -> renk
// (subpass zinciri, transient depth), renk PRESENT'e cikar. Ucuslu kare 2.
// Yeniden boyutlanma: PENCERE OLCUSU (sync_size) — OUT_OF_DATE yalniz ek
// sinyaldir, bkz. asagidaki ResizeAction. Present FIFO (vsync; A7: kilitli
// kare > oynak).
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::rhi {

// --- YENIDEN KURMA KARARI (saf; Vulkan gerektirmez, kapi bunu olcer) --------
//
// TUZAK: "OUT_OF_DATE gelince yeniden kur" TASINABILIR DEGIL. Wayland'de yuzey
// olcusunu SUREN taraf UYGULAMADIR: `caps.currentExtent` 0xFFFFFFFF doner ve
// pencere buyudugunde (tam ekran!) acquire/present OUT_OF_DATE HIC DEMEZ.
// Eski goruntu olcusu korunur, kompozitor onu pencereye GERER — kullanicinin
// gordugu "tam ekran olmuyor, icerik ayni oranda buyuyup bozuluyor" tam olarak
// bu. X11'de OUT_OF_DATE gelir, yani hata yalniz Wayland'de gorunur.
// Dogru sinyal: pencerenin bildirdigi framebuffer olcusu ISTENEN olcuden
// farkliysa yeniden kur. OUT_OF_DATE ek sebeptir (yuzey gercekten gecersiz).
enum class ResizeAction {
  None = 0,    // dokunma
  OutOfDate,   // acquire/present OUT_OF_DATE dedi: yuzey gecersiz
  SizeChanged, // pencere olcusu istenenden farkli (Wayland'de TEK sinyal)
};
// requested: son create_swapchain'e verilen olcu. win_w/win_h: pencerenin SU AN
// bildirdigi framebuffer olcusu (0 = kucultulmus -> None; 0 olculu swapchain
// yaratilamaz).
ResizeAction swapchain_resize_action(VkExtent2D requested, uint32_t win_w, uint32_t win_h, bool out_of_date);
const char *resize_action_str(ResizeAction a);

struct FrameContext {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint32_t image_index = 0;
  uint32_t frame_index = 0; // 0..kFramesInFlight-1
  VkExtent2D extent{};
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

struct SwapchainConfig {
  // ON-DONDURME (Android): preTransform = yuzeyin currentTransform'u; goruntu
  // panelin dogal yonunde uretilir, kompozitor DONDURMEZ (tam ekran dondurme
  // gecisi = bant genisligi). Uygulama projeksiyonu clip uzayinda dondurur
  // (rotation_radians()). false: IDENTITY iste, kompozitor dondursun (her kare
  // SUBOPTIMAL; A/B olcumu icin).
  bool prerotate = true;
  // sRGB yuzey bicimi tercih et (dogrusal aydinlatma, Filament tarifi): shader
  // dogrusal yazar, donanim kodlar. Yuzey vermezse UNORM'a duser ve
  // srgb_output() false doner; renderer o zaman shader'da kodlar.
  bool srgb = true;
  // Istenen sunum kipi; yoksa FIFO'ya duser. IMMEDIATE/MAILBOX: vsync kilidi
  // kalkar, GPU'nun gercek kare maliyeti olculur (A7: urun FIFO ile kilitli).
  VkPresentModeKHR preferred_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  // Sunum kancalari: kare temposu katmani (Android AGDK Swappy) sunumu sarar.
  // present verilmisse vkQueuePresentKHR yerine o cagrilir; on_create/on_destroy
  // swapchain omrunu bildirir (Swappy swapchain basina baglam tutar).
  struct Hooks {
    void *user = nullptr;
    void (*on_create)(void *user, VkPhysicalDevice phys, VkDevice dev, VkQueue q, uint32_t queue_family, VkSwapchainKHR sc) = nullptr;
    void (*on_destroy)(void *user, VkDevice dev, VkSwapchainKHR sc) = nullptr;
    VkResult (*present)(void *user, VkQueue q, const VkPresentInfoKHR *pi) = nullptr;
  } hooks;
};

class Swapchain {
public:
  static constexpr uint32_t kFramesInFlight = 2;
  bool init(Device &dev, Arena &arena, VkSurfaceKHR surface, uint32_t width, uint32_t height,
            const SwapchainConfig &cfg = SwapchainConfig{});
  void shutdown();
  // Pencere boyutu degisince (ya da acquire OUT_OF_DATE deyince).
  bool recreate(uint32_t width, uint32_t height);
  // KARE BASINDA cagrilir (kayittan ONCE: arayuzun DisplaySize'i ile hedefin
  // olcusu ayni karede ayrismasin). Karari swapchain_resize_action verir;
  // gerekiyorsa recreate eder. Donus: yeniden kuruldu mu. Ne oldugu
  // last_resize_action()/last_resize_reason() ile okunur (sessiz gecis yok).
  bool sync_size(uint32_t width, uint32_t height);

  // Kare: acquire + komut tamponu baslat + render pass baslat (subpass 0).
  // false = yeniden kurulmali (recreate cagir) ya da hata.
  bool begin_frame(FrameContext *out);
  // Ikiye ayrilmis hali: arada KENDI render pass'i olan isler (golge haritasi)
  // kaydedilebilsin — bir render pass icinde baska pass baslatilamaz.
  bool acquire(FrameContext *out);
  void begin_render_pass(const FrameContext &fc);
  // Render pass bitir + gonder + sun. false = OUT_OF_DATE (recreate).
  bool end_frame(const FrameContext &fc);

  VkRenderPass render_pass() const { return rp_; }
  VkFormat color_format() const { return format_; }
  bool srgb_output() const { return format_ == VK_FORMAT_B8G8R8A8_SRGB || format_ == VK_FORMAT_R8G8B8A8_SRGB; }
  // Goruntu (framebuffer) olcusu: viewport/scissor bunu kullanir. On-dondurmede
  // 90/270'te logical_extent'in devrigi.
  VkExtent2D extent() const { return extent_; }
  // Kullanicinin gordugu yon: en-boy orani BUNDAN hesaplanir.
  VkExtent2D logical_extent() const { return logical_extent_; }
  // Projeksiyona clip uzayinda uygulanacak donus (0, pi/2, pi, 3pi/2).
  float rotation_radians() const;
  VkPresentModeKHR present_mode() const { return present_mode_; }
  uint32_t image_count() const { return image_count_; }
  bool needs_recreate() const { return needs_recreate_; }
  // En son create_swapchain'e ISTENEN olcu (Wayland'de goruntu olcusu budur;
  // X11'de surucu currentExtent'i dayatabilir, o zaman extent() ondan gelir).
  VkExtent2D requested_extent() const { return requested_; }
  uint64_t recreate_count() const { return recreates_; }
  ResizeAction last_resize_action() const { return last_action_; }
  const char *last_resize_reason() const { return resize_action_str(last_action_); }
  // Sunumun "calisir ama optimum degil" dedigi kare sayisi (Android: preTransform
  // uyusmazligi). Yeniden yaratmayi TETIKLEMEZ; raporlanir.
  uint64_t suboptimal_frames() const { return suboptimal_; }
  VkSurfaceTransformFlagBitsKHR pretransform() const { return pretransform_; }
  uint64_t frames_presented() const { return frames_; }

private:
  bool create_swapchain(uint32_t w, uint32_t h);
  void destroy_swapchain();
  bool create_render_pass();

  Device *dev_ = nullptr;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swap_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent2D extent_{};
  uint32_t image_count_ = 0;
  static constexpr uint32_t kMaxImages = 8;
  VkImage images_[kMaxImages] = {};
  VkImageView views_[kMaxImages] = {};
  VkFramebuffer fbs_[kMaxImages] = {};
  VkImage depth_ = VK_NULL_HANDLE;
  MemoryAlloc depth_mem_{};
  VkImageView depth_view_ = VK_NULL_HANDLE;
  VkRenderPass rp_ = VK_NULL_HANDLE;
  // ucuslu kare
  VkCommandBuffer cmds_[kFramesInFlight] = {};
  VkSemaphore acquire_sem_[kFramesInFlight] = {};
  VkSemaphore render_sem_[kMaxImages] = {};
  VkFence fences_[kFramesInFlight] = {};
  uint32_t frame_ = 0;
  uint64_t frames_ = 0;
  bool needs_recreate_ = false;
  uint64_t suboptimal_ = 0;
  SwapchainConfig cfg_;
  VkExtent2D logical_extent_{};
  VkExtent2D requested_{};
  uint64_t recreates_ = 0;
  ResizeAction last_action_ = ResizeAction::None;
  VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  VkSurfaceTransformFlagBitsKHR pretransform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
};

} // namespace tulpar::engine::rhi
