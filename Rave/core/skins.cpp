#include "skins.hpp"
#include "json.hpp"
#include "mesh.hpp"
#include "net.hpp"
#include "paths.hpp"
#include "rbxcache.hpp"
#include "system.hpp"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace
{
    const int kCatalogRes = 200;
    const unsigned kPreviewW = 432, kPreviewH = 264;
    const unsigned kThumbSide = 64;
    const int kSig = 16;

    struct Theme
    {
        const char* name;
        uint32_t dark;
        uint32_t light;
        bool rainbow;
    };

    const Theme kThemes[] = {
        { "Gold", 0x6a4a0e, 0xffe08a, false },
        { "Arctic", 0x5e7486, 0xf4faff, false },
        { "Crimson", 0x5a0c14, 0xff5a64, false },
        { "Emerald", 0x0a4a2c, 0x7af0b0, false },
        { "Amethyst", 0x3a1a66, 0xd2a6ff, false },
        { "Midnight", 0x0c1638, 0x6a94ff, false },
        { "Rainbow", 0, 0, true },
    };

    struct FileSkin
    {
        std::string target;
        std::string name;
        int res = 0;
        int meshRes = 0;
        std::string url;
        std::string source;
    };

    struct GameData
    {
        std::vector<FileSkin> files;
    };

    struct Record
    {
        std::string entry;
        std::string texture;
        std::string skin;
        uint32_t hash = 0;
    };

    struct Sig
    {
        uint8_t v[kSig * kSig * 3];
    };

    std::vector<skins::Game> catalog;
    std::vector<GameData> extra;
    std::map<std::string, std::set<std::string>> seeds;
    skins::Logger logger;

    std::mutex stateLock;
    std::map<std::string, std::string> chosen;
    std::map<std::string, std::set<std::string>> known;
    std::vector<Record> records;
    std::string statusText;

    std::mutex localLock;
    std::map<std::string, std::vector<std::wstring>> localFiles;

    std::mutex applyLock;
    std::mutex wakeLock;
    std::condition_variable wake;
    std::atomic<bool> applyWanted{ false };
    std::atomic<bool> waitingForRoblox{ false };

    std::mutex artLock;
    std::condition_variable artWake;
    std::deque<std::string> artQueue;
    std::map<std::string, std::pair<skins::Art, img::Rgba>> artDone;
    std::map<int, mesh::Mesh> meshes;

    void log(const std::string& text, skins::Level level)
    {
        if (logger)
            logger(text, level);
    }

    void setStatus(const std::string& text)
    {
        std::lock_guard<std::mutex> guard(stateLock);
        statusText = text;
    }

    bool resource(int id, std::string& out)
    {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
        HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
        const char* data = mem ? (const char*)LockResource(mem) : nullptr;
        if (!data)
            return false;
        out.assign(data, SizeofResource(nullptr, res));
        return true;
    }

    bool readFile(const std::wstring& path, std::string& out, size_t limit = 32u << 20)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && (uint64_t)size.QuadPart <= limit;
        if (ok)
        {
            out.resize((size_t)size.QuadPart);
            DWORD read = 0;
            ok = ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size();
        }
        CloseHandle(h);
        return ok;
    }

    bool writeFile(const std::wstring& path, const std::string& data)
    {
        std::wstring tmp = path + L".tmp";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size());
        CloseHandle(h);
        if (ok)
            ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        if (!ok)
            DeleteFileW(tmp.c_str());
        return ok;
    }

    std::wstring skinsDir()
    {
        std::wstring dir = paths::localData() + L"\\Skins";
        paths::ensureDir(dir);
        return dir;
    }

    std::wstring downloadsDir()
    {
        std::wstring dir = skinsDir() + L"\\downloads";
        paths::ensureDir(dir);
        return dir;
    }

    std::wstring originalsDir()
    {
        std::wstring dir = skinsDir() + L"\\originals";
        paths::ensureDir(dir);
        return dir;
    }

    std::wstring statePath()
    {
        return paths::localData() + L"\\skins.json";
    }

    std::wstring safeName(const std::string& name)
    {
        std::wstring w = sys::widen(name);
        for (wchar_t& c : w)
            if (c < 32 || wcschr(L"<>:\"/\\|?*", c))
                c = L'-';
        return w;
    }

    std::wstring targetFolder(const skins::Game& g, const skins::Target& t)
    {
        return skinsDir() + L"\\" + safeName(g.name) + L"\\" + safeName(t.name);
    }

    std::string slotKey(size_t game, size_t target)
    {
        return catalog[game].id + "/" + catalog[game].targets[target].id;
    }

    bool validGame(size_t game, size_t target)
    {
        return game < catalog.size() && target < catalog[game].targets.size();
    }

    const Theme* themeByName(const std::string& name)
    {
        for (const Theme& t : kThemes)
            if (name == t.name)
                return &t;
        return nullptr;
    }

    float lum(const uint8_t* p)
    {
        return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
    }

    void hsv(float h, float s, float v, float out[3])
    {
        h = std::fmod(h, 360.f);
        if (h < 0)
            h += 360.f;
        float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.f, 2.f) - 1)), m = v - c;
        float r = 0, g = 0, b = 0;
        if (h < 60) { r = c; g = x; }
        else if (h < 120) { r = x; g = c; }
        else if (h < 180) { g = c; b = x; }
        else if (h < 240) { g = x; b = c; }
        else if (h < 300) { r = x; b = c; }
        else { r = c; b = x; }
        out[0] = (r + m) * 255;
        out[1] = (g + m) * 255;
        out[2] = (b + m) * 255;
    }

    img::Rgba recolor(const img::Rgba& src, const Theme& theme)
    {
        img::Rgba out = src;
        if (src.empty())
            return out;
        std::vector<float> values;
        values.reserve((size_t)src.w * src.h);
        for (size_t i = 0; i < src.px.size(); i += 4)
            if (src.px[i + 3] > 8)
                values.push_back(lum(&src.px[i]));
        if (values.empty())
            return out;
        std::sort(values.begin(), values.end());
        float lo = values[values.size() * 2 / 100], hi = values[std::min(values.size() - 1, values.size() * 98 / 100)];
        if (hi - lo < 24.f)
        {
            float mid = (hi + lo) / 2;
            lo = std::max(0.f, mid - 12.f);
            hi = std::min(255.f, mid + 12.f);
        }
        float dark[3] = { (float)((theme.dark >> 16) & 0xFF), (float)((theme.dark >> 8) & 0xFF), (float)(theme.dark & 0xFF) };
        float light[3] = { (float)((theme.light >> 16) & 0xFF), (float)((theme.light >> 8) & 0xFF), (float)(theme.light & 0xFF) };
        for (unsigned y = 0; y < src.h; ++y)
        {
            for (unsigned x = 0; x < src.w; ++x)
            {
                uint8_t* p = &out.px[((size_t)y * src.w + x) * 4];
                float t = std::clamp((lum(p) - lo) / (hi - lo), 0.f, 1.f);
                t = 0.30f + 0.70f * t;
                float c[3];
                if (theme.rainbow)
                    hsv(360.f * x / src.w + 50.f * y / src.h, 0.70f, 0.40f + 0.60f * t, c);
                else
                    for (int k = 0; k < 3; ++k)
                        c[k] = dark[k] + (light[k] - dark[k]) * t;
                for (int k = 0; k < 3; ++k)
                    p[k] = (uint8_t)std::clamp(c[k] + 0.5f, 0.f, 255.f);
            }
        }
        return out;
    }

    bool allowedDownload(const std::string& url)
    {
        return url.rfind("https://raw.githubusercontent.com/", 0) == 0;
    }

    bool allowedThumb(const std::string& url)
    {
        if (url.rfind("https://", 0) != 0)
            return false;
        size_t end = url.find('/', 8);
        std::string host = url.substr(8, end == std::string::npos ? std::string::npos : end - 8);
        const std::string suffix = ".rbxcdn.com";
        return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::wstring urlFile(const std::string& url)
    {
        std::string h = sys::sha256Hex(url.data(), url.size());
        return downloadsDir() + L"\\" + sys::widen(h.substr(0, 24)) + L".png";
    }

    bool download(const std::string& url, std::string& out)
    {
        std::wstring file = urlFile(url);
        if (readFile(file, out))
            return true;
        if (!allowedDownload(url))
            return false;
        net::Response r;
        if (!net::get(sys::widen(url), r, nullptr, L"", 30000) || !r.ok() || r.body.empty() || r.body.size() > (16u << 20))
            return false;
        img::Rgba check;
        if (!img::decodePng(r.body.data(), r.body.size(), check))
            return false;
        writeFile(file, r.body);
        out = std::move(r.body);
        return true;
    }

    bool thumbnail(const std::string& assetId, img::Rgba& out)
    {
        std::wstring file = downloadsDir() + L"\\thumb-" + sys::widen(assetId) + L".png";
        std::string png;
        if (readFile(file, png) && img::decodePng(png.data(), png.size(), out))
            return true;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (attempt)
                Sleep(1500);
            net::Response r;
            std::wstring api = L"https://thumbnails.roblox.com/v1/assets?assetIds=" + sys::widen(assetId) + L"&returnPolicy=PlaceHolder&size=420x420&format=Png&isCircular=false";
            if (!net::get(api, r, nullptr, L"Accept: application/json\r\n", 15000) || !r.ok())
                continue;
            json::Value v;
            if (!json::parse(r.body, v))
                continue;
            const json::Value& item = v["data"][0];
            if (item["state"].str() != "Completed")
                continue;
            std::string url = item["imageUrl"].str();
            if (!allowedThumb(url))
                return false;
            net::Response img;
            if (!net::get(sys::widen(url), img, nullptr, L"", 20000) || !img.ok())
                continue;
            if (!img::decodePng(img.body.data(), img.body.size(), out))
                return false;
            writeFile(file, img.body);
            return true;
        }
        return false;
    }

    void loadCatalog()
    {
        std::string text;
        json::Value root;
        if (!resource(kCatalogRes, text) || !json::parse(text, root))
            return;
        const json::Value& list = root["games"];
        for (const json::Value& g : list.items)
        {
            skins::Game game;
            GameData data;
            game.id = g["id"].str();
            game.name = g["name"].str();
            game.source = g["source"].str();
            game.themes = g["themes"].type == json::Value::Type::Bool && g["themes"].boolean;
            for (const json::Value& t : g["targets"].items)
            {
                skins::Target target;
                target.id = t["id"].str();
                target.name = t["name"].str();
                for (const json::Value& id : t["textures"].items)
                    if (id.isString() && !id.str().empty())
                        target.textures.push_back(id.str());
                for (const json::Value& id : t["meshes"].items)
                    if (id.isString() && !id.str().empty())
                        target.meshes.push_back(id.str());
                for (const char* field : { "texturePrints", "meshPrints" })
                {
                    const std::vector<std::string>& ids = std::string(field) == "texturePrints" ? target.textures : target.meshes;
                    for (const json::Value& p : t[field].items)
                        if (p.isString() && !ids.empty())
                            for (const std::string& id : ids)
                                seeds[id].insert(p.str());
                }
                target.meshVerts = t["meshVerts"].str();
                target.ktx2 = t["format"].str() == "ktx2";
                target.meshRes = (int)t["mesh"].number;
                target.textureRes = (int)t["texture"].number;
                if (!target.id.empty() && !target.textures.empty())
                    game.targets.push_back(target);
            }
            for (const json::Value& s : g["skins"].items)
            {
                FileSkin f;
                f.target = s["target"].str();
                f.name = s["name"].str();
                f.res = (int)s["res"].number;
                f.meshRes = (int)s["meshRes"].number;
                f.url = s["url"].str();
                f.source = s["source"].str();
                if (!f.target.empty() && !f.name.empty() && (f.res > 0 || allowedDownload(f.url)))
                    data.files.push_back(f);
            }
            if (!game.id.empty() && !game.targets.empty())
            {
                catalog.push_back(game);
                extra.push_back(data);
            }
        }
    }

    void loadState()
    {
        std::string text;
        json::Value root;
        if (!readFile(statePath(), text) || !json::parse(text, root))
            return;
        std::set<std::string> slots;
        for (const skins::Game& g : catalog)
            for (const skins::Target& t : g.targets)
                slots.insert(g.id + "/" + t.id);
        const json::Value& sel = root["selected"];
        for (size_t i = 0; i < sel.keys.size(); ++i)
            if (sel.items[i].isString() && !sel.items[i].str().empty() && slots.count(sel.keys[i]))
                chosen[sel.keys[i]] = sel.items[i].str();
        const json::Value& kn = root["known"];
        for (size_t i = 0; i < kn.keys.size(); ++i)
            for (const json::Value& f : kn.items[i].items)
                if (f.isString())
                    known[kn.keys[i]].insert(f.str());
        for (const json::Value& r : root["applied"].items)
        {
            Record rec;
            rec.entry = r["entry"].str();
            rec.texture = r["texture"].str();
            rec.skin = r["skin"].str();
            rec.hash = (uint32_t)r["hash"].number;
            if (rec.entry.size() == 32 && !rec.texture.empty())
                records.push_back(rec);
        }
    }

    void saveState()
    {
        std::string out = "{\n    \"selected\": {";
        bool first = true;
        for (const auto& [k, v] : chosen)
        {
            out += first ? "\n" : ",\n";
            out += "        " + json::quote(k) + ": " + json::quote(v);
            first = false;
        }
        out += first ? "},\n" : "\n    },\n";
        out += "    \"known\": {";
        first = true;
        for (const auto& [k, set] : known)
        {
            out += first ? "\n" : ",\n";
            out += "        " + json::quote(k) + ": [";
            bool inner = true;
            for (const std::string& f : set)
            {
                out += (inner ? "" : ", ") + json::quote(f);
                inner = false;
            }
            out += "]";
            first = false;
        }
        out += first ? "},\n" : "\n    },\n";
        out += "    \"applied\": [";
        first = true;
        for (const Record& r : records)
        {
            out += first ? "\n" : ",\n";
            out += "        { \"entry\": " + json::quote(r.entry) + ", \"texture\": " + json::quote(r.texture) +
                ", \"skin\": " + json::quote(r.skin) + ", \"hash\": " + std::to_string(r.hash) + " }";
            first = false;
        }
        out += first ? "]\n}\n" : "\n    ]\n}\n";
        writeFile(statePath(), out);
    }

    std::string fingerprint(uint32_t len, uint32_t hash)
    {
        return std::to_string(len) + ":" + std::to_string(hash);
    }

    bool robloxBusy()
    {
        return !sys::findProcesses(L"RobloxPlayerBeta.exe").empty() || !sys::findProcesses(L"RobloxStudioBeta.exe").empty();
    }

    Sig signature(const img::Rgba& image)
    {
        Sig s{};
        img::Rgba tiny = img::resize(image, kSig, kSig);
        for (int i = 0; i < kSig * kSig; ++i)
        {
            const uint8_t* p = &tiny.px[(size_t)i * 4];
            s.v[i * 3] = (uint8_t)((p[0] * p[3] + 127) / 255);
            s.v[i * 3 + 1] = (uint8_t)((p[1] * p[3] + 127) / 255);
            s.v[i * 3 + 2] = (uint8_t)((p[2] * p[3] + 127) / 255);
        }
        return s;
    }

    float distance(const Sig& a, const Sig& b)
    {
        int d = 0;
        for (int i = 0; i < kSig * kSig * 3; ++i)
            d += std::abs((int)a.v[i] - (int)b.v[i]);
        return (float)d / (kSig * kSig * 3);
    }

    struct Region
    {
        bool plausible = false;
        Sig sig{};
    };

    Region thumbRegion(const img::Rgba& thumb, unsigned w, unsigned h)
    {
        Region out;
        double r = (double)w / h;
        unsigned rw = thumb.w, rh = thumb.h;
        if (r >= 1.0)
            rh = std::max(1u, (unsigned)(thumb.w / r + 0.5));
        else
            rw = std::max(1u, (unsigned)(thumb.h * r + 0.5));
        rw = std::min(rw, thumb.w);
        rh = std::min(rh, thumb.h);
        unsigned x0 = (thumb.w - rw) / 2, y0 = (thumb.h - rh) / 2;

        uint64_t alpha = 0, count = 0;
        for (unsigned y = 0; y < thumb.h; ++y)
            for (unsigned x = 0; x < thumb.w; ++x)
            {
                if (x >= x0 + 2 && x + 2 < x0 + rw && y >= y0 + 2 && y + 2 < y0 + rh)
                    continue;
                if (x >= x0 && x < x0 + rw && y >= y0 && y < y0 + rh)
                    continue;
                alpha += thumb.px[((size_t)y * thumb.w + x) * 4 + 3];
                ++count;
            }
        out.plausible = count == 0 || alpha <= count * 8;
        if (out.plausible)
            out.sig = signature(img::crop(thumb, x0, y0, rw, rh));
        return out;
    }

    struct Candidate
    {
        size_t index;
        unsigned w, h;
        const Sig* sig;
    };

    struct Scanned
    {
        uint32_t hash = 0;
        unsigned w = 0, h = 0;
        bool image = false;
        Sig sig{};
    };

    std::map<std::string, Scanned> scanCache;
    bool scanLoaded = false;
    bool scanDirty = false;
    const char kScanMagic[8] = { 'R', 'A', 'V', 'E', 'S', 'C', 'N', '1' };

    std::wstring scanPath()
    {
        return skinsDir() + L"\\scan.bin";
    }

    void loadScan()
    {
        scanLoaded = true;
        std::string data;
        if (!readFile(scanPath(), data, 64u << 20) || data.size() < 8 || memcmp(data.data(), kScanMagic, 8) != 0)
            return;
        size_t pos = 8;
        const size_t fixed = 32 + 4 + 2 + 2 + 1;
        while (pos + fixed <= data.size())
        {
            std::string id = data.substr(pos, 32);
            Scanned s;
            memcpy(&s.hash, data.data() + pos + 32, 4);
            uint16_t w = 0, h = 0;
            memcpy(&w, data.data() + pos + 36, 2);
            memcpy(&h, data.data() + pos + 38, 2);
            s.w = w;
            s.h = h;
            s.image = data[pos + 40] != 0;
            pos += fixed;
            if (s.image)
            {
                if (pos + sizeof(s.sig.v) > data.size())
                    break;
                memcpy(s.sig.v, data.data() + pos, sizeof(s.sig.v));
                pos += sizeof(s.sig.v);
            }
            scanCache[id] = s;
        }
    }

    void saveScan(const std::vector<rbxcache::Entry>& entries)
    {
        if (!scanDirty)
            return;
        std::set<std::string> present;
        for (const rbxcache::Entry& e : entries)
            present.insert(e.id);
        std::string out(kScanMagic, 8);
        for (const auto& [id, s] : scanCache)
        {
            if (!present.count(id) || id.size() != 32)
                continue;
            out += id;
            out.append((const char*)&s.hash, 4);
            uint16_t w = (uint16_t)std::min(s.w, 65535u), h = (uint16_t)std::min(s.h, 65535u);
            out.append((const char*)&w, 2);
            out.append((const char*)&h, 2);
            out += (char)(s.image ? 1 : 0);
            if (s.image)
                out.append((const char*)s.sig.v, sizeof(s.sig.v));
        }
        if (writeFile(scanPath(), out))
            scanDirty = false;
    }

    std::vector<Candidate> candidates(rbxcache::Store& store, const std::vector<rbxcache::Entry>& entries, const std::set<std::string>& owned)
    {
        if (!scanLoaded)
            loadScan();
        std::vector<Candidate> out;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const rbxcache::Entry& e = entries[i];
            if (owned.count(e.id) || e.bodyLen < 24 || e.bodyLen > (24u << 20))
                continue;
            auto it = scanCache.find(e.id);
            if (it == scanCache.end() || it->second.hash != e.bodyHash)
            {
                Scanned s;
                s.hash = e.bodyHash;
                std::string head;
                if (store.readHead(e, head, 64))
                {
                    img::Probe p = img::probe(head.data(), head.size());
                    if (p.kind != img::Kind::Unknown && p.w && p.h && std::max(p.w, p.h) <= 1024 && p.w <= p.h * 5 && p.h <= p.w * 5)
                    {
                        std::string body;
                        img::Rgba pixels;
                        if (store.readBody(e, body))
                        {
                            bool ok = p.kind == img::Kind::Ktx2 ? img::decodeKtx2(body.data(), body.size(), pixels, kSig) : img::decodePng(body.data(), body.size(), pixels);
                            if (ok && !pixels.empty())
                            {
                                s.image = true;
                                s.w = p.w;
                                s.h = p.h;
                                s.sig = signature(pixels);
                            }
                        }
                    }
                }
                it = scanCache.insert_or_assign(e.id, s).first;
                scanDirty = true;
            }
            if (it->second.image)
                out.push_back({ i, it->second.w, it->second.h, &it->second.sig });
        }
        saveScan(entries);
        return out;
    }

    std::vector<size_t> match(const img::Rgba& thumb, const std::vector<Candidate>& list)
    {
        std::map<long, Region> regions;
        std::vector<std::pair<float, size_t>> scored;
        for (const Candidate& c : list)
        {
            long key = std::lround((double)c.w / c.h * 100.0);
            auto it = regions.find(key);
            if (it == regions.end())
                it = regions.emplace(key, thumbRegion(thumb, c.w, c.h)).first;
            if (it->second.plausible)
                scored.push_back({ distance(it->second.sig, *c.sig), c.index });
        }
        std::sort(scored.begin(), scored.end());
        std::vector<size_t> out;
        if (scored.empty() || scored[0].first > 4.0f)
            return out;
        float best = scored[0].first;
        float limit = std::min(best + std::max(1.0f, best * 0.6f), 4.5f);
        for (const auto& s : scored)
        {
            if (s.first > limit)
                break;
            out.push_back(s.second);
        }
        return out;
    }

    struct Want
    {
        size_t game;
        size_t target;
        std::string skin;
        bool mesh = false;
    };

    const FileSkin* fileSkin(size_t game, size_t target, const std::string& key)
    {
        if (game >= extra.size() || target >= catalog[game].targets.size())
            return nullptr;
        const std::string& id = catalog[game].targets[target].id;
        for (const FileSkin& f : extra[game].files)
        {
            if (f.target != id)
                continue;
            if ((f.res > 0 && key == "res:" + std::to_string(f.res)) || (!f.url.empty() && key == "url:" + f.url))
                return &f;
        }
        return nullptr;
    }

    std::string unhex(const std::string& hex)
    {
        std::string out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            out += (char)strtol(hex.substr(i, 2).c_str(), nullptr, 16);
        return out;
    }

    std::vector<size_t> findMesh(rbxcache::Store& store, const std::vector<rbxcache::Entry>& entries, const std::set<std::string>& owned, const std::string& verts)
    {
        std::vector<size_t> out;
        std::string pattern = unhex(verts);
        if (pattern.size() < 24)
            return out;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const rbxcache::Entry& e = entries[i];
            if (owned.count(e.id) || e.bodyLen < 64 || e.bodyLen > (32u << 20))
                continue;
            std::string head;
            if (!store.readHead(e, head, 12) || head.compare(0, 8, "version ") != 0)
                continue;
            std::string body;
            if (store.readBody(e, body) && body.find(pattern) != std::string::npos)
                out.push_back(i);
        }
        return out;
    }

    bool skinBytes(const Want& want, std::string& out)
    {
        const std::string& key = want.skin;
        if (key.rfind("res:", 0) == 0)
            return resource(atoi(key.c_str() + 4), out);
        if (key.rfind("url:", 0) == 0)
            return download(key.substr(4), out);
        if (key.rfind("local:", 0) == 0)
        {
            const skins::Game& g = catalog[want.game];
            return readFile(targetFolder(g, g.targets[want.target]) + L"\\" + sys::widen(key.substr(6)), out);
        }
        return false;
    }

    std::wstring backupPath(const std::string& entry)
    {
        return originalsDir() + L"\\" + sys::widen(entry) + L".bin";
    }

    bool runApply(bool scan)
    {
        std::lock_guard<std::mutex> busy(applyLock);
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        std::map<std::string, Want> wanted;
        std::vector<Record> recs;
        std::map<std::string, std::set<std::string>> knownNow;
        {
            std::lock_guard<std::mutex> guard(stateLock);
            for (size_t g = 0; g < catalog.size(); ++g)
                for (size_t t = 0; t < catalog[g].targets.size(); ++t)
                {
                    auto it = chosen.find(slotKey(g, t));
                    if (it == chosen.end() || it->second.empty())
                        continue;
                    for (const std::string& tex : catalog[g].targets[t].textures)
                        wanted[tex] = { g, t, it->second, false };
                    const FileSkin* f = fileSkin(g, t, it->second);
                    if (f && f->meshRes > 0)
                        for (const std::string& m : catalog[g].targets[t].meshes)
                            wanted[m] = { g, t, it->second, true };
                }
            recs = records;
            knownNow = known;
        }
        for (const auto& [asset, set] : seeds)
            knownNow[asset].insert(set.begin(), set.end());

        auto finish = [&](bool ok) {
            if (SUCCEEDED(com))
                CoUninitialize();
            return ok;
        };

        if (wanted.empty() && recs.empty())
        {
            waitingForRoblox = false;
            setStatus("");
            return finish(true);
        }

        if (robloxBusy())
        {
            if (!waitingForRoblox.exchange(true))
                log("Skins are applied when Roblox closes", skins::Level::Info);
            setStatus("Waiting for Roblox to close");
            return finish(false);
        }
        waitingForRoblox = false;

        rbxcache::Store store;
        std::string error;
        if (!store.open(error))
        {
            setStatus("Play the game once, then the skins can be applied");
            return finish(false);
        }

        std::vector<rbxcache::Entry> entries;
        if (!store.scan(entries))
        {
            setStatus("Could not read the Roblox cache");
            return finish(false);
        }
        std::map<std::string, size_t> byId;
        std::map<std::string, std::vector<size_t>> byPrint;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            byId[entries[i].id] = i;
            byPrint[fingerprint(entries[i].bodyLen, entries[i].bodyHash)].push_back(i);
        }

        int restored = 0, written = 0, failed = 0;
        std::vector<Record> keep;
        std::set<std::string> owned;
        std::map<std::string, std::vector<size_t>> toWrite;

        for (const Record& r : recs)
        {
            auto it = byId.find(r.entry);
            if (it == byId.end() || entries[it->second].bodyHash != r.hash)
            {
                DeleteFileW(backupPath(r.entry).c_str());
                continue;
            }
            rbxcache::Entry& e = entries[it->second];
            auto w = wanted.find(r.texture);
            if (w == wanted.end())
            {
                std::string original;
                bool ok = false;
                if (readFile(backupPath(r.entry), original) && !original.empty())
                    ok = store.writeBody(e, original);
                if (!ok)
                    ok = store.remove(r.entry);
                if (ok)
                {
                    ++restored;
                    DeleteFileW(backupPath(r.entry).c_str());
                    continue;
                }
                keep.push_back(r);
                owned.insert(r.entry);
                continue;
            }
            owned.insert(r.entry);
            keep.push_back(r);
            bool wrongFormat = false;
            if (!w->second.mesh && catalog[w->second.game].targets[w->second.target].ktx2)
            {
                std::string head;
                wrongFormat = store.readHead(e, head, 64) && img::probe(head.data(), head.size()).kind != img::Kind::Ktx2;
            }
            if (w->second.skin != r.skin || wrongFormat)
                toWrite[r.texture].push_back(it->second);
        }

        std::vector<std::string> unresolved;
        for (const auto& [tex, want] : wanted)
        {
            bool any = false;
            for (const Record& r : recs)
                if (r.texture == tex && owned.count(r.entry))
                    any = true;
            auto kn = knownNow.find(tex);
            if (kn != knownNow.end())
                for (const std::string& f : kn->second)
                {
                    auto hit = byPrint.find(f);
                    if (hit == byPrint.end())
                        continue;
                    for (size_t idx : hit->second)
                        if (!owned.count(entries[idx].id))
                        {
                            toWrite[tex].push_back(idx);
                            owned.insert(entries[idx].id);
                            any = true;
                        }
                }
            if (!any)
                unresolved.push_back(tex);
        }

        std::vector<std::string> missing;
        if (!unresolved.empty() && scan)
        {
            setStatus("Looking for the originals in the Roblox cache");
            std::vector<Candidate> list;
            bool listed = false;
            for (const std::string& tex : unresolved)
            {
                const Want& w = wanted[tex];
                if (w.mesh)
                {
                    std::vector<size_t> found = findMesh(store, entries, owned, catalog[w.game].targets[w.target].meshVerts);
                    for (size_t idx : found)
                    {
                        owned.insert(entries[idx].id);
                        toWrite[tex].push_back(idx);
                        knownNow[tex].insert(fingerprint(entries[idx].bodyLen, entries[idx].bodyHash));
                    }
                    if (found.empty())
                        missing.push_back(tex);
                    continue;
                }
                if (!listed)
                {
                    list = candidates(store, entries, owned);
                    listed = true;
                }
                img::Rgba thumb;
                std::vector<size_t> found;
                if (thumbnail(tex, thumb))
                    found = match(thumb, list);
                for (size_t idx : found)
                {
                    if (owned.count(entries[idx].id))
                        continue;
                    owned.insert(entries[idx].id);
                    toWrite[tex].push_back(idx);
                    knownNow[tex].insert(fingerprint(entries[idx].bodyLen, entries[idx].bodyHash));
                }
                if (found.empty())
                    missing.push_back(tex);
            }
        }

        std::map<std::string, std::string> fileCache;
        int writtenMeshes = 0;
        for (auto& [tex, list] : toWrite)
        {
            const Want& want = wanted[tex];
            const std::string& key = want.skin;
            const bool ktx2 = !want.mesh && catalog[want.game].targets[want.target].ktx2;
            const Theme* theme = !want.mesh && key.rfind("theme:", 0) == 0 ? themeByName(key.substr(6)) : nullptr;
            std::string shared;
            if (!theme)
            {
                std::string cacheKey = key + (want.mesh ? "|mesh" : ktx2 ? "|ktx2" : "|png");
                auto fc = fileCache.find(cacheKey);
                if (fc == fileCache.end())
                {
                    std::string bytes;
                    if (want.mesh)
                    {
                        const FileSkin* f = fileSkin(want.game, want.target, key);
                        mesh::Mesh check;
                        if (!f || !resource(f->meshRes, bytes) || !mesh::parseRbx(bytes, check))
                            bytes.clear();
                    }
                    else
                    {
                        img::Rgba check;
                        if (!skinBytes(want, bytes) || !img::decodePng(bytes.data(), bytes.size(), check))
                            bytes.clear();
                        else if (ktx2 && !img::encodeKtx2(check, bytes))
                            bytes.clear();
                    }
                    fc = fileCache.emplace(cacheKey, bytes).first;
                }
                shared = fc->second;
                if (shared.empty())
                {
                    failed += (int)list.size();
                    continue;
                }
            }
            for (size_t idx : list)
            {
                rbxcache::Entry& e = entries[idx];
                std::wstring backup = backupPath(e.id);
                std::string original;
                if (!readFile(backup, original))
                {
                    if (!store.readBody(e, original) || !writeFile(backup, original))
                    {
                        ++failed;
                        continue;
                    }
                }
                std::string body = shared;
                if (theme)
                {
                    img::Rgba pixels;
                    bool encoded = img::decode(original.data(), original.size(), pixels);
                    if (encoded)
                        encoded = ktx2 ? img::encodeKtx2(recolor(pixels, *theme), body) : img::encodePng(recolor(pixels, *theme), body);
                    if (!encoded)
                    {
                        ++failed;
                        continue;
                    }
                }
                if (!store.writeBody(e, body))
                {
                    ++failed;
                    continue;
                }
                keep.erase(std::remove_if(keep.begin(), keep.end(), [&](const Record& r) { return r.entry == e.id; }), keep.end());
                keep.push_back({ e.id, tex, key, e.bodyHash });
                if (want.mesh)
                    ++writtenMeshes;
                else
                    ++written;
            }
        }

        {
            std::lock_guard<std::mutex> guard(stateLock);
            records = keep;
            for (const auto& [tex, set] : knownNow)
                known[tex].insert(set.begin(), set.end());
            saveState();
        }

        auto plural = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
        if (written || writtenMeshes)
        {
            std::string what = written ? plural(written, "texture", "textures") : "";
            if (writtenMeshes)
                what += (what.empty() ? "" : " and ") + plural(writtenMeshes, "model", "models");
            log("Skins applied to " + what + " in the Roblox cache", skins::Level::Success);
        }
        if (restored)
            log(plural(restored, "file", "files") + " set back to the original", skins::Level::Info);
        if (failed)
            log("Skins: " + plural(failed, "file", "files") + " could not be written", skins::Level::Error);

        std::set<std::string> missingNames;
        for (const std::string& tex : missing)
        {
            auto w = wanted.find(tex);
            if (w == wanted.end())
                continue;
            const skins::Game& g = catalog[w->second.game];
            missingNames.insert(g.targets[w->second.target].name + " (" + g.name + ")");
        }
        if (!missingNames.empty() && scan)
        {
            std::string names;
            for (const std::string& n : missingNames)
                names += (names.empty() ? "" : ", ") + n;
            static std::string lastMissing;
            if (names != lastMissing)
                log("Not in the Roblox cache yet: " + names + ". Play the game once, the skin is applied when Roblox closes", skins::Level::Warn);
            lastMissing = names;
            setStatus("Play the game once, the skin is applied when Roblox closes");
        }
        else if (scan || unresolved.empty())
        {
            setStatus(wanted.empty() ? "" : "Skins are applied");
        }
        return finish(true);
    }

    void applyWorker()
    {
        bool wasRunning = false;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lk(wakeLock);
                wake.wait_for(lk, std::chrono::seconds(3), [] { return applyWanted.load(); });
            }
            bool any;
            {
                std::lock_guard<std::mutex> guard(stateLock);
                any = !chosen.empty() || !records.empty();
            }
            bool running = any && robloxBusy();
            bool closedNow = wasRunning && !running;
            wasRunning = running;
            if (!applyWanted.load() && !closedNow && !(waitingForRoblox.load() && !running))
                continue;
            if (running)
            {
                if (applyWanted.exchange(false))
                    runApply(true);
                continue;
            }
            applyWanted = false;
            runApply(true);
        }
    }

    img::Rgba skinImage(size_t game, size_t target, const std::string& key)
    {
        const skins::Game& g = catalog[game];
        const skins::Target& t = g.targets[target];
        img::Rgba base;
        if (key.empty() || key.rfind("theme:", 0) == 0)
        {
            std::string png;
            if (t.textureRes && resource(t.textureRes, png))
                img::decodePng(png.data(), png.size(), base);
            if (base.empty())
                thumbnail(t.textures[0], base);
            if (key.empty() || base.empty())
                return base;
            const Theme* theme = themeByName(key.substr(6));
            return theme ? recolor(base, *theme) : img::Rgba{};
        }
        std::string bytes;
        Want want{ game, target, key, false };
        if (skinBytes(want, bytes))
            img::decodePng(bytes.data(), bytes.size(), base);
        return base;
    }

    void artWorker()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;)
        {
            std::string job;
            {
                std::unique_lock<std::mutex> lk(artLock);
                artWake.wait(lk, [] { return !artQueue.empty(); });
                job = artQueue.back();
                artQueue.pop_back();
            }
            size_t a = job.find('|'), b = job.find('|', a + 1), c = job.find('|', b + 1);
            bool isPreview = job[0] == 'p';
            size_t game = (size_t)atoi(job.c_str() + a + 1);
            size_t target = (size_t)atoi(job.c_str() + b + 1);
            std::string key = job.substr(c + 1);

            img::Rgba out;
            if (validGame(game, target))
            {
                img::Rgba image = skinImage(game, target, key);
                if (!image.empty())
                {
                    const skins::Target& t = catalog[game].targets[target];
                    const FileSkin* f = fileSkin(game, target, key);
                    int meshId = f && f->meshRes > 0 ? f->meshRes : t.meshRes;
                    const mesh::Mesh* model = nullptr;
                    if (meshId)
                    {
                        auto m = meshes.find(meshId);
                        if (m == meshes.end())
                        {
                            mesh::Mesh parsed;
                            std::string data;
                            if (resource(meshId, data))
                                mesh::parse(data, parsed);
                            m = meshes.emplace(meshId, std::move(parsed)).first;
                        }
                        if (!m->second.empty())
                            model = &m->second;
                    }
                    if (!isPreview)
                        out = model ? mesh::render(*model, image, kThumbSide, kThumbSide) : img::fit(image, kThumbSide, kThumbSide, 0);
                    else
                        out = model ? mesh::render(*model, image, kPreviewW, kPreviewH) : img::fit(image, kPreviewW, kPreviewH, 24);
                }
            }

            std::lock_guard<std::mutex> lk(artLock);
            artDone[job] = { out.empty() ? skins::Art::Failed : skins::Art::Ready, std::move(out) };
        }
    }

    skins::Art art(char kind, size_t game, size_t target, const std::string& key, img::Rgba& out)
    {
        std::string job = std::string(1, kind) + "|" + std::to_string(game) + "|" + std::to_string(target) + "|" + key;
        std::lock_guard<std::mutex> lk(artLock);
        auto it = artDone.find(job);
        if (it != artDone.end())
        {
            skins::Art state = it->second.first;
            if (state == skins::Art::Ready)
            {
                out = std::move(it->second.second);
                artDone.erase(it);
            }
            return state;
        }
        if (std::find(artQueue.begin(), artQueue.end(), job) == artQueue.end())
        {
            artQueue.push_back(job);
            artWake.notify_one();
        }
        return skins::Art::Pending;
    }
}

