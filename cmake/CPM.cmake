set(CPM_DOWNLOAD_LOCATION ${CMAKE_BINARY_DIR}/CPM.cmake)

file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/cpm.cmake
    ${CPM_DOWNLOAD_LOCATION})

include(${CPM_DOWNLOAD_LOCATION})