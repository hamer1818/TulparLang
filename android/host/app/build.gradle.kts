// Tulpar Engine Kotlin host — plan Faz 1 (REV 5/11). DERLENMEDI: bu makinede SDK yok.
plugins {
    id("com.android.application") version "8.7.0"
    id("org.jetbrains.kotlin.android") version "2.0.21"
}
android {
    namespace = "dev.tulparlang.engine"
    compileSdk = 35
    defaultConfig {
        applicationId = "dev.tulparlang.engine"
        minSdk = 29          // Vulkan 1.1 taban; AVP 2025 kademesi CIHAZ-MATRISI.md'de kesinlesir
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    // Native kutuphane: `tulpar build --target=android` zinciri jniLibs'e koyar;
    // 16 KB sayfa hizasi (zipalign -P 16) mevcut package_apk.sh'ta.
    sourceSets["main"].jniLibs.srcDirs("src/main/jniLibs")
    packaging { jniLibs { useLegacyPackaging = false } }
}
