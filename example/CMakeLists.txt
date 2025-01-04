project(example_websocket_client LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(json 
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

add_executable(example_websocket_client main.cpp)
target_compile_definitions(example_websocket_client PUBLIC _UNICODE)
target_include_directories(example_websocket_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${RapidJSON_SOURCE_DIR}/include ${slick_queue_SOURCE_DIR}/include)
target_link_libraries(example_websocket_client PRIVATE nlohmann_json::nlohmann_json)

 