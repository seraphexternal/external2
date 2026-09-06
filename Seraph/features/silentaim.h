#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/KeyBind.h"
#include <vector>

// Forward declaration - defined later in aimbot.h
inline void MouseSendInput(const Vectors::Vector2& targetPos, const POINT& currentPos, float sensitivity);

namespace SilentAim
{
    inline bool s_Active = false;
    inline uintptr_t s_TargetAddress = 0;
    inline Vectors::Vector3 s_TargetPosition = {0,0,0};
    inline int s_TargetBoneIndex = 0;

    inline RobloxInstance GetTargetPart(const RobloxPlayer& player, int boneIndex)
    {
        switch (boneIndex)
        {
            case 0: // Head
                return player.Head.address ? player.Head : player.Character.FindFirstChild("Head");
            case 1: // HumanoidRootPart
                return player.HumanoidRootPart.address ? player.HumanoidRootPart : player.Character.FindFirstChild("HumanoidRootPart");
            case 2: // Left Arm / Left Hand (R15)
                if (player.RigType == 0)
                    return player.Left_Arm.address ? player.Left_Arm : player.Character.FindFirstChild("Left Arm");
                return player.Left_Hand.address ? player.Left_Hand : player.Character.FindFirstChild("LeftHand");
            case 3: // Right Arm / Right Hand (R15)
                if (player.RigType == 0)
                    return player.Right_Arm.address ? player.Right_Arm : player.Character.FindFirstChild("Right Arm");
                return player.Right_Hand.address ? player.Right_Hand : player.Character.FindFirstChild("RightHand");
            case 4: // Left Leg / Left Foot (R15)
                if (player.RigType == 0)
                    return player.Left_Leg.address ? player.Left_Leg : player.Character.FindFirstChild("Left Leg");
                return player.Left_Foot.address ? player.Left_Foot : player.Character.FindFirstChild("LeftFoot");
            case 5: // Right Leg / Right Foot (R15)
                if (player.RigType == 0)
                    return player.Right_Leg.address ? player.Right_Leg : player.Character.FindFirstChild("Right Leg");
                return player.Right_Foot.address ? player.Right_Foot : player.Character.FindFirstChild("RightFoot");
            case 6: // UpperTorso
                if (player.RigType == 1)
                    return player.Upper_Torso.address ? player.Upper_Torso : player.Character.FindFirstChild("UpperTorso");
                return player.Torso.address ? player.Torso : player.Character.FindFirstChild("Torso");
            case 7: // LowerTorso (R15 only)
                if (player.RigType == 1)
                    return player.Lower_Torso.address ? player.Lower_Torso : player.Character.FindFirstChild("LowerTorso");
                return player.Torso.address ? player.Torso : player.Character.FindFirstChild("Torso");
            default:
                return player.Head;
        }
    }

    inline int GetEffectiveTargetBone()
    {
        // Priority: weapon profile TargetBone > SilentAimTargetBone > main aimbot TargetBone
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::TargetBone;
        return Options::Aimbot::SilentAimTargetBone;
    }

    inline Vectors::Vector3 GetTargetPositionFor(const RobloxPlayer& player)
    {
        // "Closest Part" overrides the fixed bone and aims at the body part nearest cursor
        if (Options::Aimbot::ClosestPart)
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            Vectors::Vector2 cur{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

            RobloxInstance parts[] = {
                player.Head, player.HumanoidRootPart, player.Upper_Torso, player.Lower_Torso, player.Torso,
                player.Left_Arm, player.Right_Arm, player.Left_Leg, player.Right_Leg,
                player.Left_Hand, player.Right_Hand, player.Left_Foot, player.Right_Foot,
                player.Left_Upper_Arm, player.Left_Lower_Arm, player.Right_Upper_Arm, player.Right_Lower_Arm,
                player.Left_Upper_Leg, player.Left_Lower_Leg, player.Right_Upper_Leg, player.Right_Lower_Leg,
            };

            float bestDist = FLT_MAX;
            Vectors::Vector3 bestPos = player.Head.Position();
            for (auto& part : parts)
            {
                if (!part.address) continue;
                Vectors::Vector3 worldPos = part.Position();
                Vectors::Vector2 screenPos = WorldToScreen(worldPos);
                if (screenPos.x < 0 || screenPos.y < 0) continue;
                float dx = screenPos.x - cur.x;
                float dy = screenPos.y - cur.y;
                float dist = dx*dx + dy*dy;
                if (dist < bestDist) { bestDist = dist; bestPos = worldPos; }
            }
            return bestPos;
        }

        int targetBone = GetEffectiveTargetBone();
        auto targetPart = GetTargetPart(player, targetBone);
        if (targetPart.address)
            return targetPart.Position();
        return player.Head.Position();
    }

