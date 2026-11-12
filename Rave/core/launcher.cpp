#include "launcher.hpp"
#include "report.hpp"
#include "roblox.hpp"
#include "system.hpp"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace
{
    std::atomic<launcher::Task> currentTask{ launcher::Task::Idle };
    std::atomic<float> currentProgress{ 0.f };
    std::mutex stateLock;
    launcher::Snapshot state;
    launcher::Logger logger;

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

    void refreshLatest()
    {
        roblox::Latest latest;
        std::string error;
        if (roblox::fetchLatest(latest, error))
        {
            std::lock_guard<std::mutex> guard(stateLock);
            state.latest = latest.guid;
            state.latestKnown = true;
        }
    }

    void refreshLocal()
    {
        std::string installed = roblox::installedGuid();
        bool running = roblox::isRunning();
        std::lock_guard<std::mutex> guard(stateLock);
        state.installed = installed;
        state.running = running;
    }

    void runLaunch(bool reinstall)
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
        {
            std::lock_guard<std::mutex> guard(stateLock);
            state.latest = latest.guid;
            state.latestKnown = true;
        }

        std::string installed = roblox::installedGuid();
        bool needsInstall = reinstall || installed != latest.guid;

        if (needsInstall && installed == latest.guid && roblox::isRunning())
        {
            log("Close Roblox before reinstalling it", launcher::LogLevel::Warn);
            currentTask = launcher::Task::Idle;
            CoUninitialize();
            return;
        }

        if (needsInstall)
        {
            currentTask = launcher::Task::Installing;
            log(installed.empty() ? "Installing Roblox " + latest.version : "Updating Roblox to " + latest.version, launcher::LogLevel::Warn);
            if (!roblox::install(latest.guid, [](float f) { currentProgress = f; return true; }, error))
            {
                failWithReport("Roblox install failed", error);
                refreshLocal();
                currentTask = launcher::Task::Idle;
                CoUninitialize();
                return;
            }
            roblox::removeOldVersions(latest.guid);
            log("Roblox " + latest.version + " installed", launcher::LogLevel::Success);
            refreshLocal();
        }
        else
        {
            log("Roblox is up to date", launcher::LogLevel::Success);
        }

        currentTask = launcher::Task::Launching;
        log("Launching Roblox", launcher::LogLevel::Info);
        if (!roblox::launch(latest.guid, error))
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
        refreshLocal();

        currentTask = launcher::Task::Idle;
        CoUninitialize();
    }

    void runClose()
    {
        currentTask = launcher::Task::Closing;
        int closed = roblox::close();
        if (closed > 0)
            log("Roblox was closed", launcher::LogLevel::Success);
        else
            log("Roblox is not running", launcher::LogLevel::Info);
        refreshLocal();
        currentTask = launcher::Task::Idle;
    }

    bool begin()
    {
        launcher::Task expected = launcher::Task::Idle;
        if (currentTask.compare_exchange_strong(expected, launcher::Task::Checking))
            return true;
        log("Rave is busy, please wait", launcher::LogLevel::Info);
        return false;
    }
}

void launcher::setLogger(Logger l)
{
    logger = std::move(l);
}

void launcher::startMonitor()
{
    std::thread([] {
        refreshLocal();
        refreshLatest();
        DWORD lastLatest = GetTickCount();
        for (;;)
        {
            Sleep(2000);
            refreshLocal();
            if (GetTickCount() - lastLatest > 10 * 60 * 1000)
            {
                refreshLatest();
                lastLatest = GetTickCount();
            }
        }
    }).detach();
}

void launcher::launch(bool reinstall)
{
    if (begin())
        std::thread(runLaunch, reinstall).detach();
}

void launcher::closeRoblox()
{
    if (begin())
        std::thread(runClose).detach();
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
