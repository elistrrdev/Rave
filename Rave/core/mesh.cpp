#include "mesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
    struct P3
    {
        float x, y, z;
    };

    const char* skipSpace(const char* p, const char* end)
    {
        while (p < end && (*p == ' ' || *p == '\t'))
            ++p;
        return p;
    }

    int resolve(long index, size_t count)
    {
        if (index > 0)
            return (int)index - 1;
        if (index < 0)
            return (int)count + (int)index;
        return -1;
    }

    float wrap(float t)
    {
        t = t - std::floor(t);
        return t < 0.f ? 0.f : (t >= 1.f ? 0.9999f : t);
    }
}

bool mesh::parseObj(const std::string& text, Mesh& out)
{
    out.verts.clear();
    std::vector<P3> pos;
    std::vector<std::pair<float, float>> uvs;
    const char* p = text.data();
    const char* end = p + text.size();

    while (p < end)
    {
        const char* line = p;
        while (p < end && *p != '\n')
            ++p;
        const char* lineEnd = p;
        if (p < end)
            ++p;
        line = skipSpace(line, lineEnd);
        if (lineEnd - line < 2)
            continue;

        if (line[0] == 'v' && line[1] == ' ')
        {
            char* c = (char*)line + 2;
            P3 v;
            v.x = strtof(c, &c);
            v.y = strtof(c, &c);
            v.z = strtof(c, &c);
            pos.push_back(v);
        }
        else if (line[0] == 'v' && line[1] == 't' && lineEnd - line > 2 && line[2] == ' ')
        {
            char* c = (char*)line + 3;
            float u = strtof(c, &c);
            float v = strtof(c, &c);
            uvs.push_back({ u, v });
        }
        else if (line[0] == 'f' && line[1] == ' ')
        {
            std::vector<Vertex> poly;
            const char* c = line + 2;
            while (c < lineEnd)
            {
                c = skipSpace(c, lineEnd);
                if (c >= lineEnd || *c == '\r')
                    break;
                char* next = nullptr;
                long vi = strtol(c, &next, 10);
                if (next == c)
                    break;
                c = next;
                long ti = 0;
                if (c < lineEnd && *c == '/')
                {
                    ++c;
                    if (c < lineEnd && *c != '/')
                    {
                        ti = strtol(c, &next, 10);
                        c = next;
                    }
                    if (c < lineEnd && *c == '/')
                    {
                        ++c;
                        strtol(c, &next, 10);
                        c = next;
                    }
                }
                while (c < lineEnd && *c != ' ' && *c != '\t')
                    ++c;
                int a = resolve(vi, pos.size());
                if (a < 0 || a >= (int)pos.size())
                    continue;
                int t = resolve(ti, uvs.size());
                Vertex vert{ pos[a].x, pos[a].y, pos[a].z, 0.f, 0.f };
                if (t >= 0 && t < (int)uvs.size())
                {
                    vert.u = uvs[t].first;
                    vert.v = uvs[t].second;
                }
                poly.push_back(vert);
            }
            for (size_t i = 2; i < poly.size(); ++i)
            {
                out.verts.push_back(poly[0]);
                out.verts.push_back(poly[i - 1]);
                out.verts.push_back(poly[i]);
            }
        }
    }
    return !out.empty();
}

bool mesh::parseRbx(const std::string& data, Mesh& out)
{
    out.verts.clear();
    const char* tag = "version 2.00";
    if (data.size() < 25 || data.compare(0, 12, tag) != 0)
        return false;
    size_t p = data.find('\n');
    if (p == std::string::npos)
        return false;
    ++p;
    const uint8_t* b = (const uint8_t*)data.data();
    uint16_t headerSize = (uint16_t)(b[p] | (b[p + 1] << 8));
    uint8_t vertexSize = b[p + 2];
    uint8_t faceSize = b[p + 3];
    uint32_t nv = 0, nf = 0;
    memcpy(&nv, b + p + 4, 4);
    memcpy(&nf, b + p + 8, 4);
    size_t vo = p + headerSize;
    size_t fo = vo + (size_t)nv * vertexSize;
    if (vertexSize < 36 || faceSize < 12 || fo + (size_t)nf * faceSize > data.size())
        return false;
    out.verts.reserve((size_t)nf * 3);
    for (uint32_t f = 0; f < nf; ++f)
    {
        uint32_t idx[3];
        memcpy(idx, b + fo + (size_t)f * faceSize, 12);
        for (uint32_t i : idx)
        {
            if (i >= nv)
                return false;
            float v[9];
            memcpy(v, b + vo + (size_t)i * vertexSize, 36);
            out.verts.push_back({ v[0], v[1], v[2], v[6], 1.f - v[7] });
        }
    }
    return !out.empty();
}

bool mesh::parse(const std::string& data, Mesh& out)
{
    if (data.compare(0, 8, "version ") == 0)
        return parseRbx(data, out);
    return parseObj(data, out);
}

