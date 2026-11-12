#include "launcher.hpp"
#include "accounts.hpp"
#include "fflags.hpp"
#include "report.hpp"
#include "roblox.hpp"
#include "settings.hpp"
#include "system.hpp"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <random>
#include <thread>

namespace
{
    std::atomic<launcher::Task> currentTask{ launcher::Task::Idle };
    std::atomic<float> currentProgress{ 0.f };
    std::mutex stateLock;
    launcher::Snapshot state;
    launcher::Logger logger;
    HWND window = nullptr;

    std::atomic<bool> watching{ false };
    std::atomic<bool> behaviorPending{ false };
    std::atomic<bool> seenRunning{ false };
    std::atomic<bool> hiddenByRave{ false };
    std::atomic<DWORD> watchStarted{ 0 };

    void log(const std::string& text, launcher::LogLevel level)
    {
        if (logger)
            logger(text, level);
    }

    void failWithReport(const std::string& what, const std::string& detail)
    {
        log(what + ": " + detail, launcher::LogLevel::Error);
        std::wstring path = report::writeBugReport(what + "\r\n" + detail);
        if (!path.empty())
            log("Bug report saved to %TEMP%\\rave-temp", launcher::LogLevel::Info);
    }

    void storeLatest(const std::string& guid)
    {
        std::lock_guard<std::mutex> guard(stateLock);
        state.latest = guid;
        state.latestKnown = true;
    }

    void refreshLatest()
    {
        roblox::Latest latest;
        std::string error;
        if (roblox::fetchLatest(latest, error))
            storeLatest(latest.guid);
    }

    void windowAction(launcher::WindowAction action)
    {
        if (window)
            PostMessageW(window, launcher::WM_RAVE_WINDOW, (WPARAM)action, 0);
    }

    void applyStartBehavior()
    {
        settings::StartBehavior behavior = settings::get().behavior;
        if (behavior == settings::StartBehavior::Minimize)
            windowAction(launcher::WindowMinimize);
        else if (behavior == settings::StartBehavior::Hide)
        {
            hiddenByRave = true;
            windowAction(launcher::WindowHide);
        }
    }

    void watchTick()
    {
        static int missing = 0;
        if (!watching)
        {
            missing = 0;
            return;
        }

        bool running = roblox::isRunning();
        if (running)
        {
            missing = 0;
            seenRunning = true;
            if (behaviorPending.exchange(false))
                applyStartBehavior();
            return;
        }

        if (!seenRunning)
        {
            if (GetTickCount() - watchStarted.load() > 60000)
            {
                watching = false;
                behaviorPending = false;
            }
            return;
        }

        if (++missing < 2)
            return;

        missing = 0;
        watching = false;
        seenRunning = false;
        behaviorPending = false;
        log("Roblox was closed", launcher::LogLevel::Info);
        if (hiddenByRave.exchange(false))
            windowAction(launcher::WindowRestore);
    }

    std::wstring robloxLocale()
    {
        wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
        std::wstring locale = GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0 ? name : L"en-US";
        std::wstring out;
        for (wchar_t c : locale)
        {
            if (c == L'-')
                out += L'_';
            else if (iswalpha(c))
                out += (wchar_t)towlower(c);
            else
                break;
        }
        return out.size() >= 2 ? out : L"en_us";
    }

    std::wstring signedInArgs(const std::string& ticket)
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER now{};
        now.LowPart = ft.dwLowDateTime;
        now.HighPart = ft.dwHighDateTime;
        uint64_t unixMs = (now.QuadPart - 116444736000000000ull) / 10000ull;

        std::random_device rd;
        uint64_t tracker = 100000000000ull + ((((uint64_t)rd() << 32) | rd()) % 900000000000ull);

