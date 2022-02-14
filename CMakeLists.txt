cmake_minimum_required(VERSION 3.10)

project(ZenonReverseProxy VERSION 0.01 LANGUAGES CXX)

### cmake properties

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LINUX TRUE)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "Building on windows in not supported")
endif()

# if this is set, uWebSockets must have been built with boringssl support
if (WITH_BORING_SSL)
    set(BORING_SSL TRUE)
endif()

# Disable unused build types
set (CMAKE_CONFIGURATION_TYPES "Release;Debug" CACHE STRING "Configurations" FORCE)

# default to Release type
if (${LINUX})
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
else()
    set(CMAKE_BUILD_TYPE Debug) # no release build possible on macos
endif()

message(STATUS "Building in ${CMAKE_BUILD_TYPE} mode")

# apparently this is not being set on all platforms
if (NOT CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DNDEBUG)
endif()

# TODO: Fix problems in jsonrpccxx instead of ignoring sign-compare error globally
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-error=sign-compare")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Ofast")

### subprojects and libraries

include(FindPkgConfig)
include(ExternalProject)
include(FetchContent)

# websocket client, connection to znnd
# no need to build it currently, just using the headers
# provides variables SDK_INCLUDES and SDK_LIBS
add_subdirectory(zenon-sdk-cpp)

set(json_include ${jsonrpccxx_SOURCE_DIR}/vendor)
set(sdk_include ${SDK_INCLUDES})
message(STATUS ${sdk_include})

set(uws_include "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/src/")
set(us_include "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/src")
set(us_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/uSockets.a")

if (${BORING_SSL})
    set(ssl_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/boringssl/build/ssl/libssl.a")
    set(crypto_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/boringssl/build/crypto/libcrypto.a")
    set(decrepit_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/boringssl/build/decrepit/libdecrepit.a")
    set(ssl_libs "${ssl_lib}" "${crypto_lib}" "${decrepit_lib}")
else()
    find_package(OpenSSL REQUIRED)
    set(ssl_libs ${OPENSSL_LIBRARIES})
endif()

# These must be the same versions that have been linked into uWebSockets.
# When using OpenSSL on macos, that might not be the case by default.
message(STATUS "Using ssl-libraries ${ssl_libs}")

# logging in debug mode via plog
if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(log_include "${PROJECT_SOURCE_DIR}/thirdparty/plog/include" "${PROJECT_SOURCE_DIR}/thirdparty/fmt/include")
else()
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
    set(BUILD_SPECIFIC_LIBS "${SYSTEMD_LIBRARIES}")
endif()

find_package(Threads REQUIRED)

# std::format not widely supported yet
add_subdirectory(thirdparty/fmt)

# unit test library catch2
#add_subdirectory("${PROJECT_SOURCE_DIR}/thirdparty/catch2" REQUIRED)

# path to catch2 provided module files for automatic test discovery
#list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/thirdparty/catch2/extras")

### targets

file(GLOB SRC src/*.cpp)

add_executable(znn_repro ${SRC})

target_include_directories(znn_repro
    PRIVATE ${log_include}
    PRIVATE ${wspp_include}
    PRIVATE ${uws_include}
    PRIVATE ${us_include}
    PRIVATE ${sdk_include})

target_link_libraries(znn_repro
    ${SDK_LIBS}
    Threads::Threads
    fmt::fmt
    "${BUILD_SPECIFIC_LIBS}"
    "${us_lib}"
    "${ssl_libs}")

### install

include(GNUInstallDirs)
install(TARGETS znn_repro
    CONFIGURATIONS Release
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES service/znn-repro.service
    DESTINATION /etc/systemd/system/
    CONFIGURATIONS RELEASE)


### cmake test discovery, only required if ctest is wanted

#include(CTest)
## provided by catch2 in /extras
#include(Catch)
#catch_discover_tests(reverse_proxy)
