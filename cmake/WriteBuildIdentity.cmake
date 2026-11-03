# Invoked at configure and before every BuildInfo build. Rewriting only when
# content differs lets the compiler's normal header dependency tracking rebuild
# source identity after edits/commits without forcing all code to recompile.
if(NOT DEFINED ASCIIOMIUM_IDENTITY_SOURCE_DIR OR NOT DEFINED ASCIIOMIUM_IDENTITY_OUTPUT)
  message(FATAL_ERROR "Build identity requires source directory and output path")
endif()
set(identity_revision "unavailable")
set(identity_tree "unavailable")
set(identity_available 0)
set(identity_dirty 0)
if(DEFINED ASCIIOMIUM_IDENTITY_GIT AND EXISTS "${ASCIIOMIUM_IDENTITY_GIT}")
  execute_process(COMMAND "${ASCIIOMIUM_IDENTITY_GIT}" rev-parse --verify HEAD
    WORKING_DIRECTORY "${ASCIIOMIUM_IDENTITY_SOURCE_DIR}"
    RESULT_VARIABLE revision_result OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND "${ASCIIOMIUM_IDENTITY_GIT}" rev-parse "HEAD^{tree}"
    WORKING_DIRECTORY "${ASCIIOMIUM_IDENTITY_SOURCE_DIR}"
    RESULT_VARIABLE tree_result OUTPUT_VARIABLE source_tree OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND "${ASCIIOMIUM_IDENTITY_GIT}" status --porcelain
    WORKING_DIRECTORY "${ASCIIOMIUM_IDENTITY_SOURCE_DIR}"
    RESULT_VARIABLE status_result OUTPUT_VARIABLE source_status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(revision_result EQUAL 0 AND tree_result EQUAL 0 AND status_result EQUAL 0
      AND revision MATCHES "^[0-9a-f]+$" AND source_tree MATCHES "^[0-9a-f]+$")
    set(identity_revision "${revision}")
    set(identity_tree "${source_tree}")
    set(identity_available 1)
    if(NOT source_status STREQUAL "")
      set(identity_dirty 1)
    endif()
  endif()
endif()
file(SHA256 "${ASCIIOMIUM_IDENTITY_SOURCE_DIR}/fixtures/web/MANIFEST.json" identity_fixture_sha256)
set(identity_content "#pragma once\n#define ASCIIOMIUM_SOURCE_REVISION \"${identity_revision}\"\n#define ASCIIOMIUM_SOURCE_TREE \"${identity_tree}\"\n#define ASCIIOMIUM_SOURCE_IDENTITY_AVAILABLE ${identity_available}\n#define ASCIIOMIUM_SOURCE_DIRTY ${identity_dirty}\n#define ASCIIOMIUM_FIXTURE_MANIFEST_SHA256 \"${identity_fixture_sha256}\"\n")
get_filename_component(identity_output_directory "${ASCIIOMIUM_IDENTITY_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${identity_output_directory}")
set(existing_identity "")
if(EXISTS "${ASCIIOMIUM_IDENTITY_OUTPUT}")
  file(READ "${ASCIIOMIUM_IDENTITY_OUTPUT}" existing_identity)
endif()
if(NOT existing_identity STREQUAL identity_content)
  file(WRITE "${ASCIIOMIUM_IDENTITY_OUTPUT}" "${identity_content}")
endif()
