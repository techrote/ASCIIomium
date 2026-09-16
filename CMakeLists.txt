cmake_minimum_required(VERSION 3.21)

# ASCIIomium intentionally supports only the configurations exercised by the
# pinned CEF binary distribution at this stage.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(ASCIIomium VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ASCIIomium currently targets Windows x64 only.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "ASCIIomium requires the Visual Studio 2022/MSVC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ASCIIomium requires an x64 generator (for example -A x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

foreach(config DEBUG RELEASE)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin/${config}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin/${config}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib/${config}")
endforeach()

include(cmake/AcquireCEF.cmake)
asciiomium_acquire_cef(CEF_ROOT)

# Keep the initial CEF integration deliberately small. We link directly against
# the official CEF import library so --version and the smoke test exercise the
# real libcef.dll, but browser initialisation remains deferred to issue #8.
add_library(CEF::libcef SHARED IMPORTED GLOBAL)
set_target_properties(CEF::libcef PROPERTIES
  IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
  IMPORTED_LOCATION_DEBUG "${CEF_ROOT}/Debug/libcef.dll"
  IMPORTED_IMPLIB_DEBUG "${CEF_ROOT}/Debug/libcef.lib"
  IMPORTED_LOCATION_RELEASE "${CEF_ROOT}/Release/libcef.dll"
  IMPORTED_IMPLIB_RELEASE "${CEF_ROOT}/Release/libcef.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT}")

add_library(asciiomium_build_info STATIC
  src/app/build_info.cpp
  src/app/build_info.h)
