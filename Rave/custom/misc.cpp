#include "colors.hpp"
#include "../core/system.hpp"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

auto SetStyle() noexcept -> void {
    ImGuiStyle& style = GetStyle();

    style.WindowBorderSize = 0.f;
    style.ScrollbarSize = 0.f;
    style.WindowPadding = { 0.f, 0.f };
	style.WindowShadowSize = 0.f;
    style.Colors[ImGuiCol_ChildBg] = colors::Transparent;
}

auto SmoothResize(RECT winRect) noexcept -> void {
    GetWindowRect(window::dx::hwnd, &winRect);

    int Width = static_cast<int>(window::ui::size.x);
    int Height = static_cast<int>(window::ui::size.y);

    int CenterX = winRect.left + (winRect.right - winRect.left) / 2;
    int CenterY = winRect.top + (winRect.bottom - winRect.top) / 2;

    int NewX = CenterX - Width / 2;
    int NewY = CenterY - Height / 2;

    MoveWindow(window::dx::hwnd, NewX, NewY, Width, Height, true);
}

auto ApplyCornerRegion(HWND hwnd) noexcept -> void {
    if (sys::isWindows11())
        return;

    static int lastW = 0, lastH = 0;
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w == lastW && h == lastH)
        return;
    lastW = w;
    lastH = h;

    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, 16, 16);
    if (region && !SetWindowRgn(hwnd, region, TRUE))
        DeleteObject(region);
}

auto MoveWindow(HWND g_hwnd) noexcept -> void {
    RECT rc, winRect;
    ImGuiIO& io = GetIO();

    GetWindowRect(g_hwnd, &rc);
    MoveWindow(g_hwnd, rc.left + (int)ImGui::GetWindowPos().x, rc.top + (int)ImGui::GetWindowPos().y, (int)(window::dx::size.x), (int)(window::dx::size.y), TRUE);

    GetWindowRect(window::dx::hwnd, &winRect);
    SmoothResize(winRect);
    ApplyCornerRegion(g_hwnd);

    ImGui::SetWindowPos(ImVec2(0.f, 0.f));
}

static auto getMaxSize(std::vector<std::string> arr, std::vector<ImFont*> fonts) noexcept -> _V2
{
    _V2 size;

    for (int i = 0; i < arr.size(); ++i)
    {
        if (!fonts.empty()) PushFont(fonts[i]);

        std::string clean = arr[i];
        clean.erase(0, clean.find_first_not_of('\n'));
        _V2 current = _C(clean.c_str());

        size.x = std::max({ size.x, current.x });
        size.y += current.y;
        
        if (!fonts.empty()) PopFont();
    }
    return size;
}

auto misc::separator() noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();
    _V2 pos = window->DC.CursorPos;
    _V2 size = { window->Size.x - GetCursorPosX() * 2, 1 };

    ItemSize(size, 0);
    ItemAdd({ pos, pos + size }, 0);

    window->DrawList->AddLine(pos, pos + size, _CO(colors::Main));
}

std::map<rowStatus, _V4> rowColors =
{
    { rowStatus::good, colors::Green },
    { rowStatus::bad, colors::Red },
    { rowStatus::accent, colors::Main },
    { rowStatus::info, colors::Lwhite }
};

auto misc::row(const std::string& label, const std::string& value, rowStatus status) noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();

    _V2 pos = window->DC.CursorPos;
    _V2 size;

    std::string total = label + value;
    size.x = _C(total.c_str()).x;
    size.y = _C(label.c_str()).y;

    ItemSize(size, 0);
    ItemAdd({ pos, pos + size }, 0);

    _V4 valueColor = getColor(rowColors, status);
    _V4 labelColor = status == rowStatus::info ? colors::Main : valueColor;

    window->DrawList->AddText(
        pos,
        _CO(labelColor),
        label.c_str()
    );

    window->DrawList->AddText(
        pos + _V2(_C(label.c_str()).x, 0),
        _CO(valueColor),
        value.c_str()
    );
}

auto misc::announcement(const std::string& title, const std::string& description) noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();

    _V2 pos = window->DC.CursorPos;
    float width = window->Size.x - GetCursorPosX() * 2;

    PushFont(f.montM[1]);
    _V2 body = CalcTextSize(description.c_str(), nullptr, false, width);
    PopFont();

    _V2 size = { width, std::max(77.f, 40 + body.y) };

    ItemSize(size, 0);
    ItemAdd({ pos, pos + size }, 0);
    ItemHoverable({ pos, pos + size }, 0, 0);

    PushFont(f.montM[2]);
    window->DrawList->AddText(
        pos,
        _CO(colors::Main),
        title.c_str()
    );
    PopFont();

    PushFont(f.montM[1]);
    window->DrawList->AddText(
        GetFont(),
        GetFontSize(),
        pos + _V2(0, 40),
        _CO(colors::Lwhite),
        description.c_str(),
        nullptr,
        width
    );
    PopFont();
}

auto misc::centeredText(const std::string& text, float y, const _V4& col) noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();
    _V2 size = _C(text.c_str());
    SetCursorPos({ (window->Size.x - size.x) / 2, y });
    draw::text(text, {}, col);
}

auto misc::child(const std::string& ID, _V2 size) noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();

    _V2 pos = window->DC.CursorPos;

    _V4 bg = RGBA(11, 11, 11, 77);
    window->DrawList->AddRectFilled(
        pos,
        pos + size,
        _CO(bg),
        10
    );

    SetNextWindowPos(pos);
    BeginChild(ID.c_str(), size);
}

auto misc::child2(const std::string& ID, _V2 size) noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();

    _V2 pos = window->DC.CursorPos;

    SetNextWindowPos(pos);
    BeginChild(ID.c_str(), size);
}

