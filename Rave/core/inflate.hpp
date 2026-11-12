#pragma once
#include <cstddef>
#include <cstdint>

namespace inflate
{
    bool raw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);
}
