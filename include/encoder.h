//
// Created by alex on 20.12.25.
//

#ifndef BAVITH_ENCODER_H
#define BAVITH_ENCODER_H

#include <string>
#include <vector>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
}


class VideoEncoder {
// https://ffmpeg.org/doxygen/trunk/doc_2examples_2mux_8c_source.html
    AVCodecContext* encoder_context = nullptr;
    const AVOutputFormat *output_format = nullptr;
    AVFormatContext *output_context = nullptr;
    const AVCodec *video_codec = nullptr;
    const AVPixelFormat pixelFormat;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    AVStream* stream = nullptr;
    int64_t next_pts = 0;
    int64_t frame_index = 0;
    int height = 0;
    int width = 0;

public:
    VideoEncoder(
        const std::string &filename,
        int width, int height,
        AVRational fps = {25, 1},
        AVPixelFormat pixelFormat = AV_PIX_FMT_YUV420P);
    ~VideoEncoder();

    void encode_frame(const std::vector<uint8_t> &image_buf);

private:
    void _gen_frame();
    void flush_encoder();
    void encode_frame_synthetic();
};

#endif //BAVITH_ENCODER_H