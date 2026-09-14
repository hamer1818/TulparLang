# ===========================================================================
# Tulpar Engine — Faz 0 (L0 platform + L1 core). AYRI agac: `tulpar`
# derleyicisine ve `tulpar_runtime`a LINKLENMEZ; ileride oyun ikilisine
# linklenir. Plan: docs/engine/MIMARI.md. Katman kurali (tools/layer_check.py)
# engine_core'un on kosulu — ihlal = build hatasi.
# ===========================================================================
# Tek basina yapilandirma (Android capraz derleme: cmake -S engine -B build-android
# -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a
# -DANDROID_PLATFORM=android-26). Ust agactan add_subdirectory ile de gelir.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.14)
  project(tulpar_engine CXX C ASM)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
endif()
enable_language(ASM)
find_package(Threads REQUIRED)

option(ENGINE_MEM_CANARY "Arena blok kanaryalari (tasma tespiti; ship'te kapat)" ON)
option(ENGINE_TRACY "Tracy profiler istemcisi (third_party/tracy, BSD-3): profiler bolgeleri + kare isaretleri Tracy'ye de basilir" OFF)

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
    platform/window.cpp
)
target_include_directories(engine_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw)
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
if(ENGINE_TRACY)
    # Kapaliyken sifir maliyet (makrolar bos). Acikken: talep uzerine (sunucu
    # baglaninca kayit), ornekleme/sistem izleme/callstack kapali (mobilde izin yok),
    # yayin yok (adb forward ile 8086). Fiber destegi acik: bolgeler fiber'a baglanir.
    add_library(engine_tracy STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy/TracyClient.cpp)
    target_include_directories(engine_tracy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy)
    target_compile_definitions(engine_tracy PUBLIC TRACY_ENABLE TRACY_ON_DEMAND TRACY_FIBERS TRACY_NO_SAMPLING
        TRACY_NO_SYSTEM_TRACING TRACY_NO_CALLSTACK TRACY_NO_BROADCAST TRACY_NO_FRAME_IMAGE TRACY_NO_CODE_TRANSFER
        TRACY_NO_CONTEXT_SWITCH TRACY_NO_VSYNC_CAPTURE TRACY_NO_VERIFY) # NO_EXIT YOK: sunucu yoksa surec cikista sonsuza dek beklerdi (engine_tests asili kaldi, 2026-09-14)
    target_compile_options(engine_tracy PRIVATE -w)
    target_link_libraries(engine_tracy PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    target_link_libraries(engine_core PUBLIC engine_tracy)
    target_compile_definitions(engine_core PUBLIC ENGINE_TRACY=1)
endif()
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
    rhi/swapchain.cpp
    rhi/tile_budget.cpp
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

# --- meshoptimizer (vendored, engine/third_party/meshoptimizer, MIT) ---------
# Yukleme aninda mesh optimizasyonu + LOD (Faz 6); cluster DAG'in temeli (Faz 9).
file(GLOB ENGINE_MESHOPT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshoptimizer/*.cpp)
add_library(engine_meshopt STATIC ${ENGINE_MESHOPT_SOURCES})
target_include_directories(engine_meshopt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/meshoptimizer)
target_compile_options(engine_meshopt PRIVATE -fno-exceptions -fno-rtti -w)

# --- astc-encoder (vendored, engine/third_party/astcenc, Apache-2.0) ---------
# Arac (engine_texpack) sikistirir; runtime yalniz ASTC LDR olmayan GPU'da (masaustu
# NVIDIA) cozer. ISA: x86_64 SSE4.1+POPCNT, AArch64 NEON, digerleri skaler.
file(GLOB ENGINE_ASTCENC_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/astcenc/astcenc_*.cpp)
add_library(engine_astcenc STATIC ${ENGINE_ASTCENC_SOURCES})
target_include_directories(engine_astcenc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/astcenc)
target_compile_options(engine_astcenc PRIVATE -fno-exceptions -fno-rtti -w)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_definitions(engine_astcenc PRIVATE ASTCENC_SSE=41 ASTCENC_AVX=0 ASTCENC_POPCNT=1 ASTCENC_F16C=0 ASTCENC_NEON=0 ASTCENC_SVE=0)
    target_compile_options(engine_astcenc PRIVATE -msse4.1 -mpopcnt)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    target_compile_definitions(engine_astcenc PRIVATE ASTCENC_SSE=0 ASTCENC_AVX=0 ASTCENC_POPCNT=0 ASTCENC_F16C=0 ASTCENC_NEON=1 ASTCENC_SVE=0)
else()
    target_compile_definitions(engine_astcenc PRIVATE ASTCENC_SSE=0 ASTCENC_AVX=0 ASTCENC_POPCNT=0 ASTCENC_F16C=0 ASTCENC_NEON=0 ASTCENC_SVE=0)
endif()

# --- audio (L3): miniaudio (vendored, engine/third_party/miniaudio, MIT-0) ---
add_library(engine_audio STATIC audio/mixer.cpp audio/device.cpp audio/clip.cpp audio/miniaudio_impl.c)
target_include_directories(engine_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio)
target_link_libraries(engine_audio PUBLIC engine_core ${CMAKE_DL_LIBS})
if(APPLE) # CoreAudio arka ucu: cerceveler (CI macOS; yerelde denenmedi)
    target_link_libraries(engine_audio PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
endif()
target_compile_options(engine_audio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> $<$<COMPILE_LANGUAGE:C>:-w>)
add_dependencies(engine_audio engine_layer_check)

# --- recastnavigation (vendored, engine/third_party/recast) ------------------
# Recast = bake (sahne derleyicisi/test), Detour = runtime sorgu.
file(GLOB ENGINE_RECAST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/recast/Recast/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/recast/Detour/Source/*.cpp)
add_library(engine_recast STATIC ${ENGINE_RECAST_SOURCES})
target_include_directories(engine_recast PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/recast/Recast/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/recast/Detour/Include)
target_compile_options(engine_recast PRIVATE -fno-exceptions -fno-rtti -w -ffp-contract=off)

# --- L4 SIM (ECS, zamanlayici, sabit adim, replay, fizik, navmesh) -----------
add_library(engine_sim STATIC
    sim/ecs.cpp
    sim/schedule.cpp
    sim/physics.cpp
    sim/navmesh.cpp
    sim/animation.cpp
)
target_link_libraries(engine_sim PUBLIC engine_core engine_jolt engine_recast)
target_compile_options(engine_sim PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> -ffp-contract=off)
add_dependencies(engine_sim engine_layer_check)

# --- L3 RENDERER -----------------------------------------------------------------
add_library(engine_renderer STATIC renderer/renderer.cpp renderer/cluster.cpp)
target_link_libraries(engine_renderer PUBLIC engine_rhi)
target_compile_options(engine_renderer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> -ffp-contract=off)
add_dependencies(engine_renderer engine_layer_check)

# --- L6 CONTENT: glTF (cgltf) + goruntu (stb_image), yalniz yukleme aninda malloc ---
add_library(engine_content STATIC content/gltf.cpp content/meshopt.cpp content/ktx2.cpp content/font.cpp content/vendored_impl.c)
target_include_directories(engine_content PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(engine_content PUBLIC engine_renderer engine_meshopt engine_astcenc)
target_compile_options(engine_content PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> $<$<COMPILE_LANGUAGE:C>:-w>)
add_dependencies(engine_content engine_layer_check)

# --- Demo uygulamasi (L5): pencere (GLFW dlopen) ya da --headless -------------
if(NOT ANDROID)
add_executable(engine_demo
    app/demo.cpp
    app/demo_app.cpp
    app/demo_scene.cpp
    app/virtual_stick.cpp
    core/memory/alloc_gate_override.cpp
)
target_link_libraries(engine_demo PRIVATE engine_content engine_renderer engine_sim engine_rhi engine_audio engine_core ${CMAKE_DL_LIBS})
target_compile_options(engine_demo PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> -ffp-contract=off)
target_compile_definitions(engine_demo PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# --- Testler ------------------------------------------------------------------
# alloc_gate_override.cpp YURUTULEBILIRE eklenir: hangi ikilinin sayildigi
# burada gorunur (A2 kapisi).
set(ENGINE_TEST_SOURCES
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
    tests/test_audio.cpp
    tests/test_sim.cpp
    tests/test_physics.cpp
    tests/test_navmesh.cpp
    tests/test_animation.cpp
    tests/test_scene_faz2.cpp
    tests/test_renderer.cpp
    tests/test_content.cpp
    tests/test_input.cpp
)
# Fizik altin ozeti: x86_64'te hesaplanir, CI arm64 ve telefon (Kirin 970) karsilastirir.
# 0x5dc41c2bddc345cb: x86_64 (SSE4.2, FMA yok), 65 govde x 300 adim, baslangic donusleri SABIT bit (libm yok), 2026-09-14.
set(ENGINE_PHYSICS_GOLDEN_HASH "0x5dc41c2bddc345cb" CACHE STRING "Jolt sahnesinin platformlar arasi altin ozeti (x86_64'te hesaplandi)")
# --- Dear ImGui + ImGuizmo (vendored, engine/third_party/imgui, MIT) — yalniz masaustu editor
if(NOT ANDROID)
    add_library(engine_imgui STATIC
        third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_impl_vulkan.cpp third_party/imgui/ImGuizmo.cpp)
    target_include_directories(engine_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan)
    # Prototip yok: Vulkan fonksiyonlari bizim dlopen'li yukleyiciden (ImGui_ImplVulkan_LoadFunctions).
    target_compile_definitions(engine_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_compile_options(engine_imgui PRIVATE -fno-exceptions -fno-rtti -w)
    list(APPEND ENGINE_TEST_SOURCES tests/test_editor.cpp app/editor_ui.cpp)
endif()

add_executable(engine_tests ${ENGINE_TEST_SOURCES} app/virtual_stick.cpp core/memory/alloc_gate_override.cpp)
target_link_libraries(engine_tests PRIVATE engine_content engine_renderer engine_rhi engine_sim engine_audio engine_core ${CMAKE_DL_LIBS})
if(NOT ANDROID)
    target_link_libraries(engine_tests PRIVATE engine_imgui)
    target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw)
    # --- engine_texpack: PNG -> ASTC mip zinciri -> .ktx2 (sahne derleyicisi doku dilimi)
    add_executable(engine_texpack tools/texpack.cpp)
    target_link_libraries(engine_texpack PRIVATE engine_content engine_core ${CMAKE_DL_LIBS})
    target_compile_options(engine_texpack PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)
    # --- engine_editor: motor duzenleme kipinde (ImGui paneller + ImGuizmo), masaustu / headless
    add_executable(engine_editor app/editor.cpp app/editor_app.cpp app/editor_ui.cpp app/demo_scene.cpp)
    target_link_libraries(engine_editor PRIVATE engine_content engine_renderer engine_sim engine_rhi engine_core engine_imgui ${CMAKE_DL_LIBS})
    target_include_directories(engine_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw)
    target_compile_options(engine_editor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> -ffp-contract=off)
    target_compile_definitions(engine_editor PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()
target_compile_options(engine_tests PRIVATE -ffp-contract=off) # sim belirlenimliligi (strict FP, plan C.1.5)
target_compile_definitions(engine_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" PHYSICS_GOLDEN_HASH=${ENGINE_PHYSICS_GOLDEN_HASH}ull)
target_compile_options(engine_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}>)

# --- Android NativeActivity host: libtulparengine.so (testler + demo ayni surecte) ---
# adb shell'den GPU yok (vkEnumeratePhysicalDevices = 0, SELinux/HAL); APK sureci gerekir.
# Paketleme/kosum: engine/tools/android_run.sh
if(ANDROID)
  set(ENGINE_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
  add_library(tulparengine SHARED
      app/android_main.cpp
      app/demo_app.cpp
      app/demo_scene.cpp
      app/virtual_stick.cpp
      core/memory/alloc_gate_override.cpp
      ${ENGINE_GLUE_DIR}/android_native_app_glue.c
      ${ENGINE_TEST_SOURCES}
  )
  target_include_directories(tulparengine PRIVATE ${ENGINE_GLUE_DIR})
  target_compile_definitions(tulparengine PRIVATE ENGINE_TESTS_NO_MAIN=1
      ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" PHYSICS_GOLDEN_HASH=${ENGINE_PHYSICS_GOLDEN_HASH}ull)
  target_compile_options(tulparengine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_CXX_OPTS}> -ffp-contract=off)
  target_link_libraries(tulparengine PRIVATE engine_content engine_renderer engine_sim engine_rhi engine_audio engine_core android log ${CMAKE_DL_LIBS})
  # AGDK Swappy (kare temposu, Apache-2.0): third_party/swappy/libs/<abi>/libswappy_static.a
  # (tools/fetch_swappy.sh, gitignore'lu). ENGINE_SWAPPY=ON ve lib varsa baglanir.
  option(ENGINE_SWAPPY "AGDK Swappy kare temposu (SwappyVk_queuePresent)" OFF)
  set(ENGINE_SWAPPY_LIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/swappy/libs/${ANDROID_ABI}/libswappy_static.a)
  if(ENGINE_SWAPPY AND EXISTS ${ENGINE_SWAPPY_LIB})
      target_include_directories(tulparengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/swappy/include)
      target_compile_definitions(tulparengine PRIVATE ENGINE_SWAPPY=1)
      target_link_libraries(tulparengine PRIVATE ${ENGINE_SWAPPY_LIB})
      message(STATUS "engine: Swappy bagli (${ANDROID_ABI})")
  elseif(ENGINE_SWAPPY)
      message(WARNING "engine: ENGINE_SWAPPY=ON ama ${ENGINE_SWAPPY_LIB} yok (tools/fetch_swappy.sh)")
  endif()
  # --no-undefined: eksik sembol LINK'te dussun (cihazda UnsatisfiedLinkError degil);
  # 16 KB sayfa; glue'nun ANativeActivity_onCreate'i disari ver.
  target_link_options(tulparengine PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384 -u ANativeActivity_onCreate)
endif()
