cmake_minimum_required(VERSION 3.20)
project(nightmare C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release builds link everything statically (no runtime dependencies to install).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(NC_VERSION "dev" CACHE STRING "Version string shown by --version and in the window")

# SDL3: the system package for development; NC_VENDOR_SDL builds a pinned release statically.
option(NC_VENDOR_SDL "Build SDL3 from source and link it statically" OFF)
include(FetchContent)
if(NC_VENDOR_SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 829a65d769d935c4852f8159e964312c0957260a # release-3.4.18
    )
    FetchContent_MakeAvailable(SDL3)
else()
    find_package(SDL3 REQUIRED CONFIG)
endif()

# libchdr reads the user's CHD disc image at runtime.
set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
set(MINIZ_DEFLATE_APIS ON CACHE BOOL "" FORCE) # save states are deflate-compressed
FetchContent_Declare(libchdr
    GIT_REPOSITORY https://github.com/rtissera/libchdr.git
    GIT_TAG 607694ca0812edfc9cc2030c64634fc2393668de
)
FetchContent_MakeAvailable(libchdr)
target_include_directories(miniz PUBLIC ${libchdr_SOURCE_DIR}/deps/miniz-3.1.2)

# Dear ImGui for the start screen and in-game menu (built from source with its SDL3 backends).
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 01380c579715e62fb9a8d6ec0502c4ea83bfde6e # v1.92.9
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_features(imgui PUBLIC cxx_std_17)

option(NC_WATCHPOINTS "Debug build: report guest stores to NC_WATCH=<address>" OFF)
if(NC_WATCHPOINTS)
    add_compile_definitions(NC_WATCHPOINTS=1)
endif()

# Native runtime: everything except the recompiled game code.
add_library(nc_runtime STATIC
    src/port/bios.c
    src/port/controls.c
    src/port/cpu.c
    src/port/disc.c
    src/port/dispatch.c
    src/port/exe.c
    src/port/gte.c
    src/port/input.c
    src/port/interp.c
    src/port/hw/cdrom.c
    src/port/hw/dma.c
    src/port/hw/gpu.c
    src/port/hw/irq.c
    src/port/hw/mdec.c
    src/port/hw/spu.c
    src/port/hw/timers.c
    src/port/hw/widescreen.c
    src/port/hw/xbrz_wrap.cpp
    src/port/log.c
    src/port/memcard.c
    src/port/memory.c
    src/port/patches.c
    src/port/platform.c
    src/port/savestate.c
    src/port/settings.c
    src/port/ui/menu.cpp
)
target_include_directories(nc_runtime PUBLIC src)

# xBRZ texture upscaler (GPLv3, third_party/xbrz).
add_library(xbrz STATIC third_party/xbrz/xbrz.cpp)
target_include_directories(xbrz PUBLIC third_party/xbrz)
target_compile_features(xbrz PUBLIC cxx_std_17)
target_link_libraries(nc_runtime PRIVATE xbrz)
target_link_libraries(nc_runtime PUBLIC SDL3::SDL3 chdr-static imgui miniz)
if(APPLE)
    target_link_libraries(nc_runtime PUBLIC objc) # platform.c: App Nap opt-out
endif()

if(MSVC)
    target_compile_options(nc_runtime PRIVATE /W4)
else()
    target_compile_options(nc_runtime PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
                                              $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
endif()

# Interpreter patch/hook table, generated from config/ (addresses only, no game data).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(NC_PATCH_TABLE ${CMAKE_BINARY_DIR}/nc_patch_table.c)
add_custom_command(
    OUTPUT ${NC_PATCH_TABLE}
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_patch_table.py ${NC_PATCH_TABLE}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_patch_table.py ${CMAKE_SOURCE_DIR}/config/patches.txt
            ${CMAKE_SOURCE_DIR}/config/hooks.txt
    COMMENT "Generating interpreter patch table"
)
target_sources(nc_runtime PRIVATE ${NC_PATCH_TABLE})

# Recompiled code is generated from the user's disc by `make recomp` into gen/. Without it (and
# always with NC_INTERPRETER_ONLY, as in release builds) the game code is interpreted from the
# disc at runtime, so the executable contains no code derived from the game.
# Recompiled code is machine-written, so warnings are silenced and optimisation is kept on even
# in Debug builds to keep compile and run times reasonable.
option(NC_INTERPRETER_ONLY "Do not link recompiled code from gen/ (release builds)" OFF)
set(NC_GEN_DIR ${CMAKE_SOURCE_DIR}/gen)
file(GLOB NC_GEN_SOURCES CONFIGURE_DEPENDS ${NC_GEN_DIR}/*/*.c)

add_executable(nightmare src/port/main.c)
target_compile_definitions(nightmare PRIVATE NC_VERSION="${NC_VERSION}")
if(WIN32)
    set_target_properties(nightmare PROPERTIES WIN32_EXECUTABLE ON) # no console window
    target_sources(nightmare PRIVATE packaging/windows/icon.rc)    # app icon
endif()
target_link_libraries(nightmare PRIVATE nc_runtime)
if(NC_GEN_SOURCES AND NOT NC_INTERPRETER_ONLY)
    add_library(nc_gen STATIC ${NC_GEN_SOURCES})
    target_include_directories(nc_gen PRIVATE src ${NC_GEN_DIR})
    if(MSVC)
        target_compile_options(nc_gen PRIVATE /w /O2)
    else()
        target_compile_options(nc_gen PRIVATE -w -O2 -fno-strict-aliasing)
    endif()
    target_link_libraries(nightmare PRIVATE nc_gen)
else()
    target_sources(nightmare PRIVATE src/port/no_gen.c)
    message(STATUS "Building without recompiled code: game code is interpreted")
endif()
# Export symbols so NC_TRACE_STACK backtraces show recompiled function names.
if(APPLE)
    target_link_options(nightmare PRIVATE -Wl,-export_dynamic)
elseif(UNIX)
    target_link_options(nightmare PRIVATE -rdynamic)
endif()

# Smoke test for CI (no disc available there): the executable starts and reports its version.
enable_testing()
add_test(NAME version COMMAND nightmare --version)
