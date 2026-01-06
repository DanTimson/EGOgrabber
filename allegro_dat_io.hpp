#pragma once
#include "allegro_dat.hpp"
#include <fstream>
#include <filesystem>

namespace allegro_dat {

class BeStream {
public:
    explicit BeStream(std::vector<uint8_t> buf) : buf_(std::move(buf)) {}

    size_t pos() const { return pos_; }
    size_t size() const { return buf_.size(); }
    bool eof() const { return pos_ >= buf_.size(); }

    void require(size_t n) const {
        if (pos_ + n > buf_.size()) throw ReaderError("Unexpected EOF");
    }

    uint8_t u8() {
        require(1);
        return buf_[pos_++];
    }

    uint16_t u16be() {
        require(2);
        uint16_t v = (uint16_t(buf_[pos_]) << 8) | uint16_t(buf_[pos_ + 1]);
        pos_ += 2;
        return v;
    }

    uint32_t u32be() {
        require(4);
        uint32_t v = (uint32_t(buf_[pos_]) << 24) | (uint32_t(buf_[pos_ + 1]) << 16) |
                     (uint32_t(buf_[pos_ + 2]) <<  8) | (uint32_t(buf_[pos_ + 3]) <<  0);
        pos_ += 4;
        return v;
    }

    std::vector<uint8_t> bytes(size_t n) {
        require(n);
        std::vector<uint8_t> out(buf_.begin() + (long)pos_, buf_.begin() + (long)(pos_ + n));
        pos_ += n;
        return out;
    }

    void skip(size_t n) { require(n); pos_ += n; }

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
};

// Read entire file into memory
inline std::vector<uint8_t> read_file_bytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw ReaderError("Cannot open file: " + p.string());
    in.seekg(0, std::ios::end);
    const auto len = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(len);
    in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)len);
    if (!in) throw ReaderError("Failed reading file: " + p.string());
    return buf;
}

// Allegro/Okumura LZSS parameters (match lzss.c)
static constexpr int LZSS_N = 4096;
static constexpr int LZSS_F = 18;
static constexpr int LZSS_THRESHOLD = 2;

// Core LZSS decompressor. If expected_out != 0, stops early when reached.
// Otherwise, decompresses until input is exhausted.
inline std::vector<uint8_t> lzss_decompress_allegro_impl(
    const uint8_t* in, size_t in_len, size_t expected_out /*0 => to EOF*/
) {
    std::vector<uint8_t> out;
    if (expected_out) out.reserve(expected_out);

    // Ring buffer initialized with 0 (as in Allegro)
    std::vector<uint8_t> text_buf(LZSS_N, 0);
    int r = LZSS_N - LZSS_F;

    size_t ip = 0;
    uint32_t flags = 0;

    auto need_byte = [&]() -> uint8_t {
        if (ip >= in_len) throw ReaderError("LZSS: unexpected EOF");
        return in[ip++];
    };

    while (ip < in_len) {
        // Reload flags if needed (LSB-first)
        if ((flags >>= 1) & 0x100u) {
            // still have flag bits
        } else {
            if (ip >= in_len) break;              // clean EOF
            flags = (uint32_t)in[ip++] | 0xFF00u;  // high byte to count 8 bits
        }

        if (flags & 1u) {
            // Literal
            if (ip >= in_len) break; // clean EOF
            uint8_t c = in[ip++];
            out.push_back(c);
            text_buf[r] = c;
            r = (r + 1) & (LZSS_N - 1);

        } else {
            // Match pair: 2 bytes: i, j
            if (ip + 1 >= in_len) break; // clean EOF
            uint8_t i = in[ip++];
            uint8_t j = in[ip++];

            // Okumura encoding:
            // position = i | ((j & 0xF0) << 4)
            // length = (j & 0x0F) + THRESHOLD
            int pos = int(i) | ((int(j) & 0xF0) << 4);
            int len = (int(j) & 0x0F) + LZSS_THRESHOLD;

            // Copy len+1 bytes (matches original loop k <= len)
            for (int k = 0; k <= len; ++k) {
                uint8_t c = text_buf[(pos + k) & (LZSS_N - 1)];
                out.push_back(c);
                text_buf[r] = c;
                r = (r + 1) & (LZSS_N - 1);

                if (expected_out && out.size() >= expected_out) {
                    return out;
                }
            }
        }

        if (expected_out && out.size() >= expected_out) {
            return out;
        }
    }

    // If we were given an expected size, ensure we reached it (strict).
    if (expected_out && out.size() != expected_out) {
        throw ReaderError("LZSS: output size mismatch, expected " +
                          std::to_string(expected_out) + " got " +
                          std::to_string(out.size()));
    }

    return out;
}

// Per-object blocks in Allegro .dat know expected size (negative uncompressed size).
inline std::vector<uint8_t> lzss_decompress_allegro(const std::vector<uint8_t>& compressed,
                                                    size_t expected) {
    return lzss_decompress_allegro_impl(compressed.data(), compressed.size(), expected);
}

// Optional: whole-file packed stream if you don’t know expected size
inline std::vector<uint8_t> lzss_decompress_allegro_to_eof(const std::vector<uint8_t>& compressed) {
    return lzss_decompress_allegro_impl(compressed.data(), compressed.size(), 0);
}

} // namespace allegro_dat
