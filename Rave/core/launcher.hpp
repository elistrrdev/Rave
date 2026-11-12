#pragma once
#include <functional>
#include <string>

namespace launcher
{
    enum class Task { Idle, Checking, Installing, Launching };
    enum class LogLevel { Success, Error, Warn, Info };

    struct Snapshot
    {
        std::string latest;
        bool latestKnown = false;
    };

    using Logger = std::function<void(const std::string&, LogLevel)>;

    void setLogger(Logger logger);
    void startMonitor();
    void launch();

    Task task();
    float progress();
    Snapshot snapshot();
}
