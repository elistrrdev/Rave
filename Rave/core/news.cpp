#include "news.hpp"
#include "json.hpp"
#include "net.hpp"
#include "../version.hpp"
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
    std::atomic<news::State> current{ news::State::Loading };
    std::mutex lock;
    news::Item latest;

    void trim(std::string& s)
    {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
            s.pop_back();
        size_t start = s.find_first_not_of(" \t");
        s = start == std::string::npos ? std::string() : s.substr(start);
    }

    void replaceAll(std::string& s, const std::string& from, const std::string& to)
    {
        for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
            s.replace(at, from.size(), to);
    }

    std::string clean(const std::string& markdown)
    {
        std::istringstream in(markdown);
        std::string line, out;
        int blank = 0;
        while (std::getline(in, line))
        {
            trim(line);
            while (!line.empty() && line[0] == '#')
                line.erase(0, 1);
            trim(line);
            if (line.rfind("* ", 0) == 0 || line.rfind("+ ", 0) == 0)
                line[0] = '-';
            replaceAll(line, "**", "");
            replaceAll(line, "__", "");
            replaceAll(line, "`", "");
            replaceAll(line, "\xE2\x80\x94", "-");
            replaceAll(line, "\xE2\x80\x93", "-");

            if (line.empty())
            {
                if (++blank > 1 || out.empty())
                    continue;
            }
            else
                blank = 0;
            out += line + "\n";
        }
        while (!out.empty() && out.back() == '\n')
            out.pop_back();
        return out;
    }

    void run()
    {
        net::Response r;
        const std::wstring headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        json::Value v;
        if (!net::get(RAVE_RELEASE_API, r, nullptr, headers) || r.status != 200 || !json::parse(r.body, v) || !v.isObject())
        {
            current = news::State::None;
            return;
        }

        news::Item item;
        item.title = v["name"].str();
        if (item.title.empty())
            item.title = v["tag_name"].str();
        item.date = v["published_at"].str().substr(0, 10);
        item.body = clean(v["body"].str());

        if (item.title.empty() && item.body.empty())
        {
            current = news::State::None;
            return;
        }

        {
            std::lock_guard<std::mutex> guard(lock);
            latest = item;
        }
        current = news::State::Ready;
    }
}

void news::fetchAsync()
{
    std::thread(run).detach();
}

news::State news::state()
{
    return current.load();
}

news::Item news::item()
{
    std::lock_guard<std::mutex> guard(lock);
    return latest;
}
