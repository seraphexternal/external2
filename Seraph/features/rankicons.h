#pragma once

// Rank icons for the ESP.
//
// Decoded once per process with stb_image and cached per name, because
// stbi_load + texture creation is far too expensive to repeat for every player
// every frame. Names are the file stems under assets/rank, which is also the
// order RankOrder() in globals.h returns, so the two have to stay in step.
//
// Textures are created lazily and only when a rank is actually shown, so a
// player with no rank costs nothing.

#include <string>
#include <unordered_map>
#include <vector>

#include "stb_image.h"

// Defined in renderer.cpp, inside namespace SpotifyVisualizer - so it has to be
// declared there too or the linker looks for a global symbol that does not
// exist. D3D resource creation has to stay on the render thread, which is where
// the ESP is drawn from anyway.
namespace SpotifyVisualizer
{
    ImTextureID UploadTexture(const unsigned char* px, int w, int h);
}

namespace RankIcons
{
    inline float g_Size = 18.0f;
    inline bool g_Ready = false;

    inline std::unordered_map<std::string, ImTextureID>& Cache()
    {
        static std::unordered_map<std::string, ImTextureID> cache;
        return cache;
    }

    // "<exeDir>\assets\rank\<name>.png". The assets are copied next to the
    // binary so this resolves wherever the exe is launched from.
    inline std::string PathFor(const std::string& name)
    {
        if (Globals::executablePath.empty())
            return std::string();

        return (std::filesystem::path(Globals::executablePath) / "assets" / "rank" / (name + ".png")).string();
    }

    // Returns nullptr when the file is missing, so the caller can fall back to
    // drawing the rank as text. Only division 3 of each tier has an icon.
    inline ImTextureID Get(const std::string& name)
    {
        if (name.empty())
            return nullptr;

        auto& cache = Cache();
        auto it = cache.find(name);
        if (it != cache.end())
            return it->second;

        const std::string path = PathFor(name);
        if (path.empty())
        {
            cache[name] = nullptr;
            return nullptr;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
        ImTextureID tex = nullptr;

        if (px && w > 0 && h > 0)
        {
            tex = SpotifyVisualizer::UploadTexture(px, w, h);
            stbi_image_free(px);
        }
        else if (px)
        {
            stbi_image_free(px);
        }

        cache[name] = tex;
        return tex;
    }

    // Warms every known rank so the first enemy on screen does not pay for a
    // synchronous disk read and texture upload mid-frame.
    inline void Preload()
    {
        if (g_Ready)
            return;

        static const char* stems[] =
        {
            "unranked", "bronze3", "silver3", "gold3", "platinum3",
            "onyx3", "diamond3", "nemesisrank", "archnemesis"
        };

        for (const char* stem : stems)
            Get(stem);

        g_Ready = true;
    }

// Draws an already-resolved texture. The caller has the ImTextureID from Get(),
// so there is no map lookup on the hot path.
inline void Blit(ImDrawList* draw, ImTextureID tex, float x, float y, float size)
{
    draw->AddImageRounded(tex, ImVec2(x, y), ImVec2(x + size, y + size),
        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 3.0f);
}
}