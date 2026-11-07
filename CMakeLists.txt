# CMakeLists.txt for WWF No Mercy: Recompiled (bootstrap; adapted from Revenge/World Tour)
# The real Stage-B build: recompiled game + ultramodern + librecomp + RecompFrontend + RT64.
# Adapted from BMHero. Configure with clang-cl (see tools/env-msvc.ps1):
#   cmake -S . -B build-msvc -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-msvc --target NoMercyRecompiled
cmake_minimum_required(VERSION 3.20)
project(NoMercyRecompiled LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
    add_compile_definitions(NOMINMAX)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Both SDL2 copies in this build (rt64's vendored 2.26.3 and the FetchContent
        # 2.30.3 devel headers) define _m_prefetch in SDL_endian.h behind
        # #ifndef __PRFCHWINTRIN_H, as a workaround for old clang. clang 20+ makes
        # _m_prefetch a target builtin and hard-errors on that redefinition
        # (-fno-builtin-<name> does NOT apply to target builtins — verified on CI's
        # clang-cl 22). Force-including intrin.h first lets clang's own prfchwintrin.h
        # set the guard, so SDL's hack is skipped in every TU that includes SDL.
        add_compile_options(/FIintrin.h)
    endif()
endif()
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(NMR ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)
set(RT64DIR ${CMAKE_SOURCE_DIR}/lib/rt64)

# --- Android (Android TV app, see android/README.md) ---
# The engine is built as libNoMercyRecompiled.so and started by SDL's Java glue
# (android/app). SDL2 and FreeType are built from source; host tools (file_to_c)
# and dxc run on the build machine. android/native/build.sh drives this.
if (ANDROID)
    include(FetchContent)
    # 2.30.12, not the desktop's 2.30.3: older SDL doesn't compile with NDK r27.
    # Must match the Java glue in android/app/src/main/java/org/libsdl/app.
    set(SDL2_VERSION "2.30.12")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-${SDL2_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(sdl2)
    set(SDL2_INCLUDE_DIRS ${sdl2_SOURCE_DIR}/include ${sdl2_BINARY_DIR}/include ${sdl2_BINARY_DIR}/include-config-release/SDL2)
    include_directories(${SDL2_INCLUDE_DIRS})
    # The runtime libraries call find_package(SDL2); point them at the SDL built above.
    file(WRITE ${CMAKE_BINARY_DIR}/sdl2-config/SDL2Config.cmake
        "set(SDL2_FOUND TRUE)\nset(SDL2_INCLUDE_DIRS \"${SDL2_INCLUDE_DIRS}\")\nset(SDL2_LIBRARIES SDL2::SDL2)\n")
    set(SDL2_DIR ${CMAKE_BINARY_DIR}/sdl2-config CACHE PATH "" FORCE)

    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-13-3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(freetype)
    if (NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(FREETYPE_FOUND TRUE)
    set(FREETYPE_LIBRARIES freetype)
    set(FREETYPE_INCLUDE_DIRS ${freetype_SOURCE_DIR}/include)

    # Every library renders through the SDL Vulkan window (same path as Linux).
    add_compile_definitions(PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)

    set(NMR_ANDROID_NFD_SOURCE ${CMAKE_SOURCE_DIR}/android/native/nfd_android.cpp)

    # file_to_c must run on the build machine, not the device.
    if (NOT NMR_HOST_FILE_TO_C)
        message(FATAL_ERROR "Android builds need -DNMR_HOST_FILE_TO_C=<host file_to_c> (android/native/build.sh builds it).")
    endif()
endif()

# --- RT64 (renderer) ---
set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)
add_subdirectory(${RT64DIR} ${CMAKE_BINARY_DIR}/rt64)
target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# --- Runtime core (ultramodern + librecomp) ---
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${NMR})

set(RECOMP_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${NMR}/ultramodern/include
    ${NMR}/librecomp/include
    ${NMR}/N64Recomp/include
)

# --- RecompiledFuncs: the recompiled game ---
# NMR_ENGINE_ONLY builds the runtime stack without the ROM-generated game code
# (CI checks that the Android engine compiles without having the ROM).
option(NMR_ENGINE_ONLY "Build only the runtime libraries, not the game" OFF)
if (NOT NMR_ENGINE_ONLY)
add_library(RecompiledFuncs STATIC)
target_compile_options(RecompiledFuncs PRIVATE -Wno-everything)
target_include_directories(RecompiledFuncs PRIVATE ${RECOMP_INCLUDES})
file(GLOB FUNC_C_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES})

# --- [wcw romhack] Functions the post-recompile pipeline repaired (aki-roadmap#29) ---
# tools/romhack/list_repaired_functions.py writes RecompiledFuncs/repaired_functions.txt; the runtime warns when a
# ROM-patch mod changes one of them (the live recompiler has none of the repairs). Regenerated when the list changes.
set(REPAIRED_LIST ${CMAKE_SOURCE_DIR}/RecompiledFuncs/repaired_functions.txt)
set(REPAIRED_CPP ${CMAKE_CURRENT_BINARY_DIR}/repaired_functions.cpp)
set(REPAIRED_NAMES "")
if (EXISTS ${REPAIRED_LIST})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPAIRED_LIST})
    file(STRINGS ${REPAIRED_LIST} REPAIRED_NAMES REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
else()
    message(WARNING "No ${REPAIRED_LIST}: ROM-patch mods won't be checked against repaired functions "
                    "(run tools/romhack/list_repaired_functions.py after the post-recompile pipeline).")
endif()
list(LENGTH REPAIRED_NAMES REPAIRED_COUNT)
set(REPAIRED_TEXT "// Generated by CMakeLists.txt from RecompiledFuncs/repaired_functions.txt. Do not edit.\n#include \"recomp.h\"\n#include \"librecomp/mods.hpp\"\n\n")
foreach (NAME IN LISTS REPAIRED_NAMES)
    string(APPEND REPAIRED_TEXT "extern \"C\" void ${NAME}(uint8_t* rdram, recomp_context* ctx);\n")
endforeach()
string(APPEND REPAIRED_TEXT "\nvoid wcw_register_repaired_functions() {\n")
if (REPAIRED_COUNT GREATER 0)
    string(APPEND REPAIRED_TEXT "    static recomp_func_t* const funcs[] = {\n")
    foreach (NAME IN LISTS REPAIRED_NAMES)
        string(APPEND REPAIRED_TEXT "        ${NAME},\n")
    endforeach()
    string(APPEND REPAIRED_TEXT "    };\n    recomp::mods::register_repaired_functions(funcs, recomp::mods::RepairedFunctionPolicy::Warn);\n")
else()
    string(APPEND REPAIRED_TEXT "    recomp::mods::register_repaired_functions({}, recomp::mods::RepairedFunctionPolicy::Warn);\n")
endif()
string(APPEND REPAIRED_TEXT "}\n")
file(CONFIGURE OUTPUT ${REPAIRED_CPP} CONTENT "${REPAIRED_TEXT}" @ONLY)
message(STATUS "Repaired functions registered for ROM-patch mods: ${REPAIRED_COUNT}")
endif() # NOT NMR_ENGINE_ONLY

# (Patches pipeline omitted in bootstrap — reintroduce from World Tour's
#  CMakeLists when patches/ exists. The idle-thread fix is a toml
#  instruction patch in revenge.toml, not a C patch.)

# --- DXC setup for the frontend's shaders ---
# build_vertex_shader/etc. (defined in rt64's CMakeLists) resolve ${DXC} and ${DXC_*_OPTS}
# in the CALLER's scope. rt64 sets them in its own subdir scope, so recompui (a sibling
# subdir) sees them empty -> the DXC command becomes malformed. Set them here in the parent
# scope so they propagate into RecompFrontend. (Mirrors BMHero's CMakeLists.)
set(DXC_COMMON_OPTS "-I${RT64DIR}/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set(DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set(DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")
set(DXC_RT_OPTS "${DXC_COMMON_OPTS}" "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")
if (WIN32)
    set(DXC "${RT64DIR}/src/contrib/dxc/bin/x64/dxc.exe")
elseif (ANDROID)
    # Shaders compile on the build machine with RT64's bundled Linux dxc.
    if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(DXC "LD_LIBRARY_PATH=${RT64DIR}/src/contrib/dxc/lib/x64" "${RT64DIR}/src/contrib/dxc/bin/x64/dxc-linux")
    else()
        set(DXC "LD_LIBRARY_PATH=${RT64DIR}/src/contrib/dxc/lib/arm64" "${RT64DIR}/src/contrib/dxc/bin/arm64/dxc-linux")
    endif()
endif()

# --- SDL2 (recompui/recompinput expect sdl2_SOURCE_DIR on Windows) ---
if (WIN32)
    include(FetchContent)
    set(SDL2_VERSION "2.30.3")
    FetchContent_Declare(sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip)
    FetchContent_MakeAvailable(sdl2)
endif()

# --- RecompFrontend (recompui + recompinput) ---
set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH ${NMR})
set(RECOMP_FRONTEND_RT64_PATH ${RT64DIR})
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RecompFrontend)