img::Rgba mesh::render(const Mesh& m, const img::Rgba& texture, unsigned w, unsigned h)
{
    img::Rgba result;
    if (m.empty() || texture.empty() || !w || !h)
        return result;

    const unsigned ss = 2;
    const unsigned W = w * ss, H = h * ss;

    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    for (const Vertex& v : m.verts)
    {
        const float c[3] = { v.x, v.y, v.z };
        for (int k = 0; k < 3; ++k)
        {
            mn[k] = std::min(mn[k], c[k]);
            mx[k] = std::max(mx[k], c[k]);
        }
    }
    float ext[3] = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };
    int length = (int)(std::max_element(ext, ext + 3) - ext);
    int up = length == 1 ? (ext[0] >= ext[2] ? 0 : 2) : 1;
    int depth = 3 - length - up;
    float center[3] = { (mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2 };

    const float yaw = 0.50f, pitch = 0.32f;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);

    std::vector<P3> view(m.verts.size());
    float vmin[2] = { 1e30f, 1e30f }, vmax[2] = { -1e30f, -1e30f };
    for (size_t i = 0; i < m.verts.size(); ++i)
    {
        const Vertex& v = m.verts[i];
        const float c[3] = { v.x - center[0], v.y - center[1], v.z - center[2] };
        float a = c[length], b = c[up], d = c[depth];
        float a1 = a * cy - d * sy;
        float d1 = a * sy + d * cy;
        float b2 = b * cp + d1 * sp;
        float d2 = -b * sp + d1 * cp;
        view[i] = { a1, b2, d2 };
        vmin[0] = std::min(vmin[0], a1);
        vmax[0] = std::max(vmax[0], a1);
        vmin[1] = std::min(vmin[1], b2);
        vmax[1] = std::max(vmax[1], b2);
    }

    float spanX = std::max(vmax[0] - vmin[0], 1e-4f), spanY = std::max(vmax[1] - vmin[1], 1e-4f);
    float scale = std::min(W * 0.90f / spanX, H * 0.84f / spanY);
    float offX = W / 2.f - (vmin[0] + vmax[0]) / 2 * scale;
    float offY = H / 2.f + (vmin[1] + vmax[1]) / 2 * scale;

    std::vector<float> zbuf((size_t)W * H, -1e30f);
    img::Rgba big;
    big.w = W;
    big.h = H;
    big.px.assign((size_t)W * H * 4, 0);

    float lx = -0.35f, ly = 0.60f, lz = 0.72f;
    float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll;
    ly /= ll;
    lz /= ll;

    for (size_t t = 0; t + 2 < view.size(); t += 3)
    {
        const P3& p0 = view[t];
        const P3& p1 = view[t + 1];
        const P3& p2 = view[t + 2];
        float ex = p1.x - p0.x, ey = p1.y - p0.y, ez = p1.z - p0.z;
        float fx = p2.x - p0.x, fy = p2.y - p0.y, fz = p2.z - p0.z;
        float nx = ey * fz - ez * fy, ny = ez * fx - ex * fz, nz = ex * fy - ey * fx;
        float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-12f)
            continue;
        nx /= nl;
        ny /= nl;
        nz /= nl;
        if (nz < 0)
        {
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        float diffuse = std::max(0.f, nx * lx + ny * ly + nz * lz);
        float shade = 0.40f + 0.60f * diffuse;

        float sx[3] = { p0.x * scale + offX, p1.x * scale + offX, p2.x * scale + offX };
        float syy[3] = { offY - p0.y * scale, offY - p1.y * scale, offY - p2.y * scale };
        float area = (sx[1] - sx[0]) * (syy[2] - syy[0]) - (sx[2] - sx[0]) * (syy[1] - syy[0]);
        if (std::fabs(area) < 1e-6f)
            continue;

        int x0 = std::max(0, (int)std::floor(std::min({ sx[0], sx[1], sx[2] })));
        int x1 = std::min((int)W - 1, (int)std::ceil(std::max({ sx[0], sx[1], sx[2] })));
        int y0 = std::max(0, (int)std::floor(std::min({ syy[0], syy[1], syy[2] })));
        int y1 = std::min((int)H - 1, (int)std::ceil(std::max({ syy[0], syy[1], syy[2] })));

        const Vertex& v0 = m.verts[t];
        const Vertex& v1 = m.verts[t + 1];
        const Vertex& v2 = m.verts[t + 2];

        for (int y = y0; y <= y1; ++y)
        {
            float py = y + 0.5f;
            for (int x = x0; x <= x1; ++x)
            {
                float px = x + 0.5f;
                float w0 = ((sx[1] - px) * (syy[2] - py) - (sx[2] - px) * (syy[1] - py)) / area;
                float w1 = ((sx[2] - px) * (syy[0] - py) - (sx[0] - px) * (syy[2] - py)) / area;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                float z = w0 * p0.z + w1 * p1.z + w2 * p2.z;
                size_t idx = (size_t)y * W + x;
                if (z <= zbuf[idx])
                    continue;
                zbuf[idx] = z;
                float u = wrap(w0 * v0.u + w1 * v1.u + w2 * v2.u);
                float v = wrap(w0 * v0.v + w1 * v1.v + w2 * v2.v);
                unsigned tx = std::min(texture.w - 1, (unsigned)(u * texture.w));
                unsigned ty = std::min(texture.h - 1, (unsigned)((1.f - v) * texture.h));
                const uint8_t* c = &texture.px[((size_t)ty * texture.w + tx) * 4];
                uint8_t* o = &big.px[idx * 4];
                o[0] = (uint8_t)std::min(255.f, c[0] * shade + 0.5f);
                o[1] = (uint8_t)std::min(255.f, c[1] * shade + 0.5f);
                o[2] = (uint8_t)std::min(255.f, c[2] * shade + 0.5f);
                o[3] = 255;
            }
        }
    }

    return img::resize(big, w, h);
}

