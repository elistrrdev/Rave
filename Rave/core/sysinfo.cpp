#include "sysinfo.hpp"
#include "system.hpp"
#include <windows.h>
#include <dxgi.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return {};
        size_t b = s.find_last_not_of(" \t\r\n");
        std::string out;
        bool space = false;
        for (size_t i = a; i <= b; ++i)
        {
            if (s[i] == ' ' || s[i] == '\t')
            {
                space = true;
                continue;
            }
            if (space && !out.empty())
                out += ' ';
            space = false;
            out += s[i];
        }
        return out;
    }

    std::wstring readRegString(HKEY root, const wchar_t* key, const wchar_t* value)
    {
        wchar_t buf[512]{};
        DWORD size = sizeof(buf);
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) == ERROR_SUCCESS)
            return buf;
        return {};
    }

    std::string sizeText(unsigned long long bytes)
    {
        const double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
        char buf[32];
        if (gb >= 1.0)
            snprintf(buf, sizeof(buf), "%.0f GB", gb);
        else
            snprintf(buf, sizeof(buf), "%.0f MB", (double)bytes / (1024.0 * 1024.0));
        return buf;
    }

    std::string cpuName()
    {
        std::string name = trim(sys::narrow(readRegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString")));
        return name.empty() ? "Unknown CPU" : name;
    }

    std::string cpuText(const std::string& name)
    {
        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
        int cores = 0;
        if (length > 0)
        {
            std::vector<unsigned char> buf(length);
            auto* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buf.data();
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length))
            {
                for (DWORD offset = 0; offset < length;)
                {
                    auto* item = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buf.data() + offset);
                    if (item->Relationship == RelationProcessorCore)
                        ++cores;
                    offset += item->Size;
                }
            }
        }
        DWORD threads = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (cores <= 0)
            return name + " (" + std::to_string(threads) + " threads)";
        return name + " (" + std::to_string(cores) + " cores, " + std::to_string(threads) + " threads)";
    }

    std::string gpuText()
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return "Unknown GPU";

        std::wstring best;
        SIZE_T bestMemory = 0;
        bool found = false;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                if (!found || desc.DedicatedVideoMemory > bestMemory)
                {
                    best = desc.Description;
                    bestMemory = desc.DedicatedVideoMemory;
                    found = true;
                }
            }
            adapter.Reset();
        }
        if (!found)
            return "Unknown GPU";
        std::string name = trim(sys::narrow(best));
        if (bestMemory >= 256ull * 1024 * 1024)
            name += " (" + sizeText(bestMemory) + ")";
        return name;
    }

    std::string ramText()
    {
        ULONGLONG kb = 0;
        if (GetPhysicallyInstalledSystemMemory(&kb) && kb > 0)
            return sizeText(kb * 1024ull);
        MEMORYSTATUSEX mem{ sizeof(mem) };
        if (GlobalMemoryStatusEx(&mem))
            return sizeText(mem.ullTotalPhys);
        return "Unknown";
    }

    std::string displayText()
    {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
            return "Unknown";
        std::string text = std::to_string(mode.dmPelsWidth) + " x " + std::to_string(mode.dmPelsHeight);
        if (mode.dmDisplayFrequency > 1)
            text += " @ " + std::to_string(mode.dmDisplayFrequency) + " Hz";
        int monitors = GetSystemMetrics(SM_CMONITORS);
        if (monitors > 1)
            text += " (" + std::to_string(monitors) + " monitors)";
        return text;
    }

    std::string smbiosUuid()
    {
        UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
        if (size < 8)
            return {};
        std::vector<unsigned char> buf(size);
        if (GetSystemFirmwareTable('RSMB', 0, buf.data(), size) != size)
            return {};

        DWORD length = *(DWORD*)(buf.data() + 4);
        const unsigned char* p = buf.data() + 8;
        const unsigned char* end = p + std::min<size_t>(length, size - 8);
        while (p + 4 <= end)
        {
            unsigned char type = p[0];
            unsigned char len = p[1];
            if (len < 4 || p + len > end)
                break;
            if (type == 1 && len >= 0x19)
            {
                static const char* hex = "0123456789abcdef";
                std::string out;
                for (int i = 0; i < 16; ++i)
                {
                    out += hex[p[8 + i] >> 4];
                    out += hex[p[8 + i] & 15];
                }
                return out;
            }
            if (type == 127)
                break;
            const unsigned char* next = p + len;
            while (next + 1 < end && (next[0] != 0 || next[1] != 0))
                ++next;
            p = next + 2;
        }
        return {};
    }

    std::string hardwareId(const std::string& cpu)
    {
        std::string source = "rave-hwid|" + sys::narrow(readRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid")) +
            "|" + smbiosUuid() + "|" + cpu;

        unsigned char digest[32]{};
        if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)source.data(), (ULONG)source.size(), digest, sizeof(digest)) != 0)
            return "Unknown";

        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (int i = 0; i < 16; ++i)
        {
            if (i && i % 4 == 0)
                out += '-';
            out += hex[digest[i] >> 4];
            out += hex[digest[i] & 15];
        }
        return out;
    }
}

const sysinfo::Specs& sysinfo::get()
{
    static Specs specs;
    static std::once_flag once;
    std::call_once(once, [] {
        std::string cpu = cpuName();
        specs.cpu = cpuText(cpu);
        specs.gpu = gpuText();
        specs.ram = ramText();
        specs.display = displayText();
        specs.hwid = hardwareId(cpu);
    });
    return specs;
}
