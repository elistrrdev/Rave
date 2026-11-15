#include "servers.hpp"
#include "json.hpp"
#include "net.hpp"
#include "system.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace
{
    struct Spot
    {
        std::string city;
        std::string country;
        double lat = 0, lon = 0;
    };

    struct Here
    {
        bool ok = false;
        std::string city;
        std::string country;
        double lat = 0, lon = 0;
    };

    const double kPi = 3.14159265358979323846;

    double km(double lat1, double lon1, double lat2, double lon2)
    {
        auto rad = [](double d) { return d * kPi / 180.0; };
        double dLat = rad(lat2 - lat1), dLon = rad(lon2 - lon1);
        double a = std::sin(dLat / 2) * std::sin(dLat / 2) + std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dLon / 2) * std::sin(dLon / 2);
        return 6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    }

    int pingFor(double distance)
    {
        return std::clamp((int)std::lround(8.0 + distance / 60.0), 1, 999);
    }

    bool getJson(const std::wstring& url, json::Value& out, int* status = nullptr)
    {
        net::Response r;
        bool sent = net::get(url, r, nullptr, L"Accept: application/json\r\n", 15000);
        if (status)
            *status = r.status;
        return sent && r.ok() && json::parse(r.body, out);
    }

    double number(const json::Value& v)
    {
        if (v.type == json::Value::Type::Number)
            return v.number;
        if (v.isString())
            return strtod(v.str().c_str(), nullptr);
        return 0;
    }

    Here locate()
    {
        Here h;
        json::Value v;
        if (getJson(L"https://ipinfo.io/json", v) && v.isObject())
        {
            const std::string& loc = v["loc"].str();
            size_t comma = loc.find(',');
            if (comma != std::string::npos)
            {
                h.lat = strtod(loc.substr(0, comma).c_str(), nullptr);
                h.lon = strtod(loc.substr(comma + 1).c_str(), nullptr);
                h.city = v["city"].str();
                h.country = v["country"].str();
                h.ok = !h.country.empty() && (h.lat != 0 || h.lon != 0);
            }
        }
        if (h.ok)
            return h;

        json::Value w;
        if (getJson(L"https://ipwho.is/", w) && w.isObject() && w["success"].boolean)
        {
            h.lat = number(w["latitude"]);
            h.lon = number(w["longitude"]);
            h.city = w["city"].str();
            h.country = w["country_code"].str();
            h.ok = !h.country.empty() && (h.lat != 0 || h.lon != 0);
        }
        return h;
    }

    bool datacenters(std::map<int64_t, Spot>& byId, std::vector<Spot>& spots)
    {
        json::Value v;
        if (!getJson(L"https://apis.rovalra.com/v1/datacenters/list", v) || v.type != json::Value::Type::Array)
            return false;
        for (const json::Value& item : v.items)
        {
            if (item["inactive"].boolean)
                continue;
            const json::Value& loc = item["location"];
            Spot s;
            s.city = loc["city"].str();
            s.country = loc["country"].str();
            s.lat = number(loc["latLong"][0]);
            s.lon = number(loc["latLong"][1]);
            if (s.country.size() != 2 || (s.lat == 0 && s.lon == 0))
                continue;
            spots.push_back(s);
            for (const json::Value& id : item["dataCenterIds"].items)
                byId[(int64_t)number(id)] = s;
        }
        return !spots.empty();
    }

    struct Live
    {
        int playing = 0;
        int maxPlayers = 0;
    };

    bool liveServers(uint64_t placeId, std::map<std::string, Live>& out, std::string& error)
    {
        std::string cursor;
        for (int page = 0; page < 5; ++page)
        {
            std::wstring url = L"https://games.roblox.com/v1/games/" + std::to_wstring(placeId) +
                L"/servers/Public?limit=100&excludeFullGames=true";
            if (!cursor.empty())
                url += L"&cursor=" + sys::widen(servers::urlEncode(cursor));
            json::Value v;
            int status = 0;
            if (!getJson(url, v, &status))
            {
                if (page == 0)
                    error = status == 429 ? "Roblox is busy, try again in a minute" : status == 400 ? "This place was not found" : "Could not load the server list";
                break;
            }
            for (const json::Value& s : v["data"].items)
            {
                Live l;
                l.playing = (int)number(s["playing"]);
                l.maxPlayers = (int)number(s["maxPlayers"]);
                if (!s["id"].str().empty() && l.maxPlayers > 0 && l.playing < l.maxPlayers)
                    out[s["id"].str()] = l;
            }
            cursor = v["nextPageCursor"].str();
            if (cursor.empty())
                break;
            Sleep(250);
        }
        return !out.empty();
    }

    std::string urlDecode(const std::string& text)
    {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            char c = text[i];
            if (c == '%' && i + 2 < text.size() && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2]))
            {
                out += (char)strtol(text.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            }
            else
                out += c;
        }
        return out;
    }

    std::string queryValue(const std::string& query, const std::string& name)
    {
        size_t at = 0;
        while (at <= query.size())
        {
            size_t end = query.find('&', at);
            if (end == std::string::npos)
                end = query.size();
            std::string part = query.substr(at, end - at);
            size_t eq = part.find('=');
            std::string key = part.substr(0, eq);
            if (_stricmp(key.c_str(), name.c_str()) == 0)
                return eq == std::string::npos ? std::string() : part.substr(eq + 1);
            at = end + 1;
        }
        return {};
    }

    bool digitsOnly(const std::string& s)
    {
        return !s.empty() && s.size() <= 19 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    }
}

