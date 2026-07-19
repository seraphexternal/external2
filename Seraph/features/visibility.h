#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <thread>

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"

namespace Visibility
{
    struct Occluder
    {
        Vectors::Vector3 position{};
        float radius = 1.f;
        uintptr_t modelAddress = 0;
    };

    inline std::vector<Occluder> occluders;
    inline std::mutex occluderMutex;
    inline std::atomic<int> refreshCooldown{0};
    inline std::unordered_map<uintptr_t, bool> playerVisibleCache;
    inline std::unordered_map<uintptr_t, bool> pointVisibleCache;

    inline void BeginFrame()
    {
        playerVisibleCache.clear();
        pointVisibleCache.clear();
    }

    inline uintptr_t GetModelAncestor(uintptr_t instanceAddress)
    {
        uintptr_t current = instanceAddress;
        for (int depth = 0; depth < 6 && current != 0; ++depth)
        {
            RobloxInstance inst(current);
            if (inst.Class() == "Model")
                return current;

            current = Memory->read<uintptr_t>(current + Offsets::Instance::Parent);
        }
        return 0;
    }

    inline bool IsPartClass(const std::string& className)
    {
        return className == "Part"
            || className == "MeshPart"
            || className == "UnionOperation"
            || className == "WedgePart"
            || className == "CornerWedgePart";
    }

    inline bool ModelContainsHumanoid(const RobloxInstance& model)
    {
        if (!model.address || model.Class() != "Model")
            return false;

        for (const auto& child : model.GetChildren())
        {
            if (child.Class() == "Humanoid")
                return true;
        }
        return false;
    }

    inline bool CanPartOcclude(const RobloxInstance& part, const Vectors::Vector3& cameraPos, float maxDistance)
    {
        if (!part.address)
            return false;

        const float transparency = Memory->read<float>(part.address + Offsets::BasePart::Transparency);
        if (transparency >= 0.92f)
            return false;

        const uintptr_t primitive = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
        if (!primitive)
            return false;

        const uint8_t flags = Memory->read<uint8_t>(primitive + Offsets::Primitive::Flags);
        if ((flags & Offsets::PrimitiveFlags::CanCollide) == 0)
            return false;

        const Vectors::Vector3 position = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Position);
        if (position.Distance(cameraPos) > maxDistance)
            return false;

