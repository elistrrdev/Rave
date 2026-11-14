#include "image.hpp"
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#pragma comment(lib, "shlwapi.lib")

extern "C" size_t ZSTD_decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize);
extern "C" unsigned ZSTD_isError(size_t code);

using Microsoft::WRL::ComPtr;

namespace
{
    const uint8_t kKtx2Id[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
    const uint8_t kPngId[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

    uint32_t rd32(const uint8_t* p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    uint64_t rd64(const uint8_t* p)
    {
        return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
    }

    uint32_t be32(const uint8_t* p)
    {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }

    ComPtr<IWICImagingFactory> factory()
    {
        ComPtr<IWICImagingFactory> f;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }

    void color565(uint16_t c, uint8_t out[3])
    {
        out[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
        out[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
        out[2] = (uint8_t)((c & 31) * 255 / 31);
    }

    void colorBlock(const uint8_t* b, bool forceFour, uint8_t pal[4][4])
    {
        uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8));
        uint16_t c1 = (uint16_t)(b[2] | (b[3] << 8));
        color565(c0, pal[0]);
        color565(c1, pal[1]);
        pal[0][3] = pal[1][3] = 255;
        if (c0 > c1 || forceFour)
        {
            for (int k = 0; k < 3; ++k)
            {
                pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
                pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
            }
            pal[2][3] = pal[3][3] = 255;
        }
        else
        {
            for (int k = 0; k < 3; ++k)
            {
                pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
                pal[3][k] = 0;
            }
            pal[2][3] = 255;
            pal[3][3] = 0;
        }
    }

    bool decodeBc(const uint8_t* data, size_t size, unsigned w, unsigned h, bool bc3, img::Rgba& out)
    {
        const unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
        const size_t blockSize = bc3 ? 16 : 8;
        if (size < (size_t)bw * bh * blockSize)
            return false;
        out.w = w;
        out.h = h;
        out.px.assign((size_t)w * h * 4, 0);
        const uint8_t* b = data;
        for (unsigned by = 0; by < bh; ++by)
        {
            for (unsigned bx = 0; bx < bw; ++bx, b += blockSize)
            {
                uint8_t alpha[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
                uint64_t alphaBits = 0;
                const uint8_t* cb = b;
                if (bc3)
                {
                    uint8_t a0 = b[0], a1 = b[1];
                    alpha[0] = a0;
                    alpha[1] = a1;
                    if (a0 > a1)
                        for (int i = 0; i < 6; ++i)
                            alpha[2 + i] = (uint8_t)(((6 - i) * a0 + (1 + i) * a1) / 7);
                    else
                    {
                        for (int i = 0; i < 4; ++i)
                            alpha[2 + i] = (uint8_t)(((4 - i) * a0 + (1 + i) * a1) / 5);
                        alpha[6] = 0;
                        alpha[7] = 255;
                    }
                    for (int i = 0; i < 6; ++i)
                        alphaBits |= (uint64_t)b[2 + i] << (8 * i);
                    cb = b + 8;
                }
                uint8_t pal[4][4];
                colorBlock(cb, bc3, pal);
                uint32_t bits = rd32(cb + 4);
                for (unsigned i = 0; i < 16; ++i)
                {
                    unsigned x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x >= w || y >= h)
                        continue;
                    const uint8_t* c = pal[(bits >> (2 * i)) & 3];
                    uint8_t* p = &out.px[((size_t)y * w + x) * 4];
                    p[0] = c[0];
                    p[1] = c[1];
                    p[2] = c[2];
                    p[3] = bc3 ? alpha[(alphaBits >> (3 * i)) & 7] : c[3];
                }
            }
        }
        return true;
    }
}

img::Probe img::probe(const void* data, size_t size)
{
    Probe p;
    const uint8_t* b = (const uint8_t*)data;
    if (size >= 48 && memcmp(b, kKtx2Id, 12) == 0)
    {
        p.kind = Kind::Ktx2;
        p.w = rd32(b + 20);
        p.h = rd32(b + 24);
    }
    else if (size >= 24 && memcmp(b, kPngId, 8) == 0)
    {
        p.kind = Kind::Png;
        p.w = be32(b + 16);
        p.h = be32(b + 20);
    }
    return p;
}

bool img::decode(const void* data, size_t size, Rgba& out)
{
    Probe p = probe(data, size);
    if (p.kind == Kind::Ktx2)
        return decodeKtx2(data, size, out);
    return decodePng(data, size, out);
}

bool img::decodePng(const void* data, size_t size, Rgba& out)
{
    if (!data || !size || size > 0x7FFFFFFF)
        return false;
    ComPtr<IWICImagingFactory> f = factory();
    if (!f)
        return false;
    ComPtr<IWICStream> stream;
    if (FAILED(f->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size)))
        return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(f->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return false;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(f->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (!w || !h || w > 8192 || h > 8192)
        return false;
    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * h * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, w * 4, (UINT)out.px.size(), out.px.data()));
}

bool img::decodeKtx2(const void* data, size_t size, Rgba& out, unsigned minSide)
{
    const uint8_t* b = (const uint8_t*)data;
    if (size < 80 || memcmp(b, kKtx2Id, 12) != 0)
        return false;
    uint32_t vk = rd32(b + 12);
    uint32_t w = rd32(b + 20), h = rd32(b + 24);
    uint32_t levels = std::max(1u, rd32(b + 40));
    uint32_t scheme = rd32(b + 44);
    if (!w || !h || w > 8192 || h > 8192 || levels > 16 || (scheme != 0 && scheme != 2))
        return false;
    if (size < 80 + (size_t)levels * 24)
        return false;

    uint32_t pick = 0;
    if (minSide)
        for (uint32_t i = 0; i < levels; ++i)
            if (std::max(1u, w >> i) >= minSide && std::max(1u, h >> i) >= minSide)
                pick = i;

    const uint8_t* li = b + 80 + (size_t)pick * 24;
    uint64_t off = rd64(li), len = rd64(li + 8), raw = rd64(li + 16);
    if (off > size || len > size - off || raw > 64ull * 1024 * 1024)
        return false;
    unsigned lw = std::max(1u, w >> pick), lh = std::max(1u, h >> pick);

    std::vector<uint8_t> level;
    const uint8_t* src = b + off;
    size_t srcLen = (size_t)len;
    if (scheme == 2)
    {
        level.resize((size_t)raw);
        size_t got = ZSTD_decompress(level.data(), level.size(), src, srcLen);
        if (ZSTD_isError(got))
            return false;
        level.resize(got);
        src = level.data();
        srcLen = level.size();
    }

    switch (vk)
    {
    case 131: case 132: case 133: case 134:
        return decodeBc(src, srcLen, lw, lh, false, out);
    case 137: case 138:
        return decodeBc(src, srcLen, lw, lh, true, out);
    case 37: case 43:
        if (srcLen < (size_t)lw * lh * 4)
            return false;
        out.w = lw;
        out.h = lh;
        out.px.assign(src, src + (size_t)lw * lh * 4);
        return true;
    default:
        return false;
    }
}

bool img::encodePng(const Rgba& image, std::string& out)
{
    if (image.empty())
        return false;
    ComPtr<IWICImagingFactory> f = factory();
    if (!f)
        return false;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    if (!stream)
        return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)))
        return false;
    if (FAILED(frame->SetSize(image.w, image.h)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (FAILED(frame->SetPixelFormat(&format)))
        return false;
    std::vector<uint8_t> pixels = image.px;
    if (format != GUID_WICPixelFormat32bppRGBA)
    {
        if (format != GUID_WICPixelFormat32bppBGRA)
            return false;
        for (size_t i = 0; i < pixels.size(); i += 4)
            std::swap(pixels[i], pixels[i + 2]);
    }
    if (FAILED(frame->WritePixels(image.h, image.w * 4, (UINT)pixels.size(), pixels.data())) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return false;

    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)))
        return false;
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    out.resize((size_t)stat.cbSize.QuadPart);
    ULONG read = 0;
    return SUCCEEDED(stream->Read(out.data(), (ULONG)out.size(), &read)) && read == out.size();
}

