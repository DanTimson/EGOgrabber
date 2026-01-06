#include "allegro_dat.hpp"
#include "allegro_dat_reader.cpp" // MVP include; later split into headers properly
#include "bmp_export.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>

using namespace allegro_dat;
namespace fs = std::filesystem;

static std::string fourcc_str(uint32_t t) {
    std::string s(4,' ');
    s[0]=char((t>>24)&0xFF); s[1]=char((t>>16)&0xFF);
    s[2]=char((t>>8 )&0xFF); s[3]=char((t>>0 )&0xFF);
    return s;
}

static std::string get_prop_name(const Object& o) {
    auto NAME = fourcc('N','A','M','E');
    for (auto& p: o.props) if (p.id == NAME) return p.value;
    return "";
}

static void ensure_dir(const fs::path& p) { fs::create_directories(p); }

static void dump_raw(const fs::path& out, const std::vector<uint8_t>& bytes) {
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
}

static void walk_list(const std::vector<Object>& objs, const std::string& prefix) {
    for (size_t i=0;i<objs.size();++i) {
        const auto& o = objs[i];
        std::string name = get_prop_name(o);
        std::cout << prefix << i << " type=" << fourcc_str(o.type)
                  << " name=" << (name.empty() ? "<none>" : name)
                  << " props=" << o.props.size()
                  << (o.type==fourcc('F','I','L','E') ? " (FILE)" : "")
                  << "\n";
        if (!o.children.empty()) walk_list(o.children, prefix + "  ");
    }
}

static void extract_objects(const std::vector<Object>& objs,
                            const fs::path& out_root,
                            const std::string& id_prefix,
                            std::vector<std::tuple<std::string,std::string,std::string>>& manifest_assets)
{
    for (size_t idx=0; idx<objs.size(); ++idx) {
        const auto& o = objs[idx];
        std::string base = get_prop_name(o);
        if (base.empty()) base = "obj_" + std::to_string(idx);

        std::string type = fourcc_str(o.type);

        std::string id = id_prefix.empty() ? base : (id_prefix + "/" + base);

        if (!o.children.empty()) {
            extract_objects(o.children, out_root, id, manifest_assets);
            continue;
        }

        // Decide export
        if (o.type == fourcc('B','M','P',' ') && o.data.size() >= 6) {
            // decode minimal subset: 24/32/-32 only (leave 8/15/16 for later)
            // payload: bits(u16), w(u16), h(u16), then pixel data
            auto rd_u16be = [&](size_t off)->uint16_t{
                return (uint16_t(o.data[off])<<8) | uint16_t(o.data[off+1]);
            };
            int bits = (int16_t)rd_u16be(0);
            int w = rd_u16be(2);
            int h = rd_u16be(4);
            size_t pix_off = 6;

            if ((bits == 24 || bits == 32 || bits == -32) && w>0 && h>0) {
                // Convert to RGB for BMP writer
                std::vector<uint8_t> rgb((size_t)w*(size_t)h*3);
                size_t in_stride = (bits==24) ? 3 : 4;
                size_t needed = pix_off + (size_t)w*(size_t)h*in_stride;
                if (o.data.size() >= needed) {
                    const uint8_t* src = o.data.data() + pix_off;
                    for (int i=0;i<w*h;i++) {
                        uint8_t r = src[i*in_stride + 0];
                        uint8_t g = src[i*in_stride + 1];
                        uint8_t b = src[i*in_stride + 2];
                        rgb[i*3+0]=r; rgb[i*3+1]=g; rgb[i*3+2]=b;
                    }
                    fs::path rel = fs::path("images") / (id + ".bmp");
                    fs::path out = out_root / rel;
                    ensure_dir(out.parent_path());
                    bmp_export::write_bmp_24(out, w, h, rgb);
                    manifest_assets.emplace_back(id, "image", rel.generic_string());
                    continue;
                }
            }
        }

        // default: raw dump
        fs::path rel = fs::path("raw") / (id + "." + type + ".bin");
        fs::path out = out_root / rel;
        ensure_dir(out.parent_path());
        dump_raw(out, o.data);
        manifest_assets.emplace_back(id, "raw", rel.generic_string());
    }
}

static void write_manifest(const fs::path& out_root, const std::string& root_rel,
                           const std::vector<std::tuple<std::string,std::string,std::string>>& assets)
{
    fs::path mpath = out_root / "manifest.json";
    std::ofstream f(mpath);
    f << "{\n  \"version\": 1,\n  \"root\": \"" << root_rel << "\",\n  \"assets\": [\n";
    for (size_t i=0;i<assets.size();++i) {
        auto [id, type, path] = assets[i];
        f << "    { \"id\": \"" << id << "\", \"type\": \"" << type << "\", \"path\": \"" << path << "\" }";
        f << (i+1<assets.size()? ",\n":"\n");
    }
    f << "  ]\n}\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n"
                  << "  eador_dat list <file.dat>\n"
                  << "  eador_dat extract <file.dat> <out_dir>\n";
        return 2;
    }

    std::string cmd = argv[1];
    fs::path dat = argv[2];

    try {
        auto df = load_dat_file(dat);

        if (cmd == "list") {
            walk_list(df.root_objects, "");
            return 0;
        }

        if (cmd == "extract") {
            if (argc < 4) throw ReaderError("Missing out_dir");
            fs::path out = argv[3];
            ensure_dir(out);

            std::vector<std::tuple<std::string,std::string,std::string>> assets;
            extract_objects(df.root_objects, out, "", assets);

            // manifest root is the out directory itself; your Godot VFS can treat it as repo-relative.
            write_manifest(out, out.generic_string(), assets);
            std::cout << "Extracted " << assets.size() << " assets into " << out << "\n";
            return 0;
        }

        throw ReaderError("Unknown command: " + cmd);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
