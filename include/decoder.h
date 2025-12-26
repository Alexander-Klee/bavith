//
// Created by alex on 20.12.25.
//

#ifndef BAVITH_DECODER_H
#define BAVITH_DECODER_H

#include <string>
#include <vector>
#include <expected>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/frame.h>
    #include <libavutil/rational.h>
}


class VideoDecoder {
public:
    explicit VideoDecoder(const std::string &filename);
    ~VideoDecoder();

    // Disable copy
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    void dump_info() const;
    int get_width() const;
    int get_height() const;
    int get_pixel_format() const;
    AVRational get_frame_rate() const;
    double get_duration() const;
    double get_frame_time() const;
    double get_progress() const;
    AVFrame* get_frame() const;

    /** Get the frame as a std::vector.
     *
     * @return the frame as a vector in cpu memory or a string on error.
     */
    std::expected<std::vector<uint8_t>, std::string> get_frame_vector() const;

    bool is_end_of_stream() const;
    void seek(double fraction);

    /** Decode the next frame
     *
     * demux+decode the next frame of the selected video stream, disregarding all other streams.
     *
     * @return 0 on success, < 0 on error or EOF
     */
    int decode_next_frame();

private:
    std::string filename;
    AVFormatContext* format_context = nullptr;
    AVCodecContext* decoder_context = nullptr;
    const AVCodec* decoder = nullptr;
    AVStream* video_stream = nullptr;
    int video_stream_index = -1;

    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

    bool end_of_stream = false;
    int64_t frame_pts = 0;
    int64_t video_frame_count = 0;
    double duration = 0.0;
};

#endif //BAVITH_DECODER_H