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