    inline bool IsSilentAimActive()
    {
        // Check weapon profile silent aim
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::SilentAim;

        // Check standalone silent aim
        return Options::Aimbot::SilentAimEnabled && Options::Aimbot::SilentAimToggled;
    }

    inline float GetEffectiveSilentAimFOV()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::FOV;
        return Options::Aimbot::SilentAimFOV;
    }

    inline float GetEffectiveSilentAimSmoothness()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::Smoothness;
        return Options::Aimbot::SilentAimSmoothness;
    }

    inline bool GetEffectiveSilentAimPrediction()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::Prediction;
        return Options::Aimbot::SilentAimPrediction;
    }

    inline float GetEffectiveSilentAimPredictionX()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::PredictionX;
        return Options::Aimbot::SilentAimPredictionX;
    }

    inline float GetEffectiveSilentAimPredictionY()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::PredictionY;
        return Options::Aimbot::SilentAimPredictionY;
    }

    inline int GetEffectiveSilentAimMethod()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
        {
            // Map weapon profile SilentAimMode (0=camera, 1=mouse spoof) to SilentAimMethod
            return Options::Aimbot::SilentAimMode == 1 ? 1 : 0;
        }
        return Options::Aimbot::SilentAimMethod;
    }

    inline bool GetEffectiveSilentAimTeamCheck()
    {
        if (Options::WeaponProfiles::ActiveProfile >= 0)
            return Options::Aimbot::TeamCheck;
        return Options::Aimbot::SilentAimTeamCheck;
    }

    inline void UpdateTarget()
    {
        s_Active = false;
        s_TargetAddress = 0;
        s_TargetPosition = {0,0,0};

        if (!IsSilentAimActive())
            return;

        if (!Globals::Roblox::LocalPlayer.address || !Globals::Viewport::Valid)
            return;

        auto localCharacter = Globals::Roblox::LocalPlayer.Character();
        auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
        if (!localHRP.address) localHRP = localCharacter.FindFirstChild("Torso");
        if (!localHRP.address) localHRP = localCharacter.FindFirstChild("UpperTorso");

        Vectors::Vector3 localPos = {0,0,0};
        if (localHRP.address) localPos = localHRP.Position();

        float bestFOV = GetEffectiveSilentAimFOV();
        uintptr_t bestTarget = 0;
        Vectors::Vector3 bestPos = {0,0,0};
        int targetBone = GetEffectiveTargetBone();

        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
                continue;

            if (player.Health <= 0)
                continue;

            if (GetEffectiveSilentAimTeamCheck() && IsTeammate(player))
                continue;

            if (Options::Rivals::AntiKatana && IsHoldingKatana(player))
                continue;

            if (Options::Aimbot::WallCheck && Visibility::IsPlayerOccluded(player))
                continue;

            if (localHRP.address)
            {
                Vectors::Vector3 diff = localPos - player.HumanoidRootPart.Position();
                float distance3D = diff.Magnitude();
                if (distance3D > Options::Aimbot::Range)
                    continue;
            }

            auto targetPos = GetTargetPositionFor(player);
            auto screenPos = WorldToScreen(targetPos);
            
            if (screenPos.x < 0 || screenPos.y < 0)
                continue;

            float centerX = Globals::Viewport::Dimensions.x * 0.5f;
            float centerY = Globals::Viewport::Dimensions.y * 0.5f;
            float dx = screenPos.x - centerX;
            float dy = screenPos.y - centerY;
            float fov = sqrtf(dx*dx + dy*dy);

            if (fov < bestFOV)
            {
                bestFOV = fov;
                bestTarget = player.address;
                bestPos = targetPos;
                s_TargetBoneIndex = targetBone;
            }
        }

        if (bestTarget != 0)
        {
            s_Active = true;
            s_TargetAddress = bestTarget;
            s_TargetPosition = bestPos;
        }
    }

    inline void ApplySilentAim()
    {
        if (!s_Active || !Globals::Roblox::Camera.address || !Globals::Viewport::Valid)
            return;

        auto cameraCFrame = Globals::Roblox::Camera.CFrame();
        Vectors::Vector3 cameraPos = cameraCFrame.Position();
        
        Vectors::Vector3 targetPos = s_TargetPosition;
        
        if (GetEffectiveSilentAimPrediction())
        {
            for (const auto& player : Globals::Caches::CachedPlayerObjects)
            {
                if (player.address == s_TargetAddress)
                {
                    targetPos.x += player.Velocity.x * GetEffectiveSilentAimPredictionX();
                    targetPos.y += player.Velocity.y * GetEffectiveSilentAimPredictionY();
                    targetPos.z += player.Velocity.z * GetEffectiveSilentAimPredictionX();
                    break;
                }
            }
        }

        Vectors::Vector3 delta = targetPos - cameraPos;
        float distance = delta.Magnitude();
        if (distance == 0) return;

        Vectors::Vector3 direction = delta / distance;

        Vectors::Vector3 right = direction.cross({0, 1, 0}).Normalize();
        Vectors::Vector3 up = right.cross(direction).Normalize();

        Matrixes::Matrix3x3 newRot;
        newRot.r00 = right.x; newRot.r01 = up.x; newRot.r02 = -direction.x;
        newRot.r10 = right.y; newRot.r11 = up.y; newRot.r12 = -direction.y;
        newRot.r20 = right.z; newRot.r21 = up.z; newRot.r22 = -direction.z;

        float smoothness = GetEffectiveSilentAimSmoothness();
        
        if (smoothness > 0)
        {
            Matrixes::Matrix3x3 currentRot;
            currentRot.r00 = cameraCFrame.r00; currentRot.r01 = cameraCFrame.r01; currentRot.r02 = cameraCFrame.r02;
            currentRot.r10 = cameraCFrame.r10; currentRot.r11 = cameraCFrame.r11; currentRot.r12 = cameraCFrame.r12;
            currentRot.r20 = cameraCFrame.r20; currentRot.r21 = cameraCFrame.r21; currentRot.r22 = cameraCFrame.r22;

            float t = 1.0f - smoothness * 0.01f;
            if (t < 0) t = 0;
            if (t > 1) t = 1;

            newRot.r00 = currentRot.r00 + (newRot.r00 - currentRot.r00) * t;
            newRot.r01 = currentRot.r01 + (newRot.r01 - currentRot.r01) * t;
            newRot.r02 = currentRot.r02 + (newRot.r02 - currentRot.r02) * t;
            newRot.r10 = currentRot.r10 + (newRot.r10 - currentRot.r10) * t;
            newRot.r11 = currentRot.r11 + (newRot.r11 - currentRot.r11) * t;
            newRot.r12 = currentRot.r12 + (newRot.r12 - currentRot.r12) * t;
            newRot.r20 = currentRot.r20 + (newRot.r20 - currentRot.r20) * t;
            newRot.r21 = currentRot.r21 + (newRot.r21 - currentRot.r21) * t;
            newRot.r22 = currentRot.r22 + (newRot.r22 - currentRot.r22) * t;
        }

        int method = GetEffectiveSilentAimMethod();
        
        if (method == 0 || method == 2)
        {
            uintptr_t primitiveAddr = Memory->read<uintptr_t>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
            if (primitiveAddr)
            {
                Memory->write<Matrixes::Matrix3x3>(primitiveAddr, newRot);
            }
        }

        if ((method == 1 || method == 2) && Options::Aimbot::SilentAimRealCursor2)
        {
            auto screenPos = WorldToScreen(targetPos);
            if (screenPos.x >= 0 && screenPos.y >= 0)
            {
                POINT cur;
                GetCursorPos(&cur);
                Vectors::Vector2 target2D(screenPos.x, screenPos.y);
                MouseSendInput(target2D, cur, 1.0f);
            }
        }
    }

    inline void RunSilentAim()
    {
        UpdateTarget();
        if (s_Active)
        {
            ApplySilentAim();
        }
    }
}