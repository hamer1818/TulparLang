# Karşılaştırma — runtime boyutu (ölçülmüş, 2026-09-14)

> Boyut anlatısı Unity üzerine kurulmaz; gerçek boyut şampiyonu **Defold** (~1 MB). Bu tabloda bizim
> satır **ölçülmüştür**, diğerleri kaynakların kendi beyanı. Her milestone'da yeniden ölçülür.

| Motor | Runtime | Kaynak / not |
|---|---|---|
| **Defold** | **~976 kB** | Wikipedia kutusu; 2B ağırlıklı, Lua, sınırlı 3B |
| **Tulpar Engine** (bugün) | **2.7 MB** | `libtulparengine.so` arm64-v8a, NDK r27 Release, `llvm-strip` sonrası. İçinde: Vulkan RHI + renderer (gölge, doku, kümelenmiş ışık, UI) + **Jolt** + **Recast/Detour** + glTF/stb + fiber job + **engine_tests'in tamamı** (test kodu ayrılınca daha küçük). Strip'siz 27.4 MB (semboller) |
| Tulpar (plan hedefi) | ~12 MB | PLAN §5 tahmini; gerçek çok altında çıktı |
| Godot | 40–60 MB | beyan |
| Unity IL2CPP | 60–120 MB | beyan |

**Savunulabilir cümle:** 2.7 MB'ın içinde tam 3B renderer, Jolt fiziği ve navmesh var; Defold'un 1 MB'ı
2B+Lua. Bu farkı ölçüyle veriyoruz. Nasıl ölçülür:

```
engine/tools/android_run.sh tests            # build-android/libtulparengine.so
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip -o /tmp/s.so build-android/libtulparengine.so
ls -la /tmp/s.so
```

APK: 27.8 MB (strip'siz .so + varlıklar + testler için katman); dağıtım APK'sı strip'li .so + ASTC/KTX2
paketle ölçülecek (Faz 6).
