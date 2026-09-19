# `rhi/shaders/wip/` — henüz bağlanmamış shader taslakları

Buradaki GLSL dosyaları **derlenmiyor ve hiçbir boru hattı tarafından
kullanılmıyor.** Bilerek `rhi/shaders/` dışında duruyorlar.

## Neden ayrı dizin

`tools/layout_check.py` ve `tools/compile_shaders.py`, `rhi/shaders/` altındaki
her `.vert/.frag/.comp` için üretilmiş bir `*_spv.h` bulunmasını ve o başlığın
GLSL kaynağıyla **taze** olmasını şart koşar. Bu kapı bilerek katıdır: bayat bir
`*_spv.h`, CPU–GPU yerleşim denetimini de yanıltır (yerleşim eski shader'dan
okunur).

Bu altı dosya `rhi/shaders/` içine konmuştu ama:

- hiçbirinin `*_spv.h` karşılığı yoktu,
- `renderer/` ve `rhi/` içinde **sıfır referansları** vardı,
- toplam 144 satır — altısı da taslak, uygulama değil.

Yani kapıyı kırıyorlardı ve karşılığında hiçbir şey vermiyorlardı. Silmek
yerine buraya alındılar: niyet kayıt altında kalsın, derleme yeşil olsun.

## Bir taslağı gerçekten bağlamak için gerekenler

Shader'ı `rhi/shaders/` içine taşımak **yetmez**. Sırasıyla:

1. Renderer tarafında boru hattı + descriptor set düzeni + push sabitleri.
2. Geçişin render graph'a eklenmesi (girdi/çıktı bağımlılıkları, barrier'lar).
3. `python tools/compile_shaders.py` ile `*_spv.h` üretimi.
4. `python tools/layout_check.py .` ile CPU struct ↔ SPIR-V blok yerleşimi
   denetimi (blok/shader sayıları artar).
5. Ölçüm: hangi cihazda ne kadar kare süresi ekliyor. Mobil hedefte SSAO/SSR
   gibi geçişler bedava değildir.

## Dosyalar

| Dosya | Ne olacaktı |
|---|---|
| `ssao.frag` | Ekran uzayı ortam tıkanıklığı |
| `ssr.frag` | Ekran uzayı yansıma |
| `dof.frag` | Alan derinliği |
| `motion_blur.frag` | Hareket bulanıklığı |
| `color_grading.frag` | Renk derecelendirme (LUT) |
| `deferred_gbuffer.frag` | Ertelenmiş gölgeleme G-buffer |

Bağlama işi `docs/engine/EDITOR-DURUM.md` içinde Faz D kapsamındadır.
