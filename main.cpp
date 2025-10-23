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

void save_frame(const AVFrame* frame, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);

    // https://en.wikipedia.org/wiki/Netpbm#PGM_example
    // PGM header
    file << "P5" << std::endl
         << frame->width << " " << frame->height << std::endl
         << "255" << std::endl;

    file.write(reinterpret_cast<char *>(frame->data[0]), frame->height * frame->width);
    file.close();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <media file>" << std::endl;
        return 1;
    }

    const char* filename = argv[1];

    VideoDecoder v(filename);
    v.dump_info();

    for (int i = 0; i < 4; ++i) {
        if (auto res = v.next_frame()) {
            // do sth with value
            save_pgm(res.value(), v.get_width(), v.get_height(), "frame" + std::to_string(i) + ".pgm");
        } else {
            std::cout << res.error() << std::endl;
        }
    }
    return 0;
}