std::string servers::urlEncode(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool servers::placeFromText(const std::string& text, uint64_t& placeId)
{
    std::string t = text;
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r' || t.back() == '\n' || t.back() == '/'))
        t.pop_back();
    size_t start = t.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return false;
    t = t.substr(start);

    std::string digits;
    if (digitsOnly(t))
        digits = t;
    else
    {
        std::string lower = t;
        for (char& c : lower)
            c = (char)tolower((unsigned char)c);
        auto digitsAt = [&](const char* marker) {
            size_t at = lower.find(marker);
            if (at == std::string::npos)
                return std::string();
            at += strlen(marker);
            size_t end = at;
            while (end < t.size() && isdigit((unsigned char)t[end]))
                ++end;
            return t.substr(at, end - at);
        };
        digits = digitsAt("placeid=");
        if (digits.empty())
            digits = digitsAt("/games/");
    }
    if (!digitsOnly(digits))
        return false;
    placeId = _strtoui64(digits.c_str(), nullptr, 10);
    return placeId > 0;
}

servers::Result servers::find(uint64_t placeId)
{
    Result result;

    Here here = locate();
    if (!here.ok)
    {
        result.error = "Could not find your location";
        return result;
    }
    result.where = here.city.empty() ? here.country : here.city + ", " + here.country;

    std::map<int64_t, Spot> byId;
    std::vector<Spot> spots;
    if (!datacenters(byId, spots))
    {
        result.error = "Could not load the Roblox server locations";
        return result;
    }

    std::map<std::string, double> nearest;
    for (const Spot& s : spots)
    {
        double d = km(here.lat, here.lon, s.lat, s.lon);
        auto it = nearest.find(s.country);
        if (it == nearest.end() || d < it->second)
            nearest[s.country] = d;
    }
    std::vector<std::pair<double, std::string>> countries;
    for (auto& [code, d] : nearest)
        countries.push_back({ d, code });
    std::sort(countries.begin(), countries.end());

    std::map<std::string, Live> live;
    std::string error;
    if (!liveServers(placeId, live, error))
    {
        result.error = error.empty() ? "No open public server right now" : error;
        return result;
    }

    std::set<std::string> taken;
    int bestPing = -1;
    int asked = 0;
    for (const auto& [distance, code] : countries)
    {
        int countryPing = pingFor(distance);
        if (asked >= 4 || (bestPing >= 0 && countryPing > bestPing + 25))
            break;
        ++asked;

        json::Value v;
        std::wstring url = L"https://apis.rovalra.com/v1/servers/region?place_id=" + std::to_wstring(placeId) + L"&region=" + sys::widen(code);
        if (!getJson(url, v) || !v.isObject())
            continue;
        for (const json::Value& s : v["servers"].items)
        {
            const std::string& id = s["server_id"].str();
            auto l = live.find(id);
            if (l == live.end() || taken.count(id))
                continue;
            Server out;
            out.id = id;
            auto dc = byId.find((int64_t)number(s["datacenter_id"]));
            double d = distance;
            if (dc != byId.end())
            {
                d = km(here.lat, here.lon, dc->second.lat, dc->second.lon);
                out.city = dc->second.city;
            }
            if (out.city.empty())
                out.city = s["city"].str();
            out.country = code;
            out.ping = pingFor(d);
            out.playing = l->second.playing;
            out.maxPlayers = l->second.maxPlayers;
            taken.insert(id);
            result.list.push_back(out);
            if (bestPing < 0 || out.ping < bestPing)
                bestPing = out.ping;
        }
    }

    if (result.list.empty())
    {
        result.error = "No open server near you was found";
        return result;
    }

    std::sort(result.list.begin(), result.list.end(), [](const Server& a, const Server& b) {
        int ba = a.ping / 5, bb = b.ping / 5;
        if (ba != bb)
            return ba < bb;
        bool fa = a.maxPlayers - a.playing >= 2, fb = b.maxPlayers - b.playing >= 2;
        if (fa != fb)
            return fa;
        return a.playing > b.playing;
    });
    if (result.list.size() > 5)
        result.list.resize(5);
    result.ok = true;
    return result;
}

std::string servers::placeLauncherUrl(uint64_t placeId, const std::string& jobId, const std::string& tracker)
{
    return "https://www.roblox.com/Game/PlaceLauncher.ashx?request=RequestGameJob&browserTrackerId=" + tracker +
        "&placeId=" + std::to_string(placeId) + "&gameId=" + jobId + "&isPlayTogetherGame=false";
}

bool servers::improveWebsiteJoin(std::string& launch, std::string& summary)
{
    const std::string key = "+placelauncherurl:";
    size_t at = launch.find(key);
    if (at == std::string::npos)
        return false;
    size_t valueStart = at + key.size();
    size_t valueEnd = launch.find('+', valueStart);
    if (valueEnd == std::string::npos)
        valueEnd = launch.size();

    std::string url = urlDecode(launch.substr(valueStart, valueEnd - valueStart));
    size_t q = url.find('?');
    if (q == std::string::npos)
        return false;
    std::string base = url.substr(0, q);
    std::string query = url.substr(q + 1);
    std::string lowerBase = base;
    for (char& c : lowerBase)
        c = (char)tolower((unsigned char)c);
    if (lowerBase.find("roblox.com/game/placelauncher.ashx") == std::string::npos)
        return false;
    if (_stricmp(queryValue(query, "request").c_str(), "RequestGame") != 0 || !queryValue(query, "gameId").empty())
        return false;

    uint64_t placeId = 0;
    std::string place = queryValue(query, "placeId");
    if (!digitsOnly(place))
        return false;
    placeId = _strtoui64(place.c_str(), nullptr, 10);

    Result found = find(placeId);
    if (!found.ok)
    {
        summary = found.error;
        return false;
    }
    const Server& best = found.list.front();

    std::string rebuilt;
    size_t pos = 0;
    while (pos <= query.size())
    {
        size_t end = query.find('&', pos);
        if (end == std::string::npos)
            end = query.size();
        std::string part = query.substr(pos, end - pos);
        std::string name = part.substr(0, part.find('='));
        if (_stricmp(name.c_str(), "request") == 0)
            part = "request=RequestGameJob";
        if (!part.empty())
        {
            if (!rebuilt.empty())
                rebuilt += '&';
            rebuilt += part;
        }
        pos = end + 1;
    }
    rebuilt += "&gameId=" + best.id;

    launch = launch.substr(0, valueStart) + urlEncode(base + "?" + rebuilt) + launch.substr(valueEnd);
    summary = best.city + " (about " + std::to_string(best.ping) + " ms, " + std::to_string(best.playing) + "/" + std::to_string(best.maxPlayers) + " players)";
    return true;
}
