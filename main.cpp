#include <chrono>
#include <iostream>
#include <fstream>

#include "decoder.h"
#include "encoder.h"

void save_pgm(const std::vector<uint8_t>& data, int width, int height, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);

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
        std::cerr << "Usage: " << argv[0] << " <input file> <output file>" << std::endl;
        return 1;
    }

    const char* filename_src = argv[1];
    const char* filename_dst = argv[2];


    const VideoDecoder decoder(filename_src);
    // decoder.dump_info();
    std::cout << "fps: " << static_cast<double>(decoder.get_frame_rate().num) / decoder.get_frame_rate().den << std::endl;

    if (!decoder.get_frame_rate().den || !decoder.get_frame_rate().num) {
        std::cout << "No frame rate!" << std::endl;
        return 1;
    }

    VideoEncoder encoder(filename_dst, decoder.get_width(), decoder.get_height(), decoder.get_frame_rate());

    // copy first 120 Frames to another file
    for (int i = 1; i <= 120;) {
        if (auto res = decoder.next_frame_image2()) {
            // do sth with value
            // save_pgm(res.value(), decoder.get_width(), decoder.get_height(), "frame" + std::to_string(f++) + ".pgm");
            encoder.encode_frame(res.value());
            std::cout << "frame: " + std::to_string(i) << std::endl;
            i++;
        } else {
            std::cout << res.error() << std::endl;
        }
    }

    return 0;
}
