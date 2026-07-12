# hoox tests.

# --- compat unit tests (M1) -------------------------------------------------
add_executable(test_compat compat/test_compat.c)
target_link_libraries(test_compat PRIVATE hoox)
target_include_directories(test_compat PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat")
add_test(NAME compat COMMAND test_compat)

# --- test harness (M1.5) ----------------------------------------------------
add_library(hoox_testharness STATIC harness/testrunner.c harness/testutil.c)
target_include_directories(hoox_testharness PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/harness"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat")
target_link_libraries(hoox_testharness PUBLIC hoox)

add_executable(test_harness_sample harness/test_harness_sample.c)
target_link_libraries(test_harness_sample PRIVATE hoox_testharness)
add_test(NAME harness_sample COMMAND test_harness_sample)

# --- memory primitives smoke test (M3) --------------------------------------
add_executable(test_memory core/test_memory.c)
target_link_libraries(test_memory PRIVATE hoox)
target_include_directories(test_memory PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
add_test(NAME memory COMMAND test_memory)

# --- interceptor smoke test (M5) --------------------------------------------
add_executable(test_interceptor_smoke core/test_interceptor_smoke.c)
target_link_libraries(test_interceptor_smoke PRIVATE hoox)
target_include_directories(test_interceptor_smoke PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
add_test(NAME interceptor_smoke COMMAND test_interceptor_smoke)

# --- interceptor behaviour suite (M6) ---------------------------------------
add_executable(test_interceptor
  core/test_interceptor.c
  core/interceptor-callbacklistener.c
  core/interceptor-functiondatalistener.c
  core/hoox_targetfunctions.c)
target_link_libraries(test_interceptor PRIVATE hoox_testharness)
target_include_directories(test_interceptor PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
add_test(NAME interceptor COMMAND test_interceptor)

# --- multi-threaded interceptor test ----------------------------------------
# Hammers a hooked function concurrently from several threads to exercise the
# per-thread TLS invocation stack and lock-free listener dispatch (the FreeBSD
# libthr regression class). Arch-neutral; runs on every supported platform.
add_executable(test_interceptor_threads core/test_interceptor_threads.c)
target_link_libraries(test_interceptor_threads PRIVATE hoox)
target_include_directories(test_interceptor_threads PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(test_interceptor_threads PRIVATE Threads::Threads)
endif()
add_test(NAME interceptor_threads COMMAND test_interceptor_threads)

# --- interceptor listener-count limit test ----------------------------------
add_executable(test_interceptor_limits core/test_interceptor_limits.c)
target_link_libraries(test_interceptor_limits PRIVATE hoox)
target_include_directories(test_interceptor_limits PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
add_test(NAME interceptor_limits COMMAND test_interceptor_limits)

# --- arm64 decoder golden test (arch-neutral: validates hx_disasm_arm64 on
# every host, including x86 CI, since AArch64 decoding is host-independent) ---
add_executable(test_arm64_decode
  disasm/test_arm64_decode.c
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm/hx_disasm_arm64.c")
target_include_directories(test_arm64_decode PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
if(WIN32)
  target_compile_definitions(test_arm64_decode PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME arm64_decode COMMAND test_arm64_decode)

# --- arm (A32/Thumb) decoder golden test (arch-neutral, runs on every host) --
add_executable(test_arm_decode
  disasm/test_arm_decode.c
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm/hx_disasm_arm.c")
target_include_directories(test_arm_decode PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
if(WIN32)
  target_compile_definitions(test_arm_decode PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME arm_decode COMMAND test_arm_decode)

# --- disasm differential test (M2) — x86-only (diffs hx_disasm vs capstone) -
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  add_executable(hx_disasm_dump disasm/hx_disasm_dump.c)
  target_link_libraries(hx_disasm_dump PRIVATE hoox)
  target_include_directories(hx_disasm_dump PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME disasm_diff
      COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/disasm/diff_capstone.py"
        "$<TARGET_FILE:hx_disasm_dump>")
  endif()
endif()

# --- amalgamation (single-file) build + test (M7) — every arch --------------
# The amalgamator is arch-agnostic, but its only compile-time verification must
# cover the arch actually being built: ARM/ARM64-only sources go through the
# single-TU merge too, so a symbol collision or macro leak there would slip
# past an x86-only check. This is therefore NOT gated on the x86 arch.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(HOOX_AMALGAM_DIR "${CMAKE_BINARY_DIR}/amalgam")
  set(HOOX_AMALGAM_C "${HOOX_AMALGAM_DIR}/hoox.c")
  set(HOOX_AMALGAM_H "${HOOX_AMALGAM_DIR}/hoox.h")

  set(_amalgam_inc_args "")
  foreach(d ${HOOX_ALL_INCLUDE_DIRS})
    list(APPEND _amalgam_inc_args --include-dir "${d}")
  endforeach()

  add_custom_command(
    OUTPUT "${HOOX_AMALGAM_C}" "${HOOX_AMALGAM_H}"
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_SOURCE_DIR}/tools/amalgamate.py"
      --out-c "${HOOX_AMALGAM_C}" --out-h "${HOOX_AMALGAM_H}"
      ${_amalgam_inc_args}
      --header "${CMAKE_SOURCE_DIR}/src/core/hoox.h"
      --public-header "${CMAKE_SOURCE_DIR}/include/hoox.h"
      ${HOOX_ALL_SOURCES}
    DEPENDS ${HOOX_ALL_SOURCES}
      "${CMAKE_SOURCE_DIR}/tools/amalgamate.py"
      "${CMAKE_SOURCE_DIR}/src/core/hoox.h"
      "${CMAKE_SOURCE_DIR}/include/hoox.h"
    COMMENT "Amalgamating hoox into a single .c/.h")

  # A standalone target that only *generates* hoox.c / hoox.h (pure text
  # merge, no compilation) — used by the release workflow to package the
  # amalgamation on any runner that has Python.
  add_custom_target(amalgam DEPENDS "${HOOX_AMALGAM_C}" "${HOOX_AMALGAM_H}")

  # Build the interceptor suite against the single-file library only (the
  # test files still use the normal headers for declarations; hoox.c
  # supplies every definition — proving the amalgamation is complete).
  add_executable(test_amalgam
    "${HOOX_AMALGAM_C}"
    harness/testrunner.c
    harness/testutil.c
    core/test_interceptor.c
    core/interceptor-callbacklistener.c
    core/interceptor-functiondatalistener.c
    core/hoox_targetfunctions.c)
  target_include_directories(test_amalgam PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/harness"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
  # No HOOX_STATIC / HOOX_USE_SYSTEM_ALLOC / HAVE_* here: this exercises the
  # amalgam exactly as a real consumer would — static + system allocator are
  # the defaults, and arch/OS auto-detect from compiler built-ins.
  if(WIN32)
    target_link_libraries(test_amalgam PRIVATE psapi)
    target_compile_definitions(test_amalgam PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    # A real amalgam consumer on POSIX must link the threads library, exactly
    # as it would link the platform's pthread: hoox uses pthread keys for TLS.
    # This is mandatory on FreeBSD, where pthread lives in libthr and the
    # unlinked symbols would otherwise resolve to libc's no-op weak stubs
    # (setspecific/getspecific silently do nothing -> per-call thread context).
    find_package(Threads REQUIRED)
    target_link_libraries(test_amalgam PRIVATE Threads::Threads)
  endif()

  # test_amalgam links the full interceptor suite into one binary and patches
  # its own code (self-hosting). On Apple arm64 (macOS/iOS/tvOS: 16 KiB pages,
  # no RWX) the dense single-TU layout reliably places a hook target on a page
  # shared with live patcher code — a case the in-place W^X path cannot patch
  # (the documented Apple-arm64 self-hosting limitation; reviewer finding #3).
  # The off-page patch path for self-hosting *is* exercised there by the split
  # build's interceptor_smoke. So on Apple arm64 we still BUILD the amalgam (the
  # single-TU compile is the coverage that matters — it catches symbol
  # collisions across the merged arch sources), but don't RUN this suite.
  if(APPLE AND HOOX_ARCH_FAMILY STREQUAL "arm64")
    message(STATUS
      "amalgam: build-only on Apple arm64 (self-hosting W^X limitation; "
      "the off-page path is covered by interceptor_smoke)")
  else()
    add_test(NAME amalgam COMMAND test_amalgam)
  endif()
endif()

# --- arch-x86 writer / relocator tests (M4) — x86-only ----------------------
if(HOOX_ARCH_FAMILY STREQUAL "x86")
  add_executable(test_arch_x86
    core/arch-x86/x86writer.c
    core/arch-x86/x86relocator.c
    core/arch-x86/run_arch_x86.c)
  target_link_libraries(test_arch_x86 PRIVATE hoox_testharness)
  target_include_directories(test_arch_x86 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/arch/x86"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
  target_compile_definitions(test_arch_x86 PRIVATE HAVE_I386)
  add_test(NAME arch_x86 COMMAND test_arch_x86)
endif()
