//
// Created by alex on 26.12.25.
//

#ifndef BAVITH_I_DECODER_H
#define BAVITH_I_DECODER_H

#include <deque>
#include <vector>
#include <expected>
#include <memory>
#include <string>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/rational.h>
    #include <libavutil/frame.h>
    #include <libavcodec/codec.h>
    #include <libavformat/avformat.h>
}

std::string ffmpeg_error(int errnum);

class VideoDecoderBase {
public:
    virtual ~VideoDecoderBase() = default;

    void dump_info() const;
    int get_width() const;
    int get_height() const;
    int get_pixel_format() const;
    AVRational get_frame_rate() const;
    double get_duration() const;
    double get_frame_time() const;
    double get_progress() const;
    double get_bitrate() const;
    AVFrame *get_raw_frame() const;
    bool is_end_of_stream() const;

    virtual AVFrame* get_frame() = 0;

    virtual std::expected<std::vector<uint8_t>, std::string> get_frame_vector() = 0;

    void seek(double fraction);

    /** Decode the next frame
     *
     * demux+decode the next frame of the selected video stream, disregarding all other streams.
     *
     * @return 0 on success, < 0 on error or EOF
     */
    int decode_next_frame();

protected:
    explicit VideoDecoderBase(std::string filename) : filename(std::move(filename)) {};

    struct PktDeleter     { void operator()(AVPacket* p)        const { av_packet_free(&p);       } };
    struct FrameDeleter   { void operator()(AVFrame* f)         const { av_frame_free(&f);        } };
    struct CtxDeleter     { void operator()(AVCodecContext* c)  const { avcodec_free_context(&c); } };
    struct FmtDeleter     { void operator()(AVFormatContext* f) const { avformat_close_input(&f); } };

    std::unique_ptr<AVFormatContext, FmtDeleter> format_context;
    std::unique_ptr<AVCodecContext, CtxDeleter> decoder_context;
    std::unique_ptr<AVPacket, PktDeleter> packet;
    std::unique_ptr<AVFrame, FrameDeleter> frame;

    std::string filename;
    const AVCodec* decoder = nullptr;
    AVStream* video_stream = nullptr;

    bool end_of_stream = false;
    int64_t frame_pts = 0;
    int64_t video_frame_count = 0;
    double duration = 0.0;

    std::deque<std::pair<int64_t, int>> bitrate_window;
    const size_t max_bitrate_window = 32;
};

#endif //BAVITH_I_DECODER_H