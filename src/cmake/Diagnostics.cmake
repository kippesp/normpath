# Windows policies from program_shorts cpp/cmake/Diagnostics.cmake.
include_guard(GLOBAL)

set(NP_WARNINGS_AS_ERRORS ON)
option(NP_ENABLE_TIDY "Run clang-tidy while compiling owned targets" OFF)

function(_np_diagnostic_options output_variable)
  set(_options)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND _options /W4)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND _options
         -Weverything
         -Wno-c++98-compat
         -Wno-c++98-compat-pedantic
         -Wno-padded
         # An exhaustive enum switch cannot satisfy both -Wswitch-default and
         # -Wcovered-switch-default. -Wswitch-enum still reports an omitted
         # enumerator whether or not a default label exists.
         -Wno-switch-default)
  endif()
  set("${output_variable}" "${_options}" PARENT_SCOPE)
endfunction()

# The executable path stays cached; its version is queried once per configure,
# so a reconfigure after selecting another executable reports the new one.
if(NP_ENABLE_TIDY)
  find_program(NP_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  execute_process(
    COMMAND "${NP_CLANG_TIDY_EXECUTABLE}" --version
    OUTPUT_VARIABLE _np_tidy_version
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  message(STATUS "clang-tidy: ${NP_CLANG_TIDY_EXECUTABLE}: ${_np_tidy_version}")
endif()

function(np_apply_diagnostics target)
  _np_diagnostic_options(_diagnostic_options)
  target_compile_options("${target}" PRIVATE ${_diagnostic_options})

  set_property(TARGET "${target}" PROPERTY
               COMPILE_WARNING_AS_ERROR "${NP_WARNINGS_AS_ERRORS}")

  if(NP_ENABLE_TIDY)
    message(STATUS "clang-tidy enabled for ${target}")
    # clang-tidy reads --header-filter as a regular expression.
    string(REGEX REPLACE "([][.*+?^$(){}|\\\\])" "\\\\\\1" _source_pattern
           "${PROJECT_SOURCE_DIR}")
    set_property(
      TARGET "${target}" PROPERTY CXX_CLANG_TIDY
      "${NP_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy;--header-filter=^${_source_pattern}/")
  endif()
endfunction()
