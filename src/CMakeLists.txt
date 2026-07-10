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
  "${CMAKE_CURRENT_SOURCE_DIR}/compat")

# --- compat (nano-glib) -----------------------------------------------------
list(APPEND HOOX_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxmem.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxthread.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxarray.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxhash.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxlist.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxstring.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/hxstrfuncs.c")

add_library(hoox STATIC ${HOOX_SOURCES})
target_include_directories(hoox
  PUBLIC ${HOOX_PUBLIC_INCLUDE_DIRS}
  PRIVATE ${HOOX_PRIVATE_INCLUDE_DIRS})

if(WIN32)
  target_link_libraries(hoox PRIVATE psapi)
  target_compile_definitions(hoox PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
