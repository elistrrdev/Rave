#include "report.hpp"
#include "paths.hpp"
#include "system.hpp"
#include "../version.hpp"
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <cstdio>
#include <string>

namespace
{
    std::wstring stamp()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    std::string header()
    {
        std::string text;
        text += "Rave V" RAVE_VERSION_STR "\r\n";
        text += "Windows: " + sys::windowsName() + "\r\n";
        text += std::string("Administrator: ") + (sys::isElevated() ? "yes" : "no") + "\r\n";
        return text;
    }

    bool writeText(const std::wstring& path, const std::string& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
        CloseHandle(h);
        return ok;
    }

    LONG WINAPI onCrash(EXCEPTION_POINTERS* info)
    {
        static volatile LONG entered = 0;
        if (InterlockedExchange(&entered, 1))
            return EXCEPTION_EXECUTE_HANDLER;

        std::wstring dir = paths::tempDir();
        std::wstring base = dir + L"\\rave-crash-" + stamp();

        HANDLE dump = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = info;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), &mei, nullptr, nullptr);
            CloseHandle(dump);
        }

        char buf[512];
        std::string text = header();
        void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
        DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        sprintf_s(buf, "Exception: 0x%08lX at %p\r\n", code, address);
        text += buf;

        HMODULE module = nullptr;
        if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address, &module))
        {
            wchar_t name[MAX_PATH]{};
            GetModuleFileNameW(module, name, MAX_PATH);
            sprintf_s(buf, "Module: %s +0x%llX\r\n", sys::narrow(name).c_str(), (unsigned long long)((char*)address - (char*)module));
            text += buf;
        }
        writeText(base + L".txt", text);

        std::wstring msg = L"Rave crashed.\n\nA crash report was saved to:\n" + dir + L"\n\nPlease send the rave-crash files to the Rave developer.";
        MessageBoxW(nullptr, msg.c_str(), L"Rave", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

void report::installCrashHandler()
{
    SetUnhandledExceptionFilter(onCrash);
}

std::wstring report::writeBugReport(const std::string& context)
{
    std::wstring path = paths::tempDir() + L"\\rave-bug-" + stamp() + L".txt";
    std::string text = header() + "\r\n" + context + "\r\n";
    if (!writeText(path, text))
        return {};
    return path;
}
