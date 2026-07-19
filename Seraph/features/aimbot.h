#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <functional>
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/imgui/KeyBind.h"
#include "combatfeedback.h"
#include "visibility.h"

namespace RivalsDetect
{
    struct SmokeGrenade
    {
        Vectors::Vector3 position;
        float radius;
    };

    inline std::vector<SmokeGrenade> cachedSmokes;
    inline int smokeFrameCounter = 0;
    inline bool isFlashed = false;
    inline int flashFrameCounter = 0;

    inline void UpdateSmokes()
    {
        if (!Globals::Roblox::isRivals || !Globals::Roblox::Workspace.address)
            return;

        if (++smokeFrameCounter % 120 != 0)
            return;

        cachedSmokes.clear();

        auto children = Globals::Roblox::Workspace.GetChildren();
        for (auto& child : children)
        {
            if (!child.address)
                continue;

            std::string cls = child.Class();
            if (cls != "Folder" && cls != "Model")
                continue;

            auto grandchildren = child.GetChildren();
            for (auto& gc : grandchildren)
            {
                if (!gc.address)
                    continue;

                std::string gcName = gc.Name();
                std::string gcClass = gc.Class();

                if (gcClass == "Part" || gcClass == "MeshPart")
                {
                    std::string lower = gcName;
                    for (auto& c : lower) c = (char)tolower(c);

                    if (lower.find("smoke") != std::string::npos ||
                        lower.find("grenade") != std::string::npos)
                    {
                        uintptr_t prim = Memory->read<uintptr_t>(gc.address + Offsets::BasePart::Primitive);
                        if (prim)
                        {
                            auto pos = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Position);
                            auto size = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Size);
                            float r = (size.x + size.y + size.z) / 6.f;
                            if (r < 1.f) r = 10.f;
                            cachedSmokes.push_back({ pos, r });
                        }
                    }
                }
            }
        }
    }

    inline bool IsBehindSmoke(const Vectors::Vector3& targetPos)
    {
        if (cachedSmokes.empty())
            return false;

        if (!Globals::Roblox::Camera.address)
            return false;

        auto camPos = Memory->read<Vectors::Vector3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Position);

        for (auto& smoke : cachedSmokes)
        {
            Vectors::Vector3 toTarget = { targetPos.x - camPos.x, targetPos.y - camPos.y, targetPos.z - camPos.z };
            Vectors::Vector3 toSmoke = { smoke.position.x - camPos.x, smoke.position.y - camPos.y, smoke.position.z - camPos.z };

            float targetDist = sqrtf(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
            float smokeDist = sqrtf(toSmoke.x * toSmoke.x + toSmoke.y * toSmoke.y + toSmoke.z * toSmoke.z);

            if (smokeDist > targetDist)
                continue;

            float dot = (toTarget.x * toSmoke.x + toTarget.y * toSmoke.y + toTarget.z * toSmoke.z);
            float cosAngle = dot / (targetDist * smokeDist + 0.001f);

            if (cosAngle > 0.95f)
            {
                float smokeToTargetDist = sqrtf(
                    (targetPos.x - smoke.position.x) * (targetPos.x - smoke.position.x) +
                    (targetPos.y - smoke.position.y) * (targetPos.y - smoke.position.y) +
                    (targetPos.z - smoke.position.z) * (targetPos.z - smoke.position.z));

                if (smokeToTargetDist < smoke.radius * 2.f)
                    return true;
            }
        }

        return false;
    }

    inline void UpdateFlash()
    {
        if (!Globals::Roblox::isRivals || !Globals::Roblox::Workspace.address)
            return;

        if (++flashFrameCounter % 30 != 0)
            return;

        isFlashed = false;

        auto character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address)
            return;

        auto children = character.GetChildren();
        for (auto& child : children)
        {
            if (!child.address)
                continue;

            std::string name = child.Name();
            for (auto& c : name) c = (char)tolower(c);

            if (name.find("flash") != std::string::npos)
            {
                isFlashed = true;
                return;
            }
        }

        auto guiChildren = Globals::Roblox::LocalPlayer.Character().GetChildren();
        for (auto& child : guiChildren)
        {
            if (!child.address)
                continue;

            std::string cls = child.Class();
            if (cls == "ScreenGui" || cls == "BillboardGui")
            {
                auto guikids = child.GetChildren();
                for (auto& gk : guikids)
                {
                    if (!gk.address)
                        continue;

                    std::string gkName = gk.Name();
                    for (auto& c : gkName) c = (char)tolower(c);

                    if (gkName.find("flash") != std::string::npos ||
                        gkName.find("blind") != std::string::npos ||
                        gkName.find("white") != std::string::npos)
                    {
                        isFlashed = true;
                        return;
                    }
                }
            }
        }
    }
}

inline Vectors::Vector3 GetVelocity(const RobloxInstance& part)
{
	if (!part.address)
		return Vectors::Vector3{ 0.f, 0.f, 0.f };
	
	uintptr_t primitiveAddr = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
	if (!primitiveAddr)
		return Vectors::Vector3{ 0.f, 0.f, 0.f };
	
	return Memory->read<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity);
}

