#include "settings.hpp"
#include "json.hpp"
#include "paths.hpp"
#include <windows.h>
#include <cstdio>
#include <mutex>

namespace
{
    const settings::Accent kAccents[] = {
        { "teal", 0x6addbb },
        { "blue", 0x6aa8dd },
        { "purple", 0x9f8ae6 },
        { "pink", 0xdd6ab4 },
        { "red", 0xdd6a6a },
        { "orange", 0xdd9a6a },
        { "yellow", 0xddcc6a },
        { "green", 0x8edd6a },
    };

    std::mutex lock;
    settings::Values current;

    std::wstring filePath()
    {
        return paths::localData() + L"\\settings.json";
    }

    const char* behaviorName(settings::StartBehavior b)
    {
        switch (b)
        {
        case settings::StartBehavior::Minimize: return "minimize";
        case settings::StartBehavior::Hide: return "hide";
        default: return "keep_open";
        }
    }

    bool parseHex(const std::string& text, uint32_t& out)
    {
        if (text.size() != 7 || text[0] != '#')
            return false;
        uint32_t v = 0;
        for (size_t i = 1; i < 7; ++i)
        {
            char c = text[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }
}

const settings::Accent* settings::accents(int& count)
{
    count = (int)(sizeof(kAccents) / sizeof(kAccents[0]));
    return kAccents;
}

void settings::load()
{
    Values values;
    HANDLE h = CreateFileW(filePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        std::string text;
        LARGE_INTEGER size{};
        if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 65536)
        {
            text.resize((size_t)size.QuadPart);
            DWORD read = 0;
            if (!ReadFile(h, text.data(), (DWORD)text.size(), &read, nullptr))
                text.clear();
            text.resize(read);
        }
        CloseHandle(h);

        json::Value v;
        if (json::parse(text, v) && v.isObject())
        {
            const std::string& behavior = v["startBehavior"].str();
            if (behavior == "minimize")
                values.behavior = StartBehavior::Minimize;
            else if (behavior == "hide")
                values.behavior = StartBehavior::Hide;
            uint32_t accent;
            if (parseHex(v["accent"].str(), accent))
                values.accent = accent;
        }
    }

    std::lock_guard<std::mutex> guard(lock);
    current = values;
}

bool settings::save()
{
    Values values = get();
    char buf[256];
    snprintf(buf, sizeof(buf), "{\r\n    \"startBehavior\": \"%s\",\r\n    \"accent\": \"#%06x\"\r\n}\r\n", behaviorName(values.behavior), values.accent & 0xFFFFFF);

    HANDLE h = CreateFileW(filePath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD len = (DWORD)strlen(buf), written = 0;
    bool ok = WriteFile(h, buf, len, &written, nullptr) && written == len;
    CloseHandle(h);
    return ok;
}

settings::Values settings::get()
{
    std::lock_guard<std::mutex> guard(lock);
    return current;
}

void settings::set(const Values& values)
{
    std::lock_guard<std::mutex> guard(lock);
    current = values;
}
