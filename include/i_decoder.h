//
// Created by alex on 26.12.25.
//

#ifndef BAVITH_I_DECODER_H
#define BAVITH_I_DECODER_H

#include <vector>
#include <expected>
#include <string>

extern "C" {
    #include <libavutil/rational.h>
    #include <libavutil/frame.h>
}

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;

    virtual void dump_info() const = 0;
    virtual int get_width() const = 0;
    virtual int get_height() const = 0;
    virtual int get_pixel_format() const = 0;
    virtual AVRational get_frame_rate() const = 0;
    virtual double get_duration() const = 0;
    virtual double get_frame_time() const = 0;
    virtual double get_progress() const = 0;
    virtual AVFrame* get_frame() = 0;
    virtual AVFrame *get_raw_frame() const = 0;

    virtual std::expected<std::vector<uint8_t>, std::string> get_frame_vector() = 0;

    virtual bool is_end_of_stream() const = 0;
    virtual void seek(double fraction) = 0;

    virtual int decode_next_frame() = 0;
};

#endif //BAVITH_I_DECODER_H