// Returns the world position of the body part closest to the current cursor position.
// Falls back to Head if no valid part is found.
inline Vectors::Vector3 GetClosestPartPosition(const RobloxPlayer& player)
{
    POINT cursor{};
    GetCursorPos(&cursor);
    Vectors::Vector2 cur{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

    struct PartEntry { RobloxInstance part; };
    PartEntry parts[] = {
        { player.Head },
        { player.HumanoidRootPart },
        { player.Upper_Torso },
        { player.Lower_Torso },
        { player.Torso },
        { player.Left_Arm },
        { player.Right_Arm },
        { player.Left_Leg },
        { player.Right_Leg },
        { player.Left_Hand },
        { player.Right_Hand },
        { player.Left_Foot },
        { player.Right_Foot },
        { player.Left_Upper_Arm },
        { player.Left_Lower_Arm },
        { player.Right_Upper_Arm },
        { player.Right_Lower_Arm },
        { player.Left_Upper_Leg },
        { player.Left_Lower_Leg },
        { player.Right_Upper_Leg },
        { player.Right_Lower_Leg },
    };

    float bestDist = FLT_MAX;
    Vectors::Vector3 bestPos = player.Head.Position();

    for (auto& e : parts)
    {
        if (!e.part.address)
            continue;

        Vectors::Vector3 worldPos = e.part.Position();
        Vectors::Vector2 screenPos = WorldToScreen(worldPos);

        if (screenPos.x == -1.f && screenPos.y == -1.f)
            continue;

        float dx = screenPos.x - cur.x;
        float dy = screenPos.y - cur.y;
        float dist = dx * dx + dy * dy;

        if (dist < bestDist)
        {
            bestDist = dist;
            bestPos  = worldPos;
        }
    }

    return bestPos;
}

struct CandidateEntry { RobloxPlayer player; float screenDist; };
inline float ScoreCandidate(const CandidateEntry& c, const POINT& p);
inline float ClosestPartScore(const RobloxPlayer& player, const POINT& p);

inline Vectors::Vector3 GetTargetPosition(const RobloxPlayer& player)
{
    Vectors::Vector3 basePos;
    RobloxInstance targetPart(0);
    
    // Check if player is in air (Y velocity > 1 or < -1)
    Vectors::Vector3 velocity = player.Velocity;
    bool isInAir = (velocity.y > 1.0f || velocity.y < -1.0f);

    // "Closest Part" overrides the fixed bone and aims at the body part
    // nearest the cursor on screen (the smart targeting mode).
    if (Options::Aimbot::ClosestPart)
        return GetClosestPartPosition(player);

    // Use air target bone if player is in air, otherwise use normal target bone
    int boneToUse = isInAir ? Options::Aimbot::AirTargetBone : Options::Aimbot::TargetBone;
    
    switch (boneToUse)
    {
        case 0: // Head
            targetPart = player.Head;
            basePos = player.Head.Position();
            break;
        case 1: // Torso/HumanoidRootPart
            targetPart = player.HumanoidRootPart;
            basePos = player.HumanoidRootPart.Position();
            break;
        case 2: // Left Arm
            if (player.RigType == 0)
            {
                targetPart = player.Left_Arm;
                basePos = player.Left_Arm.Position();
            }
            else
            {
                targetPart = player.Left_Hand;
                basePos = player.Left_Hand.Position();
            }
            break;
        case 3: // Right Arm
            if (player.RigType == 0)
            {
                targetPart = player.Right_Arm;
                basePos = player.Right_Arm.Position();
            }
            else
            {
                targetPart = player.Right_Hand;
                basePos = player.Right_Hand.Position();
            }
            break;
        case 4: // Left Leg
            if (player.RigType == 0)
            {
                targetPart = player.Left_Leg;
                basePos = player.Left_Leg.Position();
            }
            else
            {
                targetPart = player.Left_Foot;
                basePos = player.Left_Foot.Position();
            }
            break;
        case 5: // Right Leg
            if (player.RigType == 0)
            {
                targetPart = player.Right_Leg;
                basePos = player.Right_Leg.Position();
            }
            else
            {
                targetPart = player.Right_Foot;
                basePos = player.Right_Foot.Position();
            }
            break;
        case 6: // Lower Torso
            if (player.RigType == 1) // R15 only
            {
                targetPart = player.Lower_Torso;
                basePos = player.Lower_Torso.Position();
            }
            else
            {
                targetPart = player.HumanoidRootPart;
                basePos = player.HumanoidRootPart.Position();
            }
            break;
        case 7: // Upper Torso
            if (player.RigType == 1) // R15 only
            {
                targetPart = player.Upper_Torso;
                basePos = player.Upper_Torso.Position();
            }
            else
            {
                targetPart = player.HumanoidRootPart;
                basePos = player.HumanoidRootPart.Position();
            }
            break;
        default:
            targetPart = player.Head;
            basePos = player.Head.Position();
            break;
        case 8: // Closest Part — picks the body part nearest the cursor on screen
            // Prediction is handled separately below; return early here.
            return GetClosestPartPosition(player);
    }
    
    // Apply prediction if enabled
    if (Options::Aimbot::Prediction && targetPart.address != 0)
    {
        Vectors::Vector3 targetVelocity = player.Velocity;
        
        // Multiply velocity by prediction factors
        Vectors::Vector3 predictionOffset = {
            targetVelocity.x * Options::Aimbot::PredictionX,
            targetVelocity.y * Options::Aimbot::PredictionY,
            targetVelocity.z * Options::Aimbot::PredictionX
        };
        
        // Add prediction offset to base position
        return Vectors::Vector3{
            basePos.x + predictionOffset.x,
            basePos.y + predictionOffset.y,
            basePos.z + predictionOffset.z
        };
    }
    
    return basePos;
}

inline RobloxPlayer GetClosestPlayer()
{
    // Dynamically retrieve LocalPlayer to prevent stale pointer issues when rejoining
    if (Globals::Roblox::Players.address != 0)
    {
        Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
    }

    RobloxPlayer target;
    (void)target;
    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");

    POINT p;
    GetCursorPos(&p);

    std::vector<CandidateEntry> candidates;
    candidates.reserve(32);

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        auto HRP = player.HumanoidRootPart;
        if (!HRP.address)
            continue;

        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (Globals::Roblox::isOverkill && Globals::Roblox::LocalPlayer.address &&
            !player.Name.empty() &&
            player.Name == Globals::Roblox::LocalPlayer.Name())
            continue;

        if (Options::Aimbot::TeamCheck && IsTeammate(player))
        {
            continue;
        }

        if (player.Health == 0)
            continue;

        // Skip knocked/downed players if check is enabled (health at or below 5)
        if (player.Health > 0 && player.Health <= 5.0f && Options::Aimbot::DownedCheck)
            continue;

        if (Options::Aimbot::IgnoreJump)
        {
            auto part = player.HumanoidRootPart;
            if (part.address)
            {
                uintptr_t primitive = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
                if (primitive)
                {
                    float yVel = Memory->read<float>(primitive + Offsets::Primitive::AssemblyLinearVelocity + 4);
                    if (yVel > Options::Aimbot::JumpThreshold || yVel < -Options::Aimbot::JumpThreshold)
                        continue;
                }
            }
        }

        if (Globals::Roblox::isRivals)
        {
            if (Options::Rivals::IgnoreSmoke)
                RivalsDetect::UpdateSmokes();

            if (Options::Rivals::IgnoreFlash)
                RivalsDetect::UpdateFlash();

            if (Options::Rivals::IgnoreSmoke && player.Head.address)
            {
                auto headPos = player.Head.Position();
                if (RivalsDetect::IsBehindSmoke(headPos))
                    continue;
            }

            if (Options::Rivals::IgnoreFlash && RivalsDetect::isFlashed)
                continue;
        }

        if (Options::Aimbot::WallCheck && Visibility::IsPlayerOccluded(player))
            continue;

        // Only Visible: skip players hidden behind geometry unless wall check
        // is also enabled (in which case the occluder test above already ran).
        if (Options::Aimbot::OnlyVisible && !Options::Aimbot::WallCheck &&
            Visibility::IsPlayerOccluded(player))
            continue;

        auto targetPos = GetTargetPosition(player);
        auto targetPos2D = WorldToScreen(targetPos);

        if (targetPos2D.x == -1 && targetPos2D.y == -1)
            continue;

        if (localHRP.address)
        {
            Vectors::Vector3 diff = localHRP.Position() - targetPos;
            float distance3D = diff.Magnitude();

            if (distance3D > Options::Aimbot::Range)
                continue;
        }

        auto distance = targetPos2D.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });

        if (distance > Options::Aimbot::FOV)
            continue;

        // Collect every valid candidate, then pick by the chosen priority so
        // "Closest Part" / health / distance ordering is honoured.
        candidates.push_back({ player, distance });
    }

    if (candidates.empty())
        return target;

    RobloxPlayer best = candidates[0].player;
    float bestScore = ScoreCandidate(candidates[0], p);
    for (size_t i = 1; i < candidates.size(); ++i)
    {
        float score = ScoreCandidate(candidates[i], p);
        if (score < bestScore)
        {
            bestScore = score;
            best = candidates[i].player;
        }
    }
    return best;
}

// Lower score = higher priority. Used by GetClosestPlayer to rank candidates.
inline float ScoreCandidate(const CandidateEntry& c, const POINT& p)
{
    switch (Options::Aimbot::TargetPriority)
    {
        case 1: // Closest to crosshair
            return c.screenDist;
        case 2: // Lowest health first
            return c.player.Health;
        case 3: // Farthest first
        {
            if (!Globals::Roblox::LocalPlayer.Character().address)
                return c.screenDist;
            auto hrp = Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart");
            if (!hrp.address) return c.screenDist;
            return -hrp.Position().Distance(c.player.HumanoidRootPart.Position());
        }
        case 4: // Highest health first
        {
            if (!Globals::Roblox::LocalPlayer.Character().address)
                return c.screenDist;
            auto hrp = Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart");
            if (!hrp.address) return c.screenDist;
            return hrp.Position().Distance(c.player.HumanoidRootPart.Position());
        }
        case 0: // Closest Part: rank by nearest on-screen body part to cursor
        default:
        {
            if (Options::Aimbot::ClosestPart)
                return ClosestPartScore(c.player, p);
            return c.screenDist;
        }
    }
}

