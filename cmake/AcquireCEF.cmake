# Acquire the pinned Chromium Embedded Framework binary distribution used by
# ASCIIomium. The normal path downloads the official archive and its SHA-1
# sidecar from the CEF automated-build service, verifies the archive, and
# extracts it under third_party/cef. Developers may instead point
# ASCIIOMIUM_CEF_ROOT at an already-extracted matching distribution.

set(ASCIIOMIUM_CEF_VERSION
    "151.3.17+gf059e67+chromium-151.0.7922.138"
    CACHE STRING "Pinned CEF binary distribution version")
set(ASCIIOMIUM_CEF_PLATFORM
    "windows64"
    CACHE STRING "Pinned CEF binary distribution platform")
set(ASCIIOMIUM_CEF_URL_BASE
    "https://cef-builds.spotifycdn.com"
    CACHE STRING "CEF automated-build download base URL")
set(ASCIIOMIUM_CEF_ROOT
    ""
    CACHE PATH "Use an already-extracted CEF distribution instead of downloading it")

# Keep this list aligned with the pinned CEF release's Windows distribution
# manifest (cef/bazel/win/variables.bzl at commit f059e67). x64 adds dxil and
# dxcompiler to the common Windows DLL set.
set(ASCIIOMIUM_CEF_RUNTIME_DLLS
  chrome_elf.dll
  d3dcompiler_47.dll
  libcef.dll
  libEGL.dll
  libGLESv2.dll
  vk_swiftshader.dll
  vulkan-1.dll
  dxil.dll
  dxcompiler.dll)

function(_asciiomium_check_download_status label status log)
  list(GET status 0 status_code)
  list(GET status 1 status_text)
  if(NOT status_code EQUAL 0)
    message(FATAL_ERROR
      "${label} failed (${status_code}: ${status_text}).\n${log}")
  endif()
endfunction()

function(asciiomium_acquire_cef out_var)
  if(NOT WIN32)
    message(FATAL_ERROR
      "Issue #2 pins the Windows x64 CEF distribution; this bootstrap currently supports Windows only.")
  endif()

  if(ASCIIOMIUM_CEF_ROOT)
    get_filename_component(cef_root "${ASCIIOMIUM_CEF_ROOT}" ABSOLUTE)
  else()
    set(distribution
        "cef_binary_${ASCIIOMIUM_CEF_VERSION}_${ASCIIOMIUM_CEF_PLATFORM}")
    set(download_dir "${CMAKE_SOURCE_DIR}/third_party/cef")
    set(cef_root "${download_dir}/${distribution}")

    if(NOT EXISTS "${cef_root}/include/cef_version.h")
      file(MAKE_DIRECTORY "${download_dir}")

      set(archive_name "${distribution}.tar.bz2")
      set(archive_path "${download_dir}/${archive_name}")
      set(hash_path "${archive_path}.sha1")
      set(download_url "${ASCIIOMIUM_CEF_URL_BASE}/${archive_name}")
      string(REPLACE "+" "%2B" download_url_escaped "${download_url}")

      message(STATUS "Fetching CEF checksum: ${download_url_escaped}.sha1")
      file(DOWNLOAD
        "${download_url_escaped}.sha1"
        "${hash_path}"
        TLS_VERIFY ON
        STATUS hash_status
        LOG hash_log)
      _asciiomium_check_download_status("CEF checksum download" "${hash_status}" "${hash_log}")

      file(READ "${hash_path}" expected_sha1)
      string(STRIP "${expected_sha1}" expected_sha1)
      string(LENGTH "${expected_sha1}" expected_sha1_length)
      if(NOT expected_sha1_length EQUAL 40 OR
         NOT expected_sha1 MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR "CEF checksum sidecar did not contain a valid SHA-1: '${expected_sha1}'")
      endif()

      message(STATUS "Fetching CEF archive: ${archive_name}")
      file(DOWNLOAD
        "${download_url_escaped}"
        "${archive_path}"
        TLS_VERIFY ON
        EXPECTED_HASH "SHA1=${expected_sha1}"
        SHOW_PROGRESS
        STATUS archive_status
        LOG archive_log)
      _asciiomium_check_download_status("CEF archive download" "${archive_status}" "${archive_log}")

      message(STATUS "Extracting CEF archive under ${download_dir}")
      file(ARCHIVE_EXTRACT
        INPUT "${archive_path}"
        DESTINATION "${download_dir}")
    endif()
  endif()

  foreach(required_path
      "include/cef_version.h"
      "include/cef_version_info.h"
      "Debug/libcef.lib"
      "Release/libcef.lib")
    if(NOT EXISTS "${cef_root}/${required_path}")
      message(FATAL_ERROR
        "CEF root '${cef_root}' is incomplete or does not match the expected Windows x64 standard distribution; missing ${required_path}")
    endif()
  endforeach()

  foreach(configuration Debug Release)
    foreach(runtime_dll IN LISTS ASCIIOMIUM_CEF_RUNTIME_DLLS)
      if(NOT EXISTS "${cef_root}/${configuration}/${runtime_dll}")
        message(FATAL_ERROR
          "CEF root '${cef_root}' is incomplete or does not match the pinned Windows x64 runtime manifest; missing ${configuration}/${runtime_dll}")
      endif()
    endforeach()
  endforeach()

  set(${out_var} "${cef_root}" PARENT_SCOPE)
endfunction()
