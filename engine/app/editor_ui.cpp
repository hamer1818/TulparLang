#include "app/editor_ui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

namespace tulpar::engine::app {

namespace {
struct LoaderCtx {
  rhi::VkApi *api;
  VkInstance instance;
};
// ImGui'nin tablosu WSI fonksiyonlarini da ister (vkCreateSwapchainKHR...); yuzey
// uzantisi olmayan instance'ta (headless, testler) bunlar null gelir ve yukleme
// basarisiz olur. Yalniz ImGui_ImplVulkanH_* yardimcilari kullanir; biz kullanmiyoruz.
// Null yerine SESLI cokme stub'u verilir: yanlislikla cagrilirsa adiyla durur.
void missing_vk_function() { std::fprintf(stderr, "[editor-ui] bu instance'ta olmayan Vulkan fonksiyonu cagrildi (WSI?)\n"); std::abort(); }
PFN_vkVoidFunction imgui_loader(const char *name, void *user) {
  LoaderCtx *c = static_cast<LoaderCtx *>(user);
  PFN_vkVoidFunction f = c->api->vkGetInstanceProcAddr(c->instance, name);
  if (!f) f = (PFN_vkVoidFunction)missing_vk_function;
  return f;
}
void check_vk(VkResult r) {
  if (r != VK_SUCCESS) std::fprintf(stderr, "[editor-ui] vulkan hatasi %d\n", (int)r);
}
// GLFW tus kodu -> ImGuiKey (editor icin gereken alt kume).
ImGuiKey map_key(int k) {
  if (k >= GLFW_KEY_A && k <= GLFW_KEY_Z) return (ImGuiKey)(ImGuiKey_A + (k - GLFW_KEY_A));
  if (k >= GLFW_KEY_0 && k <= GLFW_KEY_9) return (ImGuiKey)(ImGuiKey_0 + (k - GLFW_KEY_0));
  switch (k) {
  case GLFW_KEY_TAB: return ImGuiKey_Tab;
  case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
  case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
  case GLFW_KEY_UP: return ImGuiKey_UpArrow;
  case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
  case GLFW_KEY_HOME: return ImGuiKey_Home;
  case GLFW_KEY_END: return ImGuiKey_End;
  case GLFW_KEY_DELETE: return ImGuiKey_Delete;
  case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
  case GLFW_KEY_SPACE: return ImGuiKey_Space;
  case GLFW_KEY_ENTER: return ImGuiKey_Enter;
  case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
  case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
  case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
  case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
  case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
  case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
  case GLFW_KEY_MINUS: return ImGuiKey_Minus;
  case GLFW_KEY_PERIOD: return ImGuiKey_Period;
  case GLFW_KEY_COMMA: return ImGuiKey_Comma;
  default: return ImGuiKey_None;
  }
}
} // namespace

bool EditorUi::init(rhi::Device &dev, VkRenderPass rp, uint32_t subpass, uint32_t image_count, const char *font_ttf, float font_px) {
  dev_ = &dev;
  static LoaderCtx lc;
  lc.api = &dev.api();
  lc.instance = dev.instance();
  if (!ImGui_ImplVulkan_LoadFunctions(dev.caps().api_version, imgui_loader, &lc)) {
    std::snprintf(err_, sizeof err_, "ImGui Vulkan fonksiyonlari yuklenemedi");
    return false;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; // dosya yazma yok (belirlenimli, headless dostu)
  io.LogFilename = nullptr;
  io.BackendPlatformName = "tulpar-window";
  ImGui::StyleColorsDark();
  if (font_ttf && *font_ttf) {
    FILE *f = std::fopen(font_ttf, "rb");
    if (f) { std::fclose(f); io.Fonts->AddFontFromFileTTF(font_ttf, font_px > 8 ? font_px : 18.0f); }
  }
  ImGui_ImplVulkan_InitInfo ii{};
  ii.ApiVersion = dev.caps().api_version;
  ii.Instance = dev.instance();
  ii.PhysicalDevice = dev.physical();
  ii.Device = dev.handle();
  ii.QueueFamily = dev.queue_family();
  ii.Queue = dev.queue();
  ii.DescriptorPoolSize = 32; // dahili havuz (font atlasi + kullanici dokulari)
  ii.MinImageCount = image_count < 2 ? 2 : image_count;
  ii.ImageCount = ii.MinImageCount;
  ii.PipelineInfoMain.RenderPass = rp;
  ii.PipelineInfoMain.Subpass = subpass;
  ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ii.CheckVkResultFn = check_vk;
  ii.MinAllocationSize = 1024 * 1024; // BestPractices: kucuk ayirma uyarisi yok
  if (!ImGui_ImplVulkan_Init(&ii)) {
    std::snprintf(err_, sizeof err_, "ImGui_ImplVulkan_Init basarisiz");
    ImGui::DestroyContext();
    return false;
  }
  ok_ = true;
  return true;
}

void EditorUi::shutdown() {
  if (!ok_) return;
  dev_->api().vkDeviceWaitIdle(dev_->handle());
  ImGui_ImplVulkan_Shutdown();
  ImGui::DestroyContext();
  ok_ = false;
}

void EditorUi::begin_frame(const platform::InputState *in, float width, float height, float dt) {
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(width > 1 ? width : 1, height > 1 ? height : 1);
  io.DeltaTime = dt > 0 ? dt : 1.0f / 60.0f;
  if (in) {
    io.AddMousePosEvent((float)in->mouse_x, (float)in->mouse_y);
    for (int b = 0; b < 3; b++)
      if (in->mouse_down[b] != prev_mouse_[b]) { io.AddMouseButtonEvent(b, in->mouse_down[b]); prev_mouse_[b] = in->mouse_down[b]; }
    const double ds = in->scroll_y - prev_scroll_;
    if (ds != 0) { io.AddMouseWheelEvent(0.0f, (float)ds); prev_scroll_ = in->scroll_y; }
    for (int k = 0; k < 512; k++)
      if (in->key_down[k] != prev_keys_[k]) {
        prev_keys_[k] = in->key_down[k];
        const ImGuiKey ik = map_key(k);
        if (ik != ImGuiKey_None) io.AddKeyEvent(ik, in->key_down[k]);
        if (k == GLFW_KEY_LEFT_CONTROL || k == GLFW_KEY_RIGHT_CONTROL) io.AddKeyEvent(ImGuiMod_Ctrl, in->key_down[k]);
        if (k == GLFW_KEY_LEFT_SHIFT || k == GLFW_KEY_RIGHT_SHIFT) io.AddKeyEvent(ImGuiMod_Shift, in->key_down[k]);
        if (k == GLFW_KEY_LEFT_ALT || k == GLFW_KEY_RIGHT_ALT) io.AddKeyEvent(ImGuiMod_Alt, in->key_down[k]);
      }
    for (uint32_t i = 0; i < in->char_count; i++) io.AddInputCharacter(in->chars[i]);
  }
  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();
}

void EditorUi::end_frame() {
  ImGui::Render();
  ImDrawData *dd = ImGui::GetDrawData();
  stats_ = EditorUiStats{};
  if (dd) { stats_.vertices = (uint32_t)dd->TotalVtxCount; stats_.indices = (uint32_t)dd->TotalIdxCount; stats_.draw_lists = (uint32_t)dd->CmdLists.Size; }
}

void EditorUi::record(VkCommandBuffer cb) {
  ImDrawData *dd = ImGui::GetDrawData();
  if (dd) ImGui_ImplVulkan_RenderDrawData(dd, cb);
}

bool EditorUi::wants_mouse() const { return ok_ && ImGui::GetIO().WantCaptureMouse; }
bool EditorUi::wants_keyboard() const { return ok_ && ImGui::GetIO().WantCaptureKeyboard; }

} // namespace tulpar::engine::app
