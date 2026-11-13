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

    bool isBlank(const std::string& text)
    {
        return text.find_first_not_of(" \t\r\n") == std::string::npos;
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

    std::string asText(const json::Value& v)
    {
        switch (v.type)
        {
        case json::Value::Type::String: return v.string;
        case json::Value::Type::Bool: return v.boolean ? "true" : "false";
        case json::Value::Type::Number: return number(v.number);
        default: return {};
        }
    }

    bool sameValue(const json::Value& v, const char* expected)
    {
        if (v.type == json::Value::Type::Null || v.type == json::Value::Type::Array || v.type == json::Value::Type::Object)
            return false;
        return _stricmp(asText(v).c_str(), expected) == 0;
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

    bool readObject(const std::string& text, json::Value& obj)
    {
        if (isBlank(text))
        {
            obj = {};
            obj.type = json::Value::Type::Object;
            return true;
        }
        return json::parse(text, obj) && obj.isObject();
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

bool fflags::presetActive(const std::string& text, const Preset& preset)
{
    json::Value obj;
    if (!readObject(text, obj))
        return false;
    for (const PresetFlag& flag : preset.flags)
    {
        int i = find(obj, flag.name);
        if (i < 0 || !sameValue(obj.items[i], flag.value))
            return false;
    }
    return true;
}

bool fflags::setPreset(std::string& text, const Preset& preset, bool on, std::string& error)
{
    json::Value obj;
    if (!readObject(text, obj))
    {
        error = "Fix the JSON in the editor first";
        return false;
    }
    if (on)
    {
        for (const char* key : preset.replaces)
            erase(obj, key);
        for (const PresetFlag& flag : preset.flags)
        {
            json::Value value;
            value.type = json::Value::Type::String;
            value.string = flag.value;
            int i = find(obj, flag.name);
            if (i >= 0)
                obj.items[i] = value;
            else
            {
                obj.keys.push_back(flag.name);
                obj.items.push_back(value);
            }
        }
    }
    else
    {
        for (const PresetFlag& flag : preset.flags)
            erase(obj, flag.name);
    }
    text.clear();
    if (!obj.items.empty())
        dump(obj, 0, text);
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
    if (isBlank(text))
        return true;
    json::Value v;
    if (!json::parse(text, v))
    {
        error = "This is not valid JSON";
        return false;
    }
    if (!v.isObject())
    {
        error = "FFlags must be a JSON object like { \"Flag\": value }";
        return false;
    }
    return true;
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
    std::string text = load();
    std::string error;
    if (isBlank(text) || !validate(text, error))
        text = "{}";
    std::wstring dir = versionDir + L"\\ClientSettings";
    if (!paths::ensureDir(dir))
        return false;
    return writeText(dir + L"\\ClientAppSettings.json", text);
}
