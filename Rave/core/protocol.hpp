#pragma once
#include <string>

namespace protocol
{
    bool isLaunch(const std::wstring& arg);
    bool registered();
    bool otherRave();
    bool enable(std::string& error);
    bool disable(std::string& error);
}
