//
// Created by alex on 03.01.26.
//

#include "VideoDecoderBase.h"

#include <utility>

std::string ffmpeg_error(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

void VideoDecoderBase::dump_info() const {
    av_dump_format(format_context.get(), 0, filename.c_str(), 0);
}

int VideoDecoderBase::get_width() const { return video_stream->codecpar->width; }
int VideoDecoderBase::get_height() const { return video_stream->codecpar->height; }
int VideoDecoderBase::get_pixel_format() const { return video_stream->codecpar->format; }
AVRational VideoDecoderBase::get_frame_rate() const { return video_stream->avg_frame_rate; }
double VideoDecoderBase::get_duration() const { return duration; }
double VideoDecoderBase::get_frame_time() const { return static_cast<double>(frame_pts) * av_q2d(video_stream->time_base); }
double VideoDecoderBase::get_progress() const { return get_frame_time() / duration; }
AVFrame* VideoDecoderBase::get_raw_frame() const { return frame.get(); }
bool VideoDecoderBase::is_end_of_stream() const { return end_of_stream; }

double VideoDecoderBase::get_bitrate() const {
    if (bitrate_window.size() < 2) return 0.0;

    int64_t sum_bytes = 0;
    for (const auto &[_, bytes]: bitrate_window)
        sum_bytes += bytes;

    int64_t time_delta = bitrate_window.back().first - bitrate_window.front().first;
    double duration = time_delta * av_q2d(video_stream->time_base);

    return sum_bytes / duration;
}

void VideoDecoderBase::seek(double fraction) {
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
        // (some decoders give old frames despite flushing)
        if (frame_pts >= target_pts && frame_pts < target_pts + 200) // TODO: magic number (make this depend on frame->duration?)
            break;
    }
}

int VideoDecoderBase::decode_next_frame() {
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