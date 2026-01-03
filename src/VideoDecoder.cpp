//
// Created by alex on 20.12.25.
//

#include "../include/VideoDecoder.h"

#include <stdexcept>
#include <vector>
#include <expected>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/error.h>
}


VideoDecoder::VideoDecoder(const std::string &filename) : VideoDecoderBase(filename) {
    int ret = 0;

    AVFormatContext* raw_fmt_ctx = nullptr;
    if ((ret = avformat_open_input(&raw_fmt_ctx, filename.c_str(), nullptr, nullptr)) < 0)
        throw std::runtime_error("Could not open input file '" + filename + "': " + ffmpeg_error(ret));
    format_context.reset(raw_fmt_ctx);

    if ((ret = avformat_find_stream_info(format_context.get(), nullptr)) < 0)
        throw std::runtime_error("Could not find stream info for '" + filename + "': " + ffmpeg_error(ret));

    int video_stream_index = av_find_best_stream(format_context.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
        throw std::runtime_error("No suitable video stream found in file '" + filename + "'");

    video_stream = format_context->streams[video_stream_index];
    duration = (format_context->duration != AV_NOPTS_VALUE)
                   ? static_cast<double>(format_context->duration) / AV_TIME_BASE
                   : 0.0;

    decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!decoder)
        throw std::runtime_error("Unsupported codec for file '" + filename + "'");

    decoder_context.reset(avcodec_alloc_context3(decoder));
    if (!decoder_context)
        throw std::runtime_error("Failed to allocate AVCodecContext");

    if ((ret = avcodec_parameters_to_context(decoder_context.get(), video_stream->codecpar)) < 0)
        throw std::runtime_error("Failed to copy codec parameters: " + ffmpeg_error(ret));

    if ((ret = avcodec_open2(decoder_context.get(), decoder, nullptr)) < 0)
        throw std::runtime_error("Failed to open codec: " + ffmpeg_error(ret));

    packet.reset(av_packet_alloc());
    if (!packet) throw std::runtime_error("Failed to allocate AVPacket");

    frame.reset(av_frame_alloc());
    if (!frame) throw std::runtime_error("Failed to allocate AVFrame");

    // discard all non-video streams
    // for (unsigned i = 0; i < format_context->nb_streams; i++) {
    //     if (format_context->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    //         format_context->streams[i]->discard = AVDISCARD_ALL;
    // }
}



AVFrame* VideoDecoder::get_frame() { return frame.get(); }

std::expected<std::vector<uint8_t>, std::string> VideoDecoder::get_frame_vector() {
    if (!frame || !frame->data[0])
        return std::unexpected("No frame available");

    const auto pixel_format = static_cast<AVPixelFormat>(frame->format);
    int buf_size = av_image_get_buffer_size(pixel_format, get_width(), get_height(), 1);
    if (buf_size < 0)
        return std::unexpected("Failed to get buffer size for frame");

    std::vector<uint8_t> buf(buf_size);
    if (av_image_copy_to_buffer(buf.data(), buf_size,
                                frame->data, frame->linesize,
                                pixel_format, get_width(), get_height(), 1) < 0) {
        return std::unexpected("Failed to copy frame to buffer");
    }

    return buf;
}