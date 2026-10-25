cmake_minimum_required(VERSION 4.2.5)

# Set before project() and dependencies so all targets share the CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(np VERSION 1.0.1 LANGUAGES CXX)
include(cmake/ToolchainReport.cmake)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER support/generated)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(MSVC|Clang)$")
  message(FATAL_ERROR "np requires Windows with MSVC or GNU-style Clang")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  message(FATAL_ERROR "Use clang++ with the GNU-style frontend")
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Diagnostics)
include(Hardening)
include(Sanitizers)
include(Coverage)
include(ProjectIdentity)
np_configure_sanitizers()

function(np_apply_checks target)
  target_compile_features("${target}" PRIVATE cxx_std_23)
  np_apply_diagnostics("${target}")
  np_apply_hardening("${target}")
  np_apply_sanitizers("${target}")
  np_apply_coverage("${target}")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options("${target}" PRIVATE /utf-8)
  endif()
endfunction()

# link.exe and lld-link merge manifest sources; a resource here would duplicate ID 1.
function(np_embed_manifest target)
  add_dependencies("${target}" np_project_identity)
  if(CMAKE_CXX_COMPILER_LINKER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_sources("${target}" PRIVATE "${PROJECT_BINARY_DIR}/generated/np.manifest")
  else()
    target_sources("${target}" PRIVATE "${PROJECT_SOURCE_DIR}/np.rc")
    target_include_directories("${target}" PRIVATE "${PROJECT_BINARY_DIR}/generated")
  endif()
endfunction()
# Every configuration compiles the generated VERSIONINFO resource.
enable_language(RC)

add_executable(np_exe main.cpp path_conversion.h path_conversion.cpp
  standard_streams.h standard_streams.cpp)
set_target_properties(np_exe PROPERTIES OUTPUT_NAME np)
np_apply_checks(np_exe)
np_add_project_identity(np_exe)
np_embed_manifest(np_exe)

option(BUILD_TESTING "Build and register tests" ON)
if(BUILD_TESTING)
  enable_testing()
  include(FetchContent)
  include(DependencySource)
  set(_catch2_commit 08092139210881f01a34e60c5f2fd6afa0ea4024)
  np_select_dependency_source(
    NAME Catch2 VERSION 3.16.1 COMMIT "${_catch2_commit}"
    UPSTREAM https://github.com/catchorg/Catch2.git
    CACHE_REPOSITORY catch2.git OUTPUT _catch2_repository)
  set(CMAKE_FOLDER support/generated)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY "${_catch2_repository}"
    GIT_TAG "${_catch2_commit}"
    GIT_SHALLOW FALSE
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(Catch2)
  np_report_dependency_source(Catch2 "${catch2_SOURCE_DIR}")
  foreach(dependency IN ITEMS Catch2 Catch2WithMain)
    set_property(TARGET "${dependency}" PROPERTY FOLDER support/dependencies)
    np_apply_sanitizers("${dependency}")
  endforeach()

  set(CMAKE_FOLDER support/tests)
  add_executable(np_build_tests tests/build_contract_tests.cpp)
  target_link_libraries(np_build_tests PRIVATE Catch2::Catch2WithMain)
  np_apply_checks(np_build_tests)
  np_embed_manifest(np_build_tests)
  add_test(NAME build_contract COMMAND np_build_tests)

  add_executable(np_path_tests tests/path_conversion_tests.cpp
    path_conversion.h path_conversion.cpp)
  target_include_directories(np_path_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(np_path_tests PRIVATE Catch2::Catch2WithMain)
  np_apply_checks(np_path_tests)
  np_embed_manifest(np_path_tests)
  add_test(NAME path_conversion COMMAND np_path_tests)

  add_executable(np_stream_tests tests/standard_streams_tests.cpp
    standard_streams.h standard_streams.cpp)
  target_include_directories(np_stream_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(np_stream_tests PRIVATE Catch2::Catch2WithMain)
  np_apply_checks(np_stream_tests)
  np_embed_manifest(np_stream_tests)
  add_test(NAME standard_streams COMMAND np_stream_tests)

  add_executable(np_stream_console_driver tests/standard_streams_console_driver.cpp
    standard_streams.h standard_streams.cpp)
  target_include_directories(np_stream_console_driver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  np_apply_checks(np_stream_console_driver)
  np_embed_manifest(np_stream_console_driver)

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.py"
    --product "$<TARGET_FILE:np_exe>"
    --version-string "${PROJECT_VERSION}"
    --address-sanitizer "$<IF:${NP_ASAN_CONDITION},on,off>")
  set_tests_properties(cli PROPERTIES TIMEOUT 60)
  add_test(NAME standard_streams_console COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/console_integration.py"
    --driver "$<TARGET_FILE:np_stream_console_driver>")
  set_tests_properties(standard_streams_console PROPERTIES TIMEOUT 30)
  add_test(NAME cli_console COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/console_integration.py"
    --driver "$<TARGET_FILE:np_stream_console_driver>"
    --product "$<TARGET_FILE:np_exe>")
  set_tests_properties(cli_console PROPERTIES TIMEOUT 30)
  find_program(NP_MT_EXECUTABLE NAMES mt.exe
    HINTS
      "$ENV{CMAKE_WINDOWS_KITS_10_DIR}"
      "[HKEY_LOCAL_MACHINE/SOFTWARE/Microsoft/Windows Kits/Installed Roots;KitsRoot10]"
    PATH_SUFFIXES
      "bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/${CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE}"
    REQUIRED)
  add_test(NAME executable_manifests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_contract.py" manifests
    --mt "${NP_MT_EXECUTABLE}" --product "$<TARGET_FILE:np_exe>"
    --test "$<TARGET_FILE:np_build_tests>" --test "$<TARGET_FILE:np_path_tests>"
    --test "$<TARGET_FILE:np_stream_tests>"
    --test "$<TARGET_FILE:np_stream_console_driver>")
  add_test(NAME git_identity_refresh COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_contract.py" identity
    --source "${CMAKE_CURRENT_SOURCE_DIR}" --cmake "${CMAKE_COMMAND}"
    --git "${GIT_EXECUTABLE}" --generator "${CMAKE_GENERATOR}"
    "--toolset=${CMAKE_GENERATOR_TOOLSET}"
    --compiler "${CMAKE_CXX_COMPILER}" --config "$<CONFIG>"
    --work "${CMAKE_CURRENT_BINARY_DIR}/identity-fixture-$<CONFIG>")
  set_tests_properties(git_identity_refresh PROPERTIES TIMEOUT 180)
  if(NP_ENABLE_COVERAGE)
    add_custom_target(coverage
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CollectCoverage.py"
        --ctest "${CMAKE_CTEST_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}"
        --profdata "${NP_LLVM_PROFDATA}" --cov "${NP_LLVM_COV}"
        --output "${NP_COVERAGE_DIRECTORY}"
        --object "$<TARGET_FILE:np_exe>" --object "$<TARGET_FILE:np_build_tests>"
        --object "$<TARGET_FILE:np_path_tests>"
        --object "$<TARGET_FILE:np_stream_tests>"
        --object "$<TARGET_FILE:np_stream_console_driver>"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/path_conversion.cpp"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/standard_streams.cpp"
      DEPENDS np_exe np_build_tests np_path_tests np_stream_tests
        np_stream_console_driver VERBATIM)
    set_property(TARGET coverage PROPERTY FOLDER support/tools)
  endif()
  unset(CMAKE_FOLDER)
endif()

add_custom_target(format-check
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
  VERBATIM)
set_property(TARGET format-check PROPERTY FOLDER support/tools)

message(STATUS "np: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}; C++23")
set(CMAKE_FOLDER support/generated)

install(TARGETS np_exe RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES "${PROJECT_SOURCE_DIR}/../README.md" "${PROJECT_SOURCE_DIR}/../LICENSE"
  DESTINATION . COMPONENT Runtime)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "np-v${PROJECT_VERSION}-windows-x64")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
# Package only the product and its documentation, including when tests are built.
set(CPACK_INSTALL_CMAKE_PROJECTS "${PROJECT_BINARY_DIR};${PROJECT_NAME};Runtime;/")
include(CPack)
