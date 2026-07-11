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

# --- disasm (compact per-arch decoder; capstone shim) -----------------------
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/disasm/hx_disasm_x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/hooxx86writer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/hooxx86reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86/hooxx86relocator.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/x86")
elseif(HOOX_ARCH_FAMILY STREQUAL "arm64")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/disasm/hx_disasm_arm64.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/hooxarm64writer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/hooxarm64reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/hooxarm64relocator.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64")
elseif(HOOX_ARCH_FAMILY STREQUAL "arm")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/disasm/hx_disasm_arm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxarmreg.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxarmwriter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxarmreader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxarmrelocator.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxthumbwriter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxthumbreader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/hooxthumbrelocator.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/arm")
endif()

# --- core (extracted hoox primitives) ----------------------------------------
list(APPEND HOOX_SOURCES
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
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/posix/hooxmemory-posix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/posix/hooxtls-posix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/linux/hooxmemory-linux.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/linux/hoox_process-linux.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/posix"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/posix/hooxmemory-posix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/posix/hooxtls-posix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/darwin/hooxmemory-darwin.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/darwin/hoox_process-darwin.c")
  # macOS needs the real code segment (RX + ad-hoc code signing + vm_remap),
  # not the non-Darwin stub, so W^X targets (esp. Apple Silicon) can be patched.
  list(REMOVE_ITEM HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/core/hooxcodesegment.c")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/darwin/hooxcodesegment-darwin.c")
  set(HOOX_PRIVATE_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/posix"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/darwin")
endif()

if(HOOX_ARCH_FAMILY STREQUAL "x86")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/hooxcpucontext-x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/hooxcpu-x86.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/x86/hooxinterceptor-x86.c")
elseif(HOOX_ARCH_FAMILY STREQUAL "arm64")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/arm64/hooxcpucontext-arm64.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/arm64/hooxinterceptor-arm64.c")
elseif(HOOX_ARCH_FAMILY STREQUAL "arm")
  list(APPEND HOOX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/arm/hooxcpucontext-arm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/arm/hooxcpu-arm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/arm/hooxinterceptor-arm.c")
endif()

add_library(hoox STATIC ${HOOX_SOURCES})
target_include_directories(hoox
  PUBLIC ${HOOX_PUBLIC_INCLUDE_DIRS}
  PRIVATE ${HOOX_PRIVATE_INCLUDE_DIRS})

# Static linkage and the system allocator are the defaults (no macro needed);
# HOOX_SHARED/HOOX_USE_DLMALLOC opt into the alternatives. Arch/OS are
# auto-detected from compiler built-ins, but the build system passes them
# explicitly here so an -DHOOX_TARGET_ARCH override stays authoritative.
if(WIN32)
  target_link_libraries(hoox PRIVATE psapi)
  target_compile_definitions(hoox PRIVATE _CRT_SECURE_NO_WARNINGS HAVE_WINDOWS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Threads REQUIRED)
  target_link_libraries(hoox PUBLIC Threads::Threads)
  target_compile_definitions(hoox PRIVATE HAVE_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  find_package(Threads REQUIRED)
  target_link_libraries(hoox PUBLIC Threads::Threads)
  target_compile_definitions(hoox PRIVATE HAVE_DARWIN)
endif()
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  target_compile_definitions(hoox PRIVATE HAVE_I386)
elseif(HOOX_ARCH_FAMILY STREQUAL "arm64")
  target_compile_definitions(hoox PRIVATE HAVE_ARM64)
elseif(HOOX_ARCH_FAMILY STREQUAL "arm")
  target_compile_definitions(hoox PRIVATE HAVE_ARM)
endif()

# Export the source list and private include dirs so the amalgamation test
# (tests/CMakeLists.txt) can regenerate and build the single-file library.
set(HOOX_ALL_SOURCES ${HOOX_SOURCES} PARENT_SCOPE)
set(HOOX_ALL_INCLUDE_DIRS ${HOOX_PRIVATE_INCLUDE_DIRS} PARENT_SCOPE)
