#pragma once
#include <string>

namespace report
{
    void installCrashHandler();
    std::wstring writeBugReport(const std::string& context);
}
