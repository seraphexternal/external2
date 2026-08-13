#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/KeyBind.h"
#include <thread>
#include <chrono>
#include <mutex>

namespace DesyncVisual
{
    inline std::mutex desyncMutex;
    inline bool isActive = false;
    inline bool wasActive = false;
    inline Vectors::Vector3 ghostPosition = { 0, 0, 0 };
    inline Vectors::Vector3 realPosition = { 0, 0, 0 };
    inline Vectors::Vector3 localHeadPos = { 0, 0, 0 };
    inline bool hasValidData = false;

    inline void OnActivate()
    {
        std::lock_guard<std::mutex> lock(desyncMutex);
        ghostPosition = realPosition;
        hasValidData = true;
    }

    inline void OnDeactivate()
    {
        std::lock_guard<std::mutex> lock(desyncMutex);
        hasValidData = false;
    }

    inline void UpdatePosition()
    {
        if (!Globals::Roblox::LocalPlayer.address)
        {
            hasValidData = false;
            return;
        }

        auto character = Globals::Roblox::LocalPlayer.Character();
        auto hrp = character.FindFirstChild("HumanoidRootPart");
        if (!hrp.address) hrp = character.FindFirstChild("Torso");
        if (!hrp.address) hrp = character.FindFirstChild("UpperTorso");

        auto head = character.FindFirstChild("Head");

        if (hrp.address)
        {
            Vectors::Vector3 pos = hrp.Position();
            {
                std::lock_guard<std::mutex> lock(desyncMutex);
                realPosition = pos;
            }
        }

        if (head.address)
        {
            std::lock_guard<std::mutex> lock(desyncMutex);
            localHeadPos = head.Position();
        }
    }

    inline void RenderDesyncVisual(ImDrawList* drawList)
    {
        if (!Options::Desync::Enabled || !Options::Desync::ShowVisual)
            return;

        bool shouldDraw = false;
        Vectors::Vector3 gPos, rPos, headPos;

        {
            std::lock_guard<std::mutex> lock(desyncMutex);
            shouldDraw = hasValidData;
            gPos = ghostPosition;
            rPos = realPosition;
            headPos = localHeadPos;
        }

        if (!shouldDraw)
            return;
            
        // Fallback for missing Globals
        if (!Globals::Roblox::LocalPlayer.address) return;

        // Rather than drawing a stickman, let's draw chams for the ghost
        // We will project the player's parts offset by the difference between ghost and real pos
        Vectors::Vector3 offset = gPos - rPos;
        float dist = rPos.Distance(gPos);

        // if too close, don't render to avoid clutter
        if (dist < 1.0f) return;

        auto character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address) return;

        std::vector<RobloxInstance> partsToDraw;
        auto children = character.GetChildren();
        for (auto& child : children)
        {
            if (child.IsA("BasePart") || child.IsA("MeshPart") || child.IsA("Part"))
            {
                if (child.Name() != "HumanoidRootPart")
                {
                    partsToDraw.push_back(child);
                }
            }
        }
        
        // Also get accessories
        for (auto& child : children)
        {
            std::string cls = child.Class();
            if (cls == "Accessory" || cls == "Hat" || cls == "Backpack" || cls == "Tool")
            {
                auto handle = child.FindFirstChild("Handle");
                if (handle.address) partsToDraw.push_back(handle);
            }
        }

        if (partsToDraw.empty()) return;

        // Group projected corners by limb so the visualizer renders chams
        // (per-limb silhouette) at the ghost location instead of one solid block.
        auto limbKey = [](const RobloxInstance& p) -> int {
            std::string n = p.Name();
            if (n == "Head") return 0;
            if (n.find("Torso") != std::string::npos) return 1;
            bool left = n.find("Left") != std::string::npos;
            bool right = n.find("Right") != std::string::npos;
            if (n.find("Arm") != std::string::npos || n.find("Hand") != std::string::npos) return left ? 2 : (right ? 3 : 2);
            if (n.find("Leg") != std::string::npos || n.find("Foot") != std::string::npos) return left ? 4 : (right ? 5 : 4);
            if (n.find("Handle") != std::string::npos) return 6;
            return 7;
        };

