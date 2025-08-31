cmake_minimum_required(VERSION 3.16)

set(VCPKG_TARGET_TRIPLET x64-windows-static)

set(CMAKE_CXX_STANDARD 20)

file (STRINGS "version" BUILD_VERSION)

project(websocket_proxy
        VERSION ${BUILD_VERSION}
        LANGUAGES CXX)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions(-DNDEBUG)
endif()

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost CONFIG REQUIRED COMPONENTS beast context)

set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_MSVC_STATIC_RT TRUE)
find_package(OpenSSL REQUIRED)

include(FetchContent)
# Disable examples, tests, and benchmarks for slick_logger
set(BUILD_SLICK_LOGGER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SLICK_LOGGER_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SLICK_LOGGER_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    slick_logger
    GIT_REPOSITORY https://github.com/SlickTech/slick_logger.git
    GIT_TAG v0.1.0.1
)
FetchContent_MakeAvailable(slick_logger)

message(STATUS "Slick Queue: ${slick_queue_SOURCE_DIR}")
message(STATUS "Slick Logger: ${slick_logger_SOURCE_DIR}")
message(STATUS "OpenSSL: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "Beast: ${boost_beast_CONFIG}")

set(RELEASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
configure_file(src/version.h.in dist/include/websocket_proxy/version.h)

set(SOURCES
    src/main.cpp
    src/websocket_proxy.cpp
)

add_executable(websocket_proxy ${SOURCES})
target_include_directories(websocket_proxy PUBLIC include ${RELEASE_DIR}/include)
target_link_libraries(websocket_proxy PRIVATE slick_queue slick_logger Boost::asio Boost::beast Boost::context OpenSSL::SSL OpenSSL::Crypto)
# target_precompile_headers(websocket_proxy PRIVATE "src/pch.hpp")

if (MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2")
    add_definitions(-D_WIN32_WINNT=0x0A00)
    set(CMAKE_SUPPRESS_REGENERATION true)   # supress zero_check
    set_target_properties(websocket_proxy PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set_target_properties(websocket_proxy PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")
    set_target_properties(websocket_proxy PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")
    set_target_properties(websocket_proxy PROPERTIES LINK_INCREMENTAL ON)
    target_compile_definitions(websocket_proxy PUBLIC _UNICODE) # set CharacterSet to unicode
    target_compile_options(websocket_proxy PRIVATE "/bigobj")
    set_target_properties(websocket_proxy PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto")
endif()

# Installation rules
install(DIRECTORY include/ DESTINATION websocket_proxy/include)

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_custom_command(TARGET websocket_proxy
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include ${RELEASE_DIR}/include
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/_deps/slick_queue-src/include ${RELEASE_DIR}/include
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/Release ${RELEASE_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E tar "cfv" "websocket_proxy_${BUILD_VERSION}.zip" --format=zip "${RELEASE_DIR}/bin" "${RELEASE_DIR}/include"
        WORKING_DIRECTORY "${RELEASE_DIR}"
        COMMENT "Creating zip archive"
    )
endif()

option(BUILD_EXAMPLE "Build example" ON)
if(BUILD_EXAMPLE)
    message(STATUS "Building example")
    add_subdirectory(example)
else()
    message(STATUS "Skipping example")
endif()