if (NMR_ENGINE_ONLY)
    # Still compile the game's own (non-generated) sources so CI catches
    # platform breakage without needing the ROM.
    add_library(NoMercyGameSources OBJECT
        ${CMAKE_SOURCE_DIR}/src/main/main.cpp
        ${CMAKE_SOURCE_DIR}/src/game/recomp_api.cpp
        ${CMAKE_SOURCE_DIR}/src/game/raytracing.cpp
        ${CMAKE_SOURCE_DIR}/src/ra/ra_client.cpp)
    foreach (IMG icon logo)
        set(IMG_PNG ${CMAKE_SOURCE_DIR}/icons/app.png)
        if (IMG STREQUAL "logo")
            set(IMG_PNG ${CMAKE_SOURCE_DIR}/icons/logo.png)
        endif()
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${IMG}_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/${IMG}_bytes.h
            COMMAND file_to_c ${IMG_PNG} ${IMG}_bytes ${CMAKE_CURRENT_BINARY_DIR}/${IMG}_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/${IMG}_bytes.h
            DEPENDS ${IMG_PNG} file_to_c)
        target_sources(NoMercyGameSources PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${IMG}_bytes.c)
    endforeach()
    target_include_directories(NoMercyGameSources PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${NMR}/N64Recomp/include
        ${NMR}/thirdparty/sse2neon
        ${RT64DIR}/src
        ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
        ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/include
        ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/include
        ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src
        ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(NoMercyGameSources PRIVATE RC_DISABLE_LUA RC_CLIENT_SUPPORTS_HASH)
    target_compile_options(NoMercyGameSources PRIVATE -fno-strict-aliasing -fms-extensions)
    target_link_libraries(NoMercyGameSources PRIVATE recompui recompinput librecomp ultramodern rt64)
    return()
endif()

# --- Main executable (a shared library on Android, loaded by SDLActivity) ---
if (ANDROID)
    add_library(NoMercyRecompiled SHARED)
    if (NMR_ANDROID_LIB_NAME)
        # One engine, one .so per app: libNoMercyRecompiled.so / libECWRecompiled.so
        set_target_properties(NoMercyRecompiled PROPERTIES OUTPUT_NAME ${NMR_ANDROID_LIB_NAME})
    endif()
    target_link_libraries(NoMercyRecompiled PRIVATE SDL2::SDL2 android log)
else()
    add_executable(NoMercyRecompiled)
endif()

# App icon: exe resource (Windows title bar/taskbar/Explorer) + embedded PNG bytes for
# the launcher thumbnail (GameEntry.thumbnail_bytes).
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
    COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/app.png icon_bytes ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
    DEPENDS ${CMAKE_SOURCE_DIR}/icons/app.png file_to_c
)
target_sources(NoMercyRecompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c)
# Game logo for the recompui launcher menu (replaces the default text title; see
# register_launcher_init_callback in src/main/main.cpp — mirrors World Tour).
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/logo_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/logo_bytes.h
    COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/logo.png logo_bytes ${CMAKE_CURRENT_BINARY_DIR}/logo_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/logo_bytes.h
    DEPENDS ${CMAKE_SOURCE_DIR}/icons/logo.png file_to_c
)
target_sources(NoMercyRecompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/logo_bytes.c)
if (WIN32)
    enable_language(RC)
    target_sources(NoMercyRecompiled PRIVATE ${CMAKE_SOURCE_DIR}/icons/app.rc)
    # The RC compiler doesn't emit depfiles for referenced resources, so without this
    # an app.ico change leaves a stale app.rc.res and the exe keeps the OLD icon
    # (bit us when replacing the bootstrap's Wm2k-derived icon).
    set_property(SOURCE ${CMAKE_SOURCE_DIR}/icons/app.rc PROPERTY
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/icons/app.ico)
endif()
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main/main.cpp
    ${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp
    ${REPAIRED_CPP}
    ${CMAKE_SOURCE_DIR}/src/game/recomp_api.cpp
    ${CMAKE_SOURCE_DIR}/src/game/raytracing.cpp
    ${CMAKE_SOURCE_DIR}/src/ra/ra_client.cpp
    ${CMAKE_SOURCE_DIR}/rsp/nomercy_audio.cpp
)