        return true;
    }

    inline void TryAddOccluder(const RobloxInstance& part, const Vectors::Vector3& cameraPos, float maxDistance)
    {
        if (!CanPartOcclude(part, cameraPos, maxDistance))
            return;

        const uintptr_t primitive = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
        const Vectors::Vector3 position = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Position);
        const Vectors::Vector3 size = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Size);

        const float radius = (size.x + size.y + size.z) / 6.f;
        if (radius < 0.5f)
            return;

        Occluder entry;
        entry.position = position;
        entry.radius = radius;
        entry.modelAddress = GetModelAncestor(part.address);

        std::lock_guard<std::mutex> lock(occluderMutex);
        if (occluders.size() < 450)
            occluders.push_back(entry);
    }

    inline void ScanTree(
        const RobloxInstance& node,
        int depth,
        const Vectors::Vector3& cameraPos,
        float maxDistance,
        uintptr_t skipCharacterModel)
    {
        if (!node.address || depth > 4)
            return;

        if (depth == 0 && node.Class() == "Model")
        {
            if (node.address == skipCharacterModel || ModelContainsHumanoid(node))
                return;
        }

        const std::string className = node.Class();
        if (IsPartClass(className))
            TryAddOccluder(node, cameraPos, maxDistance);

        if (depth >= 4)
            return;

        for (const auto& child : node.GetChildren())
            ScanTree(child, depth + 1, cameraPos, maxDistance, skipCharacterModel);
    }

    inline void RunOccluderScan()
    {
        if (!Globals::Roblox::Workspace.address || !Globals::Roblox::Camera.address)
            return;

        const Vectors::Vector3 cameraPos = Memory->read<Vectors::Vector3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Position);
        const float maxDistance = Options::ESP::VisibilityMaxDistance;
        const uintptr_t localCharacter = Globals::Roblox::LocalPlayer.Character().address;

        std::vector<Occluder> collected;
        collected.reserve(450);

        for (const auto& child : Globals::Roblox::Workspace.GetChildren())
        {
            const std::string cls = child.Class();
            if (cls == "Folder" || cls == "Model" || cls == "Terrain" || IsPartClass(cls))
                ScanTree(child, 0, cameraPos, maxDistance, localCharacter);
        }

        std::lock_guard<std::mutex> lock(occluderMutex);
        occluders = std::move(collected);
    }

    inline std::atomic<bool> scanInProgress{ false };

    inline void RefreshOccludersIfNeeded()
    {
        if (--refreshCooldown > 0)
            return;

        refreshCooldown = 240;

        if (scanInProgress.load())
            return;

        scanInProgress.store(true);
        std::thread([]() {
            RunOccluderScan();
            scanInProgress.store(false);
        }).detach();
    }

    inline void StartOccluderThread()
    {
        std::thread([]() {
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (--refreshCooldown > 0)
                    continue;
                refreshCooldown = 240;
                if (scanInProgress.load())
                    continue;
                scanInProgress.store(true);
                RunOccluderScan();
                scanInProgress.store(false);
            }
        }).detach();
    }

    inline bool RayIntersectsSphere(
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        const Vectors::Vector3& center,
        float radius,
        float maxDistance)
    {
        const Vectors::Vector3 oc = {
            origin.x - center.x,
            origin.y - center.y,
            origin.z - center.z
        };

        const float b = oc.x * dir.x + oc.y * dir.y + oc.z * dir.z;
        const float c = (oc.x * oc.x + oc.y * oc.y + oc.z * oc.z) - (radius * radius);
        const float discriminant = (b * b) - c;
        if (discriminant < 0.f)
            return false;

        const float sqrtD = std::sqrt(discriminant);
        const float t0 = -b - sqrtD;
        const float t1 = -b + sqrtD;

        const float hit = (t0 > 0.01f) ? t0 : t1;
        return hit > 0.01f && hit < maxDistance;
    }

    inline bool IsPointVisible(const Vectors::Vector3& target, uintptr_t ignoreModelAddress)
    {
        if ((!Options::ESP::VisibilityCheck && !Options::ESP::VisibilityChams && !Options::Chams::Enabled) || !Globals::Roblox::Camera.address)
            return true;

        Vectors::Vector3 origin;
        try
        {
            origin = Memory->read<Vectors::Vector3>(
                Globals::Roblox::Camera.address + Offsets::Camera::Position);
        }
        catch (...) { return true; }

        Vectors::Vector3 delta = {
            target.x - origin.x,
            target.y - origin.y,
            target.z - origin.z
        };

        const float distance = delta.Magnitude();
        if (distance < 1.5f)
            return true;

        delta.x /= distance;
        delta.y /= distance;
        delta.z /= distance;

        const float maxDistance = distance - 0.75f;
        if (maxDistance <= 0.5f)
            return true;

        const float cullDistance = distance + 12.f;

        std::lock_guard<std::mutex> lock(occluderMutex);
        for (const auto& wall : occluders)
        {
            if (ignoreModelAddress != 0 && wall.modelAddress == ignoreModelAddress)
                continue;

            const float wallDist = wall.position.Distance(origin);
            if (wallDist > cullDistance)
                continue;

            if (RayIntersectsSphere(origin, delta, wall.position, wall.radius + 0.35f, maxDistance))
                return false;
        }

        return true;
    }

    inline bool IsPointVisibleCached(uintptr_t cacheKey, const Vectors::Vector3& target, uintptr_t ignoreModelAddress)
    {
        if (!Options::ESP::VisibilityCheck && !Options::ESP::VisibilityChams && !Options::Chams::Enabled)
            return true;

        if (cacheKey != 0)
        {
            const auto found = pointVisibleCache.find(cacheKey);
            if (found != pointVisibleCache.end())
                return found->second;
        }

        const bool visible = IsPointVisible(target, ignoreModelAddress);
        if (cacheKey != 0)
            pointVisibleCache[cacheKey] = visible;

        return visible;
    }

    // Forced line-of-sight test that ignores the ESP visibility toggles, so
    // bullet tracers and hitsounds can respect walls even when ESP is off.
    inline bool IsPointVisibleForced(const Vectors::Vector3& target, uintptr_t ignoreModelAddress)
    {
        if (!Globals::Roblox::Camera.address)
            return true;

        Vectors::Vector3 origin;
        try
        {
            origin = Memory->read<Vectors::Vector3>(
                Globals::Roblox::Camera.address + Offsets::Camera::Position);
        }
        catch (...) { return true; }

        Vectors::Vector3 delta = {
            target.x - origin.x,
            target.y - origin.y,
            target.z - origin.z
        };

        const float distance = delta.Magnitude();
        if (distance < 1.5f)
            return true;

        delta.x /= distance;
        delta.y /= distance;
        delta.z /= distance;

        const float maxDistance = distance - 0.75f;
        if (maxDistance <= 0.5f)
            return true;

        const float cullDistance = distance + 12.f;

        std::lock_guard<std::mutex> lock(occluderMutex);
        for (const auto& wall : occluders)
        {
            if (ignoreModelAddress != 0 && wall.modelAddress == ignoreModelAddress)
                continue;

            const float wallDist = wall.position.Distance(origin);
            if (wallDist > cullDistance)
                continue;

            if (RayIntersectsSphere(origin, delta, wall.position, wall.radius + 0.35f, maxDistance))
                return false;
        }

        return true;
    }

    inline bool IsPlayerVisibleImpl(const RobloxPlayer& player)
    {
        const uintptr_t ignoreModel = player.Character.address;

        if (player.Head.address && IsPointVisible(player.Head.Position(), ignoreModel))
            return true;

        if (player.HumanoidRootPart.address && IsPointVisible(player.HumanoidRootPart.Position(), ignoreModel))
            return true;

        return false;
    }

    inline bool IsPlayerVisible(const RobloxPlayer& player)
    {
        if (!Options::ESP::VisibilityCheck)
            return true;

        if (player.address != 0)
        {
            const auto found = playerVisibleCache.find(player.address);
            if (found != playerVisibleCache.end())
                return found->second;
        }

        const bool visible = IsPlayerVisibleImpl(player);
        if (player.address != 0)
            playerVisibleCache[player.address] = visible;

        return visible;
    }

    // Always-perform raycast check, independent of ESP::VisibilityCheck toggle.
    // Used by aimbot/triggerbot/ragebot WallCheck so it works even when ESP visibility colors are off.
    inline bool IsPlayerOccluded(const RobloxPlayer& player)
    {
        if (!Globals::Roblox::Camera.address)
            return false;
        return !IsPlayerVisibleImpl(player);
    }

    inline ImU32 MakeColor(const float rgb[3], int alpha = 255)
    {
        return IM_COL32(
            static_cast<int>(rgb[0] * 255.f),
            static_cast<int>(rgb[1] * 255.f),
            static_cast<int>(rgb[2] * 255.f),
            alpha);
    }

    inline ImU32 GetVisibleColor(int alpha = 255)
    {
        return MakeColor(Options::ESP::VisibleColor, alpha);
    }

    inline ImU32 GetHiddenColor(int alpha = 255)
    {
        return MakeColor(Options::ESP::HiddenColor, alpha);
    }

    inline void DrawVisibilityBone(
        ImDrawList* drawList,
        const RobloxInstance& a,
        const RobloxInstance& b,
        uintptr_t ignoreModel,
        float thickness)
    {
        if (!drawList || !a.address || !b.address)
            return;

        const auto p1 = WorldToScreen(a.Position());
        const auto p2 = WorldToScreen(b.Position());
        if (p1.x < 0.f || p1.y < 0.f || p2.x < 0.f || p2.y < 0.f)
            return;

        const Vectors::Vector3 mid = {
            (a.Position().x + b.Position().x) * 0.5f,
            (a.Position().y + b.Position().y) * 0.5f,
            (a.Position().z + b.Position().z) * 0.5f
        };

        const uintptr_t cacheKey = a.address ^ (b.address << 1);
        const bool visible = IsPointVisibleCached(cacheKey, mid, ignoreModel);
        const ImU32 color = visible ? GetVisibleColor(235) : GetHiddenColor(235);

        drawList->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), color, thickness);
    }

    inline void DrawPlayerVisibilityChams(ImDrawList* drawList, const RobloxPlayer& player, float /*distance3D*/)
    {
        if (!drawList || !Options::ESP::VisibilityChams)
            return;

        const uintptr_t ignoreModel = player.Character.address;
        float thickness = Options::ESP::SkeletonThickness + 1.5f;
        if (thickness < 2.5f)
            thickness = 2.5f;

        if (player.RigType == 0)
        {
            DrawVisibilityBone(drawList, player.Head, player.Torso, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Torso, player.Left_Arm, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Torso, player.Right_Arm, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Torso, player.Left_Leg, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Torso, player.Right_Leg, ignoreModel, thickness);
        }
        else
        {
            DrawVisibilityBone(drawList, player.Head, player.Upper_Torso, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Upper_Torso, player.Lower_Torso, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Upper_Torso, player.Left_Upper_Arm, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Left_Upper_Arm, player.Left_Lower_Arm, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Left_Lower_Arm, player.Left_Hand, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Upper_Torso, player.Right_Upper_Arm, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Right_Upper_Arm, player.Right_Lower_Arm, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Right_Lower_Arm, player.Right_Hand, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Lower_Torso, player.Left_Upper_Leg, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Left_Upper_Leg, player.Left_Lower_Leg, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Left_Lower_Leg, player.Left_Foot, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Lower_Torso, player.Right_Upper_Leg, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Right_Upper_Leg, player.Right_Lower_Leg, ignoreModel, thickness);
            DrawVisibilityBone(drawList, player.Right_Lower_Leg, player.Right_Foot, ignoreModel, thickness);
        }
    }
}
