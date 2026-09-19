# Tulpar Engine — Kotlin host (Faz 1 iskeleti)

Plan `docs/PLAN.md` (motor deposu) Faz 1: saf NativeActivity + aapt2 zinciri yerine Kotlin Activity +
SurfaceView + JNI köprüsü (`engine/platform/android/jni_bridge.cpp`). Sebep: Play Asset Delivery,
Firebase, ADPF Performance Hint, uygulama içi güncelleme Java/Kotlin tarafından geçer.

**Durum 2026-09-14: DERLENMEDİ.** Bu makinede Android SDK/NDK/Gradle yok; dosyalar tasarım kaydıdır,
ilk derlemede JNI imzaları (`Java_dev_tulparlang_engine_TulparActivity_*`) birlikte doğrulanır.
Mevcut `android/*.sh` betikleri (NDK derleme, apk paketleme, adb kurulum) bu host'un altında kalır:
`.so` çıktısı `app/src/main/jniLibs/<abi>/libtulpargame.so` olarak buraya konur.
