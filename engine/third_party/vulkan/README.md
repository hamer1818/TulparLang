# Vulkan-Headers (vendored)

Khronos Vulkan-Headers **v1.4.351**, Apache-2.0 (LICENSE.md). Yalnız C başlıkları:
`vulkan/vulkan.h`, `vulkan_core.h`, `vk_platform.h`, `vulkan_android.h`, `vulkan_metal.h` ve
`vk_video/*.h` (vulkan_core.h onları koşulsuz içerir). C++ (`vulkan.hpp`) kasıtlı olarak yok.

Neden vendor: derleme makinesinde/CI'da `vulkan-headers` paketi gerekmesin (raylib/sqlite ile
aynı desen). Loader (`libvulkan.so.1` / `libvulkan.1.dylib`) **dlopen** ile yüklenir, link
zamanı bağımlılık yok; loader yoksa RHI testleri GÖRÜNÜR şekilde atlanır (ATLANDI), sessiz değil.
Güncelleme: aynı sürüm etiketiyle tarball indir, aynı dosyaları kopyala, bu satırı güncelle.
