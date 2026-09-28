# ============================================
# Tulpar Embedded Libraries - CMake Generator
# ============================================
# This script reads lib/*.tpr files and generates
# embedded_libs.h at build time automatically.
# ============================================

# Function to escape file content for C string literal
function(escape_for_c_string INPUT_STRING OUTPUT_VAR)
    # Read the string
    set(RESULT "${INPUT_STRING}")
    
    # Escape backslashes first (must be first!)
    string(REPLACE "\\" "\\\\" RESULT "${RESULT}")
    
    # Escape double quotes
    string(REPLACE "\"" "\\\"" RESULT "${RESULT}")
    
    # Escape newlines - replace with \n" newline "
    string(REPLACE "\n" "\\n\"\n    \"" RESULT "${RESULT}")
    
    # Escape carriage returns
    string(REPLACE "\r" "\\r" RESULT "${RESULT}")
    
    # Escape tabs
    string(REPLACE "\t" "\\t" RESULT "${RESULT}")
    
    # Wrap in quotes
    set(RESULT "\"${RESULT}\"")
    
    # Return
    set(${OUTPUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

# Function to embed a library file
function(embed_library LIB_NAME LIB_FILE OUTPUT_VAR)
    set(LIB_PATH "${CMAKE_SOURCE_DIR}/lib/${LIB_FILE}")
    
    if(EXISTS "${LIB_PATH}")
        # YENIDEN YAPILANDIRMA BAGIMLILIGI. Icerik burada, YAPILANDIRMA
        # aninda okunuyor; bu satir olmadan `cmake --build` lib/*.tpr
        # degisikligini hic gormuyordu — derleme "basarili" bitiyor, ikili
        # ESKI stdlib'i gomulu tasiyordu ve test/wings/orm'a yapilan duzeltme
        # "ise yaramamis" gorunuyordu (Tuzaklar 7). Dosya CMAKE_CONFIGURE_DEPENDS'e
        # girince derleme sistemi onu izliyor: degisirse once cmake yeniden
        # kosuyor, configure_file basligi yeniden yaziyor, onu iceren uc TU
        # yeniden derleniyor. Kapisi: tests/gomulu_stdlib_tazelik.py.
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
            CMAKE_CONFIGURE_DEPENDS "${LIB_PATH}")
        file(READ "${LIB_PATH}" LIB_CONTENT)
        escape_for_c_string("${LIB_CONTENT}" ESCAPED_CONTENT)
        set(${OUTPUT_VAR} "${ESCAPED_CONTENT}" PARENT_SCOPE)
        message(STATUS "Embedded library: ${LIB_NAME} (${LIB_FILE})")
    else()
        message(WARNING "Library file not found: ${LIB_PATH}")
        set(${OUTPUT_VAR} "\"// Library not found: ${LIB_FILE}\"" PARENT_SCOPE)
    endif()
endfunction()

# Embed all libraries
embed_library("wings" "wings.tpr" EMBEDDED_WINGS_CONTENT)
embed_library("router" "router.tpr" EMBEDDED_ROUTER_CONTENT)
embed_library("http_utils" "http_utils.tpr" EMBEDDED_HTTP_UTILS_CONTENT)
embed_library("async" "async.tpr" EMBEDDED_ASYNC_CONTENT)
embed_library("middleware" "middleware.tpr" EMBEDDED_MIDDLEWARE_CONTENT)
embed_library("socket" "socket.tpr" EMBEDDED_SOCKET_CONTENT)
embed_library("tulpar_api" "tulpar_api.tpr" EMBEDDED_TULPAR_API_CONTENT)
embed_library("test" "test.tpr" EMBEDDED_TEST_CONTENT)
embed_library("http_client" "http_client.tpr" EMBEDDED_HTTP_CLIENT_CONTENT)
embed_library("orm" "orm.tpr" EMBEDDED_ORM_CONTENT)
embed_library("wings_tls" "wings_tls.tpr" EMBEDDED_WINGS_TLS_CONTENT)
embed_library("tame" "tame.tpr" EMBEDDED_TAME_CONTENT)
embed_library("arcade" "arcade.tpr" EMBEDDED_ARCADE_CONTENT)
embed_library("scene3d" "scene3d.tpr" EMBEDDED_SCENE3D_CONTENT)

# Generate the header file from template
configure_file(
    "${CMAKE_SOURCE_DIR}/src/embedded_libs.h.in"
    "${CMAKE_SOURCE_DIR}/src/embedded_libs.h"
    @ONLY
)

message(STATUS "Generated: src/embedded_libs.h")

# ============================================
# gdb pretty-printer'ı (tools/gdb/tulpar_printers.py) -> `tulpar debug`
# ============================================
# DAP bağdaştırıcısı gdb'yi başlatınca bu betiği yüklüyor; kurulu bir tulpar'ın
# yanında tools/ dizini olmadığı için metin ikiliye gömülüyor (tek kaynak:
# .py dosyası). Başlık DERLEME dizininde üretiliyor, kaynak ağacına yazılmıyor;
# içerik aynıysa dosyaya dokunulmuyor (configure_file COPYONLY), yani her
# yeniden yapılandırma debug_cmd.cpp'yi yeniden derletmiyor.
set(TULPAR_GDB_SCRIPT "${CMAKE_SOURCE_DIR}/tools/gdb/tulpar_printers.py")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS "${TULPAR_GDB_SCRIPT}")
file(READ "${TULPAR_GDB_SCRIPT}" TULPAR_GDB_SCRIPT_CONTENT)
file(WRITE "${CMAKE_BINARY_DIR}/generated/tulpar_gdb_printers.h.tmp"
"// URETILMIS — tools/gdb/tulpar_printers.py'den (cmake/EmbedLibraries.cmake). Elle duzenleme.
#pragma once
static const char *kTulparGdbPrinters = R\"TPGDB(${TULPAR_GDB_SCRIPT_CONTENT})TPGDB\";
")
configure_file("${CMAKE_BINARY_DIR}/generated/tulpar_gdb_printers.h.tmp"
               "${CMAKE_BINARY_DIR}/generated/tulpar_gdb_printers.h" COPYONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
