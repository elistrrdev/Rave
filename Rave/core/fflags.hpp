#pragma once
#include <string>
#include <vector>

namespace fflags
{
    struct PresetFlag
    {
        const char* name;
        const char* value;
    };

    struct Preset
    {
        const char* name;
        const char* info;
        std::vector<PresetFlag> flags;
        std::vector<const char*> replaces;
    };

    const std::vector<Preset>& presets();
    bool presetActive(const std::string& text, const Preset& preset);
    bool setPreset(std::string& text, const Preset& preset, bool on, std::string& error);

    std::string load();
    bool validate(const std::string& text, std::string& error);
    bool save(const std::string& text, std::string& error);
    bool apply(const std::wstring& versionDir);
}
