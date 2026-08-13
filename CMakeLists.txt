cmake_minimum_required(VERSION 3.20)

project(hoox
  VERSION 0.2.3
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
option(HOOX_BUILD_AMALGAMATION "Build the universal single-file library" OFF)
option(HOOX_WINDOWS_PATCH_PC_GUARD
  "On Windows, scan suspended thread instruction pointers against patched byte ranges" OFF)
option(HOOX_POSIX_PATCH_PC_GUARD
  "On Linux, park peer threads in a signal handler while patched bytes are written" OFF)

message(STATUS "hoox ${PROJECT_VERSION}")
message(STATUS "  target arch : ${HOOX_TARGET_ARCH} (family ${HOOX_ARCH_FAMILY})")
message(STATUS "  system      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  compiler    : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  tests       : ${HOOX_ENABLE_TESTS}")
message(STATUS "  amalgamation: ${HOOX_BUILD_AMALGAMATION}")
message(STATUS "  pc guard    : ${HOOX_WINDOWS_PATCH_PC_GUARD} (Windows) / ${HOOX_POSIX_PATCH_PC_GUARD} (Linux)")

add_subdirectory(src)

# The amalgamation is universal: every supported architecture and OS backend
# is present behind the same compiler-macro detection used by hooxdefs.h. Every
# CI target compiles and runs this exact output, while the normal static library
# remains target-selected by src/CMakeLists.txt.
set(HOOX_AMALGAMATION_AVAILABLE FALSE)
if(HOOX_BUILD_AMALGAMATION OR HOOX_ENABLE_TESTS)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    set(HOOX_AMALGAM_DIR "${CMAKE_BINARY_DIR}/amalgam")
    set(HOOX_AMALGAM_C "${HOOX_AMALGAM_DIR}/hoox.c")
    set(HOOX_AMALGAM_H "${HOOX_AMALGAM_DIR}/hoox.h")

    file(GLOB_RECURSE _hoox_amalgam_headers CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/include/*.h"
      "${CMAKE_SOURCE_DIR}/src/*.h")
    set(_hoox_amalgam_include_dirs
      "${CMAKE_SOURCE_DIR}/include"
      "${CMAKE_SOURCE_DIR}/src")
    foreach(h ${_hoox_amalgam_headers})
      get_filename_component(d "${h}" DIRECTORY)
      list(APPEND _hoox_amalgam_include_dirs "${d}")
    endforeach()
    list(REMOVE_DUPLICATES _hoox_amalgam_include_dirs)

    set(_hoox_amalgam_inc_args "")
    foreach(d ${_hoox_amalgam_include_dirs})
      list(APPEND _hoox_amalgam_inc_args --include-dir "${d}")
    endforeach()

    # Preserve the proven target-amalgamation layer order while including every
    # implementation. tools/amalgamate.py supplies the mutually-exclusive
    # architecture/OS guards.
    file(GLOB _hoox_amalgam_compat CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/compat/*.c")
    file(GLOB _hoox_amalgam_disasm CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/disasm/*.c")
    file(GLOB_RECURSE _hoox_amalgam_arch CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/arch/*.c")
    file(GLOB _hoox_amalgam_core CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/core/*.c")
    file(GLOB _hoox_amalgam_backend_posix CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/posix/*.c")
    file(GLOB _hoox_amalgam_backend_windows CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/windows/*.c")
    file(GLOB _hoox_amalgam_backend_linux CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/linux/*.c")
    file(GLOB _hoox_amalgam_backend_android CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/android/*.c")
    file(GLOB _hoox_amalgam_backend_darwin CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/darwin/*.c")
    file(GLOB _hoox_amalgam_backend_freebsd CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/freebsd/*.c")
    file(GLOB _hoox_amalgam_backend_arch CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/src/backend/x86/*.c"
      "${CMAKE_SOURCE_DIR}/src/backend/arm/*.c"
      "${CMAKE_SOURCE_DIR}/src/backend/arm64/*.c")
    set(_hoox_amalgam_sources
      ${_hoox_amalgam_compat}
      ${_hoox_amalgam_disasm}
      ${_hoox_amalgam_arch}
      ${_hoox_amalgam_core}
      ${_hoox_amalgam_backend_posix}
      ${_hoox_amalgam_backend_windows}
      ${_hoox_amalgam_backend_linux}
      ${_hoox_amalgam_backend_android}
      ${_hoox_amalgam_backend_darwin}
      ${_hoox_amalgam_backend_freebsd}
      ${_hoox_amalgam_backend_arch})

    add_custom_command(
      OUTPUT "${HOOX_AMALGAM_C}" "${HOOX_AMALGAM_H}"
      COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/amalgamate.py"
        --out-c "${HOOX_AMALGAM_C}" --out-h "${HOOX_AMALGAM_H}"
        ${_hoox_amalgam_inc_args}
        --universal
        --header "${CMAKE_SOURCE_DIR}/src/core/hoox.h"
        --public-header "${CMAKE_SOURCE_DIR}/include/hoox.h"
        ${_hoox_amalgam_sources}
      DEPENDS ${_hoox_amalgam_sources} ${_hoox_amalgam_headers}
        "${CMAKE_SOURCE_DIR}/tools/amalgamate.py"
      COMMENT "Amalgamating universal hoox.c / hoox.h")

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
