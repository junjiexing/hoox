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

# Collect every built executable (tests + the example) into <build>/bin so a
# consumer — or CI packaging — can grab just the runnable binaries and their
# debug symbols (.pdb beside the .exe on MSVC; DWARF embedded elsewhere) without
# the object files and CMake bookkeeping. The static library is unaffected
# (it is an ARCHIVE target, not RUNTIME).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# MSVC: /GF pools identical string literals, /sdl enables the extra
# security-development-lifecycle checks, and /WX treats all warnings as errors.
if(MSVC)
  add_compile_options(/W3 /GF /sdl /WX)
endif()

# ---------------------------------------------------------------------------
# Target architecture detection (overridable with -DHOOX_TARGET_ARCH=...)
# ---------------------------------------------------------------------------
set(HOOX_TARGET_ARCH "" CACHE STRING "Target ISA: x86, x86_64, arm, arm64")

if(HOOX_TARGET_ARCH STREQUAL "")
  # Derive from the compiler's pointer size + processor.
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _hoox_proc)
  if(_hoox_proc MATCHES "aarch64|arm64")
    set(HOOX_TARGET_ARCH "arm64")
  elseif(_hoox_proc MATCHES "arm")
    set(HOOX_TARGET_ARCH "arm")
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
message(STATUS "  amalgamation: ${HOOX_BUILD_AMALGAMATION}")

add_subdirectory(src)

# The amalgamation contains the sources selected for this configured target;
# it is therefore platform/architecture-specific, just like libhoox itself.
# Tests consume it even when the standalone build option is off.
set(HOOX_AMALGAMATION_AVAILABLE FALSE)
if(HOOX_BUILD_AMALGAMATION OR HOOX_ENABLE_TESTS)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    set(HOOX_AMALGAM_DIR "${CMAKE_BINARY_DIR}/amalgam")
    set(HOOX_AMALGAM_C "${HOOX_AMALGAM_DIR}/hoox.c")
    set(HOOX_AMALGAM_H "${HOOX_AMALGAM_DIR}/hoox.h")

    set(_hoox_amalgam_inc_args "")
    foreach(d ${HOOX_ALL_INCLUDE_DIRS})
      list(APPEND _hoox_amalgam_inc_args --include-dir "${d}")
    endforeach()

    file(GLOB_RECURSE _hoox_amalgam_headers CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/include/*.h"
      "${CMAKE_SOURCE_DIR}/src/*.h")
    add_custom_command(
      OUTPUT "${HOOX_AMALGAM_C}" "${HOOX_AMALGAM_H}"
      COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/amalgamate.py"
        --out-c "${HOOX_AMALGAM_C}" --out-h "${HOOX_AMALGAM_H}"
        ${_hoox_amalgam_inc_args}
        --header "${CMAKE_SOURCE_DIR}/src/core/hoox.h"
        --public-header "${CMAKE_SOURCE_DIR}/include/hoox.h"
        ${HOOX_ALL_SOURCES}
      DEPENDS ${HOOX_ALL_SOURCES} ${_hoox_amalgam_headers}
        "${CMAKE_SOURCE_DIR}/tools/amalgamate.py"
      COMMENT "Amalgamating hoox for ${CMAKE_SYSTEM_NAME}/${HOOX_TARGET_ARCH}")

    if(HOOX_BUILD_AMALGAMATION)
      add_custom_target(amalgam ALL
        DEPENDS "${HOOX_AMALGAM_C}" "${HOOX_AMALGAM_H}")
    else()
      add_custom_target(amalgam
        DEPENDS "${HOOX_AMALGAM_C}" "${HOOX_AMALGAM_H}")
    endif()
    set(HOOX_AMALGAMATION_AVAILABLE TRUE)
  elseif(HOOX_BUILD_AMALGAMATION)
    message(FATAL_ERROR
      "HOOX_BUILD_AMALGAMATION requires a Python 3 interpreter")
  endif()
endif()

if(HOOX_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
