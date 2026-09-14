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

# --- Jolt Physics (vendored, engine/third_party/jolt) ------------------------
# Plan Faz 2: "yazma, bagla". Bayraklar: platformlar arasi belirlenimlilik
# (FMA kapali), x86_64 SSE4.2 (AVX yok), ObjectStream/DebugRenderer/Profile
# kapali. Uyarilar Jolt'un kendi isi (-w).
file(GLOB_RECURSE ENGINE_JOLT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jolt/Jolt/*.cpp)
# ObjectStream/*.cpp de derlenir: SerializableObject.cpp'deki GetRTTIOfType her
# yerden cagriliyor; serializer'in kendisi JPH_OBJECT_STREAM tanimsiz oldugu
# icin derlenmiyor (dosya ici #ifdef).
add_library(engine_jolt STATIC ${ENGINE_JOLT_SOURCES})
target_include_directories(engine_jolt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jolt)
target_compile_definitions(engine_jolt PUBLIC JPH_CROSS_PLATFORM_DETERMINISTIC)
# -ffp-contract=off PUBLIC: AArch64'te GCC/Clang a*b+c'yi varsayilan olarak FMA'ya
# birlestirir, x86_64'te (-mfma yok) birlestirmez -> ayni kaynak, farkli yuvarlama.
# Olculdu (CI 2026-09-14): sabit girdiyle x86_64 5dc41c2b... vs arm64 4087b9af...
# Jolt'un kendi derlemesi de bunu kapatir; define tek basina yetmiyor.
target_compile_options(engine_jolt PUBLIC -ffp-contract=off)
target_compile_options(engine_jolt PRIVATE -fno-exceptions -fno-rtti -w)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_definitions(engine_jolt PUBLIC JPH_USE_SSE4_1 JPH_USE_SSE4_2)
    target_compile_options(engine_jolt PUBLIC -msse4.2 -mpopcnt)
endif()
target_link_libraries(engine_jolt PUBLIC Threads::Threads)

# --- L4 SIM (ECS, zamanlayici, sabit adim, replay, fizik) --------------------
add_library(engine_sim STATIC
    sim/ecs.cpp
    sim/schedule.cpp
    sim/physics.cpp
)
target_link_libraries(engine_sim PUBLIC engine_core engine_jolt)
target_compile_options(engine_sim PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> -ffp-contract=off)
add_dependencies(engine_sim engine_layer_check)

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
    tests/test_sim.cpp
    tests/test_physics.cpp
    core/memory/alloc_gate_override.cpp
)
target_link_libraries(engine_tests PRIVATE engine_rhi engine_sim engine_core ${CMAKE_DL_LIBS})
target_compile_options(engine_tests PRIVATE -ffp-contract=off) # sim belirlenimliligi (strict FP, plan C.1.5)
# Fizik altin ozeti: x86_64'te hesaplanir, CI arm64 karsilastirir (0 = henuz yok).
# 0x5dc41c2bddc345cb: x86_64 (SSE4.2, FMA yok), 65 govde x 300 adim, baslangic donusleri SABIT bit (libm yok), 2026-09-14.
set(ENGINE_PHYSICS_GOLDEN_HASH "0x5dc41c2bddc345cb" CACHE STRING "Jolt sahnesinin platformlar arasi altin ozeti (x86_64'te hesaplandi)")
target_compile_definitions(engine_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" PHYSICS_GOLDEN_HASH=${ENGINE_PHYSICS_GOLDEN_HASH}ull)
target_compile_options(engine_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)
