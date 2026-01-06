#include "allegro_dat.hpp"
#include "allegro_dat_io.hpp"
#include <filesystem>
#include <iostream>

namespace allegro_dat {

static constexpr uint32_t DAT_MAGIC = fourcc('A','L','L','.');
static constexpr uint32_t PROP_MAGIC = fourcc('p','r','o','p');
static constexpr uint32_t TYPE_FILE  = fourcc('F','I','L','E');
static constexpr uint32_t TYPE_BITMAP= fourcc('B','M','P',' ');

// IMPORTANT: replace with real values from Allegro 4 headers you target.
static constexpr uint32_t F_PACK_MAGIC   = 0x736C6821; // placeholder
static constexpr uint32_t F_NOPACK_MAGIC = 0x736C6820; // placeholder

static uint32_t peek_u32be(const BeStream& s, size_t at) {
    if (at + 4 > s.size()) return 0;
    // We don't have direct buffer access; so for MVP, adjust BeStream to expose const ref.
    throw ReaderError("peek_u32be requires BeStream to expose buffer; implement before use");
}

static std::string fourcc_to_string(uint32_t v) {
    std::string s(4, ' ');
    s[0] = char((v >> 24) & 0xFF);
    s[1] = char((v >> 16) & 0xFF);
    s[2] = char((v >>  8) & 0xFF);
    s[3] = char((v >>  0) & 0xFF);
    return s;
}

// For MVP, redefine BeStream to allow peeking:
class BeStream2 {
public:
    explicit BeStream2(std::vector<uint8_t> buf) : buf_(std::move(buf)) {}
    size_t pos() const { return pos_; }
    size_t size() const { return buf_.size(); }
    void require(size_t n) const { if (pos_ + n > buf_.size()) throw ReaderError("Unexpected EOF"); }

    uint32_t peek_u32be() const {
        if (pos_ + 4 > buf_.size()) return 0;
        return (uint32_t(buf_[pos_]) << 24) | (uint32_t(buf_[pos_+1]) << 16) |
               (uint32_t(buf_[pos_+2]) <<  8) | (uint32_t(buf_[pos_+3]) <<  0);
    }

    uint8_t u8() { require(1); return buf_[pos_++]; }
    uint16_t u16be() {
        require(2);
        uint16_t v = (uint16_t(buf_[pos_]) << 8) | uint16_t(buf_[pos_ + 1]);
        pos_ += 2; return v;
    }
    uint32_t u32be() {
        require(4);
        uint32_t v = peek_u32be();
        pos_ += 4; return v;
    }
    std::vector<uint8_t> bytes(size_t n) {
        require(n);
        std::vector<uint8_t> out(buf_.begin() + (long)pos_, buf_.begin() + (long)(pos_ + n));
        pos_ += n; return out;
    }

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
};

static std::vector<Property> read_properties(BeStream2& s) {
    std::vector<Property> props;
    while (s.peek_u32be() == PROP_MAGIC) {
        (void)s.u32be(); // "prop"
        uint32_t id = s.u32be();
        uint32_t size = s.u32be();
        auto bytes = s.bytes(size);
        props.push_back(Property{id, std::string(bytes.begin(), bytes.end())});
    }
    return props;
}

static std::vector<uint8_t> read_object_payload(BeStream2& s, uint32_t comp_size, int32_t uncomp_size) {
    // Spec: if uncompressed size positive => not compressed.
    // if negative => LZSS compressed and expands to -uncomp_size. :contentReference[oaicite:7]{index=7}
    auto raw = s.bytes(comp_size);

    if (uncomp_size >= 0) {
        // should match comp_size, but don't hard-fail
        return raw;
    }

    size_t expected = (size_t)(-uncomp_size);
    return lzss_decompress_allegro(raw, expected);
}

static Object read_object(BeStream2& s);

static std::vector<Object> read_object_list(BeStream2& s, uint32_t count) {
    std::vector<Object> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        out.push_back(read_object(s));
    }
    return out;
}

static Object read_object(BeStream2& s) {
    Object obj;
    obj.props = read_properties(s);

    obj.type = s.u32be();
    uint32_t comp_size = s.u32be();
    int32_t  uncomp_size = (int32_t)s.u32be();

    obj.data = read_object_payload(s, comp_size, uncomp_size);

    if (obj.type == TYPE_FILE) {
        // Payload is: u32 object_count + object list. :contentReference[oaicite:8]{index=8}
        BeStream2 sub(std::move(obj.data));
        uint32_t n = sub.u32be();
        obj.children = read_object_list(sub, n);
        obj.data.clear();
    }

    return obj;
}

Datafile load_dat_file(const std::filesystem::path& path) {
    auto file = read_file_bytes(path);

    BeStream2 top(file);
    uint32_t pack_magic = top.u32be();

    std::vector<uint8_t> dat_stream;

    if (pack_magic == F_NOPACK_MAGIC) {
        // Unpacked: remainder is the dat stream beginning at DAT_MAGIC.
        dat_stream = top.bytes(top.size() - top.pos());
    } else if (pack_magic == F_PACK_MAGIC) {
        // Packed: remainder is an LZSS stream; decompress it to get dat stream.
        auto compressed = top.bytes(top.size() - top.pos());
        dat_stream = lzss_decompress_allegro_to_eof(compressed);
    } else {
        throw ReaderError("Unknown pack magic: " + std::to_string(pack_magic));
    }

    BeStream2 s(std::move(dat_stream));

    uint32_t dm = s.u32be();
    if (dm != DAT_MAGIC) {
        throw ReaderError("Bad DAT_MAGIC (expected ALL.) got: " + fourcc_to_string(dm));
    }

    uint32_t num = s.u32be();
    Datafile df;
    df.root_objects = read_object_list(s, num);
    return df;
}

} // namespace allegro_dat
