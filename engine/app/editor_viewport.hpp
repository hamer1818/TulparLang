// L6 APP — Editor goruntu kapisi (viewport): 3B sahne artik SWAPCHAIN'e degil
// KENDI dokusuna cizilir, ImGui o dokuyu bir panelde gosterir.
//
// Neden: editor_app.cpp bugun ucunu de ayni renk subpass'ine kaydediyor
//   c->r->record(cb); c->r->ui_record(cb); c->ui->record(cb);
// yani 3B tam ekran, ImGui uzerinde yuzuyor — "debug kaplamali oyun" modeli.
// Editorde 3B bir PENCERE ICERIGIDIR: kendi olcusu, kendi en-boy orani, kendi
// fare uzayi vardir. Bu sinif o hedefi (renk + derinlik + gecis + ImGui dokusu)
// tutar; sahneyi KENDISI cizmez, yalniz gecisi acar/kapar.
//
// KURULUM SIRASI — zorunlu, sebebi olculdu:
//   vp.init(dev, vc, w, h);                      // 1. ONCE viewport (gecisi kurar)
//   ren.init(dev, arena, vp.render_pass(), rc);  // 2. renderer BU gecise gore
//   ui.init(dev, swap.render_pass(), ...);       // 3. ImGui SWAPCHAIN gecisine
// renderer'a swapchain'in gecisi verilip cizim viewport gecisinde kaydedilirse
// Vulkan bunu REDDEDER: gecis uyumlulugu subpass BAGIMLILIKLARINI da kapsar
// ("otherwise identical except for ... layouts / load-store"), yalniz bicimleri
// degil. Dogrulama katmaniyla olculdu (2026-09-17, RTX 5080):
//   "vkCmdDrawIndexed(): pDependencies[2].srcStageMask is incompatible between
//    VkRenderPass A (from VkCommandBuffer) and VkRenderPass B (from VkPipeline)"
// Bu yuzden buradaki bagimliliklar swapchain'inkinin kopyasi DEGIL, ImGui'nin
// ornekleme tehlikesini anlatan KENDI listesidir — ve boru hatlarini kuran da
// bu gecistir. render_pass() nesnenin omru boyunca AYNI kalir (yeniden boyutlanma
// yalniz imge/framebuffer'i yeniler), yani renderer'in boru hatlari gecerli kalir.
//
// Kullanim (cagiranin karesi):
//   vp.resize(panel_w, panel_h);              // yalniz gercekten degistiyse yeniden yaratir
//   ren.set_render_size(vp.width(), vp.height());
//   ren.record_shadow(cb);                    // kendi gecisi — viewport gecisinden ONCE
//   if (vp.begin_pass(cb)) { ren.record(cb); ren.ui_record(cb); vp.end_pass(cb); }
//   swap.begin_render_pass(fc); ui.record(fc.cmd); swap.end_frame(fc);
//   // panelde:  ImGui::Image(vp.texture_id(), ImVec2(vp.width(), vp.height()));
//
// Ayirma sozlesmesi: KARE ICINDE hicbir sey ayrilmaz (begin_pass/end_pass
// yalniz komut kaydeder). Ayirma SADECE init() ve gercek boyut degisiminde
// olur; sayisi recreate_count() ile raporlanir.
#pragma once
#include <cstdint>

#include "rhi/device.hpp"