// Distance (squared-ish) from the cursor to the nearest body part of the player.
inline float ClosestPartScore(const RobloxPlayer& player, const POINT& p)
{
    POINT cursor{};
    GetCursorPos(&cursor);
    Vectors::Vector2 cur{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

    RobloxInstance parts[] = {
        player.Head, player.HumanoidRootPart, player.Upper_Torso, player.Lower_Torso,
        player.Torso, player.Left_Arm, player.Right_Arm, player.Left_Leg, player.Right_Leg,
        player.Left_Hand, player.Right_Hand, player.Left_Foot, player.Right_Foot,
        player.Left_Upper_Arm, player.Left_Lower_Arm, player.Right_Upper_Arm,
        player.Right_Lower_Arm, player.Left_Upper_Leg, player.Left_Lower_Leg,
        player.Right_Upper_Leg, player.Right_Lower_Leg
    };

    float best = FLT_MAX;
    for (auto& part : parts)
    {
        if (!part.address) continue;
        Vectors::Vector2 sp = WorldToScreen(part.Position());
        if (sp.x == -1.f && sp.y == -1.f) continue;
        float dx = sp.x - cur.x, dy = sp.y - cur.y;
        float d = dx * dx + dy * dy;
        if (d < best) best = d;
    }
    return best;
}

inline float ApplySmoothnessCurve(float smoothness, int curveType)
{
    // Apply curve transformation based on selected type
    // Use exponential scaling for more balanced control across the range
    float t;
    switch (curveType)
    {
        case 0: // Linear - exponential scaling for better balance
        {
            // Map 0.0-1.0 smoothness to exponential speed curve
            // Lower values = faster, higher values = much slower
            float exponent = 1.0f + (smoothness * 4.0f); // 1.0 to 5.0
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 1: // Ease In (starts slow, ends fast)
        {
            float exponent = 1.5f + (smoothness * 3.0f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 2: // Ease Out (starts fast, ends slow)
        {
            float exponent = 2.0f + (smoothness * 2.5f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 3: // Ease In-Out (smooth on both ends)
        {
            float exponent = 1.8f + (smoothness * 3.5f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
        case 4: // Custom Bezier Curve
        {
            if (Options::Aimbot::CustomCurveEnabled)
            {
                // Cubic Bezier curve with control points
                float p0 = 0.0f;
                float p1 = Options::Aimbot::CustomCurveP1[1];
                float p2 = Options::Aimbot::CustomCurveP2[1];
                float p3 = 1.0f;
                
                float u = 1.0f - smoothness;
                float tt = smoothness * smoothness;
                float ttt = tt * smoothness;
                float uu = u * u;
                float uuu = uu * u;
                
                // Bezier formula
                float curveValue = uuu * p0 + 3 * uu * smoothness * p1 + 3 * u * tt * p2 + ttt * p3;
                t = 1.0f - curveValue;
            }
            else
            {
                // Fallback to linear if custom not enabled
                float exponent = 1.0f + (smoothness * 4.0f);
                t = pow(1.0f - smoothness, exponent);
            }
            break;
        }
        default:
        {
            float exponent = 1.0f + (smoothness * 4.0f);
            t = pow(1.0f - smoothness, exponent);
            break;
        }
    }
    return std::clamp<float>(t, 0.001f, 1.0f);
}

inline void CameraRotation(const RobloxPlayer& target)
{
    Matrixes::Matrix3x3 currentRotation = Memory->read<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);

    sCFrame cameraCFrame = Globals::Roblox::Camera.CFrame();
    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);

    Vectors::Vector3 targetPos = GetTargetPosition(target);
    
    // Add shake if enabled
    if (Options::Aimbot::Shake && Options::Aimbot::ShakeIntensity > 0.0f)
    {
        static float shakeTime = 0.0f;
        shakeTime += 0.1f;
        
        float shakeX = sin(shakeTime * 10.0f) * Options::Aimbot::ShakeIntensity * 0.1f;
        float shakeY = cos(shakeTime * 8.0f) * Options::Aimbot::ShakeIntensity * 0.1f;
        float shakeZ = sin(shakeTime * 12.0f) * Options::Aimbot::ShakeIntensity * 0.1f;
        
        targetPos.x += shakeX;
        targetPos.y += shakeY;
        targetPos.z += shakeZ;
    }

    sCFrame lookAtCFrame = LookAt(camPos, targetPos);

    Vectors::Vector3 rightVec = lookAtCFrame.GetRightVector();
    Vectors::Vector3 upVec = lookAtCFrame.GetUpVector();
    Vectors::Vector3 lookVec = lookAtCFrame.GetLookVector();

    Matrixes::Matrix3x3 rotationMatrix
    {
        rightVec.x, upVec.x, lookVec.x,
        rightVec.y, upVec.y, lookVec.y,
        rightVec.z, upVec.z, lookVec.z
    };

    Vectors::Vector4 currentQuat = Vectors::Vector4::FromMatrix(currentRotation);
    Vectors::Vector4 targetQuat = Vectors::Vector4::FromMatrix(rotationMatrix);

    // Apply smoothness curve
    float t = ApplySmoothnessCurve(Options::Aimbot::Smoothness, Options::Aimbot::SmoothnessCurve);

    Vectors::Vector4 smoothedQuat = Vectors::Vector4::Slerp(currentQuat, targetQuat, t);
    Matrixes::Matrix3x3 smoothedMatrix = smoothedQuat.ToMatrix();

    Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, smoothedMatrix);
}

inline void Mouse(const Vectors::Vector2& targetPos, const POINT& p)
{
    static float accumulatedX = 0.0f;
    static float accumulatedY = 0.0f;

    float dx = static_cast<float>(targetPos.x - p.x);
    float dy = static_cast<float>(targetPos.y - p.y);

    // Add shake if enabled
    if (Options::Aimbot::Shake && Options::Aimbot::ShakeIntensity > 0.0f)
    {
        static float shakeTime = 0.0f;
        shakeTime += 0.1f;
        
        float shakeX = sin(shakeTime * 10.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        float shakeY = cos(shakeTime * 8.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        
        dx += shakeX;
        dy += shakeY;
    }

    // Apply smoothness curve
    float t = ApplySmoothnessCurve(Options::Aimbot::Smoothness, Options::Aimbot::SmoothnessCurve);
    
    // Scale for mouse movement (higher = faster)
    float speedScale = 50.0f;
    t = t * speedScale;

    float moveX = dx * t;
    float moveY = dy * t;

    accumulatedX += moveX;
    accumulatedY += moveY;

    int intMoveX = static_cast<int>(accumulatedX);
    int intMoveY = static_cast<int>(accumulatedY);

    accumulatedX -= intMoveX;
    accumulatedY -= intMoveY;

    if (intMoveX != 0 || intMoveY != 0)
    {
        SetCursorPos(p.x + intMoveX, p.y + intMoveY);
    }
}

inline void SpoofMousePosition(const Vectors::Vector2& screenPos)
{
    if (!Globals::Roblox::DataModel.address)
        return;

    auto mouseService = Globals::Roblox::DataModel.FindFirstChildWhichIsA("MouseService");
    if (!mouseService.address)
        return;

    uintptr_t inputObject = Memory->read<uintptr_t>(mouseService.address + Offsets::MouseService::InputObject);
    if (!inputObject)
        inputObject = Memory->read<uintptr_t>(mouseService.address + Offsets::MouseService::InputObject2);

    if (inputObject)
        Memory->write<Vectors::Vector2>(inputObject + Offsets::MouseService::MousePosition, screenPos);
}

struct Vector2int16 {
    int16_t x;
    int16_t y;
};

inline void ViewportSilentAim(const Vectors::Vector2& target_screen_pos, const Vectors::Vector2& screen_size)
{
    if (!Globals::Roblox::Camera.address)
        return;
        
    Vector2int16 result;
    result.x = (int16_t)(2 * (screen_size.x - target_screen_pos.x));
    result.y = (int16_t)(2 * (screen_size.y - target_screen_pos.y));
    
    Memory->write<Vector2int16>(Globals::Roblox::Camera.address + Offsets::Camera::Viewport, result);
}

inline void ResetViewport(const Vectors::Vector2& screen_size)
{
    if (!Globals::Roblox::Camera.address)
        return;
        
    Vector2int16 result;
    result.x = (int16_t)screen_size.x;
    result.y = (int16_t)screen_size.y;
    
    Memory->write<Vector2int16>(Globals::Roblox::Camera.address + Offsets::Camera::Viewport, result);
}

inline void SilentLockAim()
{
    static Matrixes::Matrix3x3 savedRotation{};
    static Vector2int16 savedViewport{};
    static bool hasSaved = false;
    static bool wasActive = false;

    bool keyDown = Options::Aimbot::SilentLockKey == 0 ||
        KeyBind::IsPressed(Options::Aimbot::SilentLockKey);

    if (!Options::Aimbot::SilentLock || !keyDown || !Globals::Roblox::Camera.address)
    {
        if (wasActive && hasSaved)
        {
            if (Globals::Roblox::Camera.address)
            {
                Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, savedRotation);
                Memory->write<Vector2int16>(Globals::Roblox::Camera.address + Offsets::Camera::Viewport, savedViewport);
            }
            hasSaved = false;
        }
        wasActive = false;
        return;
    }

    if (!wasActive)
    {
        savedRotation = Memory->read<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
        savedViewport = Memory->read<Vector2int16>(Globals::Roblox::Camera.address + Offsets::Camera::Viewport);
        hasSaved = true;
    }

    RobloxPlayer target = GetClosestPlayer();
    if (target.address == 0)
    {
        wasActive = true;
        return;
    }

    Vectors::Vector3 camPos = Globals::Roblox::Camera.Position();
    Vectors::Vector3 targetPos = GetTargetPosition(target);

    if (Options::Aimbot::SilentLockMode == 1)
    {
        // Viewport offset: shift the hit point onto the target without moving the view.
        Vectors::Vector2 screen = WorldToScreen(targetPos);
        if (screen.x != -1 && screen.y != -1)
        {
            Vectors::Vector2 dims = Memory->read<Vectors::Vector2>(
                Globals::Roblox::VisualEngine + Offsets::VisualEngine::Dimensions);
            Vector2int16 result;
            result.x = (int16_t)(2 * (dims.x - screen.x));
            result.y = (int16_t)(2 * (dims.y - screen.y));
            Memory->write<Vector2int16>(Globals::Roblox::Camera.address + Offsets::Camera::Viewport, result);
        }
    }
    else
    {
        // Camera-rotation write: aim silently at the locked target.
        sCFrame aimCFrame = LookAt(camPos, targetPos);
        Vectors::Vector3 lookVec = aimCFrame.GetLookVector();
        Matrixes::Matrix3x3 newRot = savedRotation;
        newRot.r02 = lookVec.x;
        newRot.r12 = lookVec.y;
        newRot.r22 = lookVec.z;
        Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, newRot);
    }

    wasActive = true;
}

// ── Aim Info runtime state (filled each frame by RunAimbot) ────────────────
struct AimInfoState
{
    bool valid = false;
    std::string name;
    float distance = 0.f;
    float health = 0.f;
    float maxHealth = 0.f;
    std::string part;
};
inline AimInfoState g_AimInfo;

inline const char* AimInfoPartName()
{
    if (Options::Aimbot::ClosestPart)
        return "Closest Part";
    switch (Options::Aimbot::TargetBone)
    {
    case 0: return "Head";
    case 1: return "Torso";
    case 2: return "Left Arm";
    case 3: return "Right Arm";
    case 4: return "Left Leg";
    case 5: return "Right Leg";
    case 6: return "Lower Torso";
    case 7: return "Upper Torso";
    default: return "Head";
    }
}

inline void RenderAimInfo()
{
    if (!Options::Aimbot::AimInfo || !g_AimInfo.valid)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    // Build the lines we want to show.
    std::vector<std::pair<std::string, ImU32>> lines;
    if (Options::Aimbot::AimInfoName)
        lines.push_back({ g_AimInfo.name, IM_COL32(235, 235, 235, 255) });
    if (Options::Aimbot::AimInfoDistance)
    {
        char b[48]; snprintf(b, sizeof(b), "Distance  %.0f studs", g_AimInfo.distance);
        lines.push_back({ b, IM_COL32(180, 200, 255, 255) });
    }
    if (Options::Aimbot::AimInfoHealth)
    {
        char b[48]; snprintf(b, sizeof(b), "Health  %.0f / %.0f", g_AimInfo.health, g_AimInfo.maxHealth);
        lines.push_back({ b, IM_COL32(180, 255, 200, 255) });
    }
    if (Options::Aimbot::AimInfoPart)
    {
        std::string s = "Target  " + g_AimInfo.part;
        lines.push_back({ s, IM_COL32(255, 210, 150, 255) });
    }

    if (lines.empty()) return;

    const float padX = 12.0f, padY = 8.0f;
    const float lineH = 20.0f;
    const float barH = 6.0f;

    // Measure widest line for panel width.
    float maxW = 0.0f;
    for (auto& l : lines)
        maxW = (std::max)(maxW, ImGui::CalcTextSize(l.first.c_str()).x);

    const float panelW = maxW + padX * 2.0f;
    const float headerH = 26.0f;
    const bool showBar = Options::Aimbot::AimInfoHealth && g_AimInfo.maxHealth > 0.0f;
    const float contentH = headerH + static_cast<float>(lines.size()) * lineH
        + (showBar ? barH + 6.0f : 0.0f) + padY * 2.0f - 4.0f;

    ImVec2 pos(io.DisplaySize.x - panelW - 18.0f, 120.0f);
    ImVec2 rectMin(pos.x, pos.y);
    ImVec2 rectMax(pos.x + panelW, pos.y + contentH);

    // Body
    dl->AddRectFilled(rectMin, rectMax, IM_COL32(14, 16, 22, 225), 6.0f);
    dl->AddRect(rectMin, rectMax, IM_COL32(90, 120, 180, 200), 6.0f, 0, 1.5f);

    // Header strip
    ImVec2 headerMax(pos.x + panelW, pos.y + headerH);
    dl->AddRectFilled(rectMin, headerMax, IM_COL32(40, 60, 110, 235), 6.0f);
    dl->AddLine(ImVec2(pos.x, pos.y + headerH), ImVec2(pos.x + panelW, pos.y + headerH), IM_COL32(90, 120, 180, 200), 1.0f);
    dl->AddText(ImVec2(pos.x + padX, pos.y + 5.0f), IM_COL32(255, 255, 255, 255), "TARGET LOCKED");

    // Lines
    float y = pos.y + headerH + 4.0f;
    for (auto& l : lines)
    {
        dl->AddText(ImVec2(pos.x + padX, y + 2.0f), l.second, l.first.c_str());
        y += lineH;
    }

    // Health bar
    if (showBar)
    {
        float frac = (std::max)(0.0f, (std::min)(1.0f, g_AimInfo.health / g_AimInfo.maxHealth));
        ImVec2 barMin(pos.x + padX, y);
        ImVec2 barMax(pos.x + panelW - padX, y + barH);
        dl->AddRectFilled(barMin, barMax, IM_COL32(30, 30, 35, 255), 3.0f);
        ImU32 barCol = frac > 0.5f ? IM_COL32(80, 220, 120, 255)
            : (frac > 0.25f ? IM_COL32(230, 200, 70, 255) : IM_COL32(230, 80, 80, 255));
        dl->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * frac, barMax.y), barCol, 3.0f);
    }
}

inline void FlickbotTick()
{
    if (!Options::Aimbot::Flickbot || Options::Aimbot::FlickbotKey == 0)
        return;
    if (!Globals::Roblox::Camera.address)
        return;

    static bool wasDown = false;
    bool isDown = KeyBind::IsPressed(Options::Aimbot::FlickbotKey);
    if (!isDown || wasDown)
    {
        wasDown = isDown;
        return;
    }
    wasDown = isDown;

    // Pick the closest-to-crosshair enemy inside the flick FOV.
    RobloxPlayer best;
    float bestScore = FLT_MAX;
    POINT cursor;
    GetCursorPos(&cursor);

    for (const auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (player.address == 0 || player.address == Globals::Roblox::LocalPlayer.address)
            continue;
        if (Options::Aimbot::FlickbotTeamCheck && IsTeammate(player))
            continue;

        Vectors::Vector3 hp = GetTargetPosition(player);
        Vectors::Vector2 screen = WorldToScreen(hp);
        if (screen.x == -1.f || screen.y == -1.f)
            continue;

        float dx = screen.x - static_cast<float>(cursor.x);
        float dy = screen.y - static_cast<float>(cursor.y);
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > Options::Aimbot::FlickbotFOV)
            continue;

        float score = dist; // crosshair-proximity
        if (score < bestScore)
        {
            bestScore = score;
            best = player;
        }
    }

    if (best.address == 0)
        return;

    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);
    Vectors::Vector3 targetPos = GetTargetPosition(best);

    sCFrame lookAtCFrame = LookAt(camPos, targetPos);
    Vectors::Vector3 rightVec = lookAtCFrame.GetRightVector();
    Vectors::Vector3 upVec = lookAtCFrame.GetUpVector();
    Vectors::Vector3 lookVec = lookAtCFrame.GetLookVector();

    Matrixes::Matrix3x3 targetRotation
    {
        rightVec.x, upVec.x, lookVec.x,
        rightVec.y, upVec.y, lookVec.y,
        rightVec.z, upVec.z, lookVec.z
    };

    Matrixes::Matrix3x3 currentRotation = Memory->read<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);

    if (Options::Aimbot::FlickbotSmoothing > 0.0f)
    {
        // Slerp the camera orientation toward the target for a smooth flick.
        Vectors::Vector4 currentQuat = Vectors::Vector4::FromMatrix(currentRotation);
        Vectors::Vector4 targetQuat = Vectors::Vector4::FromMatrix(targetRotation);
        float t = Options::Aimbot::FlickbotSmoothing;
        if (t > 1.0f) t = 1.0f;
        Vectors::Vector4 flickQuat = Vectors::Vector4::Slerp(currentQuat, targetQuat, t);
        Matrixes::Matrix3x3 flickMatrix = flickQuat.ToMatrix();
        Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, flickMatrix);
    }
    else
    {
        Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, targetRotation);
    }
}

