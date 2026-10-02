cmake_minimum_required(VERSION 3.20)
project(nightmare C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

add_executable(nightmare
    src/port/main.c
)
target_include_directories(nightmare PRIVATE src)
target_link_libraries(nightmare PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(nightmare PRIVATE /W4)
else()
    target_compile_options(nightmare PRIVATE -Wall -Wextra)
endif()
