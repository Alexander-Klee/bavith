cmake_minimum_required(VERSION 4.1)
project(bavith VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 23)

option(BUILD_EXAMPLES "Build example executable" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
)

add_library(${PROJECT_NAME}
        src/encoder.cpp include/VideoDecoderBase.h
        src/HWVideoDecoder.cpp include/HWVideoDecoder.h
        src/VideoDecoder.cpp include/VideoDecoder.h
)

target_include_directories(${PROJECT_NAME}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
        PUBLIC PkgConfig::LIBAV
)

if(BUILD_EXAMPLES)
    add_executable(${PROJECT_NAME}_example main.cpp)
    target_link_libraries(${PROJECT_NAME}_example PRIVATE ${PROJECT_NAME})
endif()