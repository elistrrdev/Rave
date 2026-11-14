#pragma once
#include "image.hpp"
#include <string>
#include <vector>

namespace mesh
{
    struct Vertex
    {
        float x, y, z, u, v;
    };

    struct Mesh
    {
        std::vector<Vertex> verts;
        bool empty() const { return verts.size() < 3; }
    };

    bool parseObj(const std::string& text, Mesh& out);
    img::Rgba render(const Mesh& m, const img::Rgba& texture, unsigned w, unsigned h);
}