inline void PFSilentAim(const RobloxPlayer& target)
{
    static Matrixes::Matrix3x3 savedRotation{};
    static bool wasActive = false;
    static bool hasSaved = false;

    bool isActive = target.address != 0 && Globals::Roblox::Camera.address != 0;

    if (!isActive && wasActive && hasSaved)
    {
        if (Globals::Roblox::Camera.address)
            Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, savedRotation);
        hasSaved = false;
    }

    if (isActive)
    {
        if (!wasActive)
        {
            savedRotation = Memory->read<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
            hasSaved = true;
        }

        Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);
        Vectors::Vector3 targetPos = GetTargetPosition(target);

        sCFrame aimCFrame = LookAt(camPos, targetPos);
        Vectors::Vector3 lookVec = aimCFrame.GetLookVector();

        Matrixes::Matrix3x3 newRot = savedRotation;
        newRot.r02 = lookVec.x;
        newRot.r12 = lookVec.y;
        newRot.r22 = lookVec.z;

        Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, newRot);
    }

    wasActive = isActive;
}

inline void PFHeadTransparency()
{
    if (!Globals::Roblox::isPhantomForces)
        return;

    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    if (!localCharacter.address)
        return;

    auto localHead = localCharacter.FindFirstChild("Head");
    if (localHead.address)
        Memory->write<float>(localHead.address + Offsets::BasePart::Transparency, 1.0f);
}

