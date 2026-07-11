cmake_minimum_required(VERSION 3.20)

project(hoox
  VERSION 0.1.0
  DESCRIPTION "Minimal inline-hooking library (extracted from frida-gum)"
  LANGUAGES C)

# ---------------------------------------------------------------------------
# Standard
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# The engine uses a few GNU extensions (statement expressions / __typeof__ in
# the atomics, __builtin_* on clang/gcc). Keep extensions on so gcc/clang use
# gnu99; MSVC compiles the same sources via its own intrinsic fallbacks.
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: pool identical string literals so equal literals share one address,
# matching the default behaviour of gcc/clang (a couple of tests compare a
# stored user-data pointer against a string literal for identity).
if(MSVC)
  add_compile_options(/GF)
endif()

# ---------------------------------------------------------------------------
# Target architecture detection (overridable with -DHOOX_TARGET_ARCH=...)
# ---------------------------------------------------------------------------
set(HOOX_TARGET_ARCH "" CACHE STRING "Target ISA: x86, x86_64, arm, arm64, mips")

if(HOOX_TARGET_ARCH STREQUAL "")
  # Derive from the compiler's pointer size + processor.
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _hoox_proc)
  if(_hoox_proc MATCHES "aarch64|arm64")
    set(HOOX_TARGET_ARCH "arm64")
  elseif(_hoox_proc MATCHES "arm")
    set(HOOX_TARGET_ARCH "arm")
  elseif(_hoox_proc MATCHES "mips")
    set(HOOX_TARGET_ARCH "mips")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(HOOX_TARGET_ARCH "x86_64")
  else()
    set(HOOX_TARGET_ARCH "x86")
  endif()
endif()

if(HOOX_TARGET_ARCH MATCHES "^(x86|x86_64)$")
  set(HOOX_ARCH_FAMILY "x86")
elseif(HOOX_TARGET_ARCH MATCHES "^(arm|arm64)$")
  set(HOOX_ARCH_FAMILY "${HOOX_TARGET_ARCH}")
elseif(HOOX_TARGET_ARCH STREQUAL "mips")
  set(HOOX_ARCH_FAMILY "mips")
else()
  message(FATAL_ERROR "Unsupported HOOX_TARGET_ARCH: ${HOOX_TARGET_ARCH}")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(HOOX_ENABLE_TESTS "Build tests" ON)
option(HOOX_BUILD_AMALGAMATION "Build the amalgamated single-file library" OFF)

message(STATUS "hoox ${PROJECT_VERSION}")
message(STATUS "  target arch : ${HOOX_TARGET_ARCH} (family ${HOOX_ARCH_FAMILY})")
message(STATUS "  system      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  compiler    : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  tests       : ${HOOX_ENABLE_TESTS}")

add_subdirectory(src)

if(HOOX_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
