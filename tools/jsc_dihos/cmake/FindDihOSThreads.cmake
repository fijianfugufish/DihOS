# Native SACX runtime, not a pthread availability override. This check must
# actually link a freestanding ARM64 ELF against the supplied runtime archive.
include(FindPackageHandleStandardArgs)
if(NOT EXISTS "${DIHOS_RUNTIME_ARCHIVE}" OR NOT EXISTS "${DIHOS_THREAD_SMOKE}")
  message(FATAL_ERROR "Build the DihOS runtime archive and provide its thread link fixture")
endif()
set(_saved_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
try_compile(DIHOS_THREAD_LINK_OK
  SOURCES "${DIHOS_THREAD_SMOKE}"
  LINK_LIBRARIES "${DIHOS_RUNTIME_ARCHIVE}"
  COPY_FILE "${CMAKE_BINARY_DIR}/dihos-thread-link.elf"
  OUTPUT_VARIABLE DIHOS_THREAD_LINK_OUTPUT)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_saved_try_type}")
file(WRITE "${CMAKE_BINARY_DIR}/dihos-thread-link.log" "${DIHOS_THREAD_LINK_OUTPUT}")
find_package_handle_standard_args(DihOSThreads REQUIRED_VARS DIHOS_THREAD_LINK_OK)
if(DihOSThreads_FOUND AND NOT TARGET Threads::Threads)
  add_library(Threads::Threads STATIC IMPORTED)
  set_target_properties(Threads::Threads PROPERTIES IMPORTED_LOCATION "${DIHOS_RUNTIME_ARCHIVE}")
endif()
