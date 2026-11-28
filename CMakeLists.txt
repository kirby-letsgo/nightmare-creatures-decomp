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
if(ANDROID OR NC_VENDOR_SDL)
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
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
    src/port/raw_disc.c
    src/port/replay.c
    src/port/savestate.c
    src/port/settings.c
    src/port/shadow.c
    src/port/ui/menu.cpp
)
# Embed the menu typeface from the pinned ImGui checkout so single-file packages can use it.
file(READ "${imgui_SOURCE_DIR}/misc/fonts/Karla-Regular.ttf" karla_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," karla_bytes "${karla_hex}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/menu_font.h"
     CONTENT "static const unsigned char menu_font[] = {@karla_bytes@};" @ONLY)
target_include_directories(nc_runtime PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

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

if(ANDROID)
    add_library(nightmare SHARED src/port/main.c src/port/android_pad.c src/port/no_gen.c)
    set_target_properties(nightmare PROPERTIES OUTPUT_NAME main)
    target_compile_definitions(nightmare PRIVATE NC_VERSION="${NC_VERSION}")
    target_link_libraries(nightmare PRIVATE nc_runtime)
    # Compile the Android activity against the Java glue from the same SDL checkout.
    file(WRITE "${CMAKE_BINARY_DIR}/sdl-java-dir.txt"
        "${sdl3_SOURCE_DIR}/android-project/app/src/main/java")
    return()
endif()

# Recompiled code is generated from the user's disc by `make recomp` into gen/. Without it (and
# always with NC_INTERPRETER_ONLY, as in release builds) the game code is interpreted from the
# disc at runtime, so the executable contains no code derived from the game.
# Recompiled code is machine-written, so warnings are silenced and optimisation is kept on even
# in Debug builds to keep compile and run times reasonable.
option(NC_INTERPRETER_ONLY "Do not link recompiled code from gen/ (release builds)" OFF)
option(NC_NATIVE_DECOMP "Without gen/, call handwritten routines instead of interpreting them" ON)
set(NC_GEN_DIR ${CMAKE_SOURCE_DIR}/gen)
file(GLOB NC_GEN_SOURCES CONFIGURE_DEPENDS ${NC_GEN_DIR}/*/*.c)

add_executable(nightmare src/port/main.c)
target_compile_definitions(nightmare PRIVATE NC_VERSION="${NC_VERSION}")
if(WIN32)
    set_target_properties(nightmare PROPERTIES WIN32_EXECUTABLE ON) # no console window
    target_sources(nightmare PRIVATE packaging/windows/icon.rc)    # app icon
endif()
target_link_libraries(nightmare PRIVATE nc_runtime)
# Handwritten C for original routines (src/decomp), verified against the interpreter by tests/.
# It contains no code derived from the game, so every build links it.
file(GLOB NC_DECOMP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/decomp/*.c
    ${CMAKE_SOURCE_DIR}/src/decomp/twins/*.c)
add_library(nc_decomp STATIC ${NC_DECOMP_SOURCES})
target_link_libraries(nc_decomp PUBLIC nc_runtime)
if(NC_GEN_SOURCES AND NOT NC_INTERPRETER_ONLY)
    add_library(nc_gen STATIC ${NC_GEN_SOURCES})
    target_link_libraries(nc_gen PRIVATE nc_decomp)
    target_include_directories(nc_gen PRIVATE src ${NC_GEN_DIR})
    if(MSVC)
        target_compile_options(nc_gen PRIVATE /w /O2)
    else()
        target_compile_options(nc_gen PRIVATE -w -O2 -fno-strict-aliasing)
    endif()
    target_link_libraries(nightmare PRIVATE nc_gen)
elseif(NC_NATIVE_DECOMP)
    # Without recompiled code, handwritten routines replace the original ones they cover
    # (the `entry` rows in config/splices) and everything else is interpreted.
    file(GLOB NC_SPLICES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/config/splices/*.txt)
    set(NC_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/native_registry.c)
    add_custom_command(OUTPUT ${NC_REGISTRY}
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_registry.py ${NC_REGISTRY}
        DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_registry.py ${NC_SPLICES}
        COMMENT "Generating native function tables from config/splices")
    target_sources(nightmare PRIVATE ${NC_REGISTRY})
    target_link_libraries(nightmare PRIVATE nc_decomp)
    message(STATUS "Building without recompiled code: handwritten routines, the rest interpreted")
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

add_test(NAME first_level COMMAND Python3::Interpreter
    ${CMAKE_SOURCE_DIR}/tools/test_first_level.py $<TARGET_FILE:nightmare>)
add_test(NAME first_combat COMMAND Python3::Interpreter
    ${CMAKE_SOURCE_DIR}/tools/test_first_level.py $<TARGET_FILE:nightmare> --scenario first_combat)
add_test(NAME first_level_no_adrenaline COMMAND Python3::Interpreter
    ${CMAKE_SOURCE_DIR}/tools/test_first_level.py $<TARGET_FILE:nightmare> --no-adrenaline)
set_tests_properties(first_level first_combat first_level_no_adrenaline PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_disc_formats tests/disc_formats.c src/port/no_gen.c)
target_link_libraries(test_disc_formats PRIVATE nc_runtime)
add_test(NAME disc_formats COMMAND test_disc_formats
    "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd"
    "${CMAKE_SOURCE_DIR}/build/nc.cue" "${CMAKE_SOURCE_DIR}/build/nc.bin")
set_tests_properties(disc_formats PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_adrenaline tests/adrenaline.c src/decomp/adrenaline.c src/port/no_gen.c)
target_link_libraries(test_adrenaline PRIVATE nc_runtime)
add_test(NAME adrenaline COMMAND test_adrenaline "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(adrenaline PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_damage tests/damage.c src/decomp/damage.c src/port/no_gen.c)
target_link_libraries(test_damage PRIVATE nc_runtime)
add_test(NAME damage_rules COMMAND test_damage)
add_test(NAME damage_disc COMMAND test_damage "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(damage_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_base_damage tests/base_damage.c src/decomp/damage.c src/port/no_gen.c)
target_link_libraries(test_base_damage PRIVATE nc_runtime)
add_test(NAME base_damage_rules COMMAND test_base_damage)
add_test(NAME base_damage_disc COMMAND test_base_damage "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(base_damage_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_contact tests/contact.c src/decomp/contact.c src/port/no_gen.c)
target_link_libraries(test_contact PRIVATE nc_runtime)
add_test(NAME contact_rules COMMAND test_contact)
add_test(NAME contact_disc COMMAND test_contact "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(contact_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_distance tests/distance.c src/decomp/distance.c src/port/no_gen.c)
target_link_libraries(test_distance PRIVATE nc_runtime)
add_test(NAME distance_rules COMMAND test_distance)
add_test(NAME distance_disc COMMAND test_distance "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(distance_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_test(NAME continue_game COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/test_continue.py"
    $<TARGET_FILE:nightmare> "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(continue_game PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_resume_exec tests/resume_exec.c src/port/no_gen.c)
target_link_libraries(test_resume_exec PRIVATE nc_runtime)
foreach(point playing paused)
    set(resume_dir "${CMAKE_CURRENT_BINARY_DIR}/test-resume-${point}/")
    file(MAKE_DIRECTORY "${resume_dir}")
    add_test(NAME resume_save_${point} COMMAND test_resume_exec "${resume_dir}" save ${point})
    add_test(NAME resume_load_${point} COMMAND test_resume_exec "${resume_dir}" load ${point})
    set_tests_properties(resume_save_${point} PROPERTIES FIXTURES_SETUP saved_${point})
    set_tests_properties(resume_load_${point} PROPERTIES FIXTURES_REQUIRED saved_${point})
endforeach()

add_executable(test_launcher tests/launcher.c src/decomp/launcher.c src/port/no_gen.c)
target_link_libraries(test_launcher PRIVATE nc_runtime)
add_test(NAME launcher_disc COMMAND test_launcher "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(launcher_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc" TIMEOUT 30)

add_executable(test_credits tests/credits.c src/decomp/credits.c src/port/no_gen.c)
target_link_libraries(test_credits PRIVATE nc_runtime)
add_test(NAME credits_input_rules COMMAND test_credits)
add_test(NAME credits_input_disc COMMAND test_credits "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_input_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_credits_volume tests/credits_volume.c src/decomp/credits.c src/port/no_gen.c)
target_link_libraries(test_credits_volume PRIVATE nc_runtime)
add_test(NAME credits_volume_disc COMMAND test_credits_volume "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_volume_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_credits_heap tests/credits_heap.c src/decomp/credits.c src/port/no_gen.c)
target_link_libraries(test_credits_heap PRIVATE nc_runtime)
add_test(NAME credits_heap_disc COMMAND test_credits_heap "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_heap_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

if(TARGET nc_gen)
    add_executable(test_credits_roll_native tests/credits_roll.c)
    target_link_libraries(test_credits_roll_native PRIVATE nc_runtime nc_gen)
    add_executable(test_credits_roll_original tests/credits_roll.c src/port/no_gen.c)
    target_link_libraries(test_credits_roll_original PRIVATE nc_runtime)
    foreach(scenario skip full exception_return)
        add_test(NAME credits_roll_${scenario} COMMAND Python3::Interpreter
            "${CMAKE_SOURCE_DIR}/tools/test_credits_roll.py" $<TARGET_FILE:test_credits_roll_native>
            $<TARGET_FILE:test_credits_roll_original> "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd" ${scenario})
        set_tests_properties(credits_roll_${scenario} PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")
    endforeach()
elseif(NC_REGISTRY)
    # The same driver through the native registry, for shadow mode (make shadow).
    add_executable(test_credits_roll_registry tests/credits_roll.c ${NC_REGISTRY})
    target_link_libraries(test_credits_roll_registry PRIVATE nc_decomp)
    add_executable(test_credits_roll_original tests/credits_roll.c src/port/no_gen.c)
    target_link_libraries(test_credits_roll_original PRIVATE nc_runtime)
endif()

add_executable(test_credits_tim tests/credits_tim.c src/decomp/credits_roll.c)
target_link_libraries(test_credits_tim PRIVATE nc_runtime)
add_test(NAME credits_tim_disc COMMAND test_credits_tim "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_tim_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_credits_file tests/credits_file.c src/decomp/credits_roll.c)
target_link_libraries(test_credits_file PRIVATE nc_runtime)
add_test(NAME credits_file_disc COMMAND test_credits_file "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_file_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_credits_pad tests/credits_pad.c src/decomp/credits_roll.c)
target_link_libraries(test_credits_pad PRIVATE nc_runtime)
add_test(NAME credits_pad_disc COMMAND test_credits_pad "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_pad_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_credits_music tests/credits_music.c src/decomp/credits_roll.c)
target_link_libraries(test_credits_music PRIVATE nc_runtime)
add_test(NAME credits_music_disc COMMAND test_credits_music "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_music_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_credits_sdk tests/credits_sdk.c src/decomp/credits_sdk.c src/port/no_gen.c)
target_link_libraries(test_credits_sdk PRIVATE nc_runtime)
add_test(NAME credits_sdk_disc COMMAND test_credits_sdk "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(credits_sdk_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_psx_cd tests/psx_cd.c src/decomp/psx_cd.c src/port/no_gen.c)
target_link_libraries(test_psx_cd PRIVATE nc_runtime)
add_test(NAME psx_cd_rules COMMAND test_psx_cd)
add_test(NAME psx_cd_disc COMMAND test_psx_cd "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(psx_cd_disc PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")

add_executable(test_psx2_pad tests/psx2_pad.c)
if(TARGET nc_gen)
    target_link_libraries(test_psx2_pad PRIVATE nc_runtime nc_gen)
    target_compile_definitions(test_psx2_pad PRIVATE NC_TEST_GENERATED_PAD=1)
else()
    target_sources(test_psx2_pad PRIVATE src/decomp/pad.c src/decomp/distance.c src/port/no_gen.c)
    target_link_libraries(test_psx2_pad PRIVATE nc_runtime)
endif()
add_test(NAME psx2_pad COMMAND test_psx2_pad "${CMAKE_SOURCE_DIR}/roms/Nightmare Creatures.chd")
set_tests_properties(psx2_pad PROPERTIES SKIP_RETURN_CODE 77 LABELS "disc")
