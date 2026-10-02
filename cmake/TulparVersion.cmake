# SURUM DIZGISI — git etiketinden, her derlemede (2026-10-02).
#
# `cmake -P` ile koşar (CMakeLists.txt: yapılandırmada bir kez + her
# derlemede `tulpar_surum` hedefi). Çıktı: TULPAR_VERSION_OUT başlığı,
# `#define TULPAR_VERSION_STRING "..."`; içerik değişmediyse dosyaya
# DOKUNULMAZ (copy_if_different) — sürüm aynıyken hiçbir şey yeniden derlenmez.
#
# NEDEN: sürüm `project(VERSION 3.13.1)` satırından geliyordu ve elle
# yükseltilmesi gerekiyordu; otomatik-surum.yml her birleşmede bir etiket
# kesmeye başlayınca (2026-09-21) yerel ve CI dal derlemeleri v3.37.x
# çıkmışken hâlâ "3.13.1-dev" diyordu. Elle yazılan bir sayı ne kadar
# özenle tutulursa tutulsun kayar; burada sayı YOK, etiketin kendisi var.
#
# Öncelik:
#   1. TULPAR_VERSION_OVERRIDE (yayın CI'ı etiket derlemesinde
#      -DTULPAR_VERSION=<etiket> geçer) -> aynen.
#   2. git describe --tags --match 'v[0-9]*' --dirty
#        etiketin üstünde temiz:  v3.37.16          (yayınlanan ikiliyle aynı)
#        etiketten N commit sonra: v3.37.16-4-gabc1234[-dirty]
#   3. git var, ulaşılabilir etiket yok (sığ klon):  0.0.0-dev+gabc1234
#   4. git/depo yok (kaynak arşivi):                  0.0.0-dev
# 3 ve 4 bilerek bir SAYI iddia etmez: bilinmeyeni bilinmiyor diye söyler.
#
# `-c safe.directory=*`: CI'da depo başka bir kullanıcıya ait görünebilir
# (Windows'ta actions/checkout'un yazdığı ağacı MSYS2 git'i okur); git
# "dubious ownership" ile reddederse sürüm sessizce 0.0.0-dev'e düşerdi.
# Yalnız bu salt-okur çağrılar için.
#
# Girdiler: TULPAR_SOURCE_DIR, TULPAR_VERSION_OUT, TULPAR_VERSION_OVERRIDE.
# İsteğe bağlı TULPAR_VERSION_PRINT=1: hesaplanan dizgiyi stdout'a basar
# (tools/surum_denetle.sh öz-denetimi).

set(_surum "")
if(TULPAR_VERSION_OVERRIDE)
  set(_surum "${TULPAR_VERSION_OVERRIDE}")
else()
  find_program(_git NAMES git)
  if(_git AND EXISTS "${TULPAR_SOURCE_DIR}")
    execute_process(
      COMMAND "${_git}" -c safe.directory=* -C "${TULPAR_SOURCE_DIR}" describe --tags --match "v[0-9]*" --dirty
      OUTPUT_VARIABLE _desc
      RESULT_VARIABLE _desc_rc
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_desc_rc EQUAL 0 AND _desc)
      set(_surum "${_desc}")
    else()
      execute_process(
        COMMAND "${_git}" -c safe.directory=* -C "${TULPAR_SOURCE_DIR}" rev-parse --short HEAD
        OUTPUT_VARIABLE _sha
        RESULT_VARIABLE _sha_rc
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(_sha_rc EQUAL 0 AND _sha)
        set(_surum "0.0.0-dev+g${_sha}")
      endif()
    endif()
  endif()
  if(NOT _surum)
    set(_surum "0.0.0-dev")
  endif()
endif()

if(TULPAR_VERSION_PRINT)
  message("${_surum}")
endif()
if(TULPAR_VERSION_OUT)
  set(_tmp "${TULPAR_VERSION_OUT}.tmp")
  file(WRITE "${_tmp}"
       "// URETILDI (cmake/TulparVersion.cmake) — elle duzenleme.\n"
       "#define TULPAR_VERSION_STRING \"${_surum}\"\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_tmp}" "${TULPAR_VERSION_OUT}")
  file(REMOVE "${_tmp}")
endif()
