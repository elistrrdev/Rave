#pragma once
#include <string>

namespace paths
{
    bool ensureDir(const std::wstring& dir);
    bool removeTree(const std::wstring& dir);
    bool exists(const std::wstring& path);

    std::wstring localData();
    std::wstring versions();
    std::wstring tempDir();
}
