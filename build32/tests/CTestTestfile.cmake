# CMake generated Testfile for 
# Source directory: E:/projects/hoox/tests
# Build directory: E:/projects/hoox/build32/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[compat]=] "E:/projects/hoox/build32/tests/test_compat.exe")
set_tests_properties([=[compat]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;8;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[harness_sample]=] "E:/projects/hoox/build32/tests/test_harness_sample.exe")
set_tests_properties([=[harness_sample]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;19;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[memory]=] "E:/projects/hoox/build32/tests/test_memory.exe")
set_tests_properties([=[memory]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;28;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[interceptor_smoke]=] "E:/projects/hoox/build32/tests/test_interceptor_smoke.exe")
set_tests_properties([=[interceptor_smoke]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;37;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[interceptor]=] "E:/projects/hoox/build32/tests/test_interceptor.exe")
set_tests_properties([=[interceptor]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;49;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[disasm_diff]=] "C:/Users/junjie.xing/AppData/Local/miniconda3/python.exe" "E:/projects/hoox/tests/disasm/diff_capstone.py" "E:/projects/hoox/build32/tests/hx_disasm_dump.exe")
set_tests_properties([=[disasm_diff]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;60;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[amalgam]=] "E:/projects/hoox/build32/tests/test_amalgam.exe")
set_tests_properties([=[amalgam]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;113;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
add_test([=[arch_x86]=] "E:/projects/hoox/build32/tests/test_arch_x86.exe")
set_tests_properties([=[arch_x86]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/projects/hoox/tests/CMakeLists.txt;129;add_test;E:/projects/hoox/tests/CMakeLists.txt;0;")
