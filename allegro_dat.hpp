#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace allegro_dat {

constexpr uint32_t fourcc(char a, char b, char c, char d) noexcept {
    return (uint32_t(uint8_t(a)) << 24) |
           (uint32_t(uint8_t(b)) << 16) |
           (uint32_t(uint8_t(c)) <<  8) |
           (uint32_t(uint8_t(d)) <<  0);
}

struct Property {
    uint32_t id;          // fourcc, e.g. "NAME"
    std::string value;    // UTF-8 bytes (not null-terminated on disk)
};

struct Object {
    uint32_t type; // e.g. "BMP ", "SAMP", "FILE"
    std::vector<Property> props;

    // Raw payload after (optional) per-object decompression
    std::vector<uint8_t> data;

    // If type == "FILE", children are parsed from data
    std::vector<Object> children;
};

struct Datafile {
    std::vector<Object> root_objects;
};

class ReaderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace allegro_dat