        static const float corners[8][3] = {
            {-1,-1,-1}, {1,-1,-1}, {-1,1,-1}, {1,1,-1},
            {-1,-1, 1}, {1,-1, 1}, {-1,1, 1}, {1,1, 1}
        };

        std::vector<std::vector<ImVec2>> limbBuckets(8);

        for (const auto& part : partsToDraw)
        {
            if (!part.address) continue;
            const auto pos = part.Position();
            const auto size = part.Size();
            const auto cf = part.CFrame();

            for (int i = 0; i < 8; ++i)
            {
                Vectors::Vector3 localOffset{ corners[i][0] * size.x * 0.5f, corners[i][1] * size.y * 0.5f, corners[i][2] * size.z * 0.5f };
                Vectors::Vector3 rotated{ cf.r00 * localOffset.x + cf.r01 * localOffset.y + cf.r02 * localOffset.z,
                                          cf.r10 * localOffset.x + cf.r11 * localOffset.y + cf.r12 * localOffset.z,
                                          cf.r20 * localOffset.x + cf.r21 * localOffset.y + cf.r22 * localOffset.z };

                // Add the desync offset to place the part at the ghost location
                Vectors::Vector3 world = pos + rotated + offset;
                auto screen = WorldToScreen(world);
                if (screen.x >= 0.f && screen.y >= 0.f)
                    limbBuckets[limbKey(part)].emplace_back(screen.x, screen.y);
            }
        }

        auto buildHull = [](std::vector<ImVec2>& pts) -> std::vector<ImVec2> {
            if (pts.size() < 3) return {};
            std::sort(pts.begin(), pts.end(), [](const ImVec2& a, const ImVec2& b) {
                return a.x < b.x || (a.x == b.x && a.y < b.y);
            });
            std::vector<ImVec2> hull;
            auto cross = [](const ImVec2& o, const ImVec2& a, const ImVec2& b) {
                return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
            };
            for (auto& p : pts) {
                while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
                hull.push_back(p);
            }
            size_t t = hull.size() + 1;
            for (int i = (int)pts.size() - 1; i >= 0; --i) {
                auto& p = pts[i];
                while (hull.size() >= t && cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
                hull.push_back(p);
            }
            if (hull.size() > 1) hull.pop_back();
            return hull;
        };

        ImU32 ghostFill = IM_COL32(
            static_cast<int>(Options::Desync::VisualColor[0] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[1] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[2] * 255.f),
            static_cast<int>(40.f * Options::Desync::VisualAlpha));

        const ImU32 ghostOutline = IM_COL32(
            static_cast<int>(Options::Desync::VisualColor[0] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[1] * 255.f),
            static_cast<int>(Options::Desync::VisualColor[2] * 255.f),
            static_cast<int>(Options::Desync::VisualAlpha * 255.f));

        for (auto& bucket : limbBuckets)
        {
            std::vector<ImVec2> hull = buildHull(bucket);
            if (hull.size() < 3) continue;
            drawList->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), ghostFill);
            drawList->AddPolyline(hull.data(), static_cast<int>(hull.size()), ghostOutline, true, 2.0f);
        }

        if (Options::Desync::ShowLine && dist > 1.f)
        {
            const ImU32 lineColor = IM_COL32(
                static_cast<int>(Options::Desync::LineColor[0] * 255.f),
                static_cast<int>(Options::Desync::LineColor[1] * 255.f),
                static_cast<int>(Options::Desync::LineColor[2] * 255.f),
                220);

            const auto ghostScreen = WorldToScreen(gPos);
            const auto realScreen = WorldToScreen(rPos);

            if (ghostScreen.x != -1.f && realScreen.x != -1.f)
            {
                drawList->AddLine(
                    ImVec2(realScreen.x, realScreen.y),
                    ImVec2(ghostScreen.x, ghostScreen.y),
                    lineColor, 2.0f);
            }
        }
    }
}

