cmake_minimum_required(VERSION 3.21)

# ASCIIomium intentionally supports only the configurations exercised by the
# pinned CEF binary distribution at this stage.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(ASCIIomium VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ASCIIomium issue #2 bootstrap currently targets Windows x64 only.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "ASCIIomium issue #2 requires the Visual Studio 2022/MSVC toolchain.")
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

# Keep the initial integration deliberately small. We link directly against the
# official CEF import library so --version and the smoke test exercise the real
# libcef.dll, but we do not initialise a browser until issue #8.
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

add_executable(asciiomium src/app/main.cpp)
target_link_libraries(asciiomium PRIVATE asciiomium_build_info CEF::libcef)
target_compile_features(asciiomium PRIVATE cxx_std_20)
set_target_properties(asciiomium PROPERTIES FOLDER "ASCIIomium")

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

message(STATUS "ASCIIomium version: ${PROJECT_VERSION}")
message(STATUS "Pinned CEF: ${ASCIIOMIUM_CEF_VERSION}")
message(STATUS "CEF root: ${CEF_ROOT}")
message(STATUS "Target architecture: x64")
