cmake_minimum_required(VERSION 3.20)
project(nightmare C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

# libchdr reads the user's CHD disc image at runtime.
include(FetchContent)
set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libchdr
    GIT_REPOSITORY https://github.com/rtissera/libchdr.git
    GIT_TAG 607694ca0812edfc9cc2030c64634fc2393668de
)
FetchContent_MakeAvailable(libchdr)

# Native runtime: everything except the recompiled game code.
add_library(nc_runtime STATIC
    src/port/bios.c
    src/port/cpu.c
    src/port/disc.c
    src/port/dispatch.c
    src/port/exe.c
    src/port/gte.c
    src/port/hw/cdrom.c
    src/port/hw/dma.c
    src/port/hw/gpu.c
    src/port/hw/irq.c
    src/port/hw/spu.c
    src/port/hw/timers.c
    src/port/log.c
    src/port/memory.c
)
target_include_directories(nc_runtime PUBLIC src)
target_link_libraries(nc_runtime PUBLIC SDL3::SDL3 chdr-static)

if(MSVC)
    target_compile_options(nc_runtime PRIVATE /W4)
else()
    target_compile_options(nc_runtime PRIVATE -Wall -Wextra)
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
else()
    message(WARNING "gen/ is empty: run `make recomp` to build the game executable")
endif()
