#pragma once
#include <string>

namespace fflags
{
    std::string load();
    bool validate(const std::string& text, std::string& error);
    bool save(const std::string& text, std::string& error);
    bool apply(const std::wstring& versionDir);
}
