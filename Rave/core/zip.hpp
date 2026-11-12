#pragma once
#include <string>

namespace zip
{
    bool extract(const std::string& archive, const std::wstring& destDir, std::string& error);
}
