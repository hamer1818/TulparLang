#include "app/editor_ui.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>

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


// ===========================================================================
// Editor mantigi (ImGui'siz): coklu secim, islem gruplari, kaynak tarayici,
// tel gizmolari. Panel kodu (editor_app.cpp) yalniz bunlari cagirir; kapilar
// (tests/test_editor.cpp) ayni fonksiyonlari olcer.
// ===========================================================================

bool Selection::contains(int32_t i) const {
  for (uint32_t k = 0; k < count; k++)
    if (items[k] == i) return true;
  return false;
}
void Selection::set_single(int32_t i) {
  count = 0;
  if (i >= 0) { items[0] = i; count = 1; }
}
void Selection::erase(int32_t i) {
  for (uint32_t k = 0; k < count; k++)
    if (items[k] == i) {
      for (uint32_t j = k; j + 1 < count; j++) items[j] = items[j + 1];
      count--;
      return;
    }
}
bool Selection::toggle(int32_t i) {
  if (i < 0) return false;
  if (contains(i)) { erase(i); return false; }
  if (count >= kMax) return false;
  for (uint32_t k = count; k > 0; k--) items[k] = items[k - 1]; // en son tiklanan ana secili olur
  items[0] = i;
  count++;
  return true;
}
void Selection::after_remove(int32_t removed) {
  erase(removed);
  for (uint32_t k = 0; k < count; k++)
    if (items[k] > removed) items[k]--;
}
uint32_t Selection::sorted_desc(int32_t *out) const {
  for (uint32_t k = 0; k < count; k++) out[k] = items[k];
  for (uint32_t a = 1; a < count; a++) { // yerleştirmeli siralama (n <= 256, ayirma yok)
    const int32_t v = out[a];
    uint32_t b = a;
    while (b > 0 && out[b - 1] < v) { out[b] = out[b - 1]; b--; }
    out[b] = v;
  }
  return count;
}

void OpGroups::push(uint32_t n) {
  if (n == 0) return;
  count_ = cursor_; // yeni eylem yinele kuyrugunu siler (gunluk de siliyor)
  if (count_ >= kCap) {
    for (uint32_t i = 1; i < kCap; i++) sizes_[i - 1] = sizes_[i];
    count_ = kCap - 1;
  }
  sizes_[count_++] = n;
  cursor_ = count_;
}
uint32_t OpGroups::undo_size() { return cursor_ ? sizes_[--cursor_] : 1u; }
uint32_t OpGroups::redo_size() { return cursor_ < count_ ? sizes_[cursor_++] : 1u; }

