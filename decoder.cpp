//
// Created by alex on 20.12.25.
//

#include "decoder.h"

#include <stdexcept>

extern "C" {
    #include <libavutil/imgutils.h>
}

VideoDecoder::VideoDecoder(const std::string &filename) { // TODO: better error handling
    _filename = filename;
    // create Context for a video
    format_context = avformat_alloc_context();
    if (format_context == nullptr)
        throw std::runtime_error("Failed to allocate AVFormatContext");
    if (avformat_open_input(&format_context, filename.c_str(), nullptr, nullptr))
        throw std::runtime_error("Could not open file");
    if (avformat_find_stream_info(format_context, nullptr))
        throw std::runtime_error("Could not find stream info");


    // TODO: error handling
    // find video stream
    video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    video_stream = format_context->streams[video_stream_index];

    // create decoder (and context) for video stream
    decoder = avcodec_find_decoder(format_context->streams[video_stream_index]->codecpar->codec_id);
    decoder_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_context, video_stream->codecpar);
    avcodec_open2(decoder_context, decoder, nullptr);

    packet = av_packet_alloc();
    frame = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    av_packet_free(&packet);
    av_frame_free(&frame);
    avformat_close_input(&format_context);
}

void VideoDecoder::dump_info() const {
    // print info on stderr
    av_dump_format(format_context, 0, _filename.c_str(), 0);
}

[[nodiscard]] int VideoDecoder::get_width() const {
    return format_context->streams[video_stream_index]->codecpar->width;
}

[[nodiscard]] int VideoDecoder::get_height() const {
    return format_context->streams[video_stream_index]->codecpar->height;
}

[[nodiscard]] int VideoDecoder::get_pixel_format() const {
    return format_context->streams[video_stream_index]->codecpar->format;
}

[[nodiscard]] AVRational VideoDecoder::get_frame_rate() const {
    return format_context->streams[video_stream_index]->avg_frame_rate;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> VideoDecoder::next_frame_image() const {
    next_frame();

    const auto pixel_format = static_cast<enum AVPixelFormat>(frame->format);
    const int buf_size = av_image_get_buffer_size(pixel_format, get_width(), get_height(), 1);
    std::vector<uint8_t> image_buf(buf_size);

    // copy the image data
    if (av_image_copy_to_buffer(image_buf.data(), buf_size,
            frame->data, frame->linesize, pixel_format, get_width(), get_height(), 1) < 0) {
        return std::unexpected("failed to copy frame");
    }

    // wipe frame and packet for next iteration
    av_frame_unref(frame);
    av_packet_unref(packet);
    return image_buf;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> VideoDecoder::next_frame_image2() const {
    // read frames from streams in the file until EOF
    if (av_read_frame(format_context, packet) < 0) {
        return std::unexpected("failed to read frame or EOF");
    }

    // check if stream is the chosen stream
    if (packet->stream_index != video_stream_index) {
        return std::unexpected("packet is not part of video stream");
    }

    // video has only one frame per packet
    // decode packet to get frame
    if (avcodec_send_packet(decoder_context, packet) < 0) {
        return std::unexpected("failed to send packet");
    }
    if (avcodec_receive_frame(decoder_context, frame) < 0) {
        return std::unexpected("failed to receive frame");
    }

    const auto pixel_format = static_cast<enum AVPixelFormat>(frame->format);
    const int buf_size = av_image_get_buffer_size(pixel_format, get_width(), get_height(), 1);
    std::vector<uint8_t> image_buf(buf_size);

    // copy the image data
    if (av_image_copy_to_buffer(image_buf.data(), buf_size,
            frame->data, frame->linesize, pixel_format, get_width(), get_height(), 1) < 0) {
        return std::unexpected("failed to copy frame");
    }

    // wipe frame and packet for next iteration
    av_frame_unref(frame);
    av_packet_unref(packet);
    return image_buf;
}

[[nodiscard]] std::expected<AVFrame*, std::string> VideoDecoder::next_frame() const {
    // read frames from streams in the file until EOF
    if (av_read_frame(format_context, packet) < 0) {
        return std::unexpected("failed to read frame or EOF");
    }

    // check if stream is the chosen stream
    if (packet->stream_index != video_stream_index) {
        return std::unexpected("packet is not part of video stream");
    }

    // video has only one frame per packet
    // decode packet to get frame
    if (avcodec_send_packet(decoder_context, packet) < 0) {
        return std::unexpected("failed to send packet");
    }
    if (avcodec_receive_frame(decoder_context, frame) < 0) {
        return std::unexpected("failed to receive frame");
    }
    return frame;
}