inline bool ShouldUseSilentAim()
{
    return Options::Aimbot::AimingType == 2 || Options::Aimbot::SilentAim;
}

inline void MouseSendInput(const Vectors::Vector2& targetPos, const POINT& currentPos, float sensitivity)
{
    if (currentPos.x == targetPos.x && currentPos.y == targetPos.y)
        return;

    static float accumulatedX = 0.0f;
    static float accumulatedY = 0.0f;

    float dx = static_cast<float>(targetPos.x - currentPos.x);
    float dy = static_cast<float>(targetPos.y - currentPos.y);

    // Add shake if enabled
    if (Options::Aimbot::Shake && Options::Aimbot::ShakeIntensity > 0.0f)
    {
        static float shakeTime = 0.0f;
        shakeTime += 0.1f;
        
        float shakeX = sin(shakeTime * 10.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        float shakeY = cos(shakeTime * 8.0f) * Options::Aimbot::ShakeIntensity * 0.5f;
        
        dx += shakeX;
        dy += shakeY;
    }

    // Apply smoothness curve
    float t = ApplySmoothnessCurve(Options::Aimbot::Smoothness, Options::Aimbot::SmoothnessCurve);

    float sensitivityScale = 1.0f / (sensitivity + 0.2f);
    float speedScale = 0.5f;

    float moveX = dx * t * sensitivityScale * speedScale;
    float moveY = dy * t * sensitivityScale * speedScale;

    accumulatedX += moveX;
    accumulatedY += moveY;

    int intMoveX = static_cast<int>(accumulatedX);
    int intMoveY = static_cast<int>(accumulatedY);

    if (std::abs(dx) < 1.0f && std::abs(dy) < 1.0f)
    {
        accumulatedX = 0.0f;
        accumulatedY = 0.0f;
        return;
    }

    accumulatedX -= intMoveX;
    accumulatedY -= intMoveY;

    if (intMoveX != 0 || intMoveY != 0)
    {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = intMoveX;
        input.mi.dy = intMoveY;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;

        SendInput(1, &input, sizeof(INPUT));
    }
}

// Instantly snaps the REAL cursor onto the target screen position via SendInput.
// Unlike SpoofMousePosition (which only writes MouseService memory that Chickynoid
// ignores), this moves the actual OS cursor so the in-game shot originates from the
// enemy and lands. Used on Overkill so silent aim bullets actually connect.
inline void MouseInstant(const Vectors::Vector2& targetPos)
{
    POINT cur;
    if (!GetCursorPos(&cur))
        return;

    int dx = static_cast<int>(targetPos.x) - cur.x;
    int dy = static_cast<int>(targetPos.y) - cur.y;
    if (dx == 0 && dy == 0)
        return;

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

inline bool IsLMBDown()
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

// Experimental silent-aim variant: teleport the local character next to the
// target for ~one game frame so a client-authoritative hit registers, then
// restore its position/velocity. Keeps the crosshair still (camera unaffected
// for the rendered frame). Falls back to nothing if the character can't be found.
inline void TeleportSilentAim(const RobloxPlayer& target)
{
    if (!Globals::Roblox::LocalPlayer.address || !Globals::Roblox::Camera.address)
        return;

    RobloxInstance localChar = Globals::Roblox::LocalPlayer.Character();
    if (!localChar.address && Globals::Roblox::isOverkill && g_ResolveCharacterFallback)
        localChar = g_ResolveCharacterFallback(Globals::Roblox::LocalPlayer.address);
    if (!localChar.address)
        return;

    auto hrp = localChar.FindFirstChild("HumanoidRootPart");
    if (!hrp.address)
        return;
    auto tgtHRP = target.HumanoidRootPart;
    if (!tgtHRP.address)
        return;

    uintptr_t prim = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
    if (!prim)
        return;

    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Position);
    Vectors::Vector3 tgtPos = tgtHRP.Position();

    Vectors::Vector3 dir = { tgtPos.x - camPos.x, tgtPos.y - camPos.y, tgtPos.z - camPos.z };
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 0.001f)
        return;
    dir.x /= len; dir.y /= len; dir.z /= len;

    // A couple studs in front of the target (toward us) so the hit lands.
    Vectors::Vector3 orbit = { tgtPos.x - dir.x * 2.5f, tgtPos.y, tgtPos.z - dir.z * 2.5f };

    struct RestoreState { uintptr_t prim = 0; Vectors::Vector3 pos{}; Vectors::Vector3 vel{}; bool pending = false; std::chrono::steady_clock::time_point time; };
    static RestoreState restore;

    if (restore.pending)
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - restore.time).count() >= 18)
        {
            Memory->write<Vectors::Vector3>(restore.prim + Offsets::Primitive::Position, restore.pos);
            Memory->write<Vectors::Vector3>(restore.prim + Offsets::Primitive::AssemblyLinearVelocity, restore.vel);
            restore.pending = false;
        }
        return;
    }

    restore.prim = prim;
    restore.pos = hrp.Position();
    restore.vel = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
    restore.time = std::chrono::steady_clock::now();
    restore.pending = true;

    Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::Position, orbit);
    Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity, { 0.f, 0.f, 0.f });
}

