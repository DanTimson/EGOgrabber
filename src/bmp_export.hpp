#pragma once
#include <cstdint>
#include <vector>
#include <fstream>
#include <filesystem>

namespace bmp_export {

// writes 24-bit BGR BMP; input is RGBA (or RGB) rows top-to-bottom
inline void write_bmp_24(const std::filesystem::path& out,
                         int w, int h,
                         const std::vector<uint8_t>& rgb) {
    // BMP stores bottom-up rows, padded to 4 bytes.
    const int row_bytes = w * 3;
    const int pad = (4 - (row_bytes % 4)) % 4;
    const uint32_t pixel_data_size = (row_bytes + pad) * h;
    const uint32_t file_size = 54 + pixel_data_size;

    std::ofstream f(out, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write BMP");

    auto w16 = [&](uint16_t v){ f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); };
    auto w32 = [&](uint32_t v){
        f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF));
        f.put(char((v >>16) & 0xFF)); f.put(char((v >>24) & 0xFF));
    };

    // BITMAPFILEHEADER
    f.put('B'); f.put('M');
    w32(file_size);
    w16(0); w16(0);
    w32(54);

    // BITMAPINFOHEADER
    w32(40);
    w32((uint32_t)w);
    w32((uint32_t)h);
    w16(1);
    w16(24);
    w32(0);
    w32(pixel_data_size);
    w32(2835); w32(2835);
    w32(0); w32(0);

    // pixels
    const uint8_t* p = rgb.data();
    for (int y = h - 1; y >= 0; --y) {
        const uint8_t* row = p + (size_t)y * (size_t)w * 3;
        for (int x = 0; x < w; ++x) {
            uint8_t r = row[x*3 + 0];
            uint8_t g = row[x*3 + 1];
            uint8_t b = row[x*3 + 2];
            f.put((char)b); f.put((char)g); f.put((char)r);
        }
        for (int i = 0; i < pad; ++i) f.put((char)0);
    }
}

} // namespace bmp_export
