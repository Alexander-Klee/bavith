#include <chrono>
#include <iostream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <media file>" << std::endl;
        return 1;
    }

    const char* filename = argv[1];

    // https://ffmpeg.org/doxygen/trunk/demux__decode_8c_source.html

    // Context for a video
    AVFormatContext* format_context = avformat_alloc_context();
    avformat_open_input(&format_context, filename, nullptr, nullptr); // populate
    avformat_find_stream_info(format_context, nullptr);

    // print info, duration seems wrong?
    std::cout << "Format: " << format_context->iformat->name << ", duration: " << format_context->duration / AV_TIME_BASE << "s" << std::endl;

    // find video stream
    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVStream* video_stream = format_context->streams[video_stream_index];

    // create decoder (and context) for video stream
    const AVCodec* decoder = avcodec_find_decoder(format_context->streams[video_stream_index]->codecpar->codec_id);
    AVCodecContext* decoder_context = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_context, video_stream->codecpar);
    avcodec_open2(decoder_context, decoder, nullptr);

    // print info on stderr
    av_dump_format(format_context, 0, filename, 0);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    int video_frame_count = 0;
    auto start = std::chrono::steady_clock::now();

    // read frames from streams in the file until EOF
    while (av_read_frame(format_context, packet) >= 0) {
        // check if stream is the chosen stream
        if (packet->stream_index == video_stream_index) {
            // video has only one frame per packet

            // decode packet to get frame
            avcodec_send_packet(decoder_context, packet);
            avcodec_receive_frame(decoder_context, frame);

            std::cout << "Frame " << video_frame_count++ << ", " << av_get_picture_type_char(frame->pict_type)
            << decoder_context->frame_num
                << ", pts " << frame->pts
                << ", dts " << frame->pkt_dts
            << std::endl;
        }

        // wipe frame, packet for next iteration
        av_frame_unref(frame);
        av_packet_unref(packet);
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << std::endl << "time elapsed: " << elapsed.count() << "s, "
        << video_frame_count/elapsed.count() << " fps"<< std::endl;

    // cleanup
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_close_input(&format_context);
    return 0;
}
