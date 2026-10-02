# MACOS TASINABILIR IKILI — `tulpar` yalniz sistemin kendi kitapliklarina
# (/usr/lib, /System) baglansin; Homebrew'a degil.
#
# NIYE VAR (olculdu 2026-10-02, yayinlanmis v3.38.0 `tulpar-macos-universal`,
# `llvm-otool -L`): ikili DORT Homebrew dylib'ine dinamik bagliydi —
#   /opt/homebrew/opt/llvm@18/lib/libunwind.1.dylib
#   /opt/homebrew/opt/zstd/lib/libzstd.1.dylib
#   /opt/homebrew/opt/openssl@3/lib/libssl.3.dylib + libcrypto.3.dylib
# Biri eksik olan makinede dyld acilista abort ediyordu (motor CI'i
# llvm@18 kurmadan ikiliyi calistiramadi). CI koscusunda dordu de kurulu
# oldugu icin derleme isinde hicbir sey bunu gormedi.
#
# Kaynaklar: OpenSSL `find_package(OpenSSL)`'in dylib secmesinden (CMakeLists
# bunu OPENSSL_USE_STATIC_LIBS ile cozuyor); zstd ve libunwind ise Homebrew
# LLVM'inin disa aktardigi LLVM bilesen hedeflerinin INTERFACE_LINK_LIBRARIES
# zincirinden geliyor. Bu modul o zinciri yuruyup:
#   * sistem disi bir .dylib'in yaninda statik ikizi (lib<ad>.a) varsa onu koyar;
#   * libunwind'i duser — libSystem onu zaten yeniden disa aktariyor
#     (/usr/lib/system/libunwind.dylib), ayri bir kopyaya gerek yok;
#   * baska bir sistem disi dylib kalirsa UYARIR (kapi — tools/
#     dinamik_bag_denetle.sh — onu CI'da kirmiziya cevirir).
# Kapatmak icin: -DTULPAR_MACOS_TASINABILIR=OFF (yerel gelistirme; yayin
# ikilisi icin ACIK kalmali).

function(_tulpar_tt_sistem_mi yol out)
  if(yol MATCHES "^/usr/lib/" OR yol MATCHES "^/System/" OR yol MATCHES "\\.tbd$"
     OR yol MATCHES "\\.sdk/")
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Sistem disi bir dylib icin yerine konacak oge: statik ikiz yolu, "" (dus)
# ya da dylib'in kendisi (cozum yok — uyarilir).
function(_tulpar_tt_dylib dylib out)
  get_filename_component(d "${dylib}" DIRECTORY)
  get_filename_component(n "${dylib}" NAME)
  string(REGEX REPLACE "^lib([^.]+).*\\.dylib$" "\\1" ad "${n}")
  if(EXISTS "${d}/lib${ad}.a")
    message(STATUS "macOS tasinabilir: ${dylib} -> ${d}/lib${ad}.a (statik)")
    set(${out} "${d}/lib${ad}.a" PARENT_SCOPE)
  elseif(ad STREQUAL "unwind")
    message(STATUS "macOS tasinabilir: ${dylib} dusuruldu (libSystem saglar)")
    set(${out} "" PARENT_SCOPE)
  else()
    message(WARNING "macOS tasinabilir: ${dylib} sistem disi ve statik ikizi yok — "
                    "ikili ona bagli kalacak (tools/dinamik_bag_denetle.sh kirmizi doner)")
    set(${out} "${dylib}" PARENT_SCOPE)
  endif()
endfunction()

# Bir link ogesini cevir; sonuc listesi `out`a (bos = dus).
function(_tulpar_tt_oge oge out)
  set(on "")
  set(son "")
  set(ic "${oge}")
  if(oge MATCHES "^\\$<LINK_ONLY:(.+)>$")
    set(ic "${CMAKE_MATCH_1}")
    set(on "$<LINK_ONLY:")
    set(son ">")
  endif()
  set(yeni "${ic}")
  if(ic STREQUAL "unwind" OR ic STREQUAL "-lunwind")
    message(STATUS "macOS tasinabilir: '${ic}' dusuruldu (libSystem saglar)")
    set(yeni "")
  elseif(TARGET "${ic}")
    get_target_property(tip "${ic}" TYPE)
    get_target_property(ithal "${ic}" IMPORTED)
    if(ithal AND (tip STREQUAL "SHARED_LIBRARY" OR tip STREQUAL "UNKNOWN_LIBRARY"))
      set(konum "")
      foreach(p IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_NOCONFIG IMPORTED_LOCATION
                IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_MINSIZEREL
                IMPORTED_LOCATION_DEBUG)
        get_target_property(k "${ic}" ${p})
        if(k AND NOT konum)
          set(konum "${k}")
        endif()
      endforeach()
      if(konum MATCHES "\\.dylib$")
        _tulpar_tt_sistem_mi("${konum}" sis)
        if(NOT sis)
          _tulpar_tt_dylib("${konum}" yeni)
        endif()
      endif()
    endif()
    if(tip STREQUAL "STATIC_LIBRARY" OR tip STREQUAL "INTERFACE_LIBRARY"
       OR tip STREQUAL "UNKNOWN_LIBRARY" OR tip STREQUAL "SHARED_LIBRARY")
      _tulpar_tt_hedef("${ic}")
    endif()
  elseif(IS_ABSOLUTE "${ic}" AND ic MATCHES "\\.dylib$")
    _tulpar_tt_sistem_mi("${ic}" sis)
    if(NOT sis)
      _tulpar_tt_dylib("${ic}" yeni)
    endif()
  endif()
  if(yeni STREQUAL "")
    set(${out} "" PARENT_SCOPE)
  else()
    set(${out} "${on}${yeni}${son}" PARENT_SCOPE)
  endif()
endfunction()

# Bir ithal hedefin INTERFACE_LINK_LIBRARIES'ini yerinde cevir (bir kez).
function(_tulpar_tt_hedef hedef)
  get_property(gorulen GLOBAL PROPERTY _TULPAR_TT_GORULEN)
  if("${hedef}" IN_LIST gorulen)
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY _TULPAR_TT_GORULEN "${hedef}")
  get_target_property(ithal "${hedef}" IMPORTED)
  if(NOT ithal)
    return()
  endif()
  get_target_property(liste "${hedef}" INTERFACE_LINK_LIBRARIES)
  if(NOT liste)
    return()
  endif()
  set(yeni_liste "")
  foreach(o IN LISTS liste)
    _tulpar_tt_oge("${o}" y)
    if(NOT y STREQUAL "")
      list(APPEND yeni_liste "${y}")
    endif()
  endforeach()
  if(NOT "${yeni_liste}" STREQUAL "${liste}")
    set_property(TARGET "${hedef}" PROPERTY INTERFACE_LINK_LIBRARIES "${yeni_liste}")
  endif()
endfunction()

# Giris: bir hedefin (ornegin `tulpar`) kendi LINK_LIBRARIES'i ve oradan
# ulasilan butun ithal hedefler.
function(tulpar_macos_tasinabilir hedef)
  get_target_property(liste "${hedef}" LINK_LIBRARIES)
  if(NOT liste)
    return()
  endif()
  set(yeni_liste "")
  foreach(o IN LISTS liste)
    _tulpar_tt_oge("${o}" y)
    if(NOT y STREQUAL "")
      list(APPEND yeni_liste "${y}")
    endif()
  endforeach()
  set_property(TARGET "${hedef}" PROPERTY LINK_LIBRARIES "${yeni_liste}")
endfunction()
