#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace Chams
{
    struct LimbParts
    {
        std::vector<RobloxInstance> parts;
        uint64_t hash = 0;

        void ComputeHash()
        {
            hash = 0;
            for (auto& p : parts)
            {
                if (p.address)
                    hash = hash * 31 + (p.address >> 4);
            }
        }
    };

    struct CachedHull
    {
        std::vector<ImVec2> hull;
        uint64_t limbHash = 0;
        uint64_t frameTime = 0;
    };

    inline std::unordered_map<uintptr_t, std::vector<CachedHull>> g_HullCache;
    inline uint64_t g_CurrentFrame = 0;

    inline void ClearHullCache()
    {
        g_HullCache.clear();
    }

    inline void IncrementFrame()
    {
        g_CurrentFrame++;
        if (g_CurrentFrame % 60 == 0) ClearHullCache();
    }

    inline void GetAccessories(const RobloxPlayer& player, std::vector<RobloxInstance>& outParts)
    {
        if (!player.Character.address) return;
        auto children = player.Character.GetChildren();
        for (auto& child : children)
        {
            std::string cls = child.Class();
            if (cls == "Accessory" || cls == "Hat" || cls == "Backpack" || cls == "Tool")
            {
                auto handle = child.FindFirstChild("Handle");
                if (handle.address) outParts.push_back(handle);
            }
        }
    }

    inline std::vector<LimbParts> Get_LimbGroups(const RobloxPlayer& player)
    {
        std::vector<LimbParts> limbs;

        const bool r15 = player.Upper_Torso.address && player.Lower_Torso.address;
        const bool r6 = player.Torso.address;

        if (r15)
        {
            if (player.Head.address) { LimbParts h; h.parts.push_back(player.Head); h.ComputeHash(); limbs.push_back(h); }
            LimbParts torso;
            if (player.Upper_Torso.address) torso.parts.push_back(player.Upper_Torso);
            if (player.Lower_Torso.address) torso.parts.push_back(player.Lower_Torso);
            if (!torso.parts.empty()) { torso.ComputeHash(); limbs.push_back(torso); }
            LimbParts la; if (player.Left_Upper_Arm.address) la.parts.push_back(player.Left_Upper_Arm);
            if (player.Left_Lower_Arm.address) la.parts.push_back(player.Left_Lower_Arm);
            if (player.Left_Hand.address) la.parts.push_back(player.Left_Hand);
            if (!la.parts.empty()) { la.ComputeHash(); limbs.push_back(la); }
            LimbParts ra; if (player.Right_Upper_Arm.address) ra.parts.push_back(player.Right_Upper_Arm);
            if (player.Right_Lower_Arm.address) ra.parts.push_back(player.Right_Lower_Arm);
            if (player.Right_Hand.address) ra.parts.push_back(player.Right_Hand);
            if (!ra.parts.empty()) { ra.ComputeHash(); limbs.push_back(ra); }
            LimbParts ll; if (player.Left_Upper_Leg.address) ll.parts.push_back(player.Left_Upper_Leg);
            if (player.Left_Lower_Leg.address) ll.parts.push_back(player.Left_Lower_Leg);
            if (player.Left_Foot.address) ll.parts.push_back(player.Left_Foot);
            if (!ll.parts.empty()) { ll.ComputeHash(); limbs.push_back(ll); }
            LimbParts rl; if (player.Right_Upper_Leg.address) rl.parts.push_back(player.Right_Upper_Leg);
            if (player.Right_Lower_Leg.address) rl.parts.push_back(player.Right_Lower_Leg);
            if (player.Right_Foot.address) rl.parts.push_back(player.Right_Foot);
            if (!rl.parts.empty()) { rl.ComputeHash(); limbs.push_back(rl); }

            if (Options::Chams::IncludeAccessories)
            {
                std::vector<RobloxInstance> accessories;
                GetAccessories(player, accessories);
                if (!accessories.empty())
                {
                    LimbParts acc;
                    acc.parts = accessories;
                    acc.ComputeHash();
                    limbs.push_back(acc);
                }
            }
        }
        else if (r6)
        {
            if (player.Head.address) { LimbParts h; h.parts.push_back(player.Head); h.ComputeHash(); limbs.push_back(h); }
            if (player.Torso.address) { LimbParts t; t.parts.push_back(player.Torso); t.ComputeHash(); limbs.push_back(t); }
            if (player.Left_Arm.address) { LimbParts la; la.parts.push_back(player.Left_Arm); la.ComputeHash(); limbs.push_back(la); }
            if (player.Right_Arm.address) { LimbParts ra; ra.parts.push_back(player.Right_Arm); ra.ComputeHash(); limbs.push_back(ra); }
            if (player.Left_Leg.address) { LimbParts ll; ll.parts.push_back(player.Left_Leg); ll.ComputeHash(); limbs.push_back(ll); }
            if (player.Right_Leg.address) { LimbParts rl; rl.parts.push_back(player.Right_Leg); rl.ComputeHash(); limbs.push_back(rl); }

            if (Options::Chams::IncludeAccessories)
            {
                std::vector<RobloxInstance> accessories;
                GetAccessories(player, accessories);
                if (!accessories.empty())
                {
                    LimbParts acc;
                    acc.parts = accessories;
                    acc.ComputeHash();
                    limbs.push_back(acc);
                }
            }
        }
        else
        {
            if (player.HumanoidRootPart.address) { LimbParts hrp; hrp.parts.push_back(player.HumanoidRootPart); hrp.ComputeHash(); limbs.push_back(hrp); }
            if (player.Head.address) { LimbParts h; h.parts.push_back(player.Head); h.ComputeHash(); limbs.push_back(h); }
        }
        return limbs;
    }

    inline std::vector<ImVec2> ProjectParts(const std::vector<RobloxInstance>& parts)
    {
        std::vector<ImVec2> projected;
        static const float corners[8][3] = {
            {-1,-1,-1}, {1,-1,-1}, {-1,1,-1}, {1,1,-1},
            {-1,-1, 1}, {1,-1, 1}, {-1,1, 1}, {1,1, 1}
        };
        for (const auto& part : parts)
        {
            if (!part.address) continue;
            const auto pos = part.Position();
            const auto size = part.Size();
            const auto cf = part.CFrame();
            for (int i = 0; i < 8; ++i)
            {
                Vectors::Vector3 offset{ corners[i][0] * size.x * 0.5f, corners[i][1] * size.y * 0.5f, corners[i][2] * size.z * 0.5f };
                Vectors::Vector3 rotated{ cf.r00 * offset.x + cf.r01 * offset.y + cf.r02 * offset.z,
                                          cf.r10 * offset.x + cf.r11 * offset.y + cf.r12 * offset.z,
                                          cf.r20 * offset.x + cf.r21 * offset.y + cf.r22 * offset.z };
                Vectors::Vector3 world = pos + rotated;
                auto screen = WorldToScreen(world);
                if (screen.x >= 0.f && screen.y >= 0.f)
                    projected.emplace_back(screen.x, screen.y);
            }
        }
        if (projected.size() < 3) return {};
        std::sort(projected.begin(), projected.end(), [](const ImVec2& a, const ImVec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        std::vector<ImVec2> hull;
        auto cross = [](const ImVec2& o, const ImVec2& a, const ImVec2& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        for (auto& p : projected) {
            while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        size_t t = hull.size() + 1;
        for (int i = (int)projected.size() - 1; i >= 0; --i) {
            auto& p = projected[i];
            while (hull.size() >= t && cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        if (hull.size() > 1) hull.pop_back();
        return hull;
    }

    inline std::vector<ImVec2> GetOrCreateHull(const RobloxPlayer& player, const LimbParts& limb)
    {
        uintptr_t key = player.address;
        auto& cache = g_HullCache[key];
        
        for (auto& cached : cache)
        {
            if (cached.limbHash == limb.hash && cached.frameTime == g_CurrentFrame && cached.hull.size() > 0)
                return cached.hull;
        }

        CachedHull cached;
        cached.hull = ProjectParts(limb.parts);
        cached.limbHash = limb.hash;
        cached.frameTime = g_CurrentFrame;
        cache.push_back(cached);
        
        if (cache.size() > 20) cache.erase(cache.begin());
        
        return cached.hull;
    }

    inline void RenderChams(ImDrawList* drawList, const RobloxPlayer& player)
    {
        if (!Options::Chams::Enabled || !player.address) return;
        if (Options::Chams::TeamCheck && IsTeammate(player)) return;
        if (player.address == Globals::Roblox::LocalPlayer.address) return;

        auto limbs = Get_LimbGroups(player);
        if (limbs.empty()) return;

        g_CurrentFrame++;

        ImU32 outlineColor = IM_COL32(
            static_cast<int>(Options::Chams::OutlineColor[0] * 255.f),
            static_cast<int>(Options::Chams::OutlineColor[1] * 255.f),
            static_cast<int>(Options::Chams::OutlineColor[2] * 255.f),
            static_cast<int>(Options::Chams::OutlineColor[3] * 255.f)
        );

        for (auto& limb : limbs)
        {
            auto hull = GetOrCreateHull(player, limb);
            if (hull.size() < 3) continue;

            ImU32 outlineColor = IM_COL32(
                static_cast<int>(Options::Chams::OutlineColor[0] * 255.f),
                static_cast<int>(Options::Chams::OutlineColor[1] * 255.f),
                static_cast<int>(Options::Chams::OutlineColor[2] * 255.f),
                static_cast<int>(Options::Chams::OutlineColor[3] * 255.f)
            );

            float minY = hull[0].y, maxY = hull[0].y;
            for (auto& p : hull) { if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y; }
            float height = maxY - minY;
            if (height < 1.f) height = 1.f;

            if (Options::Chams::GradientFill)
            {
                int strips = 8;
                float stripH = (maxY - minY) / strips;
                for (int s = 0; s < strips; ++s)
                {
                    float y0_clip = minY + s * (maxY - minY) / 8.0f;
                    float y1_clip = minY + (s + 1) * (maxY - minY) / 8.0f;
                    float t0 = ((y0_clip - minY) / (maxY - minY) < 0.0f) ? 0.0f : ((y0_clip - minY) / (maxY - minY) > 1.0f) ? 1.0f : (y0_clip - minY) / (maxY - minY);
                    float t1 = ((y1_clip - minY) / (maxY - minY) < 0.0f) ? 0.0f : ((y1_clip - minY) / (maxY - minY) > 1.0f) ? 1.0f : (y1_clip - minY) / (maxY - minY);

                    std::vector<ImVec2> stripVerts;
                    for (size_t i = 0; i < hull.size(); ++i) {
                        const ImVec2& a = hull[i];
                        const ImVec2& b = hull[(i + 1) % hull.size()];
                        bool aIn = a.y >= minY + s * (maxY - minY) / 8.0f && a.y <= minY + (s + 1) * (maxY - minY) / 8.0f;
                        bool bIn = b.y >= minY + s * (maxY - minY) / 8.0f && b.y <= minY + (s + 1) * (maxY - minY) / 8.0f;

                        if (aIn) stripVerts.push_back(a);
                        if (aIn != bIn) {
                            float t = (y0_clip - a.y) / (b.y - a.y);
                            if (t >= 0 && t <= 1) stripVerts.emplace_back(a.x + (b.x - a.x) * t, y0_clip);
                            t = (y1_clip - a.y) / (b.y - a.y);
                            if (t >= 0 && t <= 1) stripVerts.emplace_back(a.x + (b.x - a.x) * t, y1_clip);
                        }
                    }

                    if (stripVerts.size() < 3) continue;

                    std::sort(stripVerts.begin(), stripVerts.end(), [](const ImVec2& a, const ImVec2& b) {
                        return a.x < b.x || (a.x == b.x && a.y < b.y);
                    });
                    std::vector<ImVec2> stripHull;
                    auto cross = [](const ImVec2& o, const ImVec2& a, const ImVec2& b) {
                        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
                    };
                    for (auto& p : stripVerts) {
                        while (stripHull.size() >= 2 && cross(stripHull[stripHull.size() - 2], stripHull.back(), p) <= 0)
                            stripHull.pop_back();
                        stripHull.push_back(p);
                    }
                    size_t t2 = stripHull.size() + 1;
                    for (int i = (int)stripVerts.size() - 1; i >= 0; --i) {
                        auto& p = stripVerts[i];
                        while (stripHull.size() >= t2 && cross(stripHull[stripHull.size() - 2], stripHull.back(), p) <= 0)
                            stripHull.pop_back();
                        stripHull.push_back(p);
                    }
                    if (stripHull.size() > 1) stripHull.pop_back();
                    if (stripHull.size() < 3) continue;

                    float tmid = (s + 0.5f) / 8.0f;
                    ImVec4 cmid = {
                        Options::Chams::FillColor[0] + (Options::Chams::FillColor2[0] - Options::Chams::FillColor[0]) * tmid,
                        Options::Chams::FillColor[1] + (Options::Chams::FillColor2[1] - Options::Chams::FillColor[1]) * tmid,
                        Options::Chams::FillColor[2] + (Options::Chams::FillColor2[2] - Options::Chams::FillColor[2]) * tmid,
                        Options::Chams::FillColor[3]
                    };
                    ImU32 stripColor = ImGui::ColorConvertFloat4ToU32(cmid);
                    drawList->AddConvexPolyFilled(stripHull.data(), static_cast<int>(stripHull.size()), stripColor);
                }
            }
            else if (Options::Chams::Wireframe)
            {
                ImU32 c = ImGui::ColorConvertFloat4ToU32({ 
                    Options::Chams::FillColor[0], 
                    Options::Chams::FillColor[1], 
                    Options::Chams::FillColor[2], 
                    Options::Chams::FillColor[3] 
                });
                drawList->AddPolyline(hull.data(), static_cast<int>(hull.size()), IM_COL32(
                    static_cast<int>(Options::Chams::OutlineColor[0] * 255.f),
                    static_cast<int>(Options::Chams::OutlineColor[1] * 255.f),
                    static_cast<int>(Options::Chams::OutlineColor[2] * 255.f),
                    static_cast<int>(Options::Chams::OutlineColor[3] * 255.f)
                ), true, Options::Chams::WireframeThickness);
            }
            else
            {
                ImU32 c = ImGui::ColorConvertFloat4ToU32({ 
                    Options::Chams::FillColor[0], 
                    Options::Chams::FillColor[1], 
                    Options::Chams::FillColor[2], 
                    Options::Chams::FillColor[3] 
                });
                drawList->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), c);
            }

            if (!Options::Chams::Wireframe)
            {
                drawList->AddPolyline(hull.data(), static_cast<int>(hull.size()), IM_COL32(
                    static_cast<int>(Options::Chams::OutlineColor[0] * 255.f),
                    static_cast<int>(Options::Chams::OutlineColor[1] * 255.f),
                    static_cast<int>(Options::Chams::OutlineColor[2] * 255.f),
                    static_cast<int>(Options::Chams::OutlineColor[3] * 255.f)
                ), true, 2.0f);
            }
        }
    }
}