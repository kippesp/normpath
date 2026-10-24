include_guard(GLOBAL)

# Source selection for pinned Git dependencies acquired through FetchContent.
# Precedence: a prepared FETCHCONTENT_SOURCE_DIR_<NAME> override, then an
# explicit repository, then the local cache, then upstream. A selected local
# repository must already contain the pinned commit; there is no fallback.

# The cache root defaults to the GIT_DEP_REPOS_CACHE environment variable, the
# per-machine convention for local dependency repositories. The literal
# location differs between hosts, so no path is assumed here.
set(_np_default_cache_root "")
if(DEFINED ENV{GIT_DEP_REPOS_CACHE} AND NOT "$ENV{GIT_DEP_REPOS_CACHE}" STREQUAL "")
  cmake_path(CONVERT "$ENV{GIT_DEP_REPOS_CACHE}" TO_CMAKE_PATH_LIST
             _np_env_cache_root NORMALIZE)
  if(IS_DIRECTORY "${_np_env_cache_root}")
    set(_np_default_cache_root "${_np_env_cache_root}")
  else()
    message(WARNING
      "GIT_DEP_REPOS_CACHE is set but is not a directory: "
      "${_np_env_cache_root}. Dependencies will be acquired from upstream.")
  endif()
endif()

set(NP_DEPENDENCY_CACHE_ROOT "${_np_default_cache_root}" CACHE PATH
    "Directory containing local dependency Git repositories")

# Sets <output> to "<commit>", "<commit>, clean" or "<commit>, dirty" for a Git
# checkout, or to "not a Git checkout" when Git or the checkout is unavailable.
function(_np_git_source_identity source_dir include_status output)
  find_package(Git QUIET)
  set(_identity "not a Git checkout")
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
      RESULT_VARIABLE _identity_result
      OUTPUT_VARIABLE _identity_output
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_identity_result EQUAL 0)
      set(_identity "${_identity_output}")
      if(include_status)
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain
          OUTPUT_VARIABLE _status
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_status STREQUAL "")
          string(APPEND _identity ", clean")
        else()
          string(APPEND _identity ", dirty")
        endif()
      endif()
    endif()
  endif()
  set("${output}" "${_identity}" PARENT_SCOPE)
endfunction()

# np_select_dependency_source(
#   NAME <FetchContent name> VERSION <release> COMMIT <full commit>
#   UPSTREAM <url> CACHE_REPOSITORY <directory under the cache root>
#   OUTPUT <variable receiving the GIT_REPOSITORY value>)
function(np_select_dependency_source)
  cmake_parse_arguments(
    PARSE_ARGV 0 _arg ""
    "NAME;VERSION;COMMIT;UPSTREAM;CACHE_REPOSITORY;OUTPUT" "")
  foreach(_required IN ITEMS NAME VERSION COMMIT UPSTREAM CACHE_REPOSITORY OUTPUT)
    if(NOT DEFINED _arg_${_required})
      message(FATAL_ERROR "np_select_dependency_source requires ${_required}")
    endif()
  endforeach()

  string(TOUPPER "${_arg_NAME}" _name_upper)
  set(_override_variable "FETCHCONTENT_SOURCE_DIR_${_name_upper}")
  if(DEFINED ${_override_variable} AND NOT ${_override_variable} STREQUAL "")
    cmake_path(ABSOLUTE_PATH ${_override_variable} NORMALIZE
               OUTPUT_VARIABLE _prepared_source)
    if(NOT IS_DIRECTORY "${_prepared_source}")
      message(FATAL_ERROR
              "${_override_variable} does not name a directory: ${_prepared_source}")
    endif()
    _np_git_source_identity("${_prepared_source}" TRUE _prepared_identity)
    message(STATUS
      "${_arg_NAME} source: prepared override ${_prepared_source} (${_prepared_identity}); "
      "the normal ${_arg_VERSION} pin is bypassed and the source is not modified")
    set("${_arg_OUTPUT}" "${_arg_UPSTREAM}" PARENT_SCOPE)
    return()
  endif()

  find_package(Git REQUIRED)
  set(_repository_variable "NP_${_name_upper}_REPOSITORY")
  set(${_repository_variable} "" CACHE STRING
      "Explicit ${_arg_NAME} Git repository path or URL (takes precedence over the cache)")
  set(_explicit_repository "${${_repository_variable}}")
  set(_cache_repository "${NP_DEPENDENCY_CACHE_ROOT}/${_arg_CACHE_REPOSITORY}")
  if(NOT _explicit_repository STREQUAL "")
    set(_repository "${_explicit_repository}")
    set(_source_kind "explicit repository")
  elseif(NOT NP_DEPENDENCY_CACHE_ROOT STREQUAL "" AND
         IS_DIRECTORY "${_cache_repository}")
    set(_repository "${_cache_repository}")
    set(_source_kind "local cache")
  else()
    set(_repository "${_arg_UPSTREAM}")
    set(_source_kind "upstream")
  endif()

  if(IS_DIRECTORY "${_repository}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${_repository}" cat-file -e
              "${_arg_COMMIT}^{commit}"
      RESULT_VARIABLE _pin_result
      ERROR_VARIABLE _pin_error
      OUTPUT_QUIET)
    if(NOT _pin_result EQUAL 0)
      message(FATAL_ERROR
        "Selected ${_arg_NAME} ${_source_kind} '${_repository}' does not contain "
        "required commit ${_arg_COMMIT}. No upstream fallback was attempted.\n"
        "Git reported: ${_pin_error}")
    endif()
  endif()
  message(STATUS
    "${_arg_NAME} source: ${_source_kind} ${_repository}; "
    "pin v${_arg_VERSION} (${_arg_COMMIT})")
  set("${_arg_OUTPUT}" "${_repository}" PARENT_SCOPE)
endfunction()

# Reports the commit FetchContent populated, or that its identity is unavailable.
function(np_report_dependency_source name source_dir)
  _np_git_source_identity("${source_dir}" FALSE _identity)
  message(STATUS "${name} resolved source: ${source_dir} (${_identity})")
endfunction()
