#include "main.hpp"
#include "core/net.hpp"
#include "core/paths.hpp"
#include "core/report.hpp"
#include "core/launcher.hpp"
#include "core/settings.hpp"
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* kWindowClass = L"Rave";

static bool focusExistingInstance()
{
    HWND existing = FindWindowW(kWindowClass, nullptr);
    if (!existing)
        return false;
    if (!IsWindowVisible(existing))
        ShowWindow(existing, SW_SHOW);
    if (IsIconic(existing))
        ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    report::installCrashHandler();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!net::hasInternet())
        ExitProcess(0);

    app::instanceMutex = CreateMutexW(nullptr, TRUE, app::mutexName);
    DWORD mutexError = GetLastError();
    if (!app::instanceMutex || mutexError == ERROR_ALREADY_EXISTS || mutexError == ERROR_ACCESS_DENIED)
    {
        for (int i = 0; i < 20 && !focusExistingInstance(); ++i)
            Sleep(100);
        ExitProcess(0);
    }

    paths::localData();
    settings::load();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, kWindowClass, nullptr };
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(2), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(2), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    ::RegisterClassExW(&wc);

    window::dx::hwnd = ::CreateWindowW(wc.lpszClassName, RAVE_WNAME, WS_POPUP, (GetSystemMetrics(SM_CXSCREEN) / 2) - (int)(window::dx::size.x) / 2, (GetSystemMetrics(SM_CYSCREEN) / 2) - (int)(window::dx::size.y) / 2, 0, 0, NULL, NULL, wc.hInstance, NULL);

    SetWindowLongA(window::dx::hwnd, GWL_EXSTYLE, GetWindowLong(window::dx::hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(window::dx::hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(window::dx::hwnd, &margins);
    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.1f, 0.f);

    if (!CreateDeviceD3D(window::dx::hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        ExitProcess(1);
    }

    SendMessageW(window::dx::hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(window::dx::hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

    ::ShowWindow(window::dx::hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(window::dx::hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;
    io.LogFilename = NULL;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(window::dx::hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ui::BeforeLoop(g_pd3dDevice);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (!IsWindowVisible(window::dx::hwnd) || IsIconic(window::dx::hwnd))
        {
            ::Sleep(50);
            continue;
        }

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ui::Render();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    if (IsWindow(window::dx::hwnd))
        ::DestroyWindow(window::dx::hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    ExitProcess(0);
}

void ui::RequestClose()
{
    PostMessageW(window::dx::hwnd, WM_CLOSE, 0, 0);
}

bool gfx::decodeImage(const void* data, size_t size, int targetHeight, Image& out)
{
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size)))
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (!w || !h)
        return false;

    std::vector<BYTE> full((size_t)w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)full.size(), full.data())))
        return false;

    UINT x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x)
            if (full[((size_t)y * w + x) * 4 + 3] > 16)
            {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    if (x0 > x1 || y0 > y1)
    {
        x0 = y0 = 0;
        x1 = w - 1;
        y1 = h - 1;
    }

    UINT cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    std::vector<BYTE> cropped((size_t)cw * ch * 4);
    for (UINT y = 0; y < ch; ++y)
        memcpy(&cropped[(size_t)y * cw * 4], &full[(((size_t)(y + y0)) * w + x0) * 4], (size_t)cw * 4);

    UINT th = targetHeight > 0 ? (UINT)targetHeight : ch;
    UINT tw = std::max(1u, (UINT)((double)cw * th / ch + 0.5));

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(cw, ch, GUID_WICPixelFormat32bppPRGBA, cw * 4, (UINT)cropped.size(), cropped.data(), &bitmap)))
        return false;

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(bitmap.Get(), tw, th, WICBitmapInterpolationModeHighQualityCubic)))
    {
        if (!scaler || FAILED(scaler->Initialize(bitmap.Get(), tw, th, WICBitmapInterpolationModeFant)))
            return false;
    }

    std::vector<BYTE> pixels((size_t)tw * th * 4);
    if (FAILED(scaler->CopyPixels(nullptr, tw * 4, (UINT)pixels.size(), pixels.data())))
        return false;

    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        BYTE a = pixels[i + 3];
        if (a == 0 || a == 255)
            continue;
        for (int k = 0; k < 3; ++k)
            pixels[i + k] = (BYTE)std::min(255, (pixels[i + k] * 255 + a / 2) / a);
    }

    out.pixels = std::move(pixels);
    out.w = tw;
    out.h = th;
    return true;
}

ID3D11ShaderResourceView* gfx::createTexture(ID3D11Device* dvc, const Image& img)
{
    if (!dvc || img.pixels.empty())
        return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = img.w;
    desc.Height = img.h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = img.pixels.data();
    sub.SysMemPitch = img.w * 4;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(dvc->CreateTexture2D(&desc, &sub, &texture)))
        return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* view = nullptr;
    dvc->CreateShaderResourceView(texture.Get(), &srv, &view);
    return view;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case launcher::WM_RAVE_WINDOW:
        if (wParam == launcher::WindowMinimize)
            ShowWindow(hWnd, SW_MINIMIZE);
        else if (wParam == launcher::WindowHide)
            ShowWindow(hWnd, SW_HIDE);
        else if (wParam == launcher::WindowRestore)
        {
            ShowWindow(hWnd, SW_SHOW);
            if (IsIconic(hWnd))
                ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        }
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
