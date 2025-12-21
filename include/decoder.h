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
    bool end_of_stream = false;
    double frame_time = 0;
    double duration = 0;

public:
    explicit VideoDecoder(const std::string &filename);
    ~VideoDecoder();

    void dump_info() const;

    [[nodiscard]] int get_width() const;
    [[nodiscard]] int get_height() const;
    [[nodiscard]] int get_pixel_format() const;
    [[nodiscard]] AVRational get_frame_rate() const;
    [[nodiscard]] double get_duration() const;
    [[nodiscard]] double get_frame_time() const;
    [[nodiscard]] double get_progress() const;
    [[nodiscard]] AVFrame* get_frame() const;

    /** Get the frame as a std::vector.
     *
     * @return the frame as an vector in cpu memory or a string on error.
     */
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> get_frame_vector() const;
    [[nodiscard]] bool is_end_of_stream() const;

    /** Decode the next frame
     *
     * demux+decode the next frame of the selected video stream, disregarding all other streams.
     *
     * @return 0 on success, < 0 on error or EOF
     */
    int decode_next_frame();
};

#endif //BAVITH_DECODER_H