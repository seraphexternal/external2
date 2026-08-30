#pragma once
#include "../../overlay/imgui/imgui.h"
#include <vector>

namespace Preview3D
{
    // Mirrors Options::Preview3D::Model integer values exactly:
    // 0 = None (default, shows 2D Roblox avatar), 1 = Roblox, 2 = Tung Tung, 3 = Mario.
    enum Model
    {
        None = 0,
        Roblox = 1,
        TungTung = 2,
        Mario = 3,
    };

    void Update();
    void SetModel(Model m);
    void DrawPreview(ImDrawList* dl, const ImVec2& min, const ImVec2& max);
    bool GetProjectedBounds(float& u0, float& v0, float& u1, float& v1);
    bool GetProjectedSkeleton(std::vector<float>& uv_segs);
    bool GetProjectedHead(float& u, float& v);
    bool GetProjectedModelBox(std::vector<float>& uv_corners);
    void AddRotation(float dyaw, float dpitch);
    void AddZoom(float delta);
    void NotifyManual();
    void ResetView();
    void Shutdown();
}