void skins::init(Logger l)
{
    logger = std::move(l);
    loadCatalog();
    {
        std::lock_guard<std::mutex> guard(stateLock);
        loadState();
    }
    refreshLocal();
    std::thread(applyWorker).detach();
    std::thread(artWorker).detach();
    requestApply();
}

const std::vector<skins::Game>& skins::games()
{
    return catalog;
}

std::vector<skins::Skin> skins::list(size_t game, size_t target)
{
    std::vector<Skin> out;
    if (!validGame(game, target))
        return out;
    const Game& g = catalog[game];
    const Target& t = g.targets[target];
    out.push_back({ "", "Original", g.name + " original", Kind::Original });
    for (const FileSkin& f : extra[game].files)
    {
        if (f.target != t.id)
            continue;
        std::string source = f.source.empty() ? g.source : f.source;
        if (f.res > 0)
            out.push_back({ "res:" + std::to_string(f.res), f.name, source, Kind::Embedded, f.meshRes > 0 });
        else
            out.push_back({ "url:" + f.url, f.name, source, Kind::Url });
    }
    if (g.themes)
        for (const Theme& th : kThemes)
            out.push_back({ std::string("theme:") + th.name, th.name, "Rave color", Kind::Theme });
    std::lock_guard<std::mutex> lk(localLock);
    auto it = localFiles.find(slotKey(game, target));
    if (it != localFiles.end())
        for (const std::wstring& file : it->second)
        {
            std::string name = sys::narrow(file);
            size_t dot = name.find_last_of('.');
            out.push_back({ "local:" + name, dot == std::string::npos ? name : name.substr(0, dot), "Your skin", Kind::Local });
        }
    return out;
}

