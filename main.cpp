#include <chrono>
#include <iostream>
#include <fstream>

#include "include/decoder.h"
#include "include/hw_decoder.h"
#include "include/encoder.h"

// TODO:
// - better api
// - seeking

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


    HWVideoDecoder decoder(filename_src, "qsv");
    // VideoDecoder decoder(filename_src);

    // decoder.dump_info();
    std::cout << "fps: " << static_cast<double>(decoder.get_frame_rate().num) / decoder.get_frame_rate().den << std::endl;

    if (!decoder.get_frame_rate().den || !decoder.get_frame_rate().num) {
        std::cout << "No frame rate!" << std::endl;
        return 1;
    }

    VideoEncoder encoder(filename_dst, decoder.get_width(), decoder.get_height(), decoder.get_frame_rate());

    int i = 0;
    while (true) {
        if (i > 120) break; // stop at frame 120

        if (decoder.decode_next_frame()) {
            std::cout << "EOF" << std::endl;
            break;
        }
        std::cout << "frame: " + std::to_string(i++) << std::endl;

        if (auto res = decoder.get_frame_vector()) {
            // do sth with value
            // save_pgm(res.value(), decoder.get_width(), decoder.get_height(), "frame" + std::to_string(i) + ".pgm");
            encoder.encode_frame(res.value());
            // std::cout << "frame: " + std::to_string(i) << std::endl;
            // i++;
        } else {
            std::cout << res.error() << std::endl;
        }
    }

    return 0;
}