//
// Created by alex on 20.12.25.
//

#ifndef BAVITH_HW_DECODER_H
#define BAVITH_HW_DECODER_H

#include <string>
#include <vector>
#include <expected>
#include <memory>

#include "VideoDecoderBase.h"

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/frame.h>
}


class HWVideoDecoder: public VideoDecoderBase {
public:
    static AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts);

    explicit HWVideoDecoder(const std::string &filename, const std::string &device_type);

    // Disable copy
    HWVideoDecoder(const HWVideoDecoder&) = delete;
    HWVideoDecoder& operator=(const HWVideoDecoder&) = delete;

    AVFrame* get_frame() override;


    /** Get the frame as a std::vector.
     *
     * @return the frame as a vector in cpu memory or a string on error.
     */
    std::expected<std::vector<uint8_t>, std::string> get_frame_vector() override;

private:
    // Custom deleters for unique_ptr
    struct SwFrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
    std::unique_ptr<AVFrame, SwFrameDeleter> sw_frame;

    AVPixelFormat hw_pixel_format = AV_PIX_FMT_NONE;

    int copy_frame_to_sw_frame();
    const AVCodec *find_hw_decoder(AVHWDeviceType type);
};

#endif //BAVITH_HW_DECODER_H