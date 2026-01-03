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

static std::string ffmpeg_error(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

VideoDecoder::VideoDecoder(const std::string &filename) : filename(filename) {
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


void VideoDecoder::dump_info() const {
    av_dump_format(format_context.get(), 0, filename.c_str(), 0);
}

int VideoDecoder::get_width() const { return video_stream->codecpar->width; }
int VideoDecoder::get_height() const { return video_stream->codecpar->height; }
int VideoDecoder::get_pixel_format() const { return video_stream->codecpar->format; }
AVRational VideoDecoder::get_frame_rate() const { return video_stream->avg_frame_rate; }
double VideoDecoder::get_duration() const { return duration; }
double VideoDecoder::get_frame_time() const { return static_cast<double>(frame_pts) * av_q2d(video_stream->time_base); }
double VideoDecoder::get_progress() const { return get_frame_time() / duration; }
AVFrame* VideoDecoder::get_frame() { return frame.get(); }
AVFrame* VideoDecoder::get_raw_frame() const { return frame.get(); }

double VideoDecoder::get_bitrate() const {
    if (bitrate_window.size() < 2) return 0.0;

    int64_t sum_bytes = 0;
    for (const auto &[_, bytes]: bitrate_window)
        sum_bytes += bytes;

    int64_t time_delta = bitrate_window.back().first - bitrate_window.front().first;
    double duration = time_delta * av_q2d(video_stream->time_base);

    return sum_bytes / duration;
}

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

bool VideoDecoder::is_end_of_stream() const { return end_of_stream; }

void VideoDecoder::seek(double fraction) {
    if (!video_stream || fraction < 0.0 || fraction > 1.0)
        return;

    int64_t target_pts = av_rescale_q(format_context->duration * fraction,
                                      AV_TIME_BASE_Q, video_stream->time_base);

    if (av_seek_frame(format_context.get(), video_stream->index, target_pts, AVSEEK_FLAG_BACKWARD) < 0)
        throw std::runtime_error("Error seeking to frame position");

    avcodec_flush_buffers(decoder_context.get());
    end_of_stream = false;

    // TODO use AVCodecContext skip_frames?? (skips B frames only)
    while (decode_next_frame() == 0) {
        // ignore frames of the old location for forward and backward seeking
        if (frame_pts >= target_pts && frame_pts < target_pts + 200) // TODO: magic number (make this depend on frame->duration?)
            break;
    }
}

int VideoDecoder::decode_next_frame() {
    int ret;

    while (true) {
        ret = avcodec_receive_frame(decoder_context.get(), frame.get());
        if (ret == 0) {
            frame_pts = frame->pts;
            video_frame_count++;
            return 0;
        } else if (ret == AVERROR(EAGAIN)) {
            // need more packets
        } else if (ret == AVERROR_EOF) {
            // Decoder is fully flushed.
            return AVERROR_EOF;
        } else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error receiving frame: %s\n", errbuf);
            return ret;
        }

        if (end_of_stream) return AVERROR_EOF;

        // read packets until we get one for our video
        while (true) {
            ret = av_read_frame(format_context.get(), packet.get());
            if (ret == AVERROR_EOF) {
                end_of_stream = true;
                ret = avcodec_send_packet(decoder_context.get(), nullptr); // flush decoder
                if (ret < 0) {
                    fprintf(stderr, "Error sending flush packet: %s\n", ffmpeg_error(ret).c_str());
                    return ret;
                }
                break; // drain decoder (receive frames)
            } else if (ret < 0) {
                return ret;
            }

            if (packet->stream_index == video_stream->index) {

                // keep track of bit rate
                bitrate_window.emplace_back(packet->pts, packet->size);
                if (bitrate_window.size() > max_bitrate_window)
                    bitrate_window.pop_front();

                ret = avcodec_send_packet(decoder_context.get(), packet.get());
                av_packet_unref(packet.get());
                if (ret < 0) {
                    fprintf(stderr, "Error sending packet: %s\n", ffmpeg_error(ret).c_str());
                    return ret;
                }
                break;
            }

            av_packet_unref(packet.get());
        }
    }
}