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

# --- disasm differential test (M2) ------------------------------------------
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

  # --- amalgamation (single-file) build + test (M7) -------------------------
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
    endif()
    add_test(NAME amalgam COMMAND test_amalgam)
  endif()

  # --- arch-x86 writer / relocator tests (M4) -------------------------------
  add_executable(test_arch_x86
    core/arch-x86/x86writer.c
    core/arch-x86/x86relocator.c
    core/arch-x86/run_arch_x86.c)
  target_link_libraries(test_arch_x86 PRIVATE hoox_testharness)
  target_include_directories(test_arch_x86 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/arch/x86"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/disasm")
  if(HAVE_I386_DEFS)
  endif()
  target_compile_definitions(test_arch_x86 PRIVATE HAVE_I386)
  add_test(NAME arch_x86 COMMAND test_arch_x86)
endif()
