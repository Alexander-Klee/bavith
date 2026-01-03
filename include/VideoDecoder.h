//
// Created by alex on 20.12.25.
//

#ifndef BAVITH_DECODER_H
#define BAVITH_DECODER_H

#include <expected>
#include <string>
#include <vector>

#include "VideoDecoderBase.h"

extern "C" {
    #include <libavutil/frame.h>
}


class VideoDecoder: public VideoDecoderBase {
public:
    explicit VideoDecoder(const std::string &filename);

    // Disable copy
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    AVFrame* get_frame() override;

    /** Get the frame as a std::vector.
     *
     * @return the frame as a vector in cpu memory or a string on error.
     */
    std::expected<std::vector<uint8_t>, std::string> get_frame_vector() override;
};

#endif //BAVITH_DECODER_H