std::string skins::selected(size_t game, size_t target)
{
    if (!validGame(game, target))
        return {};
    std::lock_guard<std::mutex> guard(stateLock);
    auto it = chosen.find(slotKey(game, target));
    return it == chosen.end() ? std::string() : it->second;
}

void skins::select(size_t game, size_t target, const std::string& key)
{
    if (!validGame(game, target))
        return;
    {
        std::lock_guard<std::mutex> guard(stateLock);
        std::string slot = slotKey(game, target);
        if (key.empty())
            chosen.erase(slot);
        else
            chosen[slot] = key;
        saveState();
    }
    const Target& t = catalog[game].targets[target];
    std::string label = "Original";
    for (const Skin& s : list(game, target))
        if (s.key == key)
            label = s.name;
    log(t.name + " (" + catalog[game].name + ") set to " + label, Level::Info);
    requestApply();
}

int skins::changedCount(size_t game)
{
    if (game >= catalog.size())
        return 0;
    int n = 0;
    std::lock_guard<std::mutex> guard(stateLock);
    for (const Target& t : catalog[game].targets)
        if (chosen.count(catalog[game].id + "/" + t.id))
            ++n;
    return n;
}

void skins::requestApply()
{
    applyWanted = true;
    wake.notify_one();
}

void skins::applyBeforeLaunch()
{
    applyWanted = false;
    runApply(false);
}

