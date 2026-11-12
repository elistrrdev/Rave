#include "inflate.hpp"
#include <cstring>

namespace
{
    constexpr int kMaxBits = 15;
    constexpr int kFastBits = 10;
    constexpr int kFastSize = 1 << kFastBits;

    struct Huffman
    {
        uint16_t count[kMaxBits + 1];
        uint16_t symbol[288];
        uint16_t fast[kFastSize];
    };

    struct Reader
    {
        const uint8_t* p;
        const uint8_t* end;
        uint64_t buf = 0;
        int cnt = 0;
        size_t pad = 0;

        void fill()
        {
            while (cnt <= 56)
            {
                uint64_t byte = 0;
                if (p < end)
                    byte = *p++;
                else
                    ++pad;
                buf |= byte << cnt;
                cnt += 8;
            }
        }

        uint32_t bits(int n)
        {
            if (cnt < n)
                fill();
            uint32_t v = (uint32_t)(buf & ((1ull << n) - 1));
            buf >>= n;
            cnt -= n;
            return v;
        }

        bool overrun() const
        {
            return pad * 8 > (size_t)cnt;
        }
    };

    bool build(Huffman& h, const uint8_t* lengths, int n)
    {
        memset(h.count, 0, sizeof(h.count));
        for (int i = 0; i < n; ++i)
            h.count[lengths[i]]++;
        h.count[0] = 0;

        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            left <<= 1;
            left -= h.count[len];
            if (left < 0)
                return false;
        }

        uint16_t offs[kMaxBits + 2]{};
        for (int len = 1; len < kMaxBits; ++len)
            offs[len + 1] = offs[len] + h.count[len];
        for (int sym = 0; sym < n; ++sym)
            if (lengths[sym])
                h.symbol[offs[lengths[sym]]++] = (uint16_t)sym;

        memset(h.fast, 0, sizeof(h.fast));
        int code = 0, k = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            for (int i = 0; i < h.count[len]; ++i)
            {
                int sym = h.symbol[k++];
                if (len <= kFastBits)
                {
                    int rev = 0;
                    for (int b = 0; b < len; ++b)
                        rev |= ((code >> b) & 1) << (len - 1 - b);
                    for (int fill = rev; fill < kFastSize; fill += 1 << len)
                        h.fast[fill] = (uint16_t)((len << 9) | sym);
                }
                ++code;
            }
            code <<= 1;
        }
        return true;
    }

    int decode(Reader& r, const Huffman& h)
    {
        if (r.cnt < kMaxBits)
            r.fill();

        uint16_t e = h.fast[r.buf & (kFastSize - 1)];
        if (e)
        {
            int len = e >> 9;
            r.buf >>= len;
            r.cnt -= len;
            return e & 511;
        }

        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            code |= (int)(r.buf & 1);
            r.buf >>= 1;
            r.cnt--;
            int c = h.count[len];
            if (code - c < first)
                return h.symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    struct Fixed
    {
        Huffman len, dist;
        Fixed()
        {
            uint8_t lengths[288];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < 288; ++i) lengths[i] = 8;
            build(len, lengths, 288);
            for (i = 0; i < 30; ++i) lengths[i] = 5;
            build(dist, lengths, 30);
        }
    };

    bool codes(Reader& r, const Huffman& lencode, const Huffman& distcode, uint8_t* out, size_t outLen, size_t& pos)
    {
        for (;;)
        {
            int sym = decode(r, lencode);
            if (sym < 0)
                return false;
            if (sym < 256)
            {
                if (pos >= outLen)
                    return false;
                out[pos++] = (uint8_t)sym;
                continue;
            }
            if (sym == 256)
                return true;

            sym -= 257;
            if (sym >= 29)
                return false;
            size_t len = kLenBase[sym] + r.bits(kLenExtra[sym]);

            int dsym = decode(r, distcode);
            if (dsym < 0 || dsym >= 30)
                return false;
            size_t dist = kDistBase[dsym] + r.bits(kDistExtra[dsym]);

            if (dist > pos || len > outLen - pos)
                return false;

            uint8_t* dst = out + pos;
            const uint8_t* src = dst - dist;
            if (dist >= len)
                memcpy(dst, src, len);
            else
                for (size_t i = 0; i < len; ++i)
                    dst[i] = src[i];
            pos += len;
        }
    }

    bool stored(Reader& r, uint8_t* out, size_t outLen, size_t& pos)
    {
        int drop = r.cnt & 7;
        r.buf >>= drop;
        r.cnt -= drop;

        uint32_t len = r.bits(16);
        uint32_t nlen = r.bits(16);
        if (len != (~nlen & 0xFFFF))
            return false;
        if (len > outLen - pos)
            return false;

        while (len > 0 && r.cnt >= 8)
        {
            out[pos++] = (uint8_t)r.bits(8);
            --len;
        }
        if (len == 0)
            return true;
        if (r.pad || (size_t)(r.end - r.p) < len)
            return false;
        memcpy(out + pos, r.p, len);
        r.p += len;
        pos += len;
        return true;
    }

    bool dynamic(Reader& r, uint8_t* out, size_t outLen, size_t& pos)
    {
        static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

        int nlen = (int)r.bits(5) + 257;
        int ndist = (int)r.bits(5) + 1;
        int ncode = (int)r.bits(4) + 4;
        if (nlen > 286 || ndist > 30)
            return false;

        uint8_t lengths[320]{};
        int index = 0;
        for (; index < ncode; ++index)
            lengths[order[index]] = (uint8_t)r.bits(3);
        for (; index < 19; ++index)
            lengths[order[index]] = 0;

        Huffman lencode, distcode;
        if (!build(lencode, lengths, 19))
            return false;

        index = 0;
        while (index < nlen + ndist)
        {
            int sym = decode(r, lencode);
            if (sym < 0)
                return false;
            if (sym < 16)
            {
                lengths[index++] = (uint8_t)sym;
                continue;
            }

            uint8_t len = 0;
            int repeat;
            if (sym == 16)
            {
                if (index == 0)
                    return false;
                len = lengths[index - 1];
                repeat = 3 + (int)r.bits(2);
            }
            else if (sym == 17)
                repeat = 3 + (int)r.bits(3);
            else
                repeat = 11 + (int)r.bits(7);

            if (index + repeat > nlen + ndist)
                return false;
            while (repeat--)
                lengths[index++] = len;
        }

        if (lengths[256] == 0)
            return false;
        if (!build(lencode, lengths, nlen))
            return false;
        if (!build(distcode, lengths + nlen, ndist))
            return false;

        return codes(r, lencode, distcode, out, outLen, pos);
    }
}

bool inflate::raw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    static const Fixed fixed;

    Reader r{ src, src + srcLen };
    size_t pos = 0;
    bool last = false;
    while (!last)
    {
        last = r.bits(1) != 0;
        uint32_t type = r.bits(2);
        bool ok;
        if (type == 0)
            ok = stored(r, dst, dstLen, pos);
        else if (type == 1)
            ok = codes(r, fixed.len, fixed.dist, dst, dstLen, pos);
        else if (type == 2)
            ok = dynamic(r, dst, dstLen, pos);
        else
            ok = false;

        if (!ok || r.overrun())
            return false;
    }
    return pos == dstLen;
}
