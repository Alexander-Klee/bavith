# Bavith

**/ˈbeɪsɪks/** (BAsic VIdeo)

Simple libav wrapper for decoding/demuxing as well as encoding/muxing video.

# Requirements

Needs libav (ffmpeg)

# Building

- Compile an example program by passing `-DBUILD_EXAMPLES=ON` to cmake
- otherwise a library will be compiled (but not installed)

- Just include this as a sub-folder/-module in you project and add this to the CMakeLists.txt:
```cmake
add_subdirectory(bavith)
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE bavith)
```

# Usage

## Decoder

```c
VideoDecoder decoder("file/path/video.mp4");

int width = decoder.get_width();
int height = decoder.get_height();

while (!decoder.is_end_of_stream()) {
    decoder.decode_next_frame();
    AVFrame* frame = decoder.get_frame();
}
```