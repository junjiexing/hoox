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
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/gumx86writer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/gumx86reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/gumx86relocator.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86")
endif()

# --- core (extracted gum primitives) ----------------------------------------
list(APPEND HOOX_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gumlibc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gumspinlock.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gummetalarray.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gummetalhash.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gummemory.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gumcodesegment.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gumcloak-stub.c")

set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
  "${CMAKE_CURRENT_SOURCE_DIR}/core")

# --- backend (platform + arch) ----------------------------------------------
if(WIN32)
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/gummemory-windows.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/gumtls-windows.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows/hoox_process-windows.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/windows")
endif()

if(HOOX_ARCH_FAMILY STREQUAL "x86")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/gumcpucontext-x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/gumcpu-x86.c")
endif()

add_library(hoox STATIC ${HOOX_SOURCES})
target_include_directories(hoox
  PUBLIC ${HOOX_PUBLIC_INCLUDE_DIRS}
  PRIVATE ${HOOX_PRIVATE_INCLUDE_DIRS})

# Platform / arch feature macros expected by the extracted gum code.
# GUM_STATIC makes GUM_API empty (we build a static archive).
target_compile_definitions(hoox PUBLIC GUM_STATIC)
target_compile_definitions(hoox PRIVATE GUM_USE_SYSTEM_ALLOC)
if(WIN32)
  target_link_libraries(hoox PRIVATE psapi)
  target_compile_definitions(hoox PRIVATE _CRT_SECURE_NO_WARNINGS HAVE_WINDOWS)
endif()
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  target_compile_definitions(hoox PRIVATE HAVE_I386)
endif()
