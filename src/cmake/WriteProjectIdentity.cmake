set(NP_GIT_COMMIT unknown)
set(NP_BUILD_REVISION unknown-dev)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --show-prefix
  RESULT_VARIABLE root_result OUTPUT_VARIABLE root_prefix
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(root_result EQUAL 0 AND root_prefix STREQUAL "")
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE commit_result OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(commit_result EQUAL 0)
    set(NP_GIT_COMMIT "${commit}")
    string(SUBSTRING "${commit}" 0 7 NP_BUILD_REVISION)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" status
        --porcelain=v1 --untracked-files=all --ignore-submodules=none
      RESULT_VARIABLE status_result OUTPUT_VARIABLE worktree_status
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT status_result EQUAL 0)
      message(FATAL_ERROR "Could not determine np Git working tree state")
    endif()
    if(NOT worktree_status STREQUAL "")
      string(APPEND NP_BUILD_REVISION "-dev")
    endif()
  endif()
endif()
cmake_path(GET OUTPUT_FILE PARENT_PATH output_directory)
file(MAKE_DIRECTORY "${output_directory}")
# configure_file preserves the output timestamp when the content is unchanged.
configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)
if(RC_TEMPLATE_FILE)
  configure_file("${RC_TEMPLATE_FILE}" "${RC_OUTPUT_FILE}" @ONLY)
endif()
if(MANIFEST_TEMPLATE_FILE)
  configure_file("${MANIFEST_TEMPLATE_FILE}" "${MANIFEST_OUTPUT_FILE}" @ONLY)
endif()
