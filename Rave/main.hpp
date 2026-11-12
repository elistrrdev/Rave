#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS

#include <iostream>
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <map>
#include <algorithm>
#include <array>
#include <vector>
#include <sstream>
#include <iomanip>
#include <string>
#include <ctime>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_freetype.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "__ext/fonts.h"
#include "version.hpp"

using namespace ImGui;

#define _C(ARG) (CalcTextSize(ARG))
#define _CO(ARG) (GetColorU32(ARG))
#define _S(ARG) (sizeof(ARG) / sizeof(ARG[0]))

#define _V2 ImVec2
#define _V4 ImVec4
#define RGBA ImColor

#define EFONT(PDATA, FSIZE, CFG) \
    (io.Fonts->AddFontFromMemoryTTF(PDATA, sizeof(PDATA), FSIZE, &CFG))

namespace window
{
    namespace ui
    {
        inline _V2 size = { 660, 500 };
        inline float rounding = 2.f;
    }

    namespace dx
    {
        inline HWND hwnd;
        inline _V2& size = window::ui::size;
    }
}

namespace ui
{
    void BeforeLoop(ID3D11Device* dvc);
    void Render();
    void RequestClose();
}

namespace app
{
    inline HANDLE instanceMutex = nullptr;
    inline const wchar_t* mutexName = L"Local\\RaveSingleInstance";
}

namespace gfx
{
    struct Image
    {
        std::vector<unsigned char> pixels;
        unsigned w = 0, h = 0;
    };

    bool decodeImage(const void* data, size_t size, int targetHeight, Image& out);
    ID3D11ShaderResourceView* createTexture(ID3D11Device* dvc, const Image& img);
}

struct Fonts {

    std::vector<float> montSSizes = { 12, 24 };
    std::vector<float> montMSizes = { 14, 16, 18 };

    std::vector<ImFont*> montS = std::vector<ImFont*>(montSSizes.size());
    std::vector<ImFont*> montM = std::vector<ImFont*>(montMSizes.size());

    auto RenderFonts() noexcept -> void;
}; inline Fonts f;

inline auto Fonts::RenderFonts() noexcept -> void {
    ImGuiIO& io = GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_LightHinting | ImGuiFreeTypeBuilderFlags_LoadColor;

    for (int i = 0; i < f.montSSizes.size(); i++)
        f.montS[i] = EFONT(__MontS, (f.montSSizes[i]) + 4, cfg);

    for (int i = 0; i < f.montMSizes.size(); i++)
        f.montM[i] = EFONT(__MontM, (f.montMSizes[i]) + 3, cfg);
}

namespace images {
    inline ID3D11ShaderResourceView* logo;
    inline _V2 logoSize;
}

enum tabs
{
    home,
    launch,
    state
};

inline std::string getLocalTime()
{
    std::time_t time = std::time(nullptr);
    std::tm local;
    localtime_s(&local, &time);

    std::ostringstream oss;
    oss << std::put_time(&local, "%H:%M");

    return oss.str();
}
