# Tulpar WebAssembly Build Configuration
# Emscripten toolchain ile WebAssembly derleme

cmake_minimum_required(VERSION 3.14)

project(TulparWasm VERSION 2.1.0 LANGUAGES C)

# Emscripten toolchain kontrolü
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Emscripten toolchain gerekli! emcmake ile çalıştırın: emcmake cmake ..")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories (wasm klasörü bir alt klasör, kaynaklar bir üst dizinde)
get_filename_component(PARENT_DIR ${CMAKE_SOURCE_DIR} DIRECTORY)
include_directories(${PARENT_DIR}/src)
include_directories(${PARENT_DIR}/runtime)
include_directories(${CMAKE_SOURCE_DIR})

# Source files (sadece VM ve temel özellikler)
set(LEXER_SOURCES
    ${PARENT_DIR}/src/lexer/lexer.c
)

set(PARSER_SOURCES
    ${PARENT_DIR}/src/parser/parser.c
)

set(COMPILER_SOURCES
    ${PARENT_DIR}/src/vm/compiler.c
    ${PARENT_DIR}/src/vm/bytecode.c
)

set(VM_SOURCES
    ${PARENT_DIR}/src/vm/vm.c
)

# Runtime bindings (WebAssembly için özelleştirilmiş)
# File I/O, network, database hariç
set(RUNTIME_SOURCES
    ${CMAKE_SOURCE_DIR}/runtime_bindings_wasm.c
)

# JSON support
set(JSON_SOURCES
    ${PARENT_DIR}/runtime/cJSON.c
)

# WebAssembly API
set(WASM_API_SOURCES
    ${CMAKE_SOURCE_DIR}/tulpar_wasm_api.c
)

# Tüm kaynaklar
set(ALL_SOURCES
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${COMPILER_SOURCES}
    ${VM_SOURCES}
    ${RUNTIME_SOURCES}
    ${JSON_SOURCES}
    ${WASM_API_SOURCES}
)

# WebAssembly modülü oluştur
add_executable(tulpar_wasm.js ${ALL_SOURCES})

# Çıktı dosyalarını belirli bir dizine kopyala
set(WASM_OUTPUT_DIR "${PARENT_DIR}/../tulpar-lang-web/public")
add_custom_command(TARGET tulpar_wasm.js POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WASM_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/tulpar_wasm.js.js"
        "${WASM_OUTPUT_DIR}/tulpar_wasm.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/tulpar_wasm.js.wasm"
        "${WASM_OUTPUT_DIR}/tulpar_wasm.wasm"
    COMMENT "WebAssembly dosyalarını tulpar-lang-web/public klasörüne kopyalıyor..."
)

# Emscripten linker flags
target_link_options(tulpar_wasm.js PRIVATE
    "SHELL:-s WASM=1"
    # addFunction gerekli; diğer metotlar opsiyonel
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"addFunction\"]"
    "SHELL:-s EXPORTED_FUNCTIONS=[\"_malloc\",\"_free\",\"_tulpar_wasm_init\",\"_tulpar_wasm_run_code\",\"_tulpar_wasm_get_output\",\"_tulpar_wasm_get_output_length\",\"_tulpar_wasm_cleanup\",\"_tulpar_wasm_set_print_callback\",\"_append_to_output\"]"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=createTulparWasmModule"
    "SHELL:-s ALLOW_TABLE_GROWTH=1"
    # addFunction için table grow yine fail olursa slot ayır
    "SHELL:-s RESERVED_FUNCTION_POINTERS=64"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=16777216"
    "SHELL:-s MAXIMUM_MEMORY=67108864"
    -O3
    --no-entry
    "SHELL:-s ASSERTIONS=0"
    "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
)

# Compile definitions
target_compile_definitions(tulpar_wasm.js PRIVATE
    TULPAR_WASM_BUILD
    TULPAR_RUNTIME_ONLY
)

# Compiler flags
target_compile_options(tulpar_wasm.js PRIVATE
    -O3
    -Wall
    -Wextra
)

message(STATUS "Tulpar WebAssembly build configuration hazır")
message(STATUS "Build için: emcmake cmake .. && emmake make")
