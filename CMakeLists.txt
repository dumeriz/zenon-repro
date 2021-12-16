cmake_minimum_required(VERSION 3.10)

project(ZenonReverseProxy VERSION 0.01 LANGUAGES CXX)

### cmake properties

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

### subprojects and libraries

include(ExternalProject)

# websockets
# can't really add more complex make commands here, so uWS must be built seperately
#ExternalProject_Add(uWebSockets
#    SOURCE_DIR ${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets
#    BUILD_IN_SOURCE 1
#    CONFIGURE_COMMAND ""
#    BUILD_COMMAND make
#    INSTALL_COMMAND ""
#)

set(uws_include "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/src/")
set(us_include "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/src")
set(us_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/uSockets.a")
set(ssl_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/boringssl/build/ssl/libssl.a")
set(crypto_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/boringssl/build/crypto/libcrypto.a")
set(decrepit_lib "${PROJECT_SOURCE_DIR}/thirdparty/uWebSockets/uSockets/boringssl/build/decrepit/libdecrepit.a")
message(STATUS "ssl-lib=${ssl_lib}")

# required by uWebSockets
find_package(ZLIB)

# logging
add_definitions(-DQUILL_NOFN_MACROS=1) # some compilers can't resolve __FUNCTION__ in lambdas; needing the _NOFN-logger macros therefore
add_subdirectory(thirdparty/quill)

# websocket client, connection to znnd
# no need to build it currently, just using the connection_ws header
#add_subdirectory(zenon-sdk-cpp)
set(sdk_include "${PROJECT_SOURCE_DIR}/zenon-sdk-cpp/include" "${PROJECT_SOURCE_DIR}/zenon-sdk-cpp/thirdparty")

find_package(Threads REQUIRED)

# unit test library catch2
#add_subdirectory("${PROJECT_SOURCE_DIR}/thirdparty/catch2" REQUIRED)

# path to catch2 provided module files for automatic test discovery
#list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/thirdparty/catch2/extras")

### targets

file(GLOB SRC src/*.cpp)

add_executable(znn_repro ${SRC})

target_include_directories(znn_repro
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE ${lyra_include}
    PRIVATE ${uws_include}
    PRIVATE ${us_include}
    PRIVATE ${sdk_include})

target_link_libraries(znn_repro
    Threads::Threads
    ZLIB::ZLIB
    quill::quill
    "${ssl_lib}"
    "${us_lib}"
    "${crypto_lib}"
    "${decrepit_lib}")

### install

include(GNUInstallDirs)
install(TARGETS znn_repro
    EXPORT znn-repro
    CONFIGURATIONS Release
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})


### cmake test discovery, only required if ctest is wanted

#include(CTest)
## provided by catch2 in /extras
#include(Catch)
#catch_discover_tests(reverse_proxy)
