# GLFW (yalnız başlık, vendored)

`GLFW/glfw3.h` — GLFW 3.5 başlığı, zlib (LICENSE.md). Kütüphane **dlopen** ile yüklenir
(`libglfw.so.3` / `libglfw.3.dylib`), link bağımlılığı yok (Vulkan loader ile aynı desen).
Masaüstü geliştirme penceresi içindir (plan: masaüstü ürün değil, geliştirme platformu); Android'de
pencere Kotlin host'tan gelir, bu dosya derlenmez. Sistemde GLFW yoksa pencere GÖRÜNÜR hata verir.
