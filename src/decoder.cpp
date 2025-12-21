//
// Created by alex on 20.12.25.
//

#include "../include/decoder.h"

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

    duration = static_cast<double>(format_context->duration) / AV_TIME_BASE;

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

int VideoDecoder::get_width() const { return format_context->streams[video_stream_index]->codecpar->width; }
int VideoDecoder::get_height() const { return format_context->streams[video_stream_index]->codecpar->height; }
int VideoDecoder::get_pixel_format() const { return format_context->streams[video_stream_index]->codecpar->format; }
AVRational VideoDecoder::get_frame_rate() const { return format_context->streams[video_stream_index]->avg_frame_rate; }
double VideoDecoder::get_duration() const { return duration; }
double VideoDecoder::get_frame_time() const { return frame_time; }
double VideoDecoder::get_progress() const { return 100 * frame_time / duration; }
AVFrame* VideoDecoder::get_frame() const { return frame; }

std::expected<std::vector<uint8_t>, std::string> VideoDecoder::get_frame_vector() const {
    // copy to CPU memory
    const auto pixel_format = static_cast<enum AVPixelFormat>(frame->format);
    const int buf_size = av_image_get_buffer_size(pixel_format, get_width(), get_height(), 1);
    std::vector<uint8_t> image_buf(buf_size);

    // copy the image data
    if (av_image_copy_to_buffer(image_buf.data(), buf_size,
            frame->data, frame->linesize, pixel_format, get_width(), get_height(), 1) < 0) {
        return std::unexpected("failed to copy frame");
    }

    return image_buf;
}

bool VideoDecoder::is_end_of_stream() const {
    return end_of_stream;
}

void VideoDecoder::seek(const double fraction) {
    // calc & rescale timestamp
    AVRational target_time_base = video_stream->time_base;
    int64_t target = av_rescale_q(format_context->duration * fraction, AV_TIME_BASE_Q, target_time_base);

    // seek to nearest keyframe
    av_seek_frame(format_context, video_stream_index, target, AVSEEK_FLAG_BACKWARD);
    // TODO maybe advance frames to the exact specified one?

    avcodec_flush_buffers(decoder_context);
}


int VideoDecoder::decode_next_frame() {
    // TODO is this the correct place? shouldnt this be done after sth? (consider the final end of streaming. Should this also be in the deconstr?)
    // wipe frame and packet
    av_frame_unref(frame);
    av_packet_unref(packet);

    while (true) { // TODO: limit this?
        // DEMUX
        // TODO: check for error in av_read_frame (pkt will be blank)
        if (av_read_frame(format_context, packet) < 0) {
            end_of_stream = true;
            return EOF;
        }

        // TODO: use packet->pts or frame->best_effort_timestamp?
        frame_time = static_cast<double>(packet->pts) * av_q2d(video_stream->time_base);

        // skip if not from selected stream
        if (packet->stream_index != video_stream_index) continue;

        // DECODE
        // video has only one frame per packet, decode to get frame
        auto res = avcodec_send_packet(decoder_context, packet);
        if (res < 0) return res;

        res = avcodec_receive_frame(decoder_context, frame);
        if (res < 0) return res;

        return 0;
    }
}
