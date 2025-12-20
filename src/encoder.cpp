//
// Created by alex on 20.12.25.
//
#include "../include/encoder.h"

#include <stdexcept>

VideoEncoder::VideoEncoder(
    const std::string &filename,
    const int width, const int height,
    const AVRational fps,
    const AVPixelFormat pixelFormat):
        pixelFormat(pixelFormat),
        width(width),
        height(height) {
    // TODO handle pixel format
    // TODO handle fps

    // guess output format based on filename
    if (avformat_alloc_output_context2(&output_context, nullptr, nullptr, filename.c_str()) < 0) {
        throw std::runtime_error("failed to allocate output context");
    }
    if (!output_context) {
        throw std::runtime_error("failed to create output context (unable to guess output format)");
    }
    output_format = output_context->oformat;

    // add video stream
    if (output_format->video_codec == AV_CODEC_ID_NONE) { // TODO: prob unnecessary?
        throw std::runtime_error("no video codec was found");
    }

    // add the video stream
    video_codec = avcodec_find_encoder(output_format->video_codec);
    if (!video_codec) {
        throw std::runtime_error("failed to find encoder");
    }

    packet = av_packet_alloc();
    if (!packet) {
        throw std::runtime_error("failed to allocate packet");
    }
    stream = avformat_new_stream(output_context, nullptr);
    if (!stream) {
        throw std::runtime_error("failed to allocate output stream");
    }
    stream->id = output_context->nb_streams - 1;
    encoder_context = avcodec_alloc_context3(video_codec);
    if (!encoder_context) {
        throw std::runtime_error("failed to allocate encoder context");
    }

    // set data for video stream
    encoder_context->codec_id = output_format->video_codec;
    encoder_context->bit_rate = 400000;
    encoder_context->width= width;
    encoder_context->height = height;

    // TODO stream->time_base = AVRational{fps.den, fps.num }; // reciprocal of fps
    stream->time_base = AVRational{1, 25 }; // reciprocal of fps
    encoder_context->time_base = stream->time_base;
    encoder_context->gop_size = 12;
    encoder_context->pix_fmt = pixelFormat;

    // if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {

    if (output_context->oformat->flags & AVFMT_GLOBALHEADER)
        encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;


    if (avcodec_open2(encoder_context, video_codec, nullptr) < 0) {
        throw std::runtime_error("failed to open encoder");
    }

    // allocate frame and its buffers
    frame = av_frame_alloc();
    if (!frame) {
        throw std::runtime_error("failed to allocate frame");
    }
    frame->format = pixelFormat;
    frame->width  = width;
    frame->height = height;
    if (av_frame_get_buffer(frame, 0) < 0) {
        throw std::runtime_error("failed to allocate frame data buffers");
    }

    // copy encoder context to the mux (stream)
    if (avcodec_parameters_from_context(stream->codecpar, encoder_context) < 0) {
        throw std::runtime_error("failed to copy encoder context");
    }

    // print info on the video stream
    av_dump_format(output_context, 0, filename.c_str(), 1);

    // open output file
    if (avio_open(&output_context->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
        throw std::runtime_error("failed to open output");
    }

    // write header to file
    if (avformat_write_header(output_context, nullptr) < 0) {
        throw std::runtime_error("failed to write header");
    }
}

void VideoEncoder::_gen_frame() {
    if (av_frame_make_writable(frame)) {
        throw std::runtime_error("failed to make frame writeable");
    }

    // Y
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            frame->data[0][y*frame->linesize[0] + x] = x + y + frame_index * 3;

    // Cb, Cr
    for (int y = 0; y < height/2; y++)
        for (int x = 0; x < width/2; x++) {
            frame->data[1][y*frame->linesize[1] + x] = 128 + y + frame_index * 2;
            frame->data[2][y*frame->linesize[2] + x] = 64 + x + frame_index * 5;
        }

    frame->pts = next_pts++;
    frame_index++;
}

void VideoEncoder::encode_frame_synthetic() {
    // populate frame with data
    _gen_frame();

    // encode frame
    if (avcodec_send_frame(encoder_context, frame)) {
        throw std::runtime_error("failed to send frame");
    }

    // frame may end up as multiple packets
    while (true) {
        // receive encoded frame
        const int ret = avcodec_receive_packet(encoder_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            throw std::runtime_error(std::string("failed to receive packet") + av_err2str(ret));
        }

        // rescale time on packet
        av_packet_rescale_ts(packet, encoder_context->time_base, stream->time_base);
        packet->stream_index = stream->index;

        // write and unref the packet
        if (av_interleaved_write_frame(output_context, packet) < 0) {
            throw std::runtime_error("failed to write frame");
        }
    }
}

void VideoEncoder::encode_frame(const std::vector<uint8_t> &image_buf) {
    // ensure image_buf size
    int buf_size = av_image_get_buffer_size(pixelFormat, width, height, 1);
    if (image_buf.size() != buf_size) {
        throw std::runtime_error("image buffer has wrong size!");
    }
    // put image into frame (copying should not be necessary, right?)
    av_image_fill_arrays(frame->data, frame->linesize, image_buf.data(), pixelFormat, width, height, 1);

    frame->pts = next_pts++;

    // encode frame
    if (avcodec_send_frame(encoder_context, frame)) {
        throw std::runtime_error("failed to send frame");
    }

    // frame may end up as multiple packets
    while (true) {
        // receive encoded frame
        const int ret = avcodec_receive_packet(encoder_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            throw std::runtime_error(std::string("failed to receive packet") + av_err2str(ret));
        }

        // rescale time on packet
        av_packet_rescale_ts(packet, encoder_context->time_base, stream->time_base);
        packet->stream_index = stream->index;

        // write and unref the packet
        if (av_interleaved_write_frame(output_context, packet) < 0) {
            throw std::runtime_error("failed to write frame");
        }
    }
}

void VideoEncoder::flush_encoder() {
    int ret = avcodec_send_frame(encoder_context, nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        throw std::runtime_error("flushing encoder failed");
    }

    while (true) {
        ret = avcodec_receive_packet(encoder_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            throw std::runtime_error("failed to receive packet (flush)");
        }

        // rescale time on packet
        av_packet_rescale_ts(packet, encoder_context->time_base, stream->time_base);
        packet->stream_index = stream->index;

        // write and unref the packet
        if (av_interleaved_write_frame(output_context, packet) < 0) {
            throw std::runtime_error("failed to write frame");
        }
    }
}

VideoEncoder::~VideoEncoder() {
    // flush encoder
    flush_encoder();

    // write the end of the file
    av_write_trailer(output_context);

    // free everything
    avcodec_free_context(&encoder_context);
    av_frame_free(&frame);
    av_packet_free(&packet);

    avio_closep(&output_context->pb);
    avformat_free_context(output_context);
}