namespace
{
    void put32(std::string& out, uint32_t v)
    {
        out.append((const char*)&v, 4);
    }

    void header(std::string& out, uint8_t vertexSize, uint32_t nv, uint32_t nf)
    {
        out = "version 2.00\n";
        uint16_t size = 12;
        out.append((const char*)&size, 2);
        out += (char)vertexSize;
        out += (char)12;
        put32(out, nv);
        put32(out, nf);
    }

    void sample(const img::Rgba& t, float u, float v, uint8_t out[3])
    {
        float fx = (u - std::floor(u)) * t.w - 0.5f;
        float fy = (v - std::floor(v)) * t.h - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float ax = fx - x0, ay = fy - y0;
        auto at = [&](int x, int y, int k) {
            x = ((x % (int)t.w) + (int)t.w) % (int)t.w;
            y = ((y % (int)t.h) + (int)t.h) % (int)t.h;
            return (float)t.px[((size_t)y * t.w + x) * 4 + k];
        };
        for (int k = 0; k < 3; ++k)
        {
            float top = at(x0, y0, k) * (1 - ax) + at(x0 + 1, y0, k) * ax;
            float bottom = at(x0, y0 + 1, k) * (1 - ax) + at(x0 + 1, y0 + 1, k) * ax;
            out[k] = (uint8_t)std::clamp(top * (1 - ay) + bottom * ay + 0.5f, 0.f, 255.f);
        }
    }
}

bool mesh::paint(const std::string& data, const img::Rgba& texture, std::string& out)
{
    out.clear();
    if (texture.empty() || data.size() < 25 || data.compare(0, 12, "version 2.00") != 0)
        return false;
    size_t p = data.find('\n');
    if (p == std::string::npos)
        return false;
    ++p;
    const uint8_t* b = (const uint8_t*)data.data();
    uint16_t headerSize = (uint16_t)(b[p] | (b[p + 1] << 8));
    uint8_t vertexSize = b[p + 2];
    uint8_t faceSize = b[p + 3];
    uint32_t nv = 0, nf = 0;
    memcpy(&nv, b + p + 4, 4);
    memcpy(&nf, b + p + 8, 4);
    size_t vo = p + headerSize;
    size_t fo = vo + (size_t)nv * vertexSize;
    if (vertexSize < 36 || faceSize < 12 || fo + (size_t)nf * faceSize > data.size())
        return false;

    const unsigned side = 256;
    img::Rgba small = texture.w > side || texture.h > side ? img::resize(texture, std::min(side, texture.w), std::min(side, texture.h)) : texture;
    header(out, 40, nv, nf);
    out.reserve(out.size() + (size_t)nv * 40 + (size_t)nf * 12);
    for (uint32_t i = 0; i < nv; ++i)
    {
        float v[9];
        memcpy(v, b + vo + (size_t)i * vertexSize, 36);
        out.append((const char*)v, 36);
        uint8_t c[4] = { 0, 0, 0, 255 };
        sample(small, v[6], v[7], c);
        out.append((const char*)c, 4);
    }
    for (uint32_t f = 0; f < nf; ++f)
    {
        uint32_t idx[3];
        memcpy(idx, b + fo + (size_t)f * faceSize, 12);
        for (uint32_t k : idx)
            if (k >= nv)
                return false;
        out.append((const char*)idx, 12);
    }
    return true;
}

std::string mesh::hidden(const float size[3])
{
    const float t = 0.001f;
    const float pts[12][3] = {
        { -size[0], 0, 0 }, { size[0], 0, 0 }, { 0, 0, 0 },
        { 0, -size[1], 0 }, { 0, size[1], 0 }, { 0, 0, 0 },
        { 0, 0, -size[2] }, { 0, 0, size[2] }, { 0, 0, 0 },
        { 0, 0, 0 }, { t, 0, 0 }, { 0, t, 0 },
    };
    std::string out;
    header(out, 36, 12, 4);
    for (const auto& q : pts)
    {
        const float v[9] = { q[0], q[1], q[2], 0.f, 0.f, 1.f, 0.f, 0.f, 0.f };
        out.append((const char*)v, 36);
    }
    for (uint32_t i = 0; i < 12; ++i)
        put32(out, i);
    return out;
}
