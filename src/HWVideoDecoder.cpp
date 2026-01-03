//
// Created by alex on 20.12.25.
//

#include "../include/HWVideoDecoder.h"

#include <stdexcept>
#include <vector>
#include <expected>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
}


AVPixelFormat HWVideoDecoder::get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts) {
    auto *self = static_cast<HWVideoDecoder *>(ctx->opaque);

    for (const AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == self->hw_pixel_format)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

HWVideoDecoder::HWVideoDecoder(const std::string &filename, const std::string &device_type) : VideoDecoderBase(filename) {
    int ret = 0;

    // av_log_set_level(AV_LOG_DEBUG);

    const AVHWDeviceType type = av_hwdevice_find_type_by_name(device_type.c_str());
    if (type == AV_HWDEVICE_TYPE_NONE)
        throw std::runtime_error("Unknown device " + device_type);

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

    decoder = find_hw_decoder(type);

    decoder_context.reset(avcodec_alloc_context3(decoder));
    if (!decoder_context)
        throw std::runtime_error("Failed to allocate AVCodecContext");

    // set callback for pixel format negotiation
    decoder_context->opaque = this;
    decoder_context->get_format = get_hw_format;

    if ((ret = avcodec_parameters_to_context(decoder_context.get(), video_stream->codecpar)) < 0)
        throw std::runtime_error("Failed to copy codec parameters: " + ffmpeg_error(ret));

    AVBufferRef* raw_av_buf = nullptr;
    if (av_hwdevice_ctx_create(&raw_av_buf, type, nullptr, nullptr, 0) < 0)
        throw std::runtime_error("Failed to create specified HW device.");
    decoder_context->hw_device_ctx = av_buffer_ref(raw_av_buf); // TODO check failure?
    av_buffer_unref(&raw_av_buf);

    if ((ret = avcodec_open2(decoder_context.get(), decoder, nullptr)) < 0)
        throw std::runtime_error("Failed to open codec: " + ffmpeg_error(ret));

    packet.reset(av_packet_alloc());
    if (!packet) throw std::runtime_error("Failed to allocate AVPacket");

    frame.reset(av_frame_alloc());
    if (!frame) throw std::runtime_error("Failed to allocate AVFrame");

    sw_frame.reset(av_frame_alloc());
    if (!sw_frame) throw std::runtime_error("Failed to allocate AVFrame");

    // discard all non-video streams // TODO discard all non selected streams
    // for (unsigned i = 0; i < format_context->nb_streams; i++) {
    //     if (format_context->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    //         format_context->streams[i]->discard = AVDISCARD_ALL;
    // }
}


const AVCodec *HWVideoDecoder::find_hw_decoder(const AVHWDeviceType type) {
    void* iter = nullptr;
    const AVCodec* av_codec = nullptr;

    while ((av_codec = av_codec_iterate(&iter))) {
        if (!av_codec_is_decoder(av_codec) || av_codec->id != video_stream->codecpar->codec_id)
            continue; // skip as decoder does not support codec

        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(av_codec, i);
            if (!config) break;

            // does decoder support HW and our device?
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
                hw_pixel_format = config->pix_fmt;
                return av_codec;
            }
        }
    }
    return nullptr;
}

AVFrame* HWVideoDecoder::get_frame() {
    copy_frame_to_sw_frame();
    // if (copy_frame_to_sw_frame() < 0) // TODO
        // return nullptr;
    return sw_frame.get();
}

std::expected<std::vector<uint8_t>, std::string> HWVideoDecoder::get_frame_vector() {
    if (copy_frame_to_sw_frame() < 0)
        return std::unexpected("Error transferring the data to system memory");

    if (!sw_frame || !sw_frame->data[0])
        return std::unexpected("No frame available");

    const auto pixel_format = static_cast<AVPixelFormat>(sw_frame->format);
    int buf_size = av_image_get_buffer_size(pixel_format, sw_frame->width, sw_frame->height, 1);
    if (buf_size < 0)
        return std::unexpected("Failed to get buffer size for frame");

    std::vector<uint8_t> buf(buf_size);
    if (av_image_copy_to_buffer(buf.data(), buf_size,
                                sw_frame->data, sw_frame->linesize,
                                pixel_format, get_width(), get_height(), 1) < 0) {
        return std::unexpected("Failed to copy frame to buffer");
    }

    // TODO convert to YUV420
    return buf;
}

int HWVideoDecoder::copy_frame_to_sw_frame() {
    // TODO unref sw_frame??
    // av_frame_unref(sw_frame.get());
    return av_hwframe_transfer_data(sw_frame.get(), frame.get(), 0);
}
