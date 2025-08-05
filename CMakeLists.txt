cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(rk3566-demo VERSION 0.0.1 LANGUAGES CXX)
message(STATUS "System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++14 -lpthread")

set(LIB_ARCH "aarch64")

# set(RKNN_API_PATH ${CMAKE_CURRENT_SOURCE_DIR}/librknn_api)
# set(RKNN_API_INCLUDE_PATH ${RKNN_API_PATH}/include)
# set(RKNN_API_LIB_PATH ${RKNN_API_PATH}/${LIB_ARCH}/librknnrt.so)

set(3RDPARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
set(OpenCV_DIR ${3RDPARTY_PATH}/opencv/opencv-linux-${LIB_ARCH}/lib)

find_package(OpenCV REQUIRED)
message(STATUS "include path: ${OpenCV_INCLUDE_DIRS}")

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(example_test 
        src/CameraCapture.cpp
        src/EncoderStreamer.cpp
        src/example.cpp
)

target_link_libraries(example_test
        ${OpenCV_LIBS}
        avformat
        avcodec
        avutil
        swscale
)

