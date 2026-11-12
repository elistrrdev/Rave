#pragma once
#include <string>

namespace news
{
    enum class State { Loading, None, Ready };

    struct Item
    {
        std::string title;
        std::string date;
        std::string body;
    };

    void fetchAsync();
    State state();
    Item item();
}
