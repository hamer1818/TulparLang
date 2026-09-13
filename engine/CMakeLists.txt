# ===========================================================================
# Tulpar Engine — Faz 0 (L0 platform + L1 core). AYRI agac: `tulpar`
# derleyicisine ve `tulpar_runtime`a LINKLENMEZ; ileride oyun ikilisine
# linklenir. Plan: docs/engine/MIMARI.md. Katman kurali (tools/layer_check.py)
# engine_core'un on kosulu — ihlal = build hatasi.
# ===========================================================================
enable_language(ASM)
find_package(Threads REQUIRED)

option(ENGINE_MEM_CANARY "Arena blok kanaryalari (tasma tespiti; ship'te kapat)" ON)

# Motorda exception ve RTTI YOK (plan L1). -Werror yok: derleyici surumune
# bagli uyari CI'i kirmasin; kod uyarisiz tutulur.
set(ENGINE_CXX_OPTS -fno-exceptions -fno-rtti -funwind-tables -Wall -Wextra)

# --- L0 ---------------------------------------------------------------------
add_library(engine_platform STATIC
    platform/fatal.cpp
    platform/time.cpp
    platform/memory.cpp
    platform/thread.cpp
    platform/crash.cpp
)
target_include_directories(engine_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(engine_platform PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)
target_link_libraries(engine_platform PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- Katman denetimi --------------------------------------------------------
add_custom_target(engine_layer_check
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/layer_check.py ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "engine: katman bagimlilik denetimi (ihlal = build hatasi)"
    VERBATIM
)

# --- L1 ---------------------------------------------------------------------
add_library(engine_core STATIC
    core/memory/arena.cpp
    core/memory/alloc_gate.cpp
    core/jobs/fiber.cpp
    core/jobs/job_system.cpp
    core/profiler/profiler.cpp
    core/jobs/fiber_switch_x86_64.S
    core/jobs/fiber_switch_aarch64.S
)
target_link_libraries(engine_core PUBLIC engine_platform)
target_compile_options(engine_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)
if(ENGINE_MEM_CANARY)
    # PUBLIC: baslik (Arena yerlesimi) ve kutuphane ayni tanimi gormeli.
    target_compile_definitions(engine_core PUBLIC ENGINE_MEM_CANARY=1)
endif()
add_dependencies(engine_core engine_layer_check)

# --- L2 RHI (Vulkan) ---------------------------------------------------------
# Basliklar vendored, loader dlopen: link zamani Vulkan bagimliligi YOK.
add_library(engine_rhi STATIC
    rhi/vk_api.cpp
    rhi/device.cpp
    rhi/command_pools.cpp
    rhi/offscreen.cpp
)
target_include_directories(engine_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan)
target_link_libraries(engine_rhi PUBLIC engine_core ${CMAKE_DL_LIBS})
target_compile_options(engine_rhi PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)
add_dependencies(engine_rhi engine_layer_check)

# --- Testler ------------------------------------------------------------------
# alloc_gate_override.cpp YURUTULEBILIRE eklenir: hangi ikilinin sayildigi
# burada gorunur (A2 kapisi).
add_executable(engine_tests
    tests/test_main.cpp
    tests/test_memory.cpp
    tests/test_alloc_gate.cpp
    tests/test_fiber.cpp
    tests/test_jobs.cpp
    tests/test_profiler.cpp
    tests/test_frame_gate.cpp
    tests/test_crash.cpp
    tests/test_math.cpp
    tests/test_containers.cpp
    tests/test_rhi.cpp
    core/memory/alloc_gate_override.cpp
)
target_link_libraries(engine_tests PRIVATE engine_rhi engine_core ${CMAKE_DL_LIBS})
target_compile_definitions(engine_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(engine_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)
