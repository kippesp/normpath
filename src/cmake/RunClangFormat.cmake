# Adapted from program_shorts cpp/cmake/RunClangFormat.cmake.
find_program(_clang_format NAMES clang-format)
if(NOT _clang_format)
  message(FATAL_ERROR "format-check requires clang-format on PATH")
endif()
execute_process(
  COMMAND "${_clang_format}" --version
  OUTPUT_VARIABLE _format_version
  OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "Using ${_format_version}")

file(GLOB _sources "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.cpp"
     "${SOURCE_DIR}/tests/*.h" "${SOURCE_DIR}/tests/*.cpp")
execute_process(
  COMMAND "${_clang_format}" --dry-run --Werror ${_sources}
  RESULT_VARIABLE _format_result)
if(NOT _format_result EQUAL 0)
  message(FATAL_ERROR "clang-format check failed")
endif()
