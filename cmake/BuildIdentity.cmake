include_guard(GLOBAL)

# Shared by the production BuildInfo target and the isolated incremental-build
# regression test. Source identity is refreshed before dependent compilation.
function(asciiomium_add_build_identity target)
  find_package(Git QUIET)
  set(identity_header "${CMAKE_CURRENT_BINARY_DIR}/generated/asciiomium_build_identity.h")
  set(identity_arguments
    "-DASCIIOMIUM_IDENTITY_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DASCIIOMIUM_IDENTITY_OUTPUT=${identity_header}"
    "-DASCIIOMIUM_IDENTITY_GIT=${GIT_EXECUTABLE}"
    "-P" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriteBuildIdentity.cmake")
  execute_process(COMMAND "${CMAKE_COMMAND}" ${identity_arguments}
    RESULT_VARIABLE identity_result)
  if(NOT identity_result EQUAL 0)
    message(FATAL_ERROR "Unable to initialize build source identity")
  endif()
  add_custom_target(asciiomium_refresh_build_identity
    COMMAND "${CMAKE_COMMAND}" ${identity_arguments}
    BYPRODUCTS "${identity_header}"
    VERBATIM)
  set_target_properties(asciiomium_refresh_build_identity PROPERTIES FOLDER "ASCIIomium")
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  add_dependencies(${target} asciiomium_refresh_build_identity)
endfunction()
