#include "fflags.hpp"
#include "json.hpp"
#include "paths.hpp"
#include "roblox.hpp"
#include <windows.h>
#include <cmath>
#include <cstdio>

namespace
{
    std::wstring storePath()
    {
        return paths::localData() + L"\\fflags.json";
    }

    bool readText(const std::wstring& path, std::string& out)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (1 << 24);
        if (ok)
        {
            out.resize((size_t)size.QuadPart);
            DWORD read = 0;
            ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
        }
        CloseHandle(h);
        return ok;
    }

    bool writeText(const std::wstring& path, const std::string& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = text.empty() || (WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size());
        CloseHandle(h);
        return ok;
    }

    std::string number(double n)
    {
        char buf[64];
        if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15)
            snprintf(buf, sizeof(buf), "%lld", (long long)n);
        else
            snprintf(buf, sizeof(buf), "%.17g", n);
        return buf;
    }

    void dump(const json::Value& v, int depth, std::string& out)
    {
        const std::string pad((depth + 1) * 4, ' ');
        const std::string end(depth * 4, ' ');
        switch (v.type)
        {
        case json::Value::Type::Null: out += "null"; break;
        case json::Value::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case json::Value::Type::Number: out += number(v.number); break;
        case json::Value::Type::String: out += json::quote(v.string); break;
        case json::Value::Type::Array:
            if (v.items.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < v.items.size(); ++i)
            {
                out += pad;
                dump(v.items[i], depth + 1, out);
                out += i + 1 < v.items.size() ? ",\n" : "\n";
            }
            out += end + "]";
            break;
        case json::Value::Type::Object:
            if (v.items.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < v.items.size(); ++i)
            {
                out += pad + json::quote(v.keys[i]) + ": ";
                dump(v.items[i], depth + 1, out);
                out += i + 1 < v.items.size() ? ",\n" : "\n";
            }
            out += end + "}";
            break;
        }
    }

    int find(const json::Value& obj, const char* key)
    {
        for (size_t i = 0; i < obj.keys.size(); ++i)
            if (obj.keys[i] == key)
                return (int)i;
        return -1;
    }

    void erase(json::Value& obj, const char* key)
    {
        int i = find(obj, key);
        if (i < 0)
            return;
        obj.keys.erase(obj.keys.begin() + i);
        obj.items.erase(obj.items.begin() + i);
    }

    bool blockEnd(const std::string& text, size_t start, size_t& end)
    {
        int depth = 0;
        bool inString = false;
        for (size_t i = start; i < text.size(); ++i)
        {
            char c = text[i];
            if (inString)
            {
                if (c == '\\')
                    ++i;
                else if (c == '"')
                    inString = false;
                continue;
            }
            if (c == '"')
                inString = true;
            else if (c == '{' || c == '[')
                ++depth;
            else if (c == '}' || c == ']')
            {
                if (--depth == 0)
                {
                    end = i + 1;
                    return true;
                }
            }
        }
        return false;
    }

    bool readBlocks(const std::string& text, std::vector<json::Value>& blocks, std::string& error)
    {
        blocks.clear();
        size_t pos = 0;
        while (true)
        {
            pos = text.find_first_not_of(" \t\r\n", pos);
            if (pos == std::string::npos)
                return true;
            size_t end = 0;
            json::Value v;
            if (!blockEnd(text, pos, end) || !json::parse(text.substr(pos, end - pos), v))
            {
                error = "This is not valid JSON";
                return false;
            }
            if (!v.isObject())
            {
                error = "FFlags must be JSON objects like { \"Flag\": value }";
                return false;
            }
            blocks.push_back(std::move(v));
            pos = end;
        }
    }

    void set(json::Value& obj, const std::string& key, const json::Value& value)
    {
        for (size_t i = 0; i < obj.keys.size(); ++i)
        {
            if (obj.keys[i] == key)
            {
                obj.items[i] = value;
                return;
            }
        }
        obj.keys.push_back(key);
        obj.items.push_back(value);
    }

    json::Value merged(const std::vector<json::Value>& blocks)
    {
        json::Value all;
        all.type = json::Value::Type::Object;
        for (const json::Value& block : blocks)
            for (size_t i = 0; i < block.keys.size(); ++i)
                set(all, block.keys[i], block.items[i]);
        return all;
    }
}

