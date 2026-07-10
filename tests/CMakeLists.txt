# hoox tests.

# --- compat unit tests (M1) -------------------------------------------------
add_executable(test_compat compat/test_compat.c)
target_link_libraries(test_compat PRIVATE hoox)
target_include_directories(test_compat PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/compat")
add_test(NAME compat COMMAND test_compat)

# --- test harness (M1.5) ----------------------------------------------------
add_library(hoox_testharness STATIC harness/testrunner.c)
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
endif()
