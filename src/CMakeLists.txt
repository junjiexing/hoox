# hoox library.
#
# Sources are accumulated into HOOX_SOURCES as milestones land. The library is
# defined once at the bottom so every layer (compat, disasm, core, arch,
# backend, facade) contributes to a single archive.

set(HOOX_SOURCES "")
set(HOOX_PUBLIC_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/../include")
set(HOOX_PRIVATE_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/disasm")

# --- compat (nano-glib) -----------------------------------------------------
list(APPEND HOOX_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxmem.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxthread.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxarray.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxhash.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxlist.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxstring.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxstrfuncs.c")

# --- disasm (compact x86/x64 decoder; capstone shim) ------------------------
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/disasm/hx_disasm_x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/hooxx86writer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/hooxx86reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/hooxx86relocator.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86")
endif()

# --- core (extracted hoox primitives) ----------------------------------------
list(APPEND HOOX_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxlibc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxspinlock.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxmetalarray.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxmetalhash.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxmemory.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxcodesegment.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxcloak-stub.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxcodeallocator.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxinvocationcontext.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxinvocationlistener.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxinterceptor.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/hoox.c")

set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
  "${CMAKE_CURRENT_SOURCE_DIR}/core")

# --- backend (platform + arch) ----------------------------------------------
if(WIN32)
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/hooxmemory-windows.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/hooxtls-windows.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/hoox_process-windows.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/hoox_msvc_intrinsics.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows")
endif()

if(HOOX_ARCH_FAMILY STREQUAL "x86")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/hooxcpucontext-x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/hooxcpu-x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/hooxinterceptor-x86.c")
endif()

add_library(hoox STATIC ${HOOX_SOURCES})
target_include_directories(hoox
  PUBLIC ${HOOX_PUBLIC_INCLUDE_DIRS}
  PRIVATE ${HOOX_PRIVATE_INCLUDE_DIRS})

# Platform / arch feature macros expected by the extracted hoox code.
# HOOX_STATIC makes HOOX_API empty (we build a static archive).
target_compile_definitions(hoox PUBLIC HOOX_STATIC)
target_compile_definitions(hoox PRIVATE HOOX_USE_SYSTEM_ALLOC)
if(WIN32)
  target_link_libraries(hoox PRIVATE psapi)
  target_compile_definitions(hoox PRIVATE _CRT_SECURE_NO_WARNINGS HAVE_WINDOWS)
endif()
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  target_compile_definitions(hoox PRIVATE HAVE_I386)
endif()

# Export the source list and private include dirs so the amalgamation test
# (tests/CMakeLists.txt) can regenerate and build the single-file library.
set(HOOX_ALL_SOURCES ${HOOX_SOURCES} PARENT_SCOPE)
set(HOOX_ALL_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS} PARENT_SCOPE)