namespace tulpar::engine::app {

struct EditorViewportConfig {
  // Hedefin renk bicimi SWAPCHAIN'IN BICIMI OLMALIDIR (rhi::Swapchain::color_format()).
  // Iki ayri sebep, ikisi de zorunlu:
  //  1) Boru hatti uyumlulugu: renderer::Renderer::init() kendisine VERILEN
  //     gecise gore boru hatti kurar. Baska bir gecisle kaydedilebilmesi icin
  //     gecisler UYUMLU olmali (ayni attachment bicimleri, ayni ornek sayisi,
  //     ayni subpass yapisi). Bu yuzden asagidaki gecis swapchain'inkinin
  //     birebir ikizidir: 2 subpass (derinlik on-gecisi -> renk), D32_SFLOAT
  //     derinlik. Bicim tutmazsa dogrulama katmani gecisi reddeder.
  //  2) Renk dogrulugu: _SRGB hedefte donanim yazarken kodlar, ImGui okurken
  //     COZER, sonra swapchain yeniden kodlar — gidis donus birimdir. UNORM
  //     swapchain'de renderer shader'da kodlar, ImGui ham okur — o da birim.
  //     Iki bicim KARISIRSA panel ya solar ya koyulasir.
  VkFormat color_format = VK_FORMAT_B8G8R8A8_SRGB;
  VkFormat depth_format = VK_FORMAT_D32_SFLOAT; // swapchain ile ayni olmali
  uint32_t min_size = 16;                       // panel kucultulunce (0 olcu gecersiz)
  uint32_t max_size = 4096;                     // cihaz limitiyle ayrica kenetlenir
  float clear[4] = {0.05f, 0.06f, 0.08f, 1.0f}; // dogrusal; post aciksa RendererConfig::post_clear ile ESLESMELI
  // ImGui dokusu kaydedilsin mi. false: headless/testte (ImGui Vulkan arka ucu
  // kurulmamisken) hedef yine de calisir, texture_id() 0 doner.
  bool imgui_texture = true;
};

// Panelin EKRAN dikdortgeni (ImGui uzayi: sol-ust koken, mantiksal piksel).
// Tipik: pos = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail().
struct ViewportRect {
  float x = 0, y = 0, w = 0, h = 0;
};

// Fare -> viewport donusumunun sonucu.
//
// SOZLESME: panel disindaki (ya da dejenere dikdortgen / kurulmamis viewport)
// bir konum icin valid = false ve u = v = x = y = -1 doner. Bilerek 0 DEGIL:
// 0 GECERLI bir piksel (sol-ust kose), yani `valid`i denetlemeyi unutan cagiran
// sessizce sol-ust koseden isin atar ve "bazen yanlis nesne seciliyor" diye
// gorunur. -1 ile atilan isin goruntu hacminin disinda kalir: hata gizlenmez.
// Kirpilmaz da (clamp yok) — panel disi bir tiklama VIEWPORT TIKLAMASI DEGILDIR.
// Kenar kurali yari-acik: [x, x+w) x [y, y+h) — sag/alt kenar DISARIDIR
// (bitisik panellerle cakisma olmasin).
struct ViewportPick {
  bool valid = false;
  float u = -1, v = -1; // [0,1), sol-ust koken (Vulkan/ImGui yonu)
  float x = -1, y = -1; // doku pikseli, [0,w) x [0,h)
};

// Saf donusum — CIHAZ GEREKTIRMEZ, bu yuzden Vulkan'siz test edilebilir
// (secim kapisi bunu olcer; uye map_mouse() aynisini kendi olcusuyle cagirir).
ViewportPick viewport_map_mouse(const ViewportRect &panel, float mouse_x, float mouse_y, uint32_t tex_w, uint32_t tex_h);

class EditorViewport {
public:
  // Gecis + ilk hedef. dev omru bu nesneden UZUN olmali; shutdown() cihaz
  // kapanmadan ONCE ve ImGui arka ucu (EditorUi::shutdown) kapanmadan once cagrilir.
  bool init(rhi::Device &dev, const EditorViewportConfig &cfg, uint32_t width, uint32_t height);
  void shutdown();

