# Jolt Physics (vendored)

**v5.3.0**, MIT (LICENSE). Yalnız `Jolt/` kaynak ağacı; upstream `Jolt.cmake` çıkarıldı — derleme `engine/CMakeLists.txt`'te
(`engine_jolt`), glob ile. `ObjectStream/` olduğu gibi derlenir (SerializableObject.cpp'deki `GetRTTIOfType` her yerden
çağrılıyor) ama `JPH_OBJECT_STREAM` tanımlı olmadığı için metin serializer'ın kendisi dosya içi
`#ifdef` ile dışarıda kalır.

Bayraklar (plan Faz 2 / REV 8): `JPH_CROSS_PLATFORM_DETERMINISTIC` (FMA kapalı, platformlar arası bit
eşitliği iddiası — CI Linux x86_64 ↔ macOS arm64 altın özetle **sınanır**); x86_64'te `-msse4.2` (AVX yok:
düşük segment ve belirlenimlilik), arm64'te NEON; `JPH_OBJECT_STREAM`, `JPH_DEBUG_RENDERER`,
`JPH_PROFILE_ENABLED` kapalı; exception/RTTI kapalı (Jolt kendi RTTI'sini kullanır).

Neden vendor: raylib/sqlite deseni; derleme makinesinde paket gerekmez. Güncelleme: aynı etiketle
tarball indir, `Jolt/` kopyala, ObjectStream'i ve Jolt.cmake'i sil, bu satırı güncelle.