target_include_directories(asciiomium_build_info PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(asciiomium_build_info PRIVATE CEF::libcef)
target_compile_features(asciiomium_build_info PUBLIC cxx_std_20)
target_compile_definitions(asciiomium_build_info PRIVATE
  __STDC_CONSTANT_MACROS
  __STDC_FORMAT_MACROS
  NOMINMAX
  ASCIIOMIUM_VERSION="${PROJECT_VERSION}"
  ASCIIOMIUM_BUILD_TYPE="$<CONFIG>"
  ASCIIOMIUM_ARCH="x64")
set_target_properties(asciiomium_build_info PROPERTIES FOLDER "ASCIIomium")

add_library(asciiomium_terminal STATIC
  src/terminal/full_frame_emitter.cpp
  src/terminal/full_frame_emitter.h
  src/terminal/terminal_diagnostics.cpp
  src/terminal/terminal_diagnostics.h
  src/terminal/terminal_geometry.cpp
  src/terminal/terminal_geometry.h
  src/terminal/terminal_sequences.cpp
  src/terminal/terminal_sequences.h
  src/terminal/terminal_session.cpp
  src/terminal/terminal_session.h)
target_include_directories(asciiomium_terminal PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(asciiomium_terminal PUBLIC cxx_std_20)
target_compile_definitions(asciiomium_terminal PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(asciiomium_terminal PROPERTIES FOLDER "ASCIIomium")

# Pure offline renderer. This target deliberately has no dependency on CEF,
# Win32 terminal I/O, stdout or the terminal runtime.
add_library(asciiomium_render STATIC
  src/render/color_quantizer.cpp
  src/render/color_quantizer.h
  src/render/halfblock_renderer.cpp
  src/render/halfblock_renderer.h
  src/render/ppm_loader.cpp
  src/render/ppm_loader.h
  src/render/render_types.h)
target_include_directories(asciiomium_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(asciiomium_render PUBLIC cxx_std_20)
set_target_properties(asciiomium_render PROPERTIES FOLDER "ASCIIomium")

add_executable(asciiomium src/app/main.cpp)
target_link_libraries(asciiomium PRIVATE
  asciiomium_build_info
  asciiomium_terminal
  CEF::libcef)
target_compile_features(asciiomium PRIVATE cxx_std_20)
set_target_properties(asciiomium PROPERTIES FOLDER "ASCIIomium")

add_executable(asciiomium_frame_dump tools/frame_dump.cpp)
target_link_libraries(asciiomium_frame_dump PRIVATE
  asciiomium_render
  asciiomium_terminal)
target_compile_features(asciiomium_frame_dump PRIVATE cxx_std_20)
set_target_properties(asciiomium_frame_dump PROPERTIES FOLDER "Tools")

add_executable(asciiomium_color_ramp tools/color_ramp.cpp)
target_link_libraries(asciiomium_color_ramp PRIVATE asciiomium_render)
target_compile_features(asciiomium_color_ramp PRIVATE cxx_std_20)
set_target_properties(asciiomium_color_ramp PROPERTIES FOLDER "Tools")

add_executable(asciiomium_vt_preview tools/vt_preview.cpp)
target_link_libraries(asciiomium_vt_preview PRIVATE
  asciiomium_render
  asciiomium_terminal)
target_compile_features(asciiomium_vt_preview PRIVATE cxx_std_20)
set_target_properties(asciiomium_vt_preview PROPERTIES FOLDER "Tools")

# Even a version-only executable can cause the Windows loader to resolve CEF's
# sibling DLL dependencies before main(), so stage the complete DLL manifest
# declared by the pinned CEF release. Browser resources/locales remain deferred.
function(asciiomium_stage_cef_runtime target)
  foreach(cef_runtime_dll IN LISTS ASCIIOMIUM_CEF_RUNTIME_DLLS)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CEF_ROOT}/$<CONFIG>/${cef_runtime_dll}"
              "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Staging ${cef_runtime_dll} for ${target}")
  endforeach()
endfunction()

asciiomium_stage_cef_runtime(asciiomium)

enable_testing()

add_executable(asciiomium_tests tests/build_info_test.cpp)
target_link_libraries(asciiomium_tests PRIVATE asciiomium_build_info CEF::libcef)
target_compile_features(asciiomium_tests PRIVATE cxx_std_20)
set_target_properties(asciiomium_tests PROPERTIES FOLDER "Tests")
asciiomium_stage_cef_runtime(asciiomium_tests)
add_test(NAME build_info COMMAND "$<TARGET_FILE:asciiomium_tests>")

add_executable(terminal_logic_tests tests/terminal_logic_test.cpp)
target_link_libraries(terminal_logic_tests PRIVATE asciiomium_terminal)
target_compile_features(terminal_logic_tests PRIVATE cxx_std_20)
set_target_properties(terminal_logic_tests PROPERTIES FOLDER "Tests")
add_test(NAME terminal_logic COMMAND "$<TARGET_FILE:terminal_logic_tests>")

add_executable(terminal_console_smoke tests/terminal_console_smoke.cpp)
target_link_libraries(terminal_console_smoke PRIVATE asciiomium_terminal)
target_compile_features(terminal_console_smoke PRIVATE cxx_std_20)
set_target_properties(terminal_console_smoke PROPERTIES FOLDER "Tests")
add_test(NAME terminal_console_smoke COMMAND "$<TARGET_FILE:terminal_console_smoke>")

add_executable(full_frame_emitter_tests tests/full_frame_emitter_test.cpp)
target_link_libraries(full_frame_emitter_tests PRIVATE asciiomium_terminal)
target_compile_features(full_frame_emitter_tests PRIVATE cxx_std_20)
set_target_properties(full_frame_emitter_tests PROPERTIES FOLDER "Tests")
add_test(NAME full_frame_emitter COMMAND "$<TARGET_FILE:full_frame_emitter_tests>")

add_executable(vt_console_smoke tests/vt_console_smoke.cpp)
target_link_libraries(vt_console_smoke PRIVATE asciiomium_terminal)
target_compile_features(vt_console_smoke PRIVATE cxx_std_20)
set_target_properties(vt_console_smoke PROPERTIES FOLDER "Tests")
add_test(NAME vt_console_smoke COMMAND "$<TARGET_FILE:vt_console_smoke>")

add_executable(render_golden_tests tests/render_golden_test.cpp)
target_link_libraries(render_golden_tests PRIVATE asciiomium_render)
target_compile_features(render_golden_tests PRIVATE cxx_std_20)
target_compile_definitions(render_golden_tests PRIVATE
  ASCIIOMIUM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(render_golden_tests PROPERTIES FOLDER "Tests")
add_test(NAME render_golden COMMAND "$<TARGET_FILE:render_golden_tests>")

add_executable(color_quantizer_tests tests/color_quantizer_test.cpp)
target_link_libraries(color_quantizer_tests PRIVATE asciiomium_render)
target_compile_features(color_quantizer_tests PRIVATE cxx_std_20)
set_target_properties(color_quantizer_tests PROPERTIES FOLDER "Tests")
add_test(NAME color_quantizer COMMAND "$<TARGET_FILE:color_quantizer_tests>")

message(STATUS "ASCIIomium version: ${PROJECT_VERSION}")
message(STATUS "Pinned CEF: ${ASCIIOMIUM_CEF_VERSION}")
message(STATUS "CEF root: ${CEF_ROOT}")
message(STATUS "Target architecture: x64")
