#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace img
{
    struct Rgba
    {
        std::vector<uint8_t> px;
        unsigned w = 0, h = 0;
        bool empty() const { return !w || !h || px.size() < (size_t)w * h * 4; }
    };

    enum class Kind { Unknown, Png, Ktx2 };

    struct Probe
    {
        Kind kind = Kind::Unknown;
        unsigned w = 0, h = 0;
    };

    Probe probe(const void* data, size_t size);
    bool decode(const void* data, size_t size, Rgba& out);
    bool decodePng(const void* data, size_t size, Rgba& out);
    bool decodeKtx2(const void* data, size_t size, Rgba& out, unsigned minSide = 0);
    bool encodePng(const Rgba& image, std::string& out);

    Rgba resize(const Rgba& src, unsigned w, unsigned h);
    Rgba crop(const Rgba& src, unsigned x, unsigned y, unsigned w, unsigned h);
    Rgba fit(const Rgba& src, unsigned w, unsigned h, unsigned pad);
}
