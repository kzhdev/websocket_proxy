cmake_minimum_required(VERSION 3.15)

file (STRINGS "version" BUILD_VERSION)

project(websocket_proxy
        VERSION ${BUILD_VERSION}
        LANGUAGES CXX)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
configure_file(src/version.h.in include/version.h)

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
target_include_directories(websocket_proxy PUBLIC include ${slick_queue_SOURCE_DIR}/include ${spdlog_SOURCE_DIR} ${CMAKE_BINARY_DIR})
target_link_libraries(websocket_proxy PRIVATE spdlog::spdlog_header_only Boost::asio Boost::beast OpenSSL::SSL OpenSSL::Crypto)

if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    set(CMAKE_SUPPRESS_REGENERATION true)   # supress zero_check
    target_compile_definitions(websocket_proxy PUBLIC _UNICODE) # set CharacterSet to unicode
    target_compile_options(websocket_proxy PRIVATE "/bigobj")
    set_target_properties(websocket_proxy PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()