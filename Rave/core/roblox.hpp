#pragma once
#include <functional>
#include <string>

namespace roblox
{
    struct Latest
    {
        std::string guid;
        std::string version;
    };

    using Progress = std::function<bool(float fraction)>;

    bool fetchLatest(Latest& out, std::string& error);
    std::string installedGuid();
    std::wstring versionDir(const std::string& guid);
    bool install(const std::string& guid, const Progress& progress, std::string& error);
    void removeOldVersions(const std::string& keepGuid);
    bool launch(const std::string& guid, const std::wstring& args, std::string& error);
    bool isRunning();
}
