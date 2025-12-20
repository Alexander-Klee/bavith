//
// Created by alex on 20.12.25.
//

#ifndef BAVITH_DECODER_H
#define BAVITH_DECODER_H

#include <expected>
#include <string>
#include <vector>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
}

class VideoDecoder {
    std::string _filename;
    AVFormatContext* format_context = nullptr;
    AVStream* video_stream = nullptr;
    int video_stream_index = -1;
    const AVCodec *decoder = nullptr;
    AVCodecContext* decoder_context = nullptr;
    int video_frame_count = 0;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

public:
    explicit VideoDecoder(const std::string &filename);
    ~VideoDecoder();

    void dump_info() const;

    [[nodiscard]] int get_width() const;
    [[nodiscard]] int get_height() const;
    [[nodiscard]] int get_pixel_format() const;
    [[nodiscard]] AVRational get_frame_rate() const;

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> next_frame_image() const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> next_frame_image2() const;
    [[nodiscard]] std::expected<AVFrame*, std::string> next_frame() const;
};

#endif //BAVITH_DECODER_H