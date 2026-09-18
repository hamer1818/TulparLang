// L0 PLATFORM — dinamik kutuphane yukleme (dlopen / LoadLibrary).
//
// Motor iki kutuphaneyi calisma zamaninda yukler ve HICBIRINE link olmaz:
// GLFW (masaustu pencere) ve Vulkan loader. Sebebi ayni: kutuphane yoksa
// program yine calismali (headless kapilar, sunucu makineleri, dosya sistemi
// olmayan cihazlar) — link zamani bagimliligi bunu imkansiz kilardi.
//
// POSIX'te dlopen/dlsym/dlclose, Windows'ta LoadLibraryA/GetProcAddress/
// FreeLibrary. Baslik icinde inline: L0'da ayri bir .cpp acmaya degmez ve
// katman denetimi (tools/layer_check.py) icin de en sade hali budur.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tulpar::engine::platform {

// Kutuphaneyi yukler; bulunamazsa nullptr (HATA DEGIL — cagiran yedege duser).
inline void *dl_open(const char *name) {
#if defined(_WIN32)
  return (void *)LoadLibraryA(name);
#else
  return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

// Sembol adresi; yoksa nullptr. Cagiran ZORUNLU sembolleri kendisi denetler.
inline void *dl_sym(void *lib, const char *sym) {
  if (!lib) return nullptr;
#if defined(_WIN32)
  return (void *)GetProcAddress((HMODULE)lib, sym);
#else
  return dlsym(lib, sym);
#endif
}

inline void dl_close(void *lib) {
  if (!lib) return;
#if defined(_WIN32)
  FreeLibrary((HMODULE)lib);
#else
  dlclose(lib);
#endif
}

} // namespace tulpar::engine::platform