// Draws the FOV zone in the requested shape. For gradient/rainbow color modes
// the circle/ellipse is built from segments so each segment can take its own
// color (via fovColorAt); solid/glow modes use a single outline color.
inline void DrawFOVShape(ImDrawList* dl, const ImVec2& center, float r, ImU32 color,
    int shape, float thickness, bool fill, ImU32 fillColor,
    const std::function<ImU32(float)>& fovColorAt, float rotation = 0.0f)
{
    const float PI = 3.1415926535897932f;
    // Helper: angle for vertex i of an n-sided regular polygon, rotated.
    auto vert = [&](int i, int n) -> ImVec2 {
        float a = rotation + (static_cast<float>(i) / static_cast<float>(n)) * 2.0f * PI;
        return ImVec2(center.x + cosf(a) * r, center.y + sinf(a) * r);
    };

    if (shape == 1) // Square
    {
        ImVec2 pts[4] = { vert(0,4), vert(1,4), vert(2,4), vert(3,4) };
        if (fill) dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], fillColor);
        dl->AddPolyline(pts, 4, color, true, thickness);
    }
    else if (shape == 2) // Triangle (pointing up, bottom edge fully closed)
    {
        ImVec2 pts[3] = { vert(0,3), vert(1,3), vert(2,3) };
        if (fill) dl->AddTriangleFilled(pts[0], pts[1], pts[2], fillColor);
        dl->AddTriangle(pts[0], pts[1], pts[2], color, thickness);
    }
    else if (shape == 3) // Hexagon
    {
        ImVec2 pts[6];
        for (int i = 0; i < 6; i++) pts[i] = vert(i, 6);
        if (fill)
        {
            dl->AddConvexPolyFilled(pts, 6, fillColor);
        }
        dl->AddPolyline(pts, 6, color, true, thickness);
    }
    else // Circle (segmented for gradient/rainbow)
    {
        const int segs = 96;
        if (fill)
        {
            dl->AddCircleFilled(center, r, fillColor, segs);
        }
        // For multi-color modes, draw per-segment colored arcs so gradient/rainbow
        // actually shows; otherwise a single clean circle.
        if (shape == 0 && fovColorAt)
        {
            ImVec2 prev(center.x + cosf(rotation) * r, center.y + sinf(rotation) * r);
            for (int i = 1; i <= segs; i++)
            {
                float t = static_cast<float>(i) / static_cast<float>(segs);
                float a = rotation + t * 2.0f * PI;
                ImVec2 cur(center.x + cosf(a) * r, center.y + sinf(a) * r);
                dl->AddLine(prev, cur, fovColorAt(t), thickness);
                prev = cur;
            }
        }
        else
        {
            dl->AddCircle(center, r, color, segs, thickness);
        }
    }
}

