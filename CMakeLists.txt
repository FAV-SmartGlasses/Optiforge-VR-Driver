cmake_minimum_required(VERSION 3.15)
project(driver_optiforge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# Fetched as a plain release tarball rather than a git clone: it avoids
# depending on the git binary, is pinned/verified with a checksum, and
# (unlike a bare `git clone`) CMake prints real download progress for it.
# This is a one-time ~150 MB download (the repo bundles prebuilt binaries
# for every platform); it's cached under build/_deps and skipped on
# subsequent configures.
FetchContent_Declare(
    openvr
    URL https://github.com/ValveSoftware/openvr/archive/refs/tags/v2.5.1.tar.gz
    URL_HASH SHA256=54f654fce001682d8ac608f544a6c41e03a672b005c1deca3579fa36480a537c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # Only the headers and prebuilt libs are used, don't build openvr_api
    # from source (it also doesn't build with MinGW).
    EXCLUDE_FROM_ALL
)

# openvr v2.5.1's CMakeLists.txt requires CMake 2.8, which CMake >= 4.0
# rejects outright. Override the minimum policy version for this
# subproject only, since we can't edit the fetched source.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(openvr)
unset(CMAKE_POLICY_VERSION_MINIMUM)

if(WIN32)
    set(OPENVR_PLATFORM_DIR win64)
    set(OPENVR_API_LIB ${openvr_SOURCE_DIR}/lib/win64/openvr_api.lib)
elseif(APPLE)
    set(OPENVR_PLATFORM_DIR osx32)
    set(OPENVR_API_LIB ${openvr_SOURCE_DIR}/lib/osx32/libopenvr_api.dylib)
else()
    set(OPENVR_PLATFORM_DIR linux64)
    set(OPENVR_API_LIB ${openvr_SOURCE_DIR}/lib/linux64/libopenvr_api.so)
endif()

add_library(driver_optiforge SHARED
    driver_optiforge/driver.cpp
    driver_optiforge/driverlog.cpp
)

target_include_directories(driver_optiforge PRIVATE
    ${openvr_SOURCE_DIR}/headers
    driver_optiforge
)

target_link_libraries(driver_optiforge PRIVATE ${OPENVR_API_LIB})

if(WIN32)
    target_link_libraries(driver_optiforge PRIVATE ws2_32)
endif()

# Bundle the MinGW runtime (libstdc++, libgcc, winpthread) into the DLL,
# so SteamVR doesn't need any extra DLLs next to it.
if(MINGW)
    target_link_options(driver_optiforge PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(driver_optiforge PROPERTIES
    PREFIX ""
    OUTPUT_NAME "driver_optiforge"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${OPENVR_PLATFORM_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${OPENVR_PLATFORM_DIR}
)
