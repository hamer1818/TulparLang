---
tags: [platform, games]
---

# Android Hedefi

`tulpar build --target=android game.tpr out` — tame/arcade/scene3d oyunlarını
NativeActivity + raylib GLES2 ile native Android uygulamasına derler.
[[Tame]]'in web hedefinin **kardeşi**.

## Akış
1. Tek derlenmiş modülden **iki ABI nesnesi**: `arm64-v8a` (cihaz) + `x86_64` (emülatör),
   `llvm_backend_emit_object_for_triple` ile, **PIC reloc**.
   → Bu yüzden CMake **her** makinede hem AArch64 hem X86 LLVM backend'ini linkler.
2. NDK `clang++` ile `out_apk/lib/<abi>/libtulpargame.so` + NativeActivity manifest.
3. `android/build_tame_android.sh` — statik arşivleri bir kez üretir
   (runtime + raylib `PLATFORM_ANDROID` + `native_app_glue` + tame) → `android/dist/<abi>/`.
4. `android/package_apk.sh` — aapt2 link + STORED native libs + `zipalign -P 16` + apksigner.
5. `android/install_run.sh out.apk [--screenshot shot.png]` — adb install + `am start` + screencap.

WSL'de 2 ve 3 numaralı betikler **Windows** SDK araçlarını interop ile sürer
(`.exe`, `wslpath -w`, `%LOCALAPPDATA%\Temp`). Linux SDK için `TULPAR_ANDROID_SDK`.

## Zor öğrenilmiş kısıtlar
- **bionic'te `makecontext`/`swapcontext` YOK** → `tulpar_async.cpp` dışlanır (web ile aynı);
  `android/android_stubs.cpp` async-HTTP sembollerini karşılar (çağrılırsa abort).
  `async` bu hedefte desteklenmiyor.
- **`.so` linki `-Wl,--no-undefined`** ile: eksik sembol **link** zamanında patlar,
  cihazda `UnsatisfiedLinkError` olarak değil.
- **x86_64 nesneleri PIC olmalı**, yoksa `R_X86_64_32 cannot be used against local symbol`.
- **`-Wl,-z,max-page-size=16384`** — Android 15+ 16KB sayfa imajları.
- **Derleme önbelleği tuzağı:** hedef `<out>_apk/` üretir, `<out>` değil. Önbellek `<out>`
  stat'lediği için önceden var olan bir dizin **tüm derlemeyi sessizce atlatıyordu** (düzeltildi).
- **`save_data`/`load_data` pencereden ÖNCE çağrılamaz:** raylib göreli dosya adlarını
  `AAssetManager`'a yönlendirir, o da ancak `InitWindow` ile kurulur → SIGSEGV.
  Guard **yalnız Android'e** kapsanmalı; her platforma koymak headless testleri kırıyor.
  [[Scene3D]] kalıcılığı pencereden sonra koşulsuz yeniden okur.
- **`wasm/dist` ve `android/dist` bayat kalır:** `tame_impl.c`'ye yeni sembol eklenince
  bu arşivler elle yeniden derlenmedikçe link hatası verir. → [[Roadmap]]

## Doğrulama
Yerel pencere **açılmaz**; `install_run.sh --screenshot` cihazda `adb screencap` alır.
Görsel/oynanış testini kullanıcı yapar. → [[Testing]]

## İlgili
[[Tame]] · [[Arcade]] · [[Scene3D]] · [[Build System]] · [[Cross-platform]]
