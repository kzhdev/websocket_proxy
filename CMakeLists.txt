cmake_minimum_required(VERSION 3.15)

file (STRINGS "version" BUILD_VERSION)

project(websocket_proxy
        VERSION ${BUILD_VERSION}
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions(-DNDEBUG)
endif()

set(RELEASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/release")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION "${RELEASE_DIR}")
configure_file(src/version.h.in release/include/websocket_proxy/version.h)

find_package(boost_beast REQUIRED CONFIG)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED CONFIG)

set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_MSVC_STATIC_RT TRUE)

include(cmake/slick_queue.cmake)

set(HEADERS
    include/websocket_proxy/type.h
    include/websocket_proxy/websocket_proxy_client.h
)

set(SOURCES
    src/main.cpp
    src/websocket_proxy.cpp
)

add_executable(websocket_proxy ${SOURCES})
target_include_directories(websocket_proxy PUBLIC include ${slick_queue_SOURCE_DIR}/include ${spdlog_SOURCE_DIR} ${RELEASE_DIR}/include)
target_link_libraries(websocket_proxy PRIVATE spdlog::spdlog Boost::asio Boost::beast OpenSSL::SSL OpenSSL::Crypto)
target_precompile_headers(websocket_proxy PRIVATE src/pch.hpp)

if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    set(CMAKE_SUPPRESS_REGENERATION true)   # supress zero_check
    set_target_properties(websocket_proxy PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set_target_properties(websocket_proxy PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")
    set_target_properties(websocket_proxy PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")
    set_target_properties(websocket_proxy PROPERTIES LINK_INCREMENTAL ON)
    target_compile_definitions(websocket_proxy PUBLIC _UNICODE) # set CharacterSet to unicode
    target_compile_options(websocket_proxy PRIVATE "/bigobj")
    set_target_properties(websocket_proxy PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_custom_command(TARGET websocket_proxy
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/Release ${RELEASE_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E tar "cfv" "websocket_proxy_${BUILD_VERSION}.zip" --format=zip "${RELEASE_DIR}/bin" "${RELEASE_DIR}/include"
        WORKING_DIRECTORY "${RELEASE_DIR}"
        COMMENT "Creating zip archive"
    )
endif()