  // Panel olcusu. Hedefi YALNIZ olcu gercekten degistiyse yeniden yaratir
  // (her karede yeniden yaratmak kare suresini yer: vkDeviceWaitIdle + 2 imge
  // + 2 gorunum + framebuffer + descriptor). Donus: yeniden yaratildi mi.
  // Olcu [min_size, min(max_size, cihaz limiti)] araligina kenetlenir, yani
  // kucultulmus/dev panel hata degil kenetlenmis olcudur — width()/height()
  // her zaman GERCEK doku olcusudur, panelinki degil.
  bool resize(uint32_t width, uint32_t height);

  // Kare kaydi. begin_pass: gecisi acar (subpass 0 = derinlik on-gecisi),
  // viewport/scissor'i doku olcusune ayarlar. false = hedef yok (kaydetme).
  // Arasina cagiran renderer::Renderer::record() (kendi icinde vkCmdNextSubpass
  // yapar) + ui_record() kaydeder. ImGui'nin KENDISI buraya DEGIL, cagiranin
  // swapchain gecisine kaydedilir — panel 3B'nin ustunde degil, DISINDADIR.
  bool begin_pass(VkCommandBuffer cb);
  void end_pass(VkCommandBuffer cb);

  // Fare (ekran/ImGui uzayi) -> viewport. Bkz. ViewportPick sozlesmesi.
  ViewportPick map_mouse(const ViewportRect &panel, float mouse_x, float mouse_y) const;

  // ImGui::Image(...) icin doku kimligi (ImTextureID == ImU64). 0 = yok
  // (imgui_texture kapali ya da arka uc kurulmamis). ImGui basligi BU BASLIGA
  // sizmasin diye ham tamsayi: ImGui::Image((ImTextureID)vp.texture_id(), sz).
  uint64_t texture_id() const { return (uint64_t)tex_set_; }
  VkDescriptorSet texture_set() const { return tex_set_; }

  VkRenderPass render_pass() const { return rp_; }
  VkImage color_image() const { return color_; }   // end_pass sonrasi duzen: SHADER_READ_ONLY_OPTIMAL
  VkImageView color_view() const { return color_view_; }
  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }
  float aspect() const { return h_ ? (float)w_ / (float)h_ : 1.0f; }
  bool ok() const { return ok_; }
  const char *last_error() const { return err_; }

  // Olcum: kac kez yeniden yaratildi / kac kez olcu istendi. Ikisi arasindaki
  // fark "gereksiz yeniden yaratma yok" iddiasinin KANITIDIR (kapi bunu olcer).
  uint32_t recreate_count() const { return recreates_; }
  uint32_t resize_requests() const { return requests_; }

private:
  bool create_render_pass();
  bool create_targets(uint32_t w, uint32_t h);
  void destroy_targets();
  bool fail(const char *what, VkResult r);

  rhi::Device *dev_ = nullptr;
  EditorViewportConfig cfg_{};
  VkRenderPass rp_ = VK_NULL_HANDLE;
  VkImage color_ = VK_NULL_HANDLE, depth_ = VK_NULL_HANDLE;
  VkImageView color_view_ = VK_NULL_HANDLE, depth_view_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc color_mem_{}, depth_mem_{}; // allocate_dedicated: yeniden boyutlanma SIZDIRMAZ
  VkFramebuffer fb_ = VK_NULL_HANDLE;
  VkDescriptorSet tex_set_ = VK_NULL_HANDLE; // ImGui_ImplVulkan_AddTexture
  uint32_t w_ = 0, h_ = 0;
  uint32_t limit_ = 4096; // cihazin maxImageDimension2D / maxFramebuffer*
  uint32_t recreates_ = 0, requests_ = 0;
  bool ok_ = false;
  // 320: `dev_->last_error()` 256 bayta kadar olabiliyor ve mesajlar onun
  // onune bir onek koyuyor ("renk bellegi: ..."). 192'de GCC
  // -Wformat-truncation ile hakli olarak uyariyordu; kesilen hata mesaji
  // teshisi zorlastirir, tampon kaynagi tam alacak kadar buyutuldu.
  char err_[320] = {0};
};

} // namespace tulpar::engine::app
