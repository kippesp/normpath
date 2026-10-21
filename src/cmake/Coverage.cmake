# Native LLVM coverage, deliberately limited to a separate Windows Clang Debug tree.
include_guard(GLOBAL)

option(NP_ENABLE_COVERAGE "Collect native LLVM coverage in Clang Debug" OFF)
if(NP_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
     NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU" OR
     CMAKE_CONFIGURATION_TYPES OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "Coverage requires GNU-style Clang and single-config Debug")
  endif()
  get_filename_component(_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(NP_LLVM_PROFDATA NAMES llvm-profdata HINTS "${_compiler_directory}"
    REQUIRED)
  find_program(NP_LLVM_COV NAMES llvm-cov HINTS "${_compiler_directory}" REQUIRED)
  set(NP_COVERAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/coverage/Debug")
  file(MAKE_DIRECTORY "${NP_COVERAGE_DIRECTORY}/raw")
endif()

function(np_apply_coverage target)
  if(NP_ENABLE_COVERAGE)
    # Compile-time default for direct runs; the collector sets LLVM_PROFILE_FILE.
    target_compile_options("${target}" PRIVATE
      "-fprofile-instr-generate=${NP_COVERAGE_DIRECTORY}/raw/%m-%p.profraw"
      -fcoverage-mapping)
    target_link_options("${target}" PRIVATE -fprofile-instr-generate)
  endif()
endfunction()
