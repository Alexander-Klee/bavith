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

    // discard all non-video streams
    // for (unsigned i = 0; i < format_context->nb_streams; i++) {
    //     if (format_context->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    //         format_context->streams[i]->discard = AVDISCARD_ALL;
    // }
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
double VideoDecoder::get_frame_time() const { return static_cast<double>(frame_pts) * av_q2d(video_stream->time_base);; }
double VideoDecoder::get_progress() const { return 100 * get_frame_time() / duration; }
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
    int64_t target_pts = av_rescale_q(format_context->duration * fraction, AV_TIME_BASE_Q, target_time_base);

    // seek to nearest keyframe
    av_seek_frame(format_context, video_stream_index, target_pts, AVSEEK_FLAG_BACKWARD);
    // TODO maybe advance frames to the exact specified one?

    avcodec_flush_buffers(decoder_context);
    end_of_stream = false;

    // decode a frame from the new location
    while (!decode_next_frame()) {
        if (frame_pts >= target_pts)
            break;
    }
}

int VideoDecoder::decode_next_frame() {
    int ret;

    while (true) {
        ret = avcodec_receive_frame(decoder_context, frame);

        if (ret == 0) {
            frame_pts = frame->pts;
            video_frame_count++;
            return 0;
        }
        else if (ret == AVERROR(EAGAIN)) {
            // Decoder needs more packets (loop below)
        }
        else if (ret == AVERROR_EOF) {
            // Decoder is fully flushed.
            return AVERROR_EOF;
        }
        else {
            // Decoding error
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error receiving frame: %s\n", errbuf);
            return ret;
        }

        if (end_of_stream) return AVERROR_EOF;

        // Loop reading packets until we find one for our video stream
        while (true) {
            ret = av_read_frame(format_context, packet);

            if (ret == AVERROR_EOF) {
                end_of_stream = true;

                // EOF => Flush decoder
                ret = avcodec_send_packet(decoder_context, nullptr);
                if (ret < 0) {
                    fprintf(stderr, "Error sending flush packet\n");
                    return ret;
                }
                break; // drain decoder (receive frames)
            } else if (ret < 0) {
                return ret;
            }

            // check if this packet belongs to video stream
            if (packet->stream_index == video_stream_index) {
                // Send the packet to the decoder
                ret = avcodec_send_packet(decoder_context, packet);
                av_packet_unref(packet);

                if (ret < 0) {
                    fprintf(stderr, "Error sending packet for decoding\n");
                    return ret;
                }

                break; // break to go to recieving frames
            }

            // otherwise discard and continue reading
            av_packet_unref(packet);
        }
    }
}