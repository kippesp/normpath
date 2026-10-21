include_guard(GLOBAL)

function(np_add_project_identity target)
  find_package(Git REQUIRED)
  set(generated_header "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h")
  set(generated_resource "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
  set(generated_manifest "${CMAKE_CURRENT_BINARY_DIR}/generated/np.manifest")
  add_custom_target(np_project_identity ALL
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}/.."
      "-DOUTPUT_FILE=${generated_header}"
      "-DTEMPLATE_FILE=${PROJECT_SOURCE_DIR}/version.h.in"
      "-DRC_OUTPUT_FILE=${generated_resource}"
      "-DRC_TEMPLATE_FILE=${PROJECT_SOURCE_DIR}/version.rc.in"
      "-DMANIFEST_OUTPUT_FILE=${generated_manifest}"
      "-DMANIFEST_TEMPLATE_FILE=${PROJECT_SOURCE_DIR}/np.manifest.in"
      "-DNP_VERSION=${PROJECT_VERSION}"
      "-DNP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
      "-DNP_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
      "-DNP_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
      "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriteProjectIdentity.cmake"
    BYPRODUCTS "${generated_header}" "${generated_resource}" "${generated_manifest}"
    COMMENT "Refreshing np Git identity"
    VERBATIM)
  set_property(TARGET np_project_identity PROPERTY FOLDER support/tools)
  add_dependencies("${target}" np_project_identity)
  target_include_directories("${target}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  # The VERSIONINFO resource is separate from the manifest, so it compiles in
  # every configuration without duplicating the manifest resource ID.
  set_source_files_properties("${generated_resource}" PROPERTIES GENERATED TRUE)
  target_sources("${target}" PRIVATE "${generated_resource}")
endfunction()
