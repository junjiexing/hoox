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

# hoox is compiled straight into the executable (no shared-lib export dance)
# and uses the system allocator rather than a bundled dlmalloc.
target_compile_definitions(hook_example PRIVATE
  GUM_STATIC
  GUM_USE_SYSTEM_ALLOC)

# --- Architecture family --------------------------------------------------
# The amalgam selects its writer/relocator via HAVE_I386 (x86 + x86_64).
# Extend here as more arch backends land in the amalgamation.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|X86|i386|i686|AMD64|amd64|x86_64")
  target_compile_definitions(hook_example PRIVATE HAVE_I386)
endif()

# --- Platform backend -----------------------------------------------------
if(WIN32)
  target_compile_definitions(hook_example PRIVATE HAVE_WINDOWS _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(hook_example PRIVATE psapi)
elseif(APPLE)
  target_compile_definitions(hook_example PRIVATE HAVE_DARWIN)
elseif(UNIX)
  target_compile_definitions(hook_example PRIVATE HAVE_LINUX)
  target_link_libraries(hook_example PRIVATE pthread)
endif()
