#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace servers
{
    struct Server
    {
        std::string id;
        std::string city;
        std::string country;
        int ping = -1;
        int playing = 0;
        int maxPlayers = 0;
    };

    struct Result
    {
        bool ok = false;
        std::string error;
        std::string where;
        std::vector<Server> list;
    };

    Result find(uint64_t placeId);
    bool improveWebsiteJoin(std::string& launch, std::string& summary);
    std::string urlEncode(const std::string& text);
}