img::Rgba img::resize(const Rgba& src, unsigned w, unsigned h)
{
    Rgba out;
    if (src.empty() || !w || !h)
        return out;
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h * 4, 0);
    const unsigned kx = std::clamp((src.w + w - 1) / w, 1u, 8u);
    const unsigned ky = std::clamp((src.h + h - 1) / h, 1u, 8u);
    for (unsigned y = 0; y < h; ++y)
    {
        for (unsigned x = 0; x < w; ++x)
        {
            double r = 0, g = 0, b = 0, a = 0;
            for (unsigned sy = 0; sy < ky; ++sy)
            {
                unsigned py = std::min(src.h - 1, (unsigned)(((y + (sy + 0.5) / ky) * src.h) / h));
                for (unsigned sx = 0; sx < kx; ++sx)
                {
                    unsigned px = std::min(src.w - 1, (unsigned)(((x + (sx + 0.5) / kx) * src.w) / w));
                    const uint8_t* p = &src.px[((size_t)py * src.w + px) * 4];
                    double al = p[3];
                    r += p[0] * al;
                    g += p[1] * al;
                    b += p[2] * al;
                    a += al;
                }
            }
            uint8_t* o = &out.px[((size_t)y * w + x) * 4];
            const double n = (double)kx * ky;
            if (a > 0)
            {
                o[0] = (uint8_t)std::clamp(r / a + 0.5, 0.0, 255.0);
                o[1] = (uint8_t)std::clamp(g / a + 0.5, 0.0, 255.0);
                o[2] = (uint8_t)std::clamp(b / a + 0.5, 0.0, 255.0);
            }
            o[3] = (uint8_t)std::clamp(a / n + 0.5, 0.0, 255.0);
        }
    }
    return out;
}

img::Rgba img::crop(const Rgba& src, unsigned x, unsigned y, unsigned w, unsigned h)
{
    Rgba out;
    if (src.empty() || x >= src.w || y >= src.h)
        return out;
    w = std::min(w, src.w - x);
    h = std::min(h, src.h - y);
    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * h * 4);
    for (unsigned row = 0; row < h; ++row)
        memcpy(&out.px[(size_t)row * w * 4], &src.px[(((size_t)(y + row)) * src.w + x) * 4], (size_t)w * 4);
    return out;
}

img::Rgba img::fit(const Rgba& src, unsigned w, unsigned h, unsigned pad)
{
    Rgba out;
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h * 4, 0);
    if (src.empty() || w <= pad * 2 || h <= pad * 2)
        return out;
    double scale = std::min((double)(w - pad * 2) / src.w, (double)(h - pad * 2) / src.h);
    unsigned tw = std::max(1u, (unsigned)(src.w * scale + 0.5));
    unsigned th = std::max(1u, (unsigned)(src.h * scale + 0.5));
    Rgba scaled = resize(src, tw, th);
    unsigned ox = (w - tw) / 2, oy = (h - th) / 2;
    for (unsigned y = 0; y < th; ++y)
        memcpy(&out.px[(((size_t)(oy + y)) * w + ox) * 4], &scaled.px[(size_t)y * tw * 4], (size_t)tw * 4);
    return out;
}
