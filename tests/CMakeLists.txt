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

# --- self-hosting W^X collision regression (Apple arm64) --------------------
add_executable(test_interceptor_selfhost core/test_interceptor_selfhost.c)
target_link_libraries(test_interceptor_selfhost PRIVATE hoox)
target_include_directories(test_interceptor_selfhost PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
add_test(NAME interceptor_selfhost COMMAND test_interceptor_selfhost)

# --- public CPU register context + arbitrary-address probe ------------------
# Includes ONLY the public include/ header (not src/core), so it validates the
# public cpu-context layout a downstream amalgam consumer actually sees.
add_executable(test_cpu_context core/test_cpu_context.c)
target_link_libraries(test_cpu_context PRIVATE hoox)
target_include_directories(test_cpu_context PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include")
add_test(NAME cpu_context COMMAND test_cpu_context)

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

# --- x86 decoder address-size golden test (arch-neutral) --------------------
add_executable(test_x86_decode
  disasm/test_x86_decode.c
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm/hx_disasm_x86.c")
target_include_directories(test_x86_decode PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
if(WIN32)
  target_compile_definitions(test_x86_decode PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME x86_decode COMMAND test_x86_decode)

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
# Generation is defined at the top level so HOOX_BUILD_AMALGAMATION also works
# with tests disabled. Here we compile and run the configured target's output.
if(HOOX_AMALGAMATION_AVAILABLE)
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
  add_dependencies(test_amalgam amalgam)
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
  # its own code (self-hosting): the dense single-TU layout reliably places hook
  # targets on pages shared with live patcher code. On Apple arm64 (16 KiB
  # pages, no RWX) this used to self-fault, so the suite was build-only there.
  # hoox now patches Apple arm64 unconditionally through the off-page stub
  # (backend/darwin/hooxpatch-darwin.c), which is correct regardless of layout,
  # so the amalgam self-hosting suite runs everywhere.
  add_test(NAME amalgam COMMAND test_amalgam)

  # Build the amalgamation example (hook_example) in-tree on every job so each
  # artifact carries a compiled example alongside the tests. It consumes the
  # generated single-file library exactly as a downstream user would — the
  # amalgam's public hoox.h from HOOX_AMALGAM_DIR. Built (not run) as part of
  # `all`.
  add_executable(hook_example
    "${CMAKE_CURRENT_SOURCE_DIR}/../example/hook_example.c"
    "${HOOX_AMALGAM_C}")
  add_dependencies(hook_example amalgam)
  target_include_directories(hook_example PRIVATE "${HOOX_AMALGAM_DIR}")
  if(WIN32)
    target_link_libraries(hook_example PRIVATE psapi)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(hook_example PRIVATE Threads::Threads)
  endif()
  if(MSVC)
    # The example is a demo, not written to satisfy the tree's strict
    # /W3 /sdl /WX; don't fail its build on warnings, and silence the CRT
    # "deprecated" (C4996) notices for printf-family calls.
    target_compile_options(hook_example PRIVATE /WX- /wd4996)
    target_compile_definitions(hook_example PRIVATE _CRT_SECURE_NO_WARNINGS)
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
