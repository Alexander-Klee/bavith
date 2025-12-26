//
// Created by alex on 20.12.25.
//

#ifndef BAVITH_HW_DECODER_H
#define BAVITH_HW_DECODER_H

#include <string>
#include <vector>
#include <expected>
#include <memory>

#include "i_decoder.h"

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/frame.h>
    #include <libavutil/rational.h>
}


class HWVideoDecoder: public IVideoDecoder {
public:
    static AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts);

    explicit HWVideoDecoder(const std::string &filename, const std::string &device_type);

    // Disable copy
    HWVideoDecoder(const HWVideoDecoder&) = delete;
    HWVideoDecoder& operator=(const HWVideoDecoder&) = delete;

    void dump_info() const override;
    int get_width() const override;
    int get_height() const override;
    int get_pixel_format() const override;
    AVRational get_frame_rate() const override;
    double get_duration() const override;
    double get_frame_time() const override;
    double get_progress() const override;
    AVFrame* get_frame() override;

    AVFrame *get_hw_frame() const;

    /** Get the frame as a std::vector.
     *
     * @return the frame as a vector in cpu memory or a string on error.
     */
    std::expected<std::vector<uint8_t>, std::string> get_frame_vector() override;

    bool is_end_of_stream() const override;
    void seek(double fraction) override;

    /** Decode the next frame
     *
     * demux+decode the next frame of the selected video stream, disregarding all other streams.
     *
     * @return 0 on success, < 0 on error or EOF
     */
    int decode_next_frame() override;

private:
    // Custom deleters for unique_ptr
    struct PktDeleter     { void operator()(AVPacket* p)        const { av_packet_free(&p);       } };
    struct FrameDeleter   { void operator()(AVFrame* f)         const { av_frame_free(&f);        } };
    struct SwFrameDeleter { void operator()(AVFrame* f)         const { av_frame_free(&f);        } };
    struct CtxDeleter     { void operator()(AVCodecContext* c)  const { avcodec_free_context(&c); } };
    struct FmtDeleter     { void operator()(AVFormatContext* f) const { avformat_close_input(&f); } };

    std::unique_ptr<AVFormatContext, FmtDeleter> format_context;
    std::unique_ptr<AVCodecContext, CtxDeleter> decoder_context;
    std::unique_ptr<AVPacket, PktDeleter> packet;
    std::unique_ptr<AVFrame, SwFrameDeleter> sw_frame;
    std::unique_ptr<AVFrame, FrameDeleter> frame;

    std::string filename;
    const AVCodec* decoder = nullptr;
    AVStream* video_stream = nullptr;
    AVPixelFormat hw_pixel_format = AV_PIX_FMT_NONE;

    bool end_of_stream = false;
    int64_t frame_pts = 0;
    int64_t video_frame_count = 0;
    double duration = 0.0;

    int copy_frame_to_sw_frame();
    const AVCodec *find_hw_decoder(AVHWDeviceType type);
};

#endif //BAVITH_HW_DECODER_H