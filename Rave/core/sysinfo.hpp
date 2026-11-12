#pragma once
#include <string>

namespace sysinfo
{
    struct Specs
    {
        std::string cpu;
        std::string gpu;
        std::string ram;
        std::string display;
        std::string hwid;
    };

    const Specs& get();
}
