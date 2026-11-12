#pragma once
#include <functional>
#include <string>

namespace launcher
{
    enum class Task { Idle, Checking, Installing, Launching };
    enum class LogLevel { Success, Error, Warn, Info };
    enum WindowAction { WindowMinimize = 0, WindowHide = 1, WindowRestore = 2 };

    constexpr unsigned WM_RAVE_WINDOW = 0x8000 + 10;

    struct Snapshot
    {
        std::string latest;
        bool latestKnown = false;
    };

    using Logger = std::function<void(const std::string&, LogLevel)>;

    void setLogger(Logger logger);
    void setWindow(void* hwnd);
    void startMonitor();
    void launch();

    Task task();
    float progress();
    Snapshot snapshot();
}
