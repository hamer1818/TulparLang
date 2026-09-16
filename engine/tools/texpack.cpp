// engine_texpack — sahne derleyicisinin doku dilimi: PNG/JPG -> ASTC mip zinciri -> .ktx2
//   engine_texpack in.png out.ktx2 [--tur albedo|orm|normal] [--block 4x4|5x5|6x6|8x8]
//                  [--quality 0..100] [--linear] [--onbellek <dizin>] [--onbelleksiz]
// Mip'ler sRGB-dogru kutu filtreyle (dogrusal uzayda ortalama). Sonunda cozup PSNR basar (olcum).
//
// --tur NEDEN VAR: bir doku "renk" olabilir de olmayabilir de, ve ikisi AYNI
// SEKILDE sikistirilamaz.
//   albedo : renk. sRGB profili; mip'ler dogrusal uzayda ortalanir.
//   orm    : glTF metallicRoughness (R occlusion, G puruzluluk, B metal). VERI,
//            renk DEGIL — sRGB profiliyle sikistirmak degerleri kaydirir ve
//            "goruntu biraz yanlis, hicbir sey kizarmaz" sinifinda bir hata
//            uretir. Dogrusal profil, dogrusal mip.
//   normal : teget uzayi normali. Dogru olcut PSNR degil ACISAL hatadir:
//            astcenc'in MAP_NORMAL kipi kullanilir (iki kanal, girdi rrrg).
//            Olculdu (content_astc_normal_map_encoding, ayni kaynak, acisal
//            hata ortalamasi): 4x4'te 1.035 vs 1.710 derece (0.61 kat),
//            6x6'da 3.395 vs 4.588 (0.74), 8x8'de 3.608 vs 6.870 (0.53) —
//            yani her blok boyunda MAP_NORMAL kazaniyor. Uretilen .ktx2 bu
//            duzeni KVD'de ("TULPAR_normalXY") tasir; tuketici X'i .r'den,
//            Y'yi .a'dan alip Z'yi kurmak zorundadir.
// ICE AKTARMA ONBELLEGI (Faz 6): anahtar = girdi baytlari + ayarlar (blok,
// kalite, sRGB) + arac surumu. Ayni girdi ikinci kez SIKISTIRILMAZ, urun
// onbellekten kopyalanir; girdi ya da ayar degisince anahtar degisir ve is
// yeniden yapilir.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stb_image.h>

#include "content/importer.hpp"
#include "content/ktx2.hpp"
#include "core/memory/arena.hpp"

using namespace tulpar::engine;

