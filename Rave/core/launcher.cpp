#include "launcher.hpp"
#include "fflags.hpp"
#include "report.hpp"
#include "roblox.hpp"
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

        currentTask = launcher::Task::Idle;
        CoUninitialize();
    }
}

void launcher::setLogger(Logger l)
{
    logger = std::move(l);
}

void launcher::startMonitor()
{
    std::thread([] {
        for (;;)
        {
            refreshLatest();
            Sleep(10 * 60 * 1000);
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
