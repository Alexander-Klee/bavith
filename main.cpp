#include <chrono>
#include <iostream>
#include <fstream>
#include <expected>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
}

void save_pgm(const std::vector<uint8_t>& data, int width, int height, const std::string& filename);

class VideoDecoder {
    std::string _filename;
    AVFormatContext* format_context;
    AVStream* video_stream;
    int video_stream_index;
    const AVCodec *decoder;
    AVCodecContext* decoder_context;
    int video_frame_count = 0;
    AVPacket* packet;
    AVFrame* frame;

public:
    explicit VideoDecoder(const std::string &filename) { // TODO: better error handling
        _filename = filename;
        // create Context for a video
        format_context = avformat_alloc_context();
        avformat_open_input(&format_context, filename.c_str(), nullptr, nullptr); // populate
        avformat_find_stream_info(format_context, nullptr);

        // print info, duration seems wrong?
        // std::cout << "Format: " << format_context->iformat->name << ", duration: " << format_context->duration / AV_TIME_BASE << "s" << std::endl;

        // find video stream
        video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        video_stream = format_context->streams[video_stream_index];

        // create decoder (and context) for video stream
        decoder = avcodec_find_decoder(format_context->streams[video_stream_index]->codecpar->codec_id);
        decoder_context = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(decoder_context, video_stream->codecpar);
        avcodec_open2(decoder_context, decoder, nullptr);

        packet = av_packet_alloc();
        frame = av_frame_alloc();
    }

    ~VideoDecoder() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avformat_close_input(&format_context);
    }

    void dump_info() const {
        // print info on stderr
        av_dump_format(format_context, 0, _filename.c_str(), 0);
    }

    [[nodiscard]] int get_width() const {
        return format_context->streams[video_stream_index]->codecpar->width;
    }

    [[nodiscard]] int get_height() const {
        return format_context->streams[video_stream_index]->codecpar->height;
    }

    [[nodiscard]] int get_pixel_format() const {
        return format_context->streams[video_stream_index]->codecpar->format;
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> next_frame() const {
        // read frames from streams in the file until EOF
        if (av_read_frame(format_context, packet) < 0) {
            return std::unexpected("failed to read frame or EOF");
        }

        // check if stream is the chosen stream
        if (packet->stream_index != video_stream_index) {
            return std::unexpected("packet is not part of video stream");
        }

        // video has only one frame per packet
        // decode packet to get frame
        if (avcodec_send_packet(decoder_context, packet) < 0) {
            return std::unexpected("failed to send packet");
        }
        if (avcodec_receive_frame(decoder_context, frame) < 0) {
            return std::unexpected("failed to receive frame");
        }

        auto pixel_format = static_cast<enum AVPixelFormat>(frame->format);
        int buf_size = av_image_get_buffer_size(pixel_format, get_width(), get_height(), 1);
        std::vector<uint8_t> image_buf(buf_size);

        // copy the image data
        if (av_image_copy_to_buffer(image_buf.data(), buf_size,
                frame->data, frame->linesize, pixel_format, get_width(), get_height(), 1) < 0) {
            return std::unexpected("failed to copy frame");
        }

        // wipe frame and packet for next iteration
        av_frame_unref(frame);
        av_packet_unref(packet);
        return image_buf;
    }
};

class VideoEncoder {
// https://ffmpeg.org/doxygen/trunk/doc_2examples_2mux_8c_source.html
    AVCodecContext* encoder_context; // TODO free this
    const AVOutputFormat *output_format;  // TODO free this
    AVFormatContext *output_context;  // TODO free this
    const AVCodec *video_codec;  // TODO free this
    AVFrame* frame; // TODO free this (buffer)
    AVPacket* packet;
    AVStream* stream;  // TODO free this
    int _height;
    int _width;
    int64_t next_pts = 0;
    int64_t frame_index = 0;

public:
    explicit VideoEncoder(const std::string &filename, const int width, const int height, const int fps = 25) {
        _width = width;
        _height = height;

        // guess output format based on filename
        // TODO: here might be an error??
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

        stream->time_base = (AVRational){1, fps};
        encoder_context->time_base = stream->time_base;
        encoder_context->gop_size = 12;
        encoder_context->pix_fmt = AV_PIX_FMT_YUV420P;

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
        frame->format = encoder_context->pix_fmt;
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

    void _gen_frame() {
        if (av_frame_make_writable(frame)) {
            throw std::runtime_error("failed to make frame writeable");
        }

        // Y
        for (int y = 0; y < _height; y++)
            for (int x = 0; x < _width; x++)
                frame->data[0][y*frame->linesize[0] + x] = x + y + frame_index * 3;

        // Cb, Cr
        for (int y = 0; y < _height/2; y++)
            for (int x = 0; x < _width/2; x++) {
                frame->data[1][y*frame->linesize[1] + x] = 128 + y + frame_index * 2;
                frame->data[2][y*frame->linesize[2] + x] = 64 + x + frame_index * 5;
            }

        frame->pts = next_pts++;
        frame_index++;
    }

    void encode_frame() {

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

    ~VideoEncoder() {
        // TODO idk where to put this ...
        // TODO ensure more than one frame gets written
        // write the end of the file
        av_write_trailer(output_context);

        // free everything
        avcodec_free_context(&encoder_context);
        av_frame_free(&frame);
        av_packet_free(&packet);

        avio_closep(&output_context->pb);
        avformat_free_context(output_context);
    }
};

void save_pgm(const std::vector<uint8_t>& data, int width, int height, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);

    // std::cout << "width * height: " << width * height << std::endl;
    // std::cout << "streamsize: " << static_cast<std::streamsize>(data.size()) << std::endl;

    // https://en.wikipedia.org/wiki/Netpbm#PGM_example
    // PGM header
    file << "P5" << std::endl
         << width << " " << height << std::endl
         << "255" << std::endl;

    // only use width*height from the buffer, (only Y plane, grayscale)
    file.write(reinterpret_cast<const char *>(data.data()), height * width);
    file.close();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <media file>" << std::endl;
        return 1;
    }

    const char* filename = argv[1];

    // VideoDecoder v(filename);
    // v.dump_info();
    //
    // for (int i = 0; i < 4; ++i) {
    //     if (auto res = v.next_frame()) {
    //         // do sth with value
    //         save_pgm(res.value(), v.get_width(), v.get_height(), "frame" + std::to_string(i) + ".pgm");
    //     } else {
    //         std::cout << res.error() << std::endl;
    //     }
    // }

    VideoEncoder encoder(filename, 352, 288);

    for (int i = 1; i < 2; i++) {
        encoder.encode_frame();
    }

    return 0;
}