        std::wstring locale = robloxLocale();
        return L"roblox-player:1+launchmode:app+gameinfo:" + sys::widen(ticket) +
            L"+launchtime:" + std::to_wstring(unixMs) +
            L"+browsertrackerid:" + std::to_wstring(tracker) +
            L"+robloxLocale:" + locale + L"+gameLocale:" + locale +
            L"+channel:+LaunchExp:InApp";
    }

    void runLaunch()
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        currentTask = launcher::Task::Checking;
        currentProgress = 0.f;
        log("Checking the latest Roblox version", launcher::LogLevel::Info);

        roblox::Latest latest;
        std::string error;
        if (!roblox::fetchLatest(latest, error))
        {
            failWithReport("Version check failed", error);
            currentTask = launcher::Task::Idle;
            CoUninitialize();
            return;
        }
        storeLatest(latest.guid);

        std::string installed = roblox::installedGuid();
        if (installed != latest.guid)
        {
            currentTask = launcher::Task::Installing;
            log(installed.empty() ? "Installing Roblox " + latest.version : "Updating Roblox to " + latest.version, launcher::LogLevel::Warn);
            if (!roblox::install(latest.guid, [](float f) { currentProgress = f; return true; }, error))
            {
                failWithReport("Roblox install failed", error);
                currentTask = launcher::Task::Idle;
                CoUninitialize();
                return;
            }
            roblox::removeOldVersions(latest.guid);
            log("Roblox " + latest.version + " installed", launcher::LogLevel::Success);
        }
        else
        {
            log("Roblox is up to date", launcher::LogLevel::Success);
        }

        currentTask = launcher::Task::Launching;
        if (!fflags::apply(roblox::versionDir(latest.guid)))
            log("Could not apply the FFlags", launcher::LogLevel::Warn);

        std::wstring args = L"--app";
        uint64_t accountId = accounts::selected();
        if (accountId)
        {
            accounts::Account who;
            std::string ticket;
            if (!accounts::ticket(accountId, ticket, who, error))
            {
                log("Sign-in failed: " + error, launcher::LogLevel::Error);
                currentTask = launcher::Task::Idle;
                CoUninitialize();
                return;
            }
            args = signedInArgs(ticket);
            log("Launching Roblox as " + accounts::label(who), launcher::LogLevel::Info);
        }
        else
        {
            log("Launching Roblox", launcher::LogLevel::Info);
        }

        if (!roblox::launch(latest.guid, args, error))
        {
            failWithReport("Roblox launch failed", error);
            currentTask = launcher::Task::Idle;
            CoUninitialize();
            return;
        }

        bool started = false;
        for (int i = 0; i < 100 && !started; ++i)
        {
            started = roblox::isRunning();
            if (!started)
                Sleep(150);
        }
        log(started ? "Roblox is running" : "Roblox was started, waiting for it to open", started ? launcher::LogLevel::Success : launcher::LogLevel::Warn);

        watchStarted = GetTickCount();
        seenRunning = false;
        behaviorPending = true;
        watching = true;

        currentTask = launcher::Task::Idle;
        CoUninitialize();
    }
}

void launcher::setLogger(Logger l)
{
    logger = std::move(l);
}

void launcher::setWindow(void* hwnd)
{
    window = (HWND)hwnd;
}

void launcher::startMonitor()
{
    std::thread([] {
        refreshLatest();
        DWORD lastLatest = GetTickCount();
        for (;;)
        {
            Sleep(1000);
            watchTick();
            if (GetTickCount() - lastLatest > 10 * 60 * 1000)
            {
                refreshLatest();
                lastLatest = GetTickCount();
            }
        }
    }).detach();
}

void launcher::launch()
{
    Task expected = Task::Idle;
    if (!currentTask.compare_exchange_strong(expected, Task::Checking))
    {
        log("Rave is busy, please wait", LogLevel::Info);
        return;
    }
    std::thread(runLaunch).detach();
}

launcher::Task launcher::task()
{
    return currentTask.load();
}

float launcher::progress()
{
    return currentProgress.load();
}

launcher::Snapshot launcher::snapshot()
{
    std::lock_guard<std::mutex> guard(stateLock);
    return state;
}
