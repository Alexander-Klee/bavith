extern "C" {
    #include <libavformat/avformat.h>
}

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <media file>" << std::endl;
        return 1;
    }

    const char* filename = argv[1];

    // Context for a video
    AVFormatContext* pFormatCtx = avformat_alloc_context();
    // populate the context
    avformat_open_input(&pFormatCtx, filename, nullptr, nullptr);

    // print info, duration seems wrong?
    std::cout << "Format: " << pFormatCtx->iformat->name << ", duration: " << pFormatCtx->duration / AV_TIME_BASE << "s" << std::endl;

    avformat_find_stream_info(pFormatCtx, nullptr);

    // iterate over all streams (e.g. audio and video)
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        AVCodecParameters* pLocalCodecParameters = pFormatCtx->streams[i]->codecpar;
        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        // print info on each stream
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            std::cout << "Video Codec: resolution " << pLocalCodecParameters->width << "x" << pLocalCodecParameters->height << ", framerate: " << pLocalCodecParameters->framerate << std::endl;
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            std::cout << "Audio Codec: " << pLocalCodecParameters->ch_layout.nb_channels << " channels, " << pLocalCodecParameters->sample_rate << " sample rate" << std::endl;
        }
        std::cout << "Codec: " << pLocalCodec->long_name << " ID: " << pLocalCodec->id << ", bitrate: " << pLocalCodecParameters->bit_rate << "bit/s" << std::endl;

        avcodec_parameters_free(&pLocalCodecParameters);
    }

    // AVPacket* packet = av_packet_alloc();
    //
    // // read frames until EOF or ERR
    // while (av_read_frame(pFormatCtx, packet) >= 0) {
    //
    // }
    //
    //
    //
    // // cleanup
    // av_packet_free(&packet);
    avformat_close_input(&pFormatCtx);

    return 0;
}
