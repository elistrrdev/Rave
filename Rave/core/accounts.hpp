#pragma once
#include "launcher.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace accounts
{
    struct Account
    {
        uint64_t id = 0;
        std::string name;
        std::string displayName;
        bool quick = false;
        bool expired = false;
    };

    enum class QuickState { Idle, Starting, Waiting, Linked, SigningIn, Done, Failed };

    struct Quick
    {
        QuickState state = QuickState::Idle;
        std::string code;
        std::string linkedName;
        std::string message;
        unsigned qrVersion = 0;
    };

    enum class PasteState { Idle, Checking, Done, Failed };

    struct Paste
    {
        PasteState state = PasteState::Idle;
        std::string message;
    };

    void setLogger(launcher::Logger logger);
    void load();

    std::vector<Account> list();
    uint64_t selected();
    void select(uint64_t id);
    void remove(uint64_t id);
    unsigned addedCount();
    std::string label(const Account& account);

    void startQuick();
    void cancelQuick();
    Quick quick();
    std::string qrPng();

    void addCookie(const std::string& cookie);
    Paste paste();
    void resetPaste();

    bool avatarPng(uint64_t id, std::string& png);

    bool ticket(uint64_t id, std::string& ticket, Account& who, std::string& error);
}
