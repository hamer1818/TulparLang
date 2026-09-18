// L0 PLATFORM — dosya sistemi farklari (yalniz gercekten AYRISAN kisim).
//
// Motorun dosya yolu kodu POSIX'te de Windows'ta da AYNI: mingw-w64 `stat`,
// `open/read/write`, hatta `dirent.h` tasiyor. Ayrisan tek cagri `mkdir`:
// POSIX'te iki argumanli (izin bitleri), Windows CRT'sinde tek argumanli.
// Uc ayri dosyada ucuncu kez `#ifdef _WIN32` yazmak yerine burada tek yer.
#pragma once

#include <cstddef>
#include <cstdio>   // ::rename (POSIX dali)

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

// TEK bir dizin yaratir (ust dizinler zaten var olmali). Donus: 0 = tamam,
// 0 disi = hata (zaten varsa da 0 disi doner — cagiran stat ile ayirir,
// POSIX davranisiyla ayni).
inline int fs_mkdir_one(const char *path) {
#if defined(_WIN32)
  return ::_mkdir(path);
#else
  return ::mkdir(path, 0777);
#endif
}

// ATOMIK YERINE KOYMA: "gecici dosyaya yaz, sonra hedefin uzerine tasi"
// deseninin tasinabilir hali. POSIX'te `rename` hedefi SESSIZCE ezer; Windows'ta
// `rename`/`MoveFile` hedef VARSA BASARISIZ olur ve bu, uzerine yazilan her
// kayit/onbellek dosyasini ikinci kayitta bozardi (olculdu 2026-09-18: motor
// kalici kaydi ilk kez calisiyor, ikinci kez "kayit yerine konamadi" diyordu —
// dosya zaten vardi). MoveFileExA + MOVEFILE_REPLACE_EXISTING dogru esdegerdir
// (MOVEFILE_WRITE_THROUGH: cagri donmeden diske yazilsin).
// Donus: 0 = tamam, 0 disi = hata (POSIX rename sozlesmesiyle ayni).
inline int fs_replace_file(const char *tmp_path, const char *dest_path) {
#if defined(_WIN32)
  return MoveFileExA(tmp_path, dest_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
  return ::rename(tmp_path, dest_path);
#endif
}

// Dosyayi SALT OKUNUR bellege esler (pack okuma yolu: KOPYA YOK). Basarisizsa
// nullptr doner ve *out_size'a dokunmaz. Esleme dosya tanitici/handle'dan
// BAGIMSIZ yasar: iki platformda da acilan tanitici hemen kapatilir.
inline void *fs_map_readonly(const char *path, size_t *out_size) {
#if defined(_WIN32)
  HANDLE fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (fh == INVALID_HANDLE_VALUE) return nullptr;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(fh, &sz) || sz.QuadPart <= 0) { CloseHandle(fh); return nullptr; }
  HANDLE mh = CreateFileMappingA(fh, nullptr, PAGE_READONLY, 0, 0, nullptr);
  CloseHandle(fh);
  if (!mh) return nullptr;
  void *p = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(mh);   // gorunum handle'dan bagimsiz yasar
  if (!p) return nullptr;
  *out_size = (size_t)sz.QuadPart;
  return p;
#else
  const int fd = ::open(path, O_RDONLY);
  if (fd < 0) return nullptr;
  struct stat st;
  if (::fstat(fd, &st) != 0 || st.st_size <= 0) { ::close(fd); return nullptr; }
  void *p = ::mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  ::close(fd);
  if (p == MAP_FAILED) return nullptr;
  *out_size = (size_t)st.st_size;
  return p;
#endif
}

inline void fs_unmap(void *p, size_t size) {
  if (!p) return;
#if defined(_WIN32)
  (void)size;   // UnmapViewOfFile boyut istemez
  UnmapViewOfFile(p);
#else
  ::munmap(p, size);
#endif
}

} // namespace tulpar::engine::platform
