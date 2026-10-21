# Adapted from program_shorts cpp/cmake/Sanitizers.cmake.
include_guard(GLOBAL)

include(CheckCXXSourceRuns)

set(NP_ENABLE_ASAN "OFF" CACHE STRING
    "AddressSanitizer mode: AUTO, ON, or OFF")
set_property(CACHE NP_ENABLE_ASAN PROPERTY STRINGS AUTO ON OFF)
set(NP_ENABLE_UBSAN "OFF" CACHE STRING
    "UndefinedBehaviorSanitizer mode: AUTO, ON, or OFF")
set_property(CACHE NP_ENABLE_UBSAN PROPERTY STRINGS AUTO ON OFF)

function(_np_validate_sanitizer_setting variable)
  string(TOUPPER "${${variable}}" _mode)
  if(NOT _mode MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "${variable} must be AUTO, ON, or OFF; got '${${variable}}'")
  endif()
  set("${variable}" "${_mode}" CACHE STRING "" FORCE)
endfunction()

function(_np_check_sanitizer name compile_option link_option output)
  # Match single-config targets' CRT and STL settings in the compatibility probe.
  if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  endif()
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND
     "${name}" STREQUAL "ASAN")
    # Match the instrumented targets so /RTC and Edit-and-Continue do not
    # make a supported ASan runtime look unavailable during try_run.
    set(CMAKE_MSVC_RUNTIME_CHECKS "")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
  endif()
  set(_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${compile_option}")
  set(CMAKE_REQUIRED_LINK_OPTIONS "${link_option}")
  check_cxx_source_runs(
    # Exercise the C++ runtime too: a C-only probe missed an MSVC Debug STL /
    # Clang UBSan runtime _ITERATOR_DEBUG_LEVEL mismatch on the actual target.
    "#include <iostream>\n#include <vector>\nint main() { std::vector<int> values{0}; std::cout << values.at(0); return values.at(0); }"
    "NP_${name}_RUNTIME_SUPPORTED")
  set(CMAKE_REQUIRED_FLAGS "${_saved_required_flags}")
  set(CMAKE_REQUIRED_LINK_OPTIONS "${_saved_required_link_options}")
  set("${output}" "${NP_${name}_RUNTIME_SUPPORTED}" PARENT_SCOPE)
endfunction()

function(_np_report_sanitizer name mode probed supported)
  if("${mode}" STREQUAL "OFF")
    set(_result "not probed")
  elseif(NOT probed)
    set(_result "not probed (unsupported compiler frontend)")
  elseif(supported)
    set(_result "probed: supported")
  else()
    set(_result
      "probed: unsupported (compile/link/run check failed; see CMakeFiles/CMakeConfigureLog.yaml)")
  endif()
  message(STATUS "np ${name}: mode=${mode}, ${_result}")
endfunction()

function(np_configure_sanitizers)
  _np_validate_sanitizer_setting(NP_ENABLE_ASAN)
  _np_validate_sanitizer_setting(NP_ENABLE_UBSAN)

  set(_asan_supported FALSE)
  set(_ubsan_supported FALSE)
  set(_asan_probed FALSE)
  set(_ubsan_probed FALSE)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if(NOT NP_ENABLE_ASAN STREQUAL "OFF")
      set(_asan_probed TRUE)
      _np_check_sanitizer(
        ASAN "/fsanitize=address /Zi"
        "/INCREMENTAL:NO" _asan_supported)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if(NOT NP_ENABLE_ASAN STREQUAL "OFF")
      set(_asan_probed TRUE)
      _np_check_sanitizer(ASAN "-fsanitize=address" "-fsanitize=address"
                            _asan_supported)
    endif()
    if(NOT NP_ENABLE_UBSAN STREQUAL "OFF")
      set(_ubsan_probed TRUE)
      _np_check_sanitizer(
        UBSAN "-fsanitize=undefined -fno-sanitize-recover=undefined"
        "-fsanitize=undefined;-fno-sanitize-recover=undefined"
        _ubsan_supported)
    endif()
  endif()

  _np_report_sanitizer(ASan "${NP_ENABLE_ASAN}" "${_asan_probed}" "${_asan_supported}")
  _np_report_sanitizer(UBSan "${NP_ENABLE_UBSAN}" "${_ubsan_probed}" "${_ubsan_supported}")

  if(NP_ENABLE_ASAN STREQUAL "ON" AND NOT _asan_supported)
    message(FATAL_ERROR
            "NP_ENABLE_ASAN=ON, but an instrumented compile, link, and run failed")
  endif()
  if(NP_ENABLE_UBSAN STREQUAL "ON" AND NOT _ubsan_supported)
    message(FATAL_ERROR
            "NP_ENABLE_UBSAN=ON, but an instrumented compile, link, and run failed")
  endif()

  if(NP_ENABLE_ASAN STREQUAL "OFF" OR NOT _asan_supported)
    set(_asan_condition "0")
  elseif(NP_ENABLE_ASAN STREQUAL "ON")
    set(_asan_condition "1")
  else()
    set(_asan_condition "$<CONFIG:Debug>")
  endif()
  if(NP_ENABLE_UBSAN STREQUAL "OFF" OR NOT _ubsan_supported)
    set(_ubsan_condition "0")
  elseif(NP_ENABLE_UBSAN STREQUAL "ON")
    set(_ubsan_condition "1")
  else()
    set(_ubsan_condition "$<CONFIG:Debug>")
  endif()

  set(NP_ASAN_CONDITION "${_asan_condition}" PARENT_SCOPE)
  set(NP_UBSAN_CONDITION "${_ubsan_condition}" PARENT_SCOPE)
endfunction()

function(np_apply_sanitizers target)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(
      "${target}" PRIVATE
      "$<${NP_ASAN_CONDITION}:/fsanitize=address>")
    target_link_options(
      "${target}" PRIVATE
      "$<${NP_ASAN_CONDITION}:/INCREMENTAL:NO>")
    set_property(
      TARGET "${target}" PROPERTY MSVC_RUNTIME_CHECKS
      "$<$<NOT:${NP_ASAN_CONDITION}>:$<$<CONFIG:Debug>:StackFrameErrorCheck;UninitializedVariable>>")
    set_property(
      TARGET "${target}" PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Workaround for a Clang 23.1.1 (x86_64-pc-windows-msvc, MSVC 14.51
    # vcruntime) AddressSanitizer defect: with the default use-after-return
    # mode, a throw while a catch handler funclet is active fast-fails in
    # __CxxFrameHandler3 (status 0xC0000409), and once
    # detect_stack_use_after_return is on it reports a false
    # stack-use-after-return instead. A standalone nested try/catch reproduces
    # it without np. The "never" mode omits the fake-stack code path, which
    # hides the defect rather than fixing it; it cannot be removed until the
    # toolchain is fixed. It costs nothing today: the ASan runtime defaults to
    # detect_stack_use_after_return=0 on Windows and no preset enables it.
    # Known triggers in this tree: main reports a stream failure from inside a
    # catch handler (cli test test_both_output_streams_failed), Catch2 rethrows
    # inside one when a test fails by throwing
    # (catch_exception_translator_registry.cpp, tryTranslators), and
    # instrumented STL templates may rethrow on error paths. Upstream state on
    # 2026-10-23: llvm/llvm-project#159618 (in LLVM 22 and later) and
    # google/sanitizers#749 cover catch parameters only; llvm/llvm-project#223073
    # is open; drsnuggles8/OloEngineBase#1193 reports this crash and flag with
    # clang 23.1.0. No upstream report covers the throw-inside-handler case.
    # The option is Clang-only.
    target_compile_options(
      "${target}" PRIVATE
      "$<${NP_ASAN_CONDITION}:-fsanitize=address>"
      "$<$<AND:${NP_ASAN_CONDITION},$<CXX_COMPILER_ID:Clang>>:-fsanitize-address-use-after-return=never>"
      "$<${NP_UBSAN_CONDITION}:-fsanitize=undefined>"
      "$<${NP_UBSAN_CONDITION}:-fno-sanitize-recover=undefined>"
      "$<$<OR:${NP_ASAN_CONDITION},${NP_UBSAN_CONDITION}>:-fno-omit-frame-pointer>")
    target_link_options(
      "${target}" PRIVATE
      "$<${NP_ASAN_CONDITION}:-fsanitize=address>"
      "$<${NP_UBSAN_CONDITION}:-fsanitize=undefined>"
      "$<${NP_UBSAN_CONDITION}:-fno-sanitize-recover=undefined>")
  endif()
endfunction()