namespace {
// 2: --tur eklendi (orm/normal icin profil ve kanal duzeni degisti). SURUM
// ARTMAZSA eski onbellek isabet eder ve normal harita icin ALBEDO urunu geri
// doner — sessiz yanlis sonuc.
constexpr uint32_t kTexpackVersion = 2;
// Anahtara giren ayarlar: tum alanlar 4 baytlik, dolgu yok (belirlenimli bayt).
// `map` ANAHTARA GIRMELI: ayni PNG'yi once albedo sonra normal olarak paketlemek
// AYNI anahtari uretirse ikincisi birincinin urununu alir.
struct TexSettings {
  uint32_t block_w, block_h, srgb;
  float quality;
  uint32_t map;
};
float srgb_to_lin(uint8_t c) { const float x = c / 255.0f; return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); }
uint8_t lin_to_srgb(float x) {
  x = x < 0 ? 0 : x > 1 ? 1 : x;
  const float s = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
  return (uint8_t)(s * 255.0f + 0.5f);
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 3) { std::fprintf(stderr, "kullanim: engine_texpack in.png out.ktx2 [--tur albedo|orm|normal] [--block 4x4] [--quality 60] [--linear]\n"); return 2; }
  const char *in = argv[1], *out = argv[2];
  uint32_t bw = 4, bh = 4;
  float quality = 60.0f;
  bool srgb = true, use_cache = true;
  content::AstcMap map = content::AstcMap::Color;
  const char *cache_dir = nullptr;
  const char *tur = "albedo";
  bool tur_verildi = false, linear_verildi = false;
  for (int i = 3; i < argc; i++) {
    if (!std::strcmp(argv[i], "--block") && i + 1 < argc) { unsigned x = 4, y = 4; if (std::sscanf(argv[++i], "%ux%u", &x, &y) == 2) { bw = x; bh = y; } }
    else if (!std::strcmp(argv[i], "--quality") && i + 1 < argc) quality = (float)std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--linear")) { linear_verildi = true; }
    else if (!std::strcmp(argv[i], "--tur") && i + 1 < argc) {
      tur_verildi = true;
      tur = argv[++i];
      if (!std::strcmp(tur, "albedo")) { map = content::AstcMap::Color; srgb = true; }
      else if (!std::strcmp(tur, "orm")) { map = content::AstcMap::Data; srgb = false; }
      // Kip SECIMI tek yerde: content::kAstcNormalUsesMapMode (ktx2.hpp), degeri
      // content_astc_normal_map_encoding kapisinin olcumuyle konmustur.
      else if (!std::strcmp(tur, "normal")) {
        map = content::kAstcNormalUsesMapMode ? content::AstcMap::Normal : content::AstcMap::Data;
        srgb = false;
      }
      else { std::fprintf(stderr, "bilinmeyen --tur '%s' (albedo|orm|normal)\n", tur); return 2; }
    }
    else if (!std::strcmp(argv[i], "--onbellek") && i + 1 < argc) cache_dir = argv[++i];
    else if (!std::strcmp(argv[i], "--onbelleksiz")) use_cache = false;
  }
  // --linear, --tur'un ESKI ve kaba hali (yalniz sRGB'yi kapatiyordu); geriye
  // donuk calisir ve --tur orm demektir. Ikisi birlikte verilip CELISIYORSA
  // "son bayrak kazanir" YAPILMAZ, ACIKCA reddedilir: sessizce birini secmek
  // tam olarak "goruntu biraz yanlis, hicbir sey kizarmaz" sinifini uretir.
  if (linear_verildi) {
    if (tur_verildi && map == content::AstcMap::Color) {
      std::fprintf(stderr, "--linear ile --tur albedo celisiyor (--linear = --tur orm)\n");
      return 2;
    }
    if (!tur_verildi) { map = content::AstcMap::Data; srgb = false; tur = "orm"; }
  }
  static SystemArena sys;
  if (!sys.reserve(256u << 20, "texpack")) { std::fprintf(stderr, "arena\n"); return 1; }
  // --- Onbellek: isabet varsa TEK BIR SIKISTIRMA BILE yapmadan cikilir.
  content::ImportCache cache;
  content::ImportProduct prod;
  bool cache_on = false;
  if (use_cache && content::import_cache_init(cache, cache_dir)) {
    TexSettings st{bw, bh, srgb ? 1u : 0u, quality, (uint32_t)map};
    const uint64_t key = content::import_key(in, &st, sizeof st, kTexpackVersion);
    cache_on = key != 0;
    if (cache_on && content::import_lookup(cache, key, ".ktx2", &prod)) {
      if (content::import_copy(prod.path, out)) {
        std::printf("%s: onbellek ISABET (%016llx) — sikistirma ATLANDI, %llu bayt kopyalandi\n", out, (unsigned long long)key,
                    (unsigned long long)prod.size);
        return 0;
      }
      std::fprintf(stderr, "onbellek urunu kopyalanamadi, yeniden sikistiriliyor\n");
    }
  }
  FILE *f = std::fopen(in, "rb");
  if (!f) { std::fprintf(stderr, "acilamadi: %s\n", in); return 1; }
  std::fseek(f, 0, SEEK_END); const long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  uint8_t *file = sys.alloc_array<uint8_t>((uint32_t)(sz > 0 ? sz : 1));
  const size_t got = sz > 0 ? std::fread(file, 1, (size_t)sz, f) : 0;
  std::fclose(f);
  int w = 0, h = 0, comp = 0;
  stbi_uc *rgba0 = stbi_load_from_memory(file, (int)got, &w, &h, &comp, 4);
  if (!rgba0 || w <= 0 || h <= 0) { std::fprintf(stderr, "goruntu cozulemedi: %s\n", in); return 1; }
  // Mip zinciri (kutu filtre, dogrusal uzay).
  const uint8_t *levels[content::Ktx2Image::kMaxLevels];
  uint32_t lw[content::Ktx2Image::kMaxLevels], lh[content::Ktx2Image::kMaxLevels];
  uint32_t nlev = 0;
  levels[0] = rgba0; lw[0] = (uint32_t)w; lh[0] = (uint32_t)h; nlev = 1;
  while ((lw[nlev - 1] > 1 || lh[nlev - 1] > 1) && nlev < content::Ktx2Image::kMaxLevels) {
    const uint32_t pw = lw[nlev - 1], ph = lh[nlev - 1];
    const uint32_t nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
    uint8_t *dst = sys.alloc_array<uint8_t>(nw * nh * 4);
    const uint8_t *src = levels[nlev - 1];
    for (uint32_t y = 0; y < nh; y++)
      for (uint32_t x = 0; x < nw; x++) {
        float acc[4] = {0, 0, 0, 0};
        int cnt = 0;
        for (uint32_t dy = 0; dy < 2; dy++)
          for (uint32_t dx = 0; dx < 2; dx++) {
            const uint32_t sx = x * 2 + dx < pw ? x * 2 + dx : pw - 1, sy = y * 2 + dy < ph ? y * 2 + dy : ph - 1;
            const uint8_t *p = src + (sy * pw + sx) * 4;
            // sRGB donusumu YALNIZ renk dokusunda; ORM/normal ham bayt ortalamasi
            // (o baytlar zaten dogrusal sayidir, isik degil).
            for (int c = 0; c < 3; c++) acc[c] += srgb ? srgb_to_lin(p[c]) : p[c] / 255.0f;
            acc[3] += p[3] / 255.0f;
            cnt++;
          }
        uint8_t *o = dst + (y * nw + x) * 4;
        for (int c = 0; c < 3; c++) o[c] = srgb ? lin_to_srgb(acc[c] / (float)cnt) : (uint8_t)(acc[c] / (float)cnt * 255.0f + 0.5f);
        o[3] = (uint8_t)(acc[3] / (float)cnt * 255.0f + 0.5f);
      }
    levels[nlev] = dst; lw[nlev] = nw; lh[nlev] = nh; nlev++;
  }
  const uint8_t *blocks[content::Ktx2Image::kMaxLevels];
  uint32_t sizes[content::Ktx2Image::kMaxLevels];
  uint32_t total = 0;
  for (uint32_t i = 0; i < nlev; i++) {
    uint8_t *b = nullptr; uint32_t n = 0;
    if (!content::astc_encode_rgba(sys, levels[i], lw[i], lh[i], bw, bh, srgb, quality, &b, &n, map)) { std::fprintf(stderr, "ASTC sikistirma (seviye %u)\n", i); return 1; }
    blocks[i] = b; sizes[i] = n; total += n;
  }
  VkFormat fmt = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
  if (bw == 4) fmt = srgb ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  else if (bw == 5) fmt = srgb ? VK_FORMAT_ASTC_5x5_SRGB_BLOCK : VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
  else if (bw == 6) fmt = srgb ? VK_FORMAT_ASTC_6x6_SRGB_BLOCK : VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
  else if (bw == 8) fmt = srgb ? VK_FORMAT_ASTC_8x8_SRGB_BLOCK : VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
  else { std::fprintf(stderr, "desteklenmeyen blok %ux%u\n", bw, bh); return 1; }
  const bool normal_xy = map == content::AstcMap::Normal;
  if (!content::ktx2_write(out, fmt, (uint32_t)w, (uint32_t)h, nlev, blocks, sizes, bw, bh, normal_xy)) { std::fprintf(stderr, "yazilamadi: %s\n", out); return 1; }
  if (cache_on && prod.key && content::import_copy(out, prod.path)) content::import_store(cache, prod);
  // Olcum: seviye 0 coz. Normal haritasinda PSNR YANLIS olcuttur (kanal duzeni
  // rrrg, ve onemli olan acisal sapma) — orada ortalama ACISAL hata basilir.
  uint8_t *dec = nullptr;
  double psnr = 0, ang = 0;
  if (content::astc_decode_rgba(sys, blocks[0], sizes[0], (uint32_t)w, (uint32_t)h, bw, bh, srgb, &dec, map)) {
    if (normal_xy) {
      double acc = 0;
      for (int i = 0; i < w * h; i++) {
        float sx = rgba0[i * 4 + 0] / 255.0f * 2.0f - 1.0f, sy = rgba0[i * 4 + 1] / 255.0f * 2.0f - 1.0f,
              sz = rgba0[i * 4 + 2] / 255.0f * 2.0f - 1.0f;
        float dx = dec[i * 4 + 0] / 255.0f * 2.0f - 1.0f, dy = dec[i * 4 + 1] / 255.0f * 2.0f - 1.0f,
              dz = dec[i * 4 + 2] / 255.0f * 2.0f - 1.0f;
        const float sl = std::sqrt(sx * sx + sy * sy + sz * sz), dl = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (sl < 1e-6f || dl < 1e-6f) { acc += 90.0; continue; }
        float d = (sx * dx + sy * dy + sz * dz) / (sl * dl);
        d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
        acc += std::acos((double)d) * 180.0 / 3.14159265358979;
      }
      ang = acc / (double)(w * h);
    } else {
      psnr = content::rgba_psnr(rgba0, dec, (uint32_t)w, (uint32_t)h);
    }
  }
  char olcum[96];
  if (normal_xy) std::snprintf(olcum, sizeof olcum, "seviye0 ortalama ACISAL hata %.3f derece", ang);
  else std::snprintf(olcum, sizeof olcum, "seviye0 PSNR %.1f dB", psnr);
  std::printf("%s: %dx%d, %u seviye, tur %s, ASTC %ux%u %s%s, kalite %.0f -> %u bayt (RGBA8 %u bayt, %.1fx), %s%s\n", out, w, h,
              nlev, tur, bw, bh, srgb ? "sRGB" : "dogrusal", normal_xy ? " MAP_NORMAL(rrrg)" : "", quality, total,
              (uint32_t)(w * h * 4), (double)(w * h * 4) / (double)(sizes[0] ? sizes[0] : 1), olcum,
              cache_on ? " [onbellege yazildi]" : "");
  stbi_image_free(rgba0);
  return 0;
}
