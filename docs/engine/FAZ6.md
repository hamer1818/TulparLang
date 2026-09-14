# Faz 6 — İçerik Boru Hattı ve Dağıtım (durum)

> İçe aktarma dilimleri (glTF, meshopt/LOD, iskelet) FAZ3.md'de; bu dosya paketleme/sıkıştırma yolunu izler.

## Doku: PNG → ASTC mip zinciri → KTX2 → GPU (2026-09-14, tarama belgesi §13)

**Teslim edilen**
- `engine/third_party/astcenc` (Arm astc-encoder 5.7.0 çekirdeği, Apache-2.0; ISA: x86_64 SSE4.1+POPCNT, AArch64
  NEON, aksi skaler). Araç sıkıştırır; runtime yalnız ASTC LDR olmayan GPU'da (masaüstü NVIDIA) çözer.
- `content/ktx2.hpp/.cpp`: **kendi KTX2 okuyucu/yazıcımız** (~200 satır; libktx vendored değil — Basis/zstd yolu
  yok, supercompression 0, tek yüz 2B); `astc_decode_rgba` / `astc_encode_rgba`; `ktx2_upload`: GPU
  `textureCompressionASTC_LDR` varsa bloklar olduğu gibi (`Renderer::create_texture_levels`, mip başına kopya, blit
  yok), yoksa CPU çözümü → RGBA8 aynı mip zinciri; `rgba_psnr`.
- `rhi::DeviceCaps::texture_compression_astc_ldr` (özellik sorgulanır ve açılır).
- **`engine_texpack`** (`tools/texpack.cpp`, masaüstü): `in.png out.ktx2 [--block 4x4|5x5|6x6|8x8] [--quality]
  [--linear]`; mip'ler sRGB-doğru kutu filtre (doğrusal uzayda ortalama); sonda çözüp seviye-0 PSNR basar (ölçüm).
- Test varlığı `tests/assets/checker_64.png` (`make_test_gltf.py`) → `checker_64.ktx2`: 64×64, 7 seviye, 4x4 sRGB,
  **5488 bayt** (RGBA8 16384; 4×; sadece seviye 0'da 4096).

**Kapı `content_ktx2_astc_decodes_and_uploads`:** konteyner alanları (vkFormat 158, 7 seviye); CPU çözümü PSNR
**999 dB** (dama iki renk ASTC'de tam temsil edilebilir → kayıpsız; kontrol: 8 px kaydırılmış dama 5.4 dB — metrik
ayırt edici); bozuk kimlik reddedilir; GPU yolu 1:1 çizilip geri okunur PSNR 999 dB (masaüstü: CPU çözümü → RGBA8;
Mali: donanım ASTC — telefon USB düştü, bekliyor). Masaüstü 72/72. **Emülatör (gfxstream, ASTC LDR var):** 71/71,
**donanım ASTC yolu** geri okuma PSNR 999 dB — bloklar olduğu gibi yüklendi ve doğru örneklendi.

**Sonraki:** pack formatı (tek dosya, mmap, varlık tablosu), glTF dokularının KTX2'ye yönlendirilmesi
(sahne derleyicisi), 6x6/8x8 kalite-boyut ölçümü Mali'de, lightmap zinciri (xatlas + kendi GPU bake; `lightmapper`
OpenGL tabanlı, Vulkan'a taşınmaz).
