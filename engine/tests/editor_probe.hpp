// Arayuz SONDASI (test yardimcisi): bir ImGui cizim geri cagrisini motorun
// offscreen gecisine, GERCEK editor yazi tipiyle (DejaVuSans, 17 px) ve editor
// temasiyla cizer; istenirse PPM yazar. Amaci: bir arayuz parcasini yazan
// kisi onu GORSUN (engine/tools/ppm2png.py ile PNG'ye cevirip bak). Piksel
// farki/vertex sayisi kapilarin olctugu seydir; goruntu dogrulamanin kendisi.
//
// Kullanim:
//   EditorProbe p; p.width = 800; p.height = 600; p.out_ppm = "/tmp/x.ppm";
//   p.draw = [](void *, uint32_t) { ImGui::Begin("A"); ...; ImGui::End(); };
//   const ProbeStatus st = editor_probe_render(p);
//   if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
// ImGui yerlesimi (boyut, dock, otomatik genislik) IKINCI karede oturur; frames
// varsayilani 2'dir ve son karenin pikselleri/vertex sayisi raporlanir.
#pragma once
#include <cstdint>

namespace tulpar::engine::test {

enum class ProbeStatus { Ok, NoVulkan, Fail };

struct EditorProbe {
  uint32_t width = 800, height = 600; // en fazla 1920x1080
  float font_px = 17.0f;              // editorun kullandigi boyut
  bool with_font = true;              // false: ImGui gomulu yazi tipi (kontrol)
  const char *out_ppm = nullptr;      // nullptr = yazma
  void (*draw)(void *ctx, uint32_t frame) = nullptr;
  void *ctx = nullptr;
  uint32_t frames = 2;
  // Sonuc (son kare):
  uint32_t vertices = 0, indices = 0;
  const uint8_t *pixels = nullptr; // RGBA8, width*height*4; bir sonraki sondaya kadar gecerli
  char err[512] = {0}; // rhi::OffscreenResult::error ile ayni boy (kirpma uyarisi yok)
};

ProbeStatus editor_probe_render(EditorProbe &p);

// Piksel yardimcilari (RGBA8): (x,y) rengini okur; iki tampon arasindaki farkli
// piksel sayisi. Kapilar icin "bir sey cizildi mi"nin olcusu.
void probe_pixel(const EditorProbe &p, uint32_t x, uint32_t y, uint8_t out[4]);
uint32_t probe_diff(const uint8_t *a, const uint8_t *b, uint32_t n_pixels);

} // namespace tulpar::engine::test
