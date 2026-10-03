cmake_minimum_required(VERSION 3.20)
project(nightmare C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

# libchdr reads the user's CHD disc image at runtime.
include(FetchContent)
set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libchdr
    GIT_REPOSITORY https://github.com/rtissera/libchdr.git
    GIT_TAG 607694ca0812edfc9cc2030c64634fc2393668de
)
FetchContent_MakeAvailable(libchdr)

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
    src/port/hw/cdrom.c
    src/port/hw/dma.c
    src/port/hw/gpu.c
    src/port/hw/irq.c
    src/port/hw/mdec.c
    src/port/hw/spu.c
    src/port/hw/timers.c
    src/port/hw/widescreen.c
    src/port/log.c
    src/port/memcard.c
    src/port/memory.c
    src/port/patches.c
    src/port/platform.c
    src/port/settings.c
    src/port/ui/menu.cpp
)
target_include_directories(nc_runtime PUBLIC src)
target_link_libraries(nc_runtime PUBLIC SDL3::SDL3 chdr-static imgui)
if(APPLE)
    target_link_libraries(nc_runtime PUBLIC objc) # platform.c: App Nap opt-out
endif()

if(MSVC)
    target_compile_options(nc_runtime PRIVATE /W4)
else()
    target_compile_options(nc_runtime PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
                                              $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
endif()

# Recompiled code is generated from the user's disc by `make recomp` into gen/.
# It is machine-written, so warnings are silenced and optimisation is kept on even in Debug
# builds to keep compile and run times reasonable.
set(NC_GEN_DIR ${CMAKE_SOURCE_DIR}/gen)
file(GLOB NC_GEN_SOURCES CONFIGURE_DEPENDS ${NC_GEN_DIR}/*/*.c)

if(NC_GEN_SOURCES)
    add_library(nc_gen STATIC ${NC_GEN_SOURCES})
    target_include_directories(nc_gen PRIVATE src ${NC_GEN_DIR})
    if(MSVC)
        target_compile_options(nc_gen PRIVATE /w /O2)
    else()
        target_compile_options(nc_gen PRIVATE -w -O2 -fno-strict-aliasing)
    endif()

    add_executable(nightmare src/port/main.c)
    target_link_libraries(nightmare PRIVATE nc_runtime nc_gen)
    # Export symbols so NC_TRACE_STACK backtraces show recompiled function names.
    if(APPLE)
        target_link_options(nightmare PRIVATE -Wl,-export_dynamic)
    elseif(UNIX)
        target_link_options(nightmare PRIVATE -rdynamic)
    endif()
else()
    message(WARNING "gen/ is empty: run `make recomp` to build the game executable")
endif()