namespace {
bool ends_with_ci(const char *s, const char *suf) {
  const size_t ls = std::strlen(s), lf = std::strlen(suf);
  if (lf > ls) return false;
  for (size_t i = 0; i < lf; i++) {
    char a = s[ls - lf + i], b = suf[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}
} // namespace

uint32_t editor_scan_assets(const char *dir, const content::SceneDesc &d, AssetFile *out, uint32_t cap) {
  if (!dir || !*dir || !out || cap == 0) return 0;
  DIR *dp = ::opendir(dir);
  if (!dp) return 0;
  uint32_t n = 0;
  for (struct dirent *de = ::readdir(dp); de; de = ::readdir(dp)) {
    const char *nm = de->d_name;
    if (nm[0] == '.') continue; // gizli + . / ..
    if (!ends_with_ci(nm, ".gltf") && !ends_with_ci(nm, ".glb")) continue;
    if (std::strlen(nm) >= content::kScenePathLen) continue;
    if (n >= cap) break;
    AssetFile f;
    std::snprintf(f.name, sizeof f.name, "%s", nm);
    for (uint32_t i = 0; i < d.asset_count; i++)
      if (!std::strcmp(d.assets[i], nm)) { f.in_scene = true; f.index = (int32_t)i; break; }
    uint32_t k = n; // ada gore sirali ekle: readdir sirasi dosya sistemine bagli
    while (k > 0 && std::strcmp(out[k - 1].name, f.name) > 0) { out[k] = out[k - 1]; k--; }
    out[k] = f;
    n++;
  }
  ::closedir(dp);
  return n;
}

uint32_t editor_add_asset_entity(content::SceneDesc &d, content::SceneHistory &h, const char *file, Vec3 pos, int32_t *out_asset) {
  if (out_asset) *out_asset = -1;
  if (!file || !*file) return 0;
  const int32_t a = d.add_asset(file);
  if (a < 0) return 0; // kaynak tablosu dolu
  if (out_asset) *out_asset = a;
  char stem[content::kSceneNameLen] = {0};
  std::snprintf(stem, sizeof stem, "%s", file);
  for (char *q = stem; *q; q++)
    if (*q == '.') { *q = 0; break; }
  content::SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "%.20s_%u", stem, d.entity_count + 1);
  e.pos = pos;
  e.components = content::kSceneModel;
  e.asset = a;
  return h.add_entity(d, e) ? 1u : 0u;
}

uint32_t selection_commit(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n,
                          const content::SceneEntity *before, const content::SceneEntity *after) {
  uint32_t ops = 0;
  for (uint32_t k = 0; k < n; k++) {
    const int32_t i = sel[k];
    if (i < 0 || i >= (int32_t)d.entity_count) continue;
    d.entities[i] = before[k]; // gunluk once/sonra'yi kendisi kaydeder
    if (h.set_entity(d, (uint32_t)i, after[k])) ops++;
  }
  return ops;
}

uint32_t selection_translate(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n, Vec3 delta) {
  uint32_t ops = 0;
  for (uint32_t k = 0; k < n; k++) {
    const int32_t i = sel[k];
    if (i < 0 || i >= (int32_t)d.entity_count) continue;
    content::SceneEntity after = d.entities[i];
    after.pos += delta;
    if (h.set_entity(d, (uint32_t)i, after)) ops++;
  }
  return ops;
}

uint32_t selection_remove(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n) {
  int32_t idx[Selection::kMax];
  const uint32_t m = n < Selection::kMax ? n : Selection::kMax;
  for (uint32_t k = 0; k < m; k++) idx[k] = sel[k];
  for (uint32_t a = 1; a < m; a++) { // buyukten kucuge: silinen indeks sonrakileri kaydirir
    const int32_t v = idx[a];
    uint32_t b = a;
    while (b > 0 && idx[b - 1] < v) { idx[b] = idx[b - 1]; b--; }
    idx[b] = v;
  }
  uint32_t ops = 0;
  int32_t prev = -1;
  for (uint32_t k = 0; k < m; k++) {
    if (idx[k] < 0 || idx[k] == prev || idx[k] >= (int32_t)d.entity_count) continue;
    prev = idx[k];
    if (h.remove_entity(d, (uint32_t)idx[k])) ops++;
  }
  return ops;
}

namespace {
// Tel kutu: 12 kenar, her biri ince eksen hizali kutu (birim kup +-0.5).
void wire_box(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 c, Vec3 half, Vec3 color, float th, uint32_t *n) {
  const Vec3 s = half * 2.0f + Vec3{th, th, th};
  for (int a = 0; a < 4; a++) {
    const float y = (a & 1) ? half.y : -half.y, z = (a & 2) ? half.z : -half.z;
    ren.draw(cube, Mat4::translate({c.x, c.y + y, c.z + z}) * Mat4::scale({s.x, th, th}), color);
    (*n)++;
  }
  for (int a = 0; a < 4; a++) {
    const float x = (a & 1) ? half.x : -half.x, z = (a & 2) ? half.z : -half.z;
    ren.draw(cube, Mat4::translate({c.x + x, c.y, c.z + z}) * Mat4::scale({th, s.y, th}), color);
    (*n)++;
  }
  for (int a = 0; a < 4; a++) {
    const float x = (a & 1) ? half.x : -half.x, y = (a & 2) ? half.y : -half.y;
    ren.draw(cube, Mat4::translate({c.x + x, c.y + y, c.z}) * Mat4::scale({th, th, s.z}), color);
    (*n)++;
  }
}
// a -> b arasi ince kutu: sutunlar (sag, yukari, yon) * kalinlik/uzunluk.
Mat4 segment_matrix(Vec3 a, Vec3 b, float th) {
  Vec3 dv = b - a;
  float len = length(dv);
  if (len < 1e-6f) { dv = {0, 0, 1e-6f}; len = 1e-6f; }
  const Vec3 f = dv * (1.0f / len);
  const Vec3 up = std::fabs(f.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  const Vec3 r = normalize(cross(up, f));
  const Vec3 u = cross(f, r);
  Mat4 m;
  m.m[0][0] = r.x * th; m.m[0][1] = r.y * th; m.m[0][2] = r.z * th; m.m[0][3] = 0;
  m.m[1][0] = u.x * th; m.m[1][1] = u.y * th; m.m[1][2] = u.z * th; m.m[1][3] = 0;
  m.m[2][0] = f.x * len; m.m[2][1] = f.y * len; m.m[2][2] = f.z * len; m.m[2][3] = 0;
  m.m[3][0] = (a.x + b.x) * 0.5f; m.m[3][1] = (a.y + b.y) * 0.5f; m.m[3][2] = (a.z + b.z) * 0.5f; m.m[3][3] = 1;
  return m;
}
// Ok: govde + uc (kalin kisa kutu). Donus: cizim sayisi.
uint32_t arrow(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 a, Vec3 b, Vec3 color, float th) {
  const Vec3 dv = b - a;
  const float len = length(dv);
  if (len < 1e-4f) return 0;
  const Vec3 f = dv * (1.0f / len);
  const Vec3 neck = a + f * (len * 0.82f);
  ren.draw(cube, segment_matrix(a, neck, th), color);
  ren.draw(cube, segment_matrix(neck, b, th * 3.0f), color);
  return 2;
}
} // namespace

uint32_t editor_draw_gizmos(renderer::Renderer &ren, renderer::MeshHandle cube, const content::SceneDesc &d, const int32_t *sel,
                            uint32_t n, const GizmoOptions &o) {
  if (!cube.valid()) return 0;
  const float th = o.thickness > 0.005f ? o.thickness : 0.005f;
  uint32_t draws = 0;
  if (o.light_radius) {
    for (uint32_t i = 0; i < d.entity_count; i++) {
      const content::SceneEntity &e = d.entities[i];
      if (!(e.components & content::kSceneLight)) continue;
      const Mat4 m = content::scene_entity_matrix(e);
      const Vec3 p{m.m[3][0], m.m[3][1], m.m[3][2]};
      bool is_sel = false;
      for (uint32_t k = 0; k < n && !is_sel; k++) is_sel = sel && sel[k] == (int32_t)i;
      const float r = e.light_radius;
      wire_box(ren, cube, p, {r, r, r}, e.light_color * (is_sel ? 1.0f : 0.4f), th, &draws);
    }
  }
  if (o.shadow_volume) {
    const float r = d.shadow_radius;
    wire_box(ren, cube, d.shadow_center, {r, r, r}, {0.25f, 0.6f, 1.0f}, th, &draws);
  }
  if (o.sun_dir) {
    const Vec3 dir = normalize(d.sun_dir); // isiga dogru (shader: dot(n, light_dir))
    const float len = d.shadow_radius > 1.0f ? d.shadow_radius * 0.8f : 4.0f;
    draws += arrow(ren, cube, d.shadow_center, d.shadow_center + dir * len, {1.0f, 0.85f, 0.25f}, th * 1.5f);
  }
  return draws;
}

} // namespace tulpar::engine::app
