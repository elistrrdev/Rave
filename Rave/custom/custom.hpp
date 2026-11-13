#pragma once
#include "..\main.hpp"
#define RAVE

inline DWORD WindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar |
ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse;

typedef int DrawFlags;
RAVE enum __DrawFlags : DrawFlags {
    NoFill       = 1 << 0,
    RoundTop     = 1 << 1,
    RoundBottom  = 1 << 2,
    RoundTopLeft = 1 << 3,
    RoundTopRight= 1 << 4,
    RoundBotLeft = 1 << 5,
    RoundBotRight= 1 << 6
};

enum class logType
{
    success,
    error,
    warn,
    info
};

namespace misc
{
    RAVE auto begin(const std::string& name) noexcept -> void;
    RAVE auto set_blur(HWND hwnd) noexcept -> void;
    auto renderMulticolorText(std::vector<std::string> texts, std::vector<ImFont*> fonts, std::vector<_V4> colors) noexcept -> void;
    auto child(const std::string& ID, _V2 size) noexcept -> void;
    auto child2(const std::string& ID, _V2 size) noexcept -> void;
    auto row(const std::string& label, const std::string& value) noexcept -> void;
}

namespace draw
{
    RAVE auto rect(const _V2& p1, const _V2& p2, const _V4& col, const float& rounding = 0, DrawFlags flags = 0) noexcept -> void;
    RAVE auto circle(const _V2& p1, float rad, const _V4& col, DrawFlags flags) noexcept -> void;
    RAVE auto image(ID3D11ShaderResourceView* texture, const _V2& p1, const _V2& p2, const _V4& col, const float& rounding = 0) noexcept -> void;
    RAVE auto text(const std::string& text, const _V2& pos_arg, const _V4& col) noexcept -> bool;
}

namespace items
{
    auto input(const std::string& label, char* buf, size_t bufsize) noexcept -> bool;
    auto button(const std::string& label, const _V2& size) noexcept -> bool;
    auto choice(const std::string& label, bool selected, const _V2& size, float rounding = 6.f) noexcept -> bool;
    auto danger(const std::string& label, const _V2& size, float rounding = 6.f) noexcept -> bool;
    auto swatch(const std::string& id, uint32_t rgb, bool selected, const _V2& size) noexcept -> bool;
    auto tab(const std::string& label, bool cond) noexcept -> bool;
    auto sendLog(std::string log, logType type) noexcept -> void;
    auto renderLogs() noexcept -> void;
    auto closeButton(const std::string& id, const _V2& size) noexcept -> bool;
}

template<typename K, typename V>
inline _V4 getColor(const std::map<K, _V4>& map, V type)
{
    auto it = map.find(type);
    if (it != map.end())
        return it->second;
    return _V4(0, 0, 0, 1);
}
