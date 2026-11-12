#pragma once
#include <cstdint>
#include <string>

namespace settings
{
    enum class StartBehavior { KeepOpen, Minimize, Hide };

    struct Values
    {
        StartBehavior behavior = StartBehavior::KeepOpen;
        uint32_t accent = 0x6addbb;
    };

    struct Accent
    {
        const char* name;
        uint32_t rgb;
    };

    const Accent* accents(int& count);

    void load();
    bool save();
    Values get();
    void set(const Values& values);
}
