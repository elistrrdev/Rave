#pragma once
#include "image.hpp"
#include <functional>
#include <string>
#include <vector>

namespace skins
{
    enum class Kind { Original, Theme, Embedded, Url, Local };
    enum class Level { Success, Error, Warn, Info };
    enum class Art { Pending, Ready, Failed };

    struct Skin
    {
        std::string key;
        std::string name;
        std::string source;
        Kind kind = Kind::Original;
        bool model = false;
    };

    struct HiddenPart
    {
        std::string id;
        std::string verts;
        float size[3] = { 1.f, 1.f, 1.f };
    };

    struct Target
    {
        std::string id;
        std::string name;
        std::vector<std::string> textures;
        std::vector<std::string> meshes;
        std::string meshVerts;
        bool ktx2 = false;
        int meshRes = 0;
        int textureRes = 0;
        bool paint = false;
        int baseMeshRes = 0;
        int baseTextureRes = 0;
        std::vector<HiddenPart> hidden;
    };

    struct Game
    {
        std::string id;
        std::string name;
        std::string label;
        std::string source;
        bool themes = false;
        std::vector<Target> targets;
    };

    using Logger = std::function<void(const std::string&, Level)>;

    void init(Logger logger);
    const std::vector<Game>& games();

    std::vector<Skin> list(size_t game, size_t target);
    std::string selected(size_t game, size_t target);
    void select(size_t game, size_t target, const std::string& key);
    int changedCount(size_t game);

    void requestApply();
    void applyBeforeLaunch();
    std::string status();

    std::wstring openFolder(size_t game);
    void refreshLocal();

    Art preview(size_t game, size_t target, const std::string& key, img::Rgba& out);
    Art thumb(size_t game, size_t target, const std::string& key, img::Rgba& out);
}