const std::vector<fflags::Preset>& fflags::presets()
{
    static const std::vector<Preset> list = {
        { "Potato mode", "Everything low, no grass, no MSAA", {
            { "DFIntDebugFRMQualityLevelOverride", "1" },
            { "DFFlagTextureQualityOverrideEnabled", "True" },
            { "DFIntTextureQualityOverride", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistance", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistanceL12", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistanceL23", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistanceL34", "0" },
            { "FIntFRMMinGrassDistance", "0" },
            { "FIntFRMMaxGrassDistance", "0" },
            { "FIntDebugForceMSAASamples", "1" } }, {} },
        { "Lowest graphics level", "Graphics quality locked to the lowest", {
            { "DFIntDebugFRMQualityLevelOverride", "1" } }, {} },
        { "Lowest textures", "All textures in their lowest quality", {
            { "DFFlagTextureQualityOverrideEnabled", "True" },
            { "DFIntTextureQualityOverride", "0" } }, {} },
        { "Low-poly meshes", "Meshes always use the least detail", {
            { "DFIntCSGLevelOfDetailSwitchingDistance", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistanceL12", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistanceL23", "0" },
            { "DFIntCSGLevelOfDetailSwitchingDistanceL34", "0" } }, {} },
        { "No grass", "Terrain grass is not drawn at all", {
            { "FIntFRMMinGrassDistance", "0" },
            { "FIntFRMMaxGrassDistance", "0" } }, {} },
        { "Still grass", "Grass stops moving in the wind", {
            { "FIntGrassMovementReducedMotionFactor", "0" } }, {} },
        { "No anti-aliasing", "Turns MSAA off for a bit more FPS", {
            { "FIntDebugForceMSAASamples", "1" } }, {} },
        { "Gray sky", "Replaces the sky with plain gray", {
            { "FFlagDebugSkyGray", "True" } }, {} },
        { "Pause lighting updates", "Lighting stops updating, helps weak PCs", {
            { "DFFlagDebugPauseVoxelizer", "True" } }, {} },
        { "Exclusive fullscreen", "Alt+Enter uses real exclusive fullscreen", {
            { "FFlagHandleAltEnterFullscreenManually", "False" } }, {} },
        { "No DPI scaling", "Sharper, smaller UI on scaled screens", {
            { "DFFlagDisableDPIScale", "True" } }, {} },
        { "Use Direct3D 11", "Roblox renders with Direct3D 11", {
            { "FFlagDebugGraphicsPreferD3D11", "True" } }, { "FFlagDebugGraphicsPreferVulkan", "FFlagDebugGraphicsPreferOpenGL" } },
        { "Use Vulkan", "Roblox renders with Vulkan", {
            { "FFlagDebugGraphicsPreferVulkan", "True" } }, { "FFlagDebugGraphicsPreferD3D11", "FFlagDebugGraphicsPreferOpenGL" } },
        { "Use OpenGL", "Roblox renders with OpenGL", {
            { "FFlagDebugGraphicsPreferOpenGL", "True" } }, { "FFlagDebugGraphicsPreferD3D11", "FFlagDebugGraphicsPreferVulkan" } },
    };
    return list;
}

bool fflags::applyPreset(std::string& text, const Preset& preset, std::string& error)
{
    std::vector<json::Value> blocks;
    if (!readBlocks(text, blocks, error))
    {
        error = "Fix the JSON in the editor first";
        return false;
    }

    json::Value added;
    added.type = json::Value::Type::Object;
    for (const PresetFlag& flag : preset.flags)
    {
        json::Value value;
        value.type = json::Value::Type::String;
        value.string = flag.value;
        set(added, flag.name, value);
    }

    std::vector<json::Value> kept;
    for (json::Value& block : blocks)
    {
        for (const char* key : preset.replaces)
            erase(block, key);
        for (const PresetFlag& flag : preset.flags)
            erase(block, flag.name);
        if (!block.items.empty())
            kept.push_back(std::move(block));
    }
    kept.push_back(std::move(added));

    text.clear();
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (i)
            text += "\n";
        dump(kept[i], 0, text);
    }
    return true;
}

std::string fflags::load()
{
    std::string text;
    if (!readText(storePath(), text))
        return {};
    return text;
}

bool fflags::validate(const std::string& text, std::string& error)
{
    std::vector<json::Value> blocks;
    return readBlocks(text, blocks, error);
}

bool fflags::save(const std::string& text, std::string& error)
{
    if (!validate(text, error))
        return false;
    if (!writeText(storePath(), text))
    {
        error = "Could not write fflags.json";
        return false;
    }
    std::string installed = roblox::installedGuid();
    if (!installed.empty() && !apply(roblox::versionDir(installed)))
    {
        error = "Saved, but could not write ClientAppSettings.json";
        return false;
    }
    return true;
}

bool fflags::apply(const std::wstring& versionDir)
{
    std::vector<json::Value> blocks;
    std::string error;
    std::string text;
    if (!readBlocks(load(), blocks, error))
        blocks.clear();
    dump(merged(blocks), 0, text);
    std::wstring dir = versionDir + L"\\ClientSettings";
    if (!paths::ensureDir(dir))
        return false;
    return writeText(dir + L"\\ClientAppSettings.json", text);
}