std::string skins::status()
{
    std::lock_guard<std::mutex> guard(stateLock);
    return statusText;
}

std::wstring skins::openFolder(size_t game)
{
    if (game >= catalog.size())
        return {};
    const Game& g = catalog[game];
    for (const Target& t : g.targets)
        paths::ensureDir(targetFolder(g, t));
    std::wstring dir = skinsDir() + L"\\" + safeName(g.name);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return dir;
}

void skins::refreshLocal()
{
    std::map<std::string, std::vector<std::wstring>> found;
    for (size_t g = 0; g < catalog.size(); ++g)
        for (size_t t = 0; t < catalog[g].targets.size(); ++t)
        {
            std::wstring dir = targetFolder(catalog[g], catalog[g].targets[t]);
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            std::vector<std::wstring>& list = found[slotKey(g, t)];
            do
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.nFileSizeHigh == 0 && fd.nFileSizeLow <= (16u << 20))
                    list.push_back(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            std::sort(list.begin(), list.end());
        }
    std::lock_guard<std::mutex> lk(localLock);
    localFiles = std::move(found);
}

skins::Art skins::preview(size_t game, size_t target, const std::string& key, img::Rgba& out)
{
    return art('p', game, target, key, out);
}

skins::Art skins::thumb(size_t game, size_t target, const std::string& key, img::Rgba& out)
{
    return art('t', game, target, key, out);
}
