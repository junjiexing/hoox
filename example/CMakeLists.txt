# Standalone build for the hoox amalgamation example.
#
# This example builds against the single-file library the main project
# generates at build/amalgam/hoox.{c,h}. Point HOOX_AMALGAM_DIR elsewhere if
# your amalgamation lives somewhere else.
#
#   # 1. generate the amalgamation from the repo root (once):
#   cmake -S .. -B ../build -G Ninja -DCMAKE_C_COMPILER=clang \
#         -DHOOX_BUILD_AMALGAMATION=ON
#   cmake --build ../build --target test_amalgam
#
#   # 2. build + run this example:
#   cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang
#   cmake --build build
#   ./build/hook_example        (Windows: .\build\hook_example.exe)
#
# Toolchain: clang / clang-cl required (the hook engine uses GNU builtins such
# as __builtin_alloca that MSVC's cl does not provide).

cmake_minimum_required(VERSION 3.20)

project(hoox_example LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Location of the project-generated single-file library.
set(HOOX_AMALGAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/amalgam"
  CACHE PATH "Directory containing the generated hoox.c / hoox.h")

if(NOT EXISTS "${HOOX_AMALGAM_DIR}/hoox.c")
  message(FATAL_ERROR
    "hoox.c not found in ${HOOX_AMALGAM_DIR}.\n"
    "Generate the amalgamation first (see the header of this file), or pass "
    "-DHOOX_AMALGAM_DIR=<dir>.")
endif()

add_executable(hook_example
  hook_example.c
  "${HOOX_AMALGAM_DIR}/hoox.c")

target_include_directories(hook_example PRIVATE "${HOOX_AMALGAM_DIR}")

# No hoox feature macros are needed: static linkage and the system allocator
# are the defaults, and the target arch/OS auto-detect from compiler built-ins.
# Consumers only link the platform's process API.
if(WIN32)
  target_link_libraries(hook_example PRIVATE psapi)
  target_compile_definitions(hook_example PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(hook_example PRIVATE pthread)
endif()
