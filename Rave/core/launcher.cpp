#include "launcher.hpp"
#include "accounts.hpp"
#include "fflags.hpp"
#include "report.hpp"
#include "roblox.hpp"
#include "servers.hpp"
#include "settings.hpp"
#include "skins.hpp"
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
    std::atomic<bool> skinsHeld{ false };

    void releaseSkins()
    {
        if (skinsHeld.exchange(false))
            skins::launchDone();
    }

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
            releaseSkins();
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
                releaseSkins();
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

    enum class JobKind { App, Website };

    struct Job
    {
        JobKind kind = JobKind::App;
        std::string website;
    };

    uint64_t unixMs()
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER now{};
        now.LowPart = ft.dwLowDateTime;
        now.HighPart = ft.dwHighDateTime;
        return (now.QuadPart - 116444736000000000ull) / 10000ull;
    }

    std::string trackerId()
    {
        std::random_device rd;
        return std::to_string(100000000000ull + ((((uint64_t)rd() << 32) | rd()) % 900000000000ull));
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
        std::wstring locale = robloxLocale();
        return L"roblox-player:1+launchmode:app+gameinfo:" + sys::widen(ticket) +
            L"+launchtime:" + std::to_wstring(unixMs()) +
            L"+browsertrackerid:" + sys::widen(trackerId()) +
            L"+robloxLocale:" + locale + L"+gameLocale:" + locale +
            L"+channel:+LaunchExp:InApp";
    }

    void wipe(std::string& s)
    {
        if (!s.empty())
            SecureZeroMemory(s.data(), s.size());
        s.clear();
    }

    void wipe(std::wstring& s)
    {
        if (!s.empty())
            SecureZeroMemory(s.data(), s.size() * sizeof(wchar_t));
        s.clear();
    }

    void runLaunch(Job job)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        currentTask = launcher::Task::Checking;
        currentProgress = 0.f;

        bool bestFound = false;
        std::string bestSummary;
        std::thread best;
        if (job.kind == JobKind::Website && settings::get().bestServer)
        {
            log("Looking for the best server", launcher::LogLevel::Info);
            best = std::thread([&job, &bestFound, &bestSummary] { bestFound = servers::improveWebsiteJoin(job.website, bestSummary); });
        }
        struct Joiner
        {
            std::thread& t;
            ~Joiner() { if (t.joinable()) t.join(); }
        } joiner{ best };

        log("Checking the latest Roblox version", launcher::LogLevel::Info);

        roblox::Latest latest;
        std::string error;
        std::string installed = roblox::installedGuid();
        if (roblox::fetchLatest(latest, error))
            storeLatest(latest.guid);
        else if (!installed.empty())
        {
            latest.guid = installed;
            log("Could not check the Roblox version (" + error + "), starting the installed one", launcher::LogLevel::Warn);
        }
        else
        {
            failWithReport("Version check failed", error);
            currentTask = launcher::Task::Idle;
            CoUninitialize();
            return;
        }

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
        else if (!latest.version.empty())
        {
            log("Roblox is up to date", launcher::LogLevel::Success);
        }

        currentTask = launcher::Task::Launching;
        skins::applyBeforeLaunch();
        skinsHeld = true;
        struct Release
        {
            bool on = true;
            ~Release() { if (on) releaseSkins(); }
        } release;
        if (!fflags::apply(roblox::versionDir(latest.guid)))
            log("Could not apply the FFlags", launcher::LogLevel::Warn);

        std::wstring args = L"--app";
        uint64_t accountId = accounts::selected();
        if (job.kind == JobKind::Website)
        {
            if (best.joinable())
            {
                best.join();
                if (bestFound)
                    log("Best server: " + bestSummary, launcher::LogLevel::Success);
                else if (!bestSummary.empty())
                    log(bestSummary + ", Roblox picks the server", launcher::LogLevel::Warn);
            }
            args = sys::widen(job.website);
            wipe(job.website);
            log("Joining from the website (with the account signed in there)", launcher::LogLevel::Info);
        }
        else if (accountId)
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
            wipe(ticket);
            log("Launching Roblox as " + accounts::label(who), launcher::LogLevel::Info);
        }
        else
        {
            log("Launching Roblox", launcher::LogLevel::Info);
        }

        bool launched = roblox::launch(latest.guid, args, error);
        wipe(args);
        if (!launched)
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
        if (!started)
            release.on = false;

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
    std::thread(runLaunch, Job{}).detach();
}

void launcher::launchWebsite(std::string launchString)
{
    Task expected = Task::Idle;
    if (!currentTask.compare_exchange_strong(expected, Task::Checking))
    {
        wipe(launchString);
        log("Rave is busy, click Play again in a moment", LogLevel::Info);
        return;
    }
    Job job;
    job.kind = JobKind::Website;
    job.website = std::move(launchString);
    std::thread(runLaunch, std::move(job)).detach();
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
