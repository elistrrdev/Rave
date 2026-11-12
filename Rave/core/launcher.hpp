#pragma once
#include <functional>
#include <string>

namespace launcher
{
    enum class Task { Idle, Checking, Installing, Launching, Closing };
    enum class LogLevel { Success, Error, Warn, Info };

    struct Snapshot
    {
        std::string installed;
        std::string latest;
        bool latestKnown = false;
        bool running = false;
    };

    using Logger = std::function<void(const std::string&, LogLevel)>;

    void setLogger(Logger logger);
    void startMonitor();
    void launch(bool reinstall);
    void closeRoblox();

    Task task();
    float progress();
    Snapshot snapshot();
}