auto misc::renderMulticolorText(std::vector<std::string> texts, std::vector<ImFont*> fonts, std::vector<_V4> colors) noexcept -> void
{
    ImGuiWindow* window = GetCurrentWindow();

    _V2 pos = window->DC.CursorPos;
    _V2 size;
    float fpos = 0;

    size.y = getMaxSize(texts, fonts).y;

    for (int i = 0; i < texts.size(); ++i)
    {
        _V2 current = _C(texts[i].c_str());
        size.x += current.x;

        window->DrawList->AddText(
            pos + _V2(fpos, 0),
            _CO(colors[i]),
            texts[i].c_str()
        );

        fpos += current.x;
    }

    ItemSize(size, 0);
    ItemAdd({ pos, pos + size }, 0);
}


auto misc::begin(const std::string& name) noexcept -> void {
    SetNextWindowPos({ 0, 0 }, ImGuiCond_Appearing);

    SetNextWindowSize((window::ui::size));
    ImGui::Begin(name.c_str(), 0, WindowFlags);
    ImGuiWindow* window = GetCurrentWindow();
    
    draw::rect({ 0, 0 }, (window::ui::size), colors::Bg, (window::ui::rounding));

    static const std::string brand = RAVE_NAME " ";
    static const std::string version = "V" RAVE_VERSION_STR;
    SetCursorPos({ window->Size.x - 20 - _C((brand + version).c_str()).x, window->Size.y - 30 });
    misc::renderMulticolorText({ brand, version }, { f.montM[0], f.montS[0] }, { colors::Lwhite, colors::Main });

    SetStyle();
    MoveWindow(window::dx::hwnd);
}

auto draw::text(const std::string& text, const _V2& pos_arg, const _V4& col) noexcept -> bool {
    ImGuiWindow* window = GetCurrentWindow();

    if (pos_arg.x != 0) SetCursorPosX(pos_arg.x);
    if (pos_arg.y != 0) SetCursorPosY(pos_arg.y);

    _V2 pos = window->DC.CursorPos;
    _V2 size = _C(text.c_str());

    ItemSize(size, 0);
    if (!ItemAdd({ pos, pos + size }, 0))
        return false;

    window->DrawList->AddText(pos, _CO(col), text.c_str());
    return true;
}

auto draw::rect(const _V2& p1, const _V2& p2, const _V4& col, const float& rounding, DrawFlags flags) noexcept -> void {
    ImGuiWindow* window = GetCurrentWindow();

    SetCursorPos(p1);
    _V2 pos(window->DC.CursorPos);

    ImDrawFlags _Flags = 0;

    if (flags & RoundTop)       _Flags |= ImDrawFlags_RoundCornersTop;
    if (flags & RoundBottom)    _Flags |= ImDrawFlags_RoundCornersBottom;
    if (flags & RoundTopLeft)   _Flags |= ImDrawFlags_RoundCornersTopLeft;
    if (flags & RoundTopRight)  _Flags |= ImDrawFlags_RoundCornersTopRight;
    if (flags & RoundBotLeft)   _Flags |= ImDrawFlags_RoundCornersBottomLeft;
    if (flags & RoundBotRight)  _Flags |= ImDrawFlags_RoundCornersBottomRight;

    if (!(flags & NoFill))
        window->DrawList->AddRectFilled(pos, pos + p2, _CO(col), rounding, _Flags);
    else
        window->DrawList->AddRect(pos, pos + p2, _CO(col), rounding, _Flags);
}

auto draw::circle(const _V2& p1, float rad, const _V4& col, DrawFlags flags) noexcept -> void {
    ImGuiWindow* window = GetCurrentWindow();

    SetCursorPos(p1);
    _V2 pos(window->DC.CursorPos);

    if (!(flags & NoFill))
        window->DrawList->AddCircleFilled(pos, rad, _CO(col), 1000);
    else
        window->DrawList->AddCircle(pos, rad, _CO(col), 1000);
}

auto draw::image(ID3D11ShaderResourceView* texture, const _V2& p1, const _V2& p2, const _V4& col, const float& rounding) noexcept -> void {
    ImGuiWindow* window = GetCurrentWindow();

    SetCursorPos(p1);
    _V2 pos(window->DC.CursorPos);

    window->DrawList->AddImageRounded((ImTextureID)texture, pos, pos + p2, { 0, 0 }, { 1, 1 }, _CO(col), rounding);
}

auto SetRoundedCorners(HWND hwnd) noexcept -> void {
    if (sys::isWindows11())
    {
        DWORD preference = 2;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }
    else
    {
        ApplyCornerRegion(hwnd);
    }
}

auto misc::set_blur(HWND hwnd) noexcept -> void {
    SetRoundedCorners(hwnd);

    struct ACCENTPOLICY
    {
        int na;
        int nf;
        int nc;
        int nA;
    };
    struct WINCOMPATTRDATA
    {
        int na;
        PVOID pd;
        ULONG ul;
    };

    const HINSTANCE hm = LoadLibrary(L"user32.dll");
    if (hm)
    {
        typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);

        const pSetWindowCompositionAttribute SetWindowCompositionAttribute = (pSetWindowCompositionAttribute)GetProcAddress(hm, "SetWindowCompositionAttribute");
        if (SetWindowCompositionAttribute)
        {
            ACCENTPOLICY policy = { 3, 0, 0, 0 };
            WINCOMPATTRDATA data = { 19, &policy,sizeof(ACCENTPOLICY) };
            SetWindowCompositionAttribute(hwnd, &data);
        }
        FreeLibrary(hm);
    }
}