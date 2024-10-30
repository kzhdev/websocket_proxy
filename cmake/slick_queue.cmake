set(slick_queue_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/slick_queue")
file(DOWNLOAD
    https://raw.githubusercontent.com/SlickTech/slick_queue/main/include/slick_queue.h
    ${slick_queue_SOURCE_DIR}/include/slick_queue.h)