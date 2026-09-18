# Windows (x86_64-w64-mingw32) capraz derleme zinciri — Linux hosttan.
#
# Kullanim:
#   cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake
# ya da dogrudan: windows/build.sh
#
# UC TOOLCHAIN VAR, karistirilmamali:
#   1) HOST derleyicisi (bu dosya secer) — tulpar.exe + libtulpar_runtime.a
#      BUNUNLA derlenir. Iki secenek:
#        a) Arch'in capraz GCC'si (mingw-w64-gcc): x86_64-w64-mingw32-g++
#           MSYS2'nin LLVM'ini derleyen GCC ile AYNI surum (16.2.0) — tercih.
#        b) Host clang (--target=x86_64-w64-mingw32 --sysroot=...): sudo
#           istemez, sysroot'un libstdc++'ini kullanir. GCC yoksa buna duser.
#   2) SYSROOT icindeki Windows GCC (MSYS2 paketi, windows/dist/mingw64/bin/g++.exe)
#      Wine altinda kosar; AOT'un uretilen .o'yu .exe'ye BAGLAYAN adimi icin
#      (bkz. windows/wine_env.sh, TULPAR_CC).
# Hepsi MSVCRT tabanlidir. UCRT tabanli bir toolchain (MSYS2 ucrt64, llvm-mingw)
# buraya KARISTIRILMAZ: tek programda iki C calisma zamani sessiz bozulmadir.
#
# Hedef LLVM agaci sysroot'tan gelir. MSYS2'nin LLVMConfig.cmake'i yolunu KENDI
# konumundan tureti (get_filename_component zinciri), yani agac tasinabilir —
# LLVM_DIR'i gostermek yetiyor, prefiks yamasi gerekmiyor (olculdu 2026-09-18).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Sysroot: varsayilan depo icindeki windows/dist/mingw64. -DTULPAR_WIN_SYSROOT
# ile baska bir agac verilebilir.
if(NOT TULPAR_WIN_SYSROOT)
    get_filename_component(TULPAR_WIN_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../windows/dist/mingw64" ABSOLUTE)
endif()
if(NOT EXISTS "${TULPAR_WIN_SYSROOT}/lib/cmake/llvm/LLVMConfig.cmake")
    message(FATAL_ERROR
        "Windows sysroot'u eksik: ${TULPAR_WIN_SYSROOT}\n"
        "Once kur: python3 windows/setup_sysroot.py")
endif()

set(TULPAR_WIN_TRIPLE x86_64-w64-mingw32)

# --- Derleyici secimi: capraz GCC varsa o, yoksa clang ----------------------
# -DTULPAR_WIN_COMPILER=gcc|clang ile elle de secilebilir.
# Capraz GCC iki yerde olabilir: sistemde (pacman) ya da depo icinde
# (windows/setup_sysroot.py --host-gcc, sudo'suz kurulum).
get_filename_component(TULPAR_WIN_HOST_BIN "${CMAKE_CURRENT_LIST_DIR}/../windows/dist/host/usr/bin" ABSOLUTE)
find_program(TULPAR_MINGW_GXX ${TULPAR_WIN_TRIPLE}-g++ HINTS "${TULPAR_WIN_HOST_BIN}")
find_program(TULPAR_MINGW_GCC ${TULPAR_WIN_TRIPLE}-gcc HINTS "${TULPAR_WIN_HOST_BIN}")

if(NOT TULPAR_WIN_COMPILER)
    if(TULPAR_MINGW_GXX)
        set(TULPAR_WIN_COMPILER gcc)
    else()
        set(TULPAR_WIN_COMPILER clang)
    endif()
endif()

if(TULPAR_WIN_COMPILER STREQUAL "gcc")
    set(CMAKE_C_COMPILER   "${TULPAR_MINGW_GCC}")
    set(CMAKE_CXX_COMPILER "${TULPAR_MINGW_GXX}")
    find_program(TULPAR_MINGW_WINDRES ${TULPAR_WIN_TRIPLE}-windres HINTS "${TULPAR_WIN_HOST_BIN}")
    find_program(TULPAR_MINGW_AR      ${TULPAR_WIN_TRIPLE}-ar      HINTS "${TULPAR_WIN_HOST_BIN}")
    find_program(TULPAR_MINGW_RANLIB  ${TULPAR_WIN_TRIPLE}-ranlib  HINTS "${TULPAR_WIN_HOST_BIN}")
    if(TULPAR_MINGW_WINDRES)
        set(CMAKE_RC_COMPILER "${TULPAR_MINGW_WINDRES}")
    endif()
    set(CMAKE_AR     "${TULPAR_MINGW_AR}"     CACHE FILEPATH "")
    set(CMAKE_RANLIB "${TULPAR_MINGW_RANLIB}" CACHE FILEPATH "")
    # === TEK AGAC KURALI (olculdu 2026-09-18, uc ayri hatayla) ===============
    # Burada IKI mingw-w64 dagitimi var ve ikisi de tam takim:
    #   * Arch'in capraz GCC'si  — SURUCU (Linux'ta kosar), kendi CRT + libstdc++
    #   * MSYS2 sysroot'u        — LLVM/OpenSSL + CRT + libstdc++ (Windows)
    # Uretilen .exe'nin AOT link adimi Wine altinda MSYS2'nin g++'i ile yapilir,
    # yani CALISMA ZAMANI AGACI MSYS2'ninkidir. Dolayisiyla derleme de MSYS2
    # agacina gore yapilmali; ikisini karistiran her kombinasyon patladi:
    #   1) MSYS2 include'u -I ile eklenince, Arch'in CRT basliklariyla cakisti
    #      (stdlib.h: redefinition of 'wcstod').
    #   2) MSYS2 lib'i once gelince, Arch'in crt2.o'su MSYS2'nin CRT arsivleriyle
    #      eslesmedi (undefined reference to `_gnu_exception_handler`).
    #   3) Arch'in libstdc++'i ile derlenen runtime arsivi, Wine'daki MSYS2 g++
    #      ile linklenince libstdc++ sembolleri bulunamadi
    #      (undefined reference to std::__codecvt_utf8_utf16_base<wchar_t>::do_in).
    # Cozum: SURUCU Arch'in, AGAC tamamen MSYS2'nin. Derleyicinin kendi CRT ve
    # C++ basliklari devre disi (-nostdinc/-nostdinc++), yerlerine MSYS2'ninkiler;
    # link yolu da MSYS2'nin lib'i (crt2.o dahil oradan bulunur).
    # Iki GCC de 16.2.0 ve iki CRT de MSVCRT oldugu icin bu gecerli bir eslesme.
    execute_process(COMMAND "${TULPAR_MINGW_GXX}" -print-file-name=include
                    OUTPUT_VARIABLE _gcc_internal_inc OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(GLOB _cxx_inc_dirs "${TULPAR_WIN_SYSROOT}/include/c++/*")
    list(GET _cxx_inc_dirs 0 _cxx_inc)
    if(NOT _cxx_inc OR NOT EXISTS "${_cxx_inc}")
        message(FATAL_ERROR "Sysroot'ta C++ basliklari yok: ${TULPAR_WIN_SYSROOT}/include/c++/*\n"
                            "python3 windows/setup_sysroot.py ile gcc paketi de kurulmali.")
    endif()
    set(_tulpar_win_inc
        "-nostdinc -nostdinc++"
        "-isystem ${_cxx_inc}"
        "-isystem ${_cxx_inc}/${TULPAR_WIN_TRIPLE}"
        "-isystem ${_cxx_inc}/backward"
        "-isystem ${_gcc_internal_inc}"
        "-isystem ${TULPAR_WIN_SYSROOT}/include")
    string(REPLACE ";" " " _tulpar_win_inc "${_tulpar_win_inc}")
    # C icin C++ baslik dizinleri anlamsiz (ve zararli): yalniz CRT + derleyici.
    set(_tulpar_win_inc_c "-nostdinc -isystem ${_gcc_internal_inc} -isystem ${TULPAR_WIN_SYSROOT}/include")
    # -B (yalniz -L DEGIL): crt2.o gibi BASLANGIC nesneleri `-L` yolundan
    # aranmaz, derleyicinin kendi startfile dizininden gelir. -B onu da
    # MSYS2'ye cevirir; yoksa Arch'in crt2.o'su MSYS2'nin libmingw32'siyle
    # eslesmez (undefined reference to `_gnu_exception_handler`).
    set(_tulpar_win_link "-B${TULPAR_WIN_SYSROOT}/lib/ -L${TULPAR_WIN_SYSROOT}/lib")
    set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_tulpar_win_link}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_tulpar_win_link}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_tulpar_win_link}")
    # Sablon agirlikli LLVM basliklari 32K'dan fazla bolum uretebiliyor.
    set(_tulpar_win_extra "-Wa,-mbig-obj ${_tulpar_win_inc}")
    set(_tulpar_win_extra_c "${_tulpar_win_inc_c}")
else()
    # Clang yolu: hedef + sysroot bayraklari HEM derlemede HEM linkte gerekir.
    set(CMAKE_C_COMPILER   clang)
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_C_COMPILER_TARGET   ${TULPAR_WIN_TRIPLE})
    set(CMAKE_CXX_COMPILER_TARGET ${TULPAR_WIN_TRIPLE})
    set(CMAKE_SYSROOT "${TULPAR_WIN_SYSROOT}")
    set(CMAKE_AR     llvm-ar     CACHE FILEPATH "")
    set(CMAKE_RANLIB llvm-ranlib CACHE FILEPATH "")
    find_program(TULPAR_LLVM_RC llvm-rc)
    if(TULPAR_LLVM_RC)
        set(CMAKE_RC_COMPILER ${TULPAR_LLVM_RC})
    endif()
    # LINKLEYICI ACIKCA lld: mingw binutils kurulu degilse clang HOST'un
    # /usr/bin/ld'sine dusuyor (ELF icin kurulu bir linkleyiciye PE
    # baglatmak). lld clang ile ayni surumden gelir ve PE/COFF'u dogal olarak
    # uretir.
    set(CMAKE_EXE_LINKER_FLAGS_INIT    "-fuse-ld=lld")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")
    set(_tulpar_win_extra "")
endif()
message(STATUS "Windows capraz derleyici: ${TULPAR_WIN_COMPILER} (${TULPAR_WIN_TRIPLE})")

# Kutuphane/baslik/paket ARAMASI yalniz sysroot'ta; PROGRAM aramasi hostta
# (cmake, python, ar... host araclaridir).
set(CMAKE_FIND_ROOT_PATH "${TULPAR_WIN_SYSROOT}" /usr/${TULPAR_WIN_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# LLVM + LLVM'in import ettigi hedefler (ZLIB::ZLIB, LibXml2::LibXml2, zstd)
# sysroot'tan bulunsun. CMakeLists bunlari find_package(LLVM)'den ONCE ariyor.
set(LLVM_DIR "${TULPAR_WIN_SYSROOT}/lib/cmake/llvm" CACHE PATH "")
list(APPEND CMAKE_PREFIX_PATH "${TULPAR_WIN_SYSROOT}")

# UTF-8 kaynak + calisma zamani kodlamasi (kaynakta Turkce dizgiler var).
set(CMAKE_CXX_FLAGS_INIT "-finput-charset=UTF-8 -fexec-charset=UTF-8 ${_tulpar_win_extra}")
set(CMAKE_C_FLAGS_INIT   "-finput-charset=UTF-8 -fexec-charset=UTF-8 ${_tulpar_win_extra_c}")
