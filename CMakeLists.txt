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

# Issue #8 begins using CEF's C++ wrapper library. Load the official CMake
# configuration from the exact binary distribution pinned by issue #2.
#
# The initial direct-executable prototype intentionally keeps USE_SANDBOX off.
# CEF 151's sandboxed Windows path uses the newer bootstrap executable/client
# DLL architecture, while issue #2 established a traditional executable linked
# directly to libcef. This temporary exception is documented in docs/CEF_OSR.md
# and will be revisited before distribution rather than hidden behind a switch.
set(USE_SANDBOX OFF CACHE BOOL "ASCIIomium direct-executable OSR prototype" FORCE)
list(PREPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/libcef_dll_wrapper")
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
set_target_properties(libcef_dll_wrapper PROPERTIES FOLDER "CEF")

# Retain the small imported target used by the original build-info bootstrap.
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

add_library(asciiomium_benchmark_support STATIC
  src/benchmark/benchmark_result.cpp
  src/benchmark/benchmark_result.h)
target_include_directories(asciiomium_benchmark_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(asciiomium_benchmark_support PUBLIC cxx_std_20)
set_target_properties(asciiomium_benchmark_support PROPERTIES FOLDER "ASCIIomium")

# Newest-frame storage is kept free of CEF headers so its ownership semantics
# can be unit-tested independently. Match CEF's Windows /MT runtime because the
# same library is linked into the OSR client executable.
add_library(asciiomium_source_frame STATIC
  src/browser/source_frame.cpp
  src/browser/source_frame.h)
target_include_directories(asciiomium_source_frame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(asciiomium_source_frame PUBLIC cxx_std_20)
set_target_properties(asciiomium_source_frame PROPERTIES
  FOLDER "ASCIIomium"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(asciiomium_browser STATIC
  src/browser/osr_client.cpp
  src/browser/osr_client.h)
target_include_directories(asciiomium_browser PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CEF_ROOT}")
target_link_libraries(asciiomium_browser PUBLIC
  asciiomium_source_frame
  libcef_dll_wrapper
  libcef_lib
  ${CEF_STANDARD_LIBS})
target_compile_features(asciiomium_browser PUBLIC cxx_std_20)
target_compile_definitions(asciiomium_browser PRIVATE
  __STDC_CONSTANT_MACROS
  __STDC_FORMAT_MACROS
  NOMINMAX
  WIN32_LEAN_AND_MEAN)
add_dependencies(asciiomium_browser libcef_dll_wrapper)
set_target_properties(asciiomium_browser PROPERTIES
  FOLDER "ASCIIomium"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

add_executable(asciiomium_benchmark tools/offline_benchmark.cpp)
target_link_libraries(asciiomium_benchmark PRIVATE
  asciiomium_benchmark_support
  asciiomium_render
  asciiomium_terminal
  Psapi)
target_compile_features(asciiomium_benchmark PRIVATE cxx_std_20)
target_compile_definitions(asciiomium_benchmark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(asciiomium_benchmark PROPERTIES FOLDER "Tools")

add_executable(asciiomium_cef_capture tools/cef_capture.cpp)
target_link_libraries(asciiomium_cef_capture PRIVATE asciiomium_browser)
target_compile_features(asciiomium_cef_capture PRIVATE cxx_std_20)
target_compile_definitions(asciiomium_cef_capture PRIVATE
  __STDC_CONSTANT_MACROS
  __STDC_FORMAT_MACROS
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  ASCIIOMIUM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(asciiomium_cef_capture PROPERTIES
  FOLDER "Tools"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Even a version-only executable can cause the Windows loader to resolve CEF's
# sibling DLL dependencies before main(), so stage the complete DLL manifest
# declared by the pinned CEF release.
function(asciiomium_stage_cef_runtime target)
  foreach(cef_runtime_dll IN LISTS ASCIIOMIUM_CEF_RUNTIME_DLLS)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CEF_ROOT}/$<CONFIG>/${cef_runtime_dll}"
              "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Staging ${cef_runtime_dll} for ${target}")
  endforeach()
endfunction()

# A functioning browser additionally needs V8/Vulkan metadata and the CEF
# resource/locales payload beside the executable.
function(asciiomium_stage_cef_browser_runtime target)
  asciiomium_stage_cef_runtime(${target})

  foreach(cef_config_file IN ITEMS v8_context_snapshot.bin vk_swiftshader_icd.json)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CEF_ROOT}/$<CONFIG>/${cef_config_file}"
              "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Staging ${cef_config_file} for ${target}")
  endforeach()

  foreach(cef_resource_file IN ITEMS
      chrome_100_percent.pak
      chrome_200_percent.pak
      resources.pak
      icudtl.dat)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CEF_ROOT}/Resources/${cef_resource_file}"
              "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Staging CEF resource ${cef_resource_file} for ${target}")
  endforeach()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources/locales"
            "$<TARGET_FILE_DIR:${target}>/locales"
    COMMENT "Staging CEF locales for ${target}")
endfunction()

asciiomium_stage_cef_runtime(asciiomium)
asciiomium_stage_cef_browser_runtime(asciiomium_cef_capture)

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

add_executable(benchmark_result_tests tests/benchmark_result_test.cpp)
target_link_libraries(benchmark_result_tests PRIVATE asciiomium_benchmark_support)
target_compile_features(benchmark_result_tests PRIVATE cxx_std_20)
set_target_properties(benchmark_result_tests PROPERTIES FOLDER "Tests")
add_test(NAME benchmark_result COMMAND "$<TARGET_FILE:benchmark_result_tests>")

add_executable(source_frame_tests tests/source_frame_test.cpp)
target_link_libraries(source_frame_tests PRIVATE asciiomium_source_frame)
target_compile_features(source_frame_tests PRIVATE cxx_std_20)
set_target_properties(source_frame_tests PROPERTIES
  FOLDER "Tests"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME source_frame COMMAND "$<TARGET_FILE:source_frame_tests>")

message(STATUS "ASCIIomium version: ${PROJECT_VERSION}")
message(STATUS "Pinned CEF: ${ASCIIOMIUM_CEF_VERSION}")
message(STATUS "CEF root: ${CEF_ROOT}")
message(STATUS "CEF sandbox for direct OSR prototype: ${USE_SANDBOX}")
message(STATUS "Target architecture: x64")