# RetroAchievements prototype: vendored rcheevos (see src/ra/rcheevos/VENDORED.txt)
# compiled into the game exe. Softcore-only client, env-gated at runtime (WCW_RA=1).
file(GLOB RCHEEVOS_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/*.c
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/rcheevos/*.c
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/rapi/*.c
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/rhash/*.c
)
list(APPEND SOURCES ${RCHEEVOS_SOURCES})
target_include_directories(NoMercyRecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/include
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src
)
target_compile_definitions(NoMercyRecompiled PRIVATE
    RC_DISABLE_LUA
    RC_CLIENT_SUPPORTS_HASH
)
if (WIN32)
    target_link_libraries(NoMercyRecompiled PRIVATE winhttp.lib)
endif()
target_include_directories(NoMercyRecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${NMR}/N64Recomp/include
    ${NMR}/thirdparty/sse2neon
    ${RT64DIR}/src
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/include
    ${CMAKE_CURRENT_BINARY_DIR}
)
if (WIN32)
    target_include_directories(NoMercyRecompiled PRIVATE ${sdl2_SOURCE_DIR}/include)
    target_link_directories(NoMercyRecompiled PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)
    target_link_libraries(NoMercyRecompiled PRIVATE SDL2 Winmm.lib)
    # Release is a windowed app (no console flash on double-click); stdio goes to a log
    # file in the app folder unless launched with --show-console or redirected handles
    # (see wcw_setup_logging in src/main/main.cpp). Debug keeps the console.
    set_target_properties(NoMercyRecompiled PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    if (MSVC)
        target_link_options(NoMercyRecompiled PRIVATE /OPT:NOICF)
    endif()
    # Always emit a linker map: release crash backtraces print RVAs that are resolved
    # against this file (see cycle.ps1 / BUILDING.md). Previously this came from an
    # uncached local configure flag, which fresh clones and CI would silently lose.
    target_link_options(NoMercyRecompiled PRIVATE /MAP:NoMercyRecompiled.map)
    # Bump the default thread stack reserve: ultramodern's gfx_thread (a std::thread, which
    # uses this PE default) reaches D3D12CreateDevice deep in its call stack, and that call
    # is very stack-hungry — on the default 1 MB it overflows (0xC0000409). 16 MB is safe.
    target_link_options(NoMercyRecompiled PRIVATE /STACK:16777216)
    # Copy SDL2 + dxc DLLs next to the exe.
    add_custom_command(TARGET NoMercyRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${RT64DIR}/src/contrib/dxc/bin/x64/dxil.dll"
            "${RT64DIR}/src/contrib/dxc/bin/x64/dxcompiler.dll"
            "${RT64DIR}/src/contrib/librashader/bin/x64/librashader.dll"
            $<TARGET_FILE_DIR:NoMercyRecompiled>
        # CRT filter presets + slang shaders (rt64 [wcw crt]).
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RT64DIR}/src/contrib/crt" "$<TARGET_FILE_DIR:NoMercyRecompiled>/crt")
endif()

# Copy the runtime UI assets and SDL controller db next to the exe so a fresh build runs
# from the build dir (recompui loads ./assets relative to the program dir).
add_custom_command(TARGET NoMercyRecompiled POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:NoMercyRecompiled>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/recompcontrollerdb.txt" "$<TARGET_FILE_DIR:NoMercyRecompiled>")

# PatchesLib MUST come before RecompiledFuncs: patched functions share their C symbol
# name with the base recompiled copies, and the patch takes effect by winning static-lib
# symbol resolution order (mirrors BMHero).
target_link_libraries(NoMercyRecompiled PRIVATE
    RecompiledFuncs
    recompui
    recompinput
    librecomp
    ultramodern
    rt64
    nfd
)
target_sources(NoMercyRecompiled PRIVATE ${SOURCES})
set_property(TARGET NoMercyRecompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# librecomp's RSP vector-unit header (rsp_vu_impl.hpp, pulled in by rsp/nomercy_audio.cpp)
# uses SSSE3/SSE4.1 intrinsics; -march=nehalem enables them (mirrors BMHero).
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(NoMercyRecompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(NoMercyRecompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()