inline void RunAimbot(ImDrawList* drawList)
{
    // Check if aimbot is enabled first
    if (!Options::Aimbot::Aimbot)
        return;

    auto localTeam = Globals::Roblox::LocalPlayer.Team();
    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    auto Dimensions = Memory->read<Vectors::Vector2>(Globals::Roblox::VisualEngine + Offsets::VisualEngine::Dimensions);

    // Silent Lock runs independently of the main aimbot toggle.
    SilentLockAim();

    // Flickbot: one-shot snap to the nearest target on key press.
    FlickbotTick();

    if (Globals::Caches::CachedPlayerObjects.empty())
        return;

    POINT p;
    GetCursorPos(&p);

    HWND robloxWindow = Globals::Viewport::RobloxHWND;
    if (robloxWindow && IsWindow(robloxWindow))
    {
        ScreenToClient(robloxWindow, &p);
    }

    int CombatType;
    
    bool yAxisCheck;

    if (Dimensions.x < GetSystemMetrics(SM_CXSCREEN) || Dimensions.y < GetSystemMetrics(SM_CYSCREEN))
    {
        yAxisCheck = (p.y - Dimensions.y / 2) <= 25; // windowed mode
    }
    else
    {
        yAxisCheck = p.y == Dimensions.y / 2;
    }

    if (p.x == Dimensions.x / 2 && yAxisCheck)
    {                                          //likely in first person
        CombatType = 0; // FPS
    }
    else
    {
        CombatType = 1; // TPS
    }

    ImColor FOVColor = IM_COL32(
        static_cast<int>(Options::Aimbot::FOVColor[0] * 255.f),
        static_cast<int>(Options::Aimbot::FOVColor[1] * 255.f),
        static_cast<int>(Options::Aimbot::FOVColor[2] * 255.f),
        255);

    ImColor FOVFillColor = IM_COL32(
        static_cast<int>(Options::Aimbot::FOVFillColor[0] * 255.f),
        static_cast<int>(Options::Aimbot::FOVFillColor[1] * 255.f),
        static_cast<int>(Options::Aimbot::FOVFillColor[2] * 255.f),
        static_cast<int>(Options::Aimbot::FOVFillColor[3] * 255.f));

    ImVec2 fovCenter(static_cast<float>(p.x), static_cast<float>(p.y));
    if (Options::Aimbot::FOVPositionMode == 1)
    {
        RobloxPlayer fovTarget = Options::Aimbot::CurrentTarget;
        if (fovTarget.address == 0)
            fovTarget = GetClosestPlayer();

        if (fovTarget.address != 0)
        {
            const auto targetScreen = WorldToScreen(GetTargetPosition(fovTarget));
            if (targetScreen.x != -1.f && targetScreen.y != -1.f)
                fovCenter = ImVec2(targetScreen.x, targetScreen.y);
        }
    }

    if (Options::Aimbot::FOV && Options::Aimbot::ShowFOV)
    {
        const float time = static_cast<float>(ImGui::GetTime());
        const float radius = Options::Aimbot::FOV;
        const float thickness = Options::Aimbot::FOVThickness;

        // Breathing: modulate radius + a global alpha multiplier.
        float breathe = 1.0f;
        if (Options::Aimbot::FOVBreathing)
            breathe = 0.85f + 0.15f * sinf(time * 3.0f);
        const float r = radius * breathe;

        // Resolve the FOV line color for a given angle [0..1) along the shape.
        auto fovColorAt = [&](float t) -> ImU32
        {
            ImVec4 base(Options::Aimbot::FOVColor[0], Options::Aimbot::FOVColor[1], Options::Aimbot::FOVColor[2], 1.0f);
            switch (Options::Aimbot::FOVColorMode)
            {
            case 1: // gradient main -> accent2
            {
                float mix = t;
                mix = mix - floorf(mix);
                return IM_COL32(
                    static_cast<int>((base.x + (main_color2.x - base.x) * mix) * 255),
                    static_cast<int>((base.y + (main_color2.y - base.y) * mix) * 255),
                    static_cast<int>((base.z + (main_color2.z - base.z) * mix) * 255), 255);
            }
            case 2: // shift / rainbow
            {
                float h = fmodf(t + time * Options::Aimbot::FOVGradientSpeed * 0.15f, 1.0f);
                return IM_COL32(
                    static_cast<int>((0.5f + 0.5f * cosf((h + 0.0f) * 6.283f)) * 255),
                    static_cast<int>((0.5f + 0.5f * cosf((h + 0.33f) * 6.283f)) * 255),
                    static_cast<int>((0.5f + 0.5f * cosf((h + 0.67f) * 6.283f)) * 255), 255);
            }
            case 3: // pulse / breathing brightness
            {
                float p = 0.5f + 0.5f * sinf(time * Options::Aimbot::FOVGradientSpeed * 3.0f);
                return IM_COL32(
                    static_cast<int>(base.x * 255 * (0.4f + 0.6f * p)),
                    static_cast<int>(base.y * 255 * (0.4f + 0.6f * p)),
                    static_cast<int>(base.z * 255 * (0.4f + 0.6f * p)), 255);
            }
            default: return FOVColor;
            }
        };

        // Rotation used by FOV Spin (visible for square/triangle/hexagon; for the
        // circle the gradient/rainbow already animate the colour phase).
        const float spinRot = Options::Aimbot::FOVSpin
            ? (time * Options::Aimbot::FOVSpinSpeed * 1.5f)
            : 0.0f;

        // Glow pass: soft, additive-looking halo built from a few faint fills +
        // progressively thicker low-alpha outlines just outside the shape.
        if (Options::Aimbot::FOVGlow)
        {
            // Faint filled disc halo (smooth bloom).
            ImU32 haloCol = (FOVColor & 0x00FFFFFF) | (static_cast<ImU32>(28) << 24);
            DrawFOVShape(drawList, fovCenter, r + 9.0f, haloCol, Options::Aimbot::FOVShape, 1.0f,
                false, 0, fovColorAt, spinRot);

            for (int g = 4; g >= 1; g--)
            {
                ImU32 gc = (FOVColor & 0x00FFFFFF) | (static_cast<ImU32>(55 / g) << 24);
                DrawFOVShape(drawList, fovCenter, r + static_cast<float>(g) * 2.2f, gc,
                    Options::Aimbot::FOVShape, static_cast<float>(g) * 1.6f, false, 0, fovColorAt, spinRot);
            }
        }

        DrawFOVShape(drawList, fovCenter, r, FOVColor, Options::Aimbot::FOVShape, thickness,
            Options::Aimbot::ShowFOVFill, FOVFillColor, fovColorAt, spinRot);

        if (Options::Aimbot::ShowFOVText)
        {
            char buf[32];
            sprintf_s(buf, "%.0f", Options::Aimbot::FOV);
            drawList->AddText(ImVec2(fovCenter.x + 6.f, fovCenter.y - radius - 14.f), FOVColor, buf);
        }
    }

    // Toggle mode: detect key press edge (only trigger once per press)
    static bool wasKeyPressed = false;
    bool isKeyPressed = KeyBind::IsPressed(Options::Aimbot::AimbotKey);
    
    if (Options::Aimbot::ToggleType == 2)
    {
        // Always On: no key state required
        Options::Aimbot::Toggled = true;
    }
    else if (Options::Aimbot::ToggleType == 1)
    {
        // Toggle mode: only toggle on key press edge (not while held)
        if (isKeyPressed && !wasKeyPressed)
        {
            Options::Aimbot::Toggled = !Options::Aimbot::Toggled;
        }
        wasKeyPressed = isKeyPressed;
        
        // In toggle mode, check if toggled state is active
        if (!Options::Aimbot::Toggled)
        {
            Options::Aimbot::CurrentTarget = RobloxPlayer(0);
            return;
        }
    }
    else
    {
        // Hold mode: check if key is currently pressed
        if (!isKeyPressed)
        {
            Options::Aimbot::CurrentTarget = RobloxPlayer(0);
            Options::Aimbot::Toggled = false; // Reset toggle state when in hold mode
            return;
        }
    }

    // Stutter logic: skip aiming every X ticks
    static int stutterTickCounter = 0;
    if (Options::Aimbot::Stutter && Options::Aimbot::StutterTicks > 0)
    {
        stutterTickCounter++;
        if (stutterTickCounter >= Options::Aimbot::StutterTicks)
        {
            stutterTickCounter = 0;
            return; // Skip this tick
        }
    }
    else
    {
        stutterTickCounter = 0;
    }

    RobloxPlayer target;
    if (Options::Aimbot::StickyAim)
    {
        static auto lastSwitch = std::chrono::steady_clock::now();
        bool needNew = (Options::Aimbot::CurrentTarget.address == 0 ||
            Options::Aimbot::CurrentTarget.Health == 0 ||
            (Options::Aimbot::CurrentTarget.Health <= 1 && Options::Aimbot::DownedCheck) ||
            (Options::Aimbot::TeamCheck && IsTeammate(Options::Aimbot::CurrentTarget)));

        if (!needNew && localHRP.address)
        {
            // Check if current target is still within range
            auto targetPos = GetTargetPosition(Options::Aimbot::CurrentTarget);
            Vectors::Vector3 diff = targetPos - localHRP.Position();
            if (diff.Magnitude() > Options::Aimbot::Range)
                needNew = true;
        }

        // Honour the target-switch delay so the lock doesn't snap between
        // enemies every frame.
        auto now = std::chrono::steady_clock::now();
        float sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSwitch).count();
        if (needNew && (Options::Aimbot::TargetSwitchDelay <= 0.f || sinceMs >= Options::Aimbot::TargetSwitchDelay))
        {
            Options::Aimbot::CurrentTarget = GetClosestPlayer();
            lastSwitch = now;
        }

        target = Options::Aimbot::CurrentTarget;
    }
    else
    {
        target = GetClosestPlayer();
    }

    // Populate Aim Info state for the current target (if any).
    g_AimInfo.valid = (target.address != 0);
    if (target.address != 0)
    {
        g_AimInfo.name = target.Name;
        g_AimInfo.part = AimInfoPartName();
        auto hrp = target.HumanoidRootPart.address ? target.HumanoidRootPart : target.Torso;
        if (hrp.address && localHRP.address)
            g_AimInfo.distance = hrp.Position().Distance(localHRP.Position());
        else
            g_AimInfo.distance = 0.f;
        float hp = 0.f, maxhp = 0.f;
        GetPlayerHealth(target, hp, maxhp);
        g_AimInfo.health = hp;
        g_AimInfo.maxHealth = maxhp;
    }
    else
    {
        g_AimInfo.name.clear();
        g_AimInfo.part.clear();
    }

    auto sensitivity = Memory->read<float>(Memory->getBaseAddress() + Offsets::MouseService::SensitivityPointer);

    if (target.address != 0)
    {
        auto targetPos = WorldToScreen(GetTargetPosition(target));

        if (targetPos.x != -1 && targetPos.y != -1)
        {
            if (Globals::Roblox::isPhantomForces)
            {
                ResetViewport(Dimensions);
                PFSilentAim(target);
            }
            else if (Globals::Roblox::isOverkill)
            {
                if (ShouldUseSilentAim())
                {
                    // Chickynoid ignores the MousePosition memory write, so the
                    // in-game bullet keeps firing from the real crosshair and misses.
                    // Move the REAL cursor onto the target while firing so the shot
                    // actually originates from the enemy and connects.
                    ViewportSilentAim(targetPos, Dimensions);

                    if (Options::Aimbot::SilentAimMode == 1)
                        SpoofMousePosition(targetPos);

                    if (IsLMBDown())
                    {
                        if (Options::Aimbot::SilentAimRealCursor)
                            MouseInstant(targetPos);
                        else if (Options::Aimbot::SilentAimTeleport)
                            TeleportSilentAim(target);
                    }
                }
                else
                {
                    ResetViewport(Dimensions);
                    switch (Options::Aimbot::AimingType)
                    {
                    case 0:
                        CameraRotation(target);
                        break;
                    case 1:
                        MouseSendInput(targetPos, p, sensitivity);
                        break;
                    case 2:
                        CameraRotation(target);
                        break;
                    default:
                        break;
                    }
                }
            }
            else
            {
                switch (CombatType)
                {
                case 0:
                {
                    if (ShouldUseSilentAim())
                    {
                        ViewportSilentAim(targetPos, Dimensions);
                        if (Options::Aimbot::SilentAimMode == 1)
                            SpoofMousePosition(targetPos);
                    }
                    else
                    {
                        ResetViewport(Dimensions);
                        switch (Options::Aimbot::AimingType)
                        {
                        case 0:
                            CameraRotation(target);
                            break;
                        case 1:
                            MouseSendInput(targetPos, p, sensitivity);
                            break;
                        case 2:
                            CameraRotation(target);
                            if (Options::Aimbot::SilentAimMode == 1)
                                SpoofMousePosition(targetPos);
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                }

                case 1:
                {
                    if (ShouldUseSilentAim())
                    {
                        ViewportSilentAim(targetPos, Dimensions);
                        if (Options::Aimbot::SilentAimMode == 1)
                            SpoofMousePosition(targetPos);
                    }
                    else
                    {
                        ResetViewport(Dimensions);
                        Mouse(targetPos, p);
                    }
                    break;
                }

                default:
                    ResetViewport(Dimensions);
                    break;
                }
            }
        }
        else
        {
            ResetViewport(Dimensions);
        }
    }
    else
    {
        ResetViewport(Dimensions);
    }

    // ── Target Line: draw line from cursor to locked target ─────────────────
    if (Options::Aimbot::TargetLine && target.address != 0)
    {
        auto tlTargetPos = WorldToScreen(GetTargetPosition(target));
        if (tlTargetPos.x != -1 && tlTargetPos.y != -1)
        {
            ImColor tlColor = IM_COL32(
                static_cast<int>(Options::Aimbot::TargetLineColor[0] * 255.f),
                static_cast<int>(Options::Aimbot::TargetLineColor[1] * 255.f),
                static_cast<int>(Options::Aimbot::TargetLineColor[2] * 255.f),
                255);
            drawList->AddLine(
                ImVec2(static_cast<float>(p.x), static_cast<float>(p.y)),
                ImVec2(tlTargetPos.x, tlTargetPos.y),
                tlColor, Options::Aimbot::TargetLineThickness);
        }
    }

    // ── Bullet Tracer: fire on LMB press / hold ────────────────────────────
    if (Options::Combat::BulletTracers && Globals::Roblox::Camera.address)
    {
        static bool  s_wasLMB        = false;
        static float s_tracerCooldown = 0.f;

        const bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        s_tracerCooldown -= ImGui::GetIO().DeltaTime;

        const bool spawnTracer = lmbDown && (!s_wasLMB || s_tracerCooldown <= 0.f);
        s_wasLMB = lmbDown;

        if (spawnTracer)
        {
            s_tracerCooldown = 0.08f;

            const Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
                Globals::Roblox::Camera.address + Offsets::Camera::Position);

            RobloxPlayer tracerTarget = Options::Aimbot::CurrentTarget;
            if (!tracerTarget.address)
                tracerTarget = GetClosestPlayer();

            if (tracerTarget.address)
            {
                CombatFeedback::RegisterShot(camPos, GetTargetPosition(tracerTarget), tracerTarget.Character.address);

                // Geometry-based hit confirmation. For silent aim the shot
                // actually resolves onto the target, so use its position.
                // Otherwise project the camera look vector out to the target
                // distance and confirm against enemy body parts.
                Vectors::Vector3 hitPoint;
                if (ShouldUseSilentAim())
                {
                    hitPoint = GetTargetPosition(tracerTarget);
                }
                else
                {
                    sCFrame camCFrame = Globals::Roblox::Camera.CFrame();
                    Vectors::Vector3 lookVec = camCFrame.GetLookVector();
                    float dist = camPos.Distance(GetTargetPosition(tracerTarget));
                    hitPoint = camPos + lookVec * dist;
                }
                CombatFeedback::RegisterShotHit(hitPoint);
            }
            else if (Options::Combat::BulletTracersAlways)
            {
                sCFrame camCFrame = Globals::Roblox::Camera.CFrame();
                Vectors::Vector3 lookVec = camCFrame.GetLookVector();
                Vectors::Vector3 endPos = camPos + lookVec * 500.0f;
                CombatFeedback::RegisterShot(camPos, endPos);
            }
        }
    }
}



