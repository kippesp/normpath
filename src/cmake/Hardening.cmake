include_guard(GLOBAL)
include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(np_security_compile_options /GS /sdl /guard:cf)
else()
  # Clang rejects -mguard=cf for the MSVC ABI; use its frontend CFG option.
  set(np_security_compile_options -fstack-protector-strong -Xclang=-cfguard)
endif()
set(np_security_link_options LINKER:/guard:cf LINKER:/DYNAMICBASE LINKER:/NXCOMPAT)
# These protections are architecture-specific; presets select x64.
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
  list(APPEND np_security_link_options LINKER:/HIGHENTROPYVA LINKER:/CETCOMPAT)
endif()

foreach(flag IN LISTS np_security_compile_options)
  string(MAKE_C_IDENTIFIER "NP_COMPILER_SUPPORTS_${flag}" result)
  check_cxx_compiler_flag("${flag}" "${result}")
  if(NOT "${${result}}")
    message(FATAL_ERROR "Required hardening compiler option unavailable: ${flag}")
  endif()
endforeach()
foreach(flag IN LISTS np_security_link_options)
  string(MAKE_C_IDENTIFIER "NP_LINKER_SUPPORTS_${flag}" result)
  check_linker_flag(CXX "${flag}" "${result}")
  if(NOT "${${result}}")
    message(FATAL_ERROR "Required hardening linker option unavailable: ${flag}")
  endif()
endforeach()

function(np_apply_hardening target)
  foreach(flag IN LISTS np_security_compile_options)
    target_compile_options("${target}" PRIVATE
      "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:${flag}>")
  endforeach()
  foreach(flag IN LISTS np_security_link_options)
    target_link_options("${target}" PRIVATE
      "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:${flag}>")
  endforeach()
  target_compile_definitions("${target}" PRIVATE
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:_MSVC_STL_HARDENING=1>")
endfunction()