inline bool DesyncIsActive()
{
    if (!Options::Desync::Enabled)
        return false;

    if (Options::Desync::ToggleType == 2)
        return true;

    if (Options::Desync::DesyncKey != 0)
    {
        if (Options::Desync::ToggleType == 1)
            return Options::Desync::Toggled;
        else
            return KeyBind::IsPressed(Options::Desync::DesyncKey);
    }

    return Options::Desync::Enabled;
}

inline void ApplyDesyncVelocity()
{
    if (Options::Desync::Method != 1)
        return;

    if (!Globals::Roblox::LocalPlayer.address)
        return;

    auto character = Globals::Roblox::LocalPlayer.Character();
    auto hrp = character.FindFirstChild("HumanoidRootPart");
    if (!hrp.address) hrp = character.FindFirstChild("Torso");
    if (!hrp.address) hrp = character.FindFirstChild("UpperTorso");
    if (!hrp.address) return;

    uintptr_t primitiveAddr = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
    if (!primitiveAddr) return;

    auto cframe = hrp.CFrame();
    Vectors::Vector3 boostDir;

    switch (Options::Desync::BoostAxis)
    {
    case 0: boostDir = cframe.GetLookVector(); break;
    case 1: boostDir = { 0.f, 1.f, 0.f }; break;
    case 2: boostDir = cframe.GetLookVector() * -1.f; break;
    default: boostDir = cframe.GetLookVector(); break;
    }

    Vectors::Vector3 currentVel = Memory->read<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity);

    Vectors::Vector3 boostVel = {
        boostDir.x * Options::Desync::BoostSpeed,
        boostDir.y * Options::Desync::BoostSpeed,
        boostDir.z * Options::Desync::BoostSpeed
    };

    Memory->write<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity, boostVel);
}

inline void DesyncLoop()
{
    bool wasActive = false;

    while (Globals::running)
    {
        try
        {
            if (Options::Desync::Enabled)
            {
                if (Options::Desync::ToggleType == 2)
                {
                    Options::Desync::Toggled = true;
                }
                else if (Options::Desync::DesyncKey != 0)
                {
                    static bool wasKeyPressed = false;
                    bool isKeyPressed = KeyBind::IsPressed(Options::Desync::DesyncKey);

                    if (Options::Desync::ToggleType == 1)
                    {
                        if (isKeyPressed && !wasKeyPressed)
                            Options::Desync::Toggled = !Options::Desync::Toggled;
                        wasKeyPressed = isKeyPressed;
                    }
                    else
                    {
                        Options::Desync::Toggled = isKeyPressed;
                        wasKeyPressed = isKeyPressed;
                    }
                }
            }

            bool shouldApply = DesyncIsActive();
            DesyncVisual::isActive = shouldApply;

            if (shouldApply && !wasActive)
            {
                DesyncVisual::OnActivate();
            }
            else if (!shouldApply && wasActive)
            {
                uintptr_t base = Memory->getBaseAddress();
                if (base)
                {
                    Memory->write<float>(base + Offsets::FFlags::GameNetCompressionLodByteBudgetThresholdPct, 1.0f);
                    Memory->write<int>(base + Offsets::FFlags::PhysicsSenderMaxBandwidthBps, 1000);
                    Memory->write<int>(base + Offsets::FFlags::NextGenReplicatorEnabledWrite4, 1);
                }
                DesyncVisual::OnDeactivate();
            }
            wasActive = shouldApply;

            if (shouldApply)
            {
                uintptr_t base = Memory->getBaseAddress();
                if (base)
                {
                    Memory->write<float>(base + Offsets::FFlags::GameNetCompressionLodByteBudgetThresholdPct, 0.0f);
                    Memory->write<int>(base + Offsets::FFlags::PhysicsSenderMaxBandwidthBps, 0);
                    Memory->write<int>(base + Offsets::FFlags::NextGenReplicatorEnabledWrite4, 1);
                }

                ApplyDesyncVelocity();
            }

            DesyncVisual::UpdatePosition();
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
