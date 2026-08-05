#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include <vector>

namespace SilentAim
{
    inline bool s_Active = false;
    inline uintptr_t s_TargetAddress = 0;
    inline Vectors::Vector3 s_TargetPosition = {0,0,0};
    inline int s_TargetBoneIndex = 0;

    inline RobloxInstance GetTargetPart(const RobloxPlayer& player, int boneIndex)
    {
        // Try primary part first, then fallbacks
        auto tryPart = [&](const char* name) -> RobloxInstance {
            auto part = player.Character.FindFirstChild(name);
            return part.address ? part : RobloxInstance(0);
        };

        // Try multiple possible part names for each bone index
        switch (boneIndex)
        {
            case 0: // Head - try multiple possible head part names
            {
                auto head = player.Head;
                if (head.address) return head;
                // Fallback: try finding by name
                auto headAlt = player.Character.FindFirstChild("Head");
                if (headAlt.address) return headAlt;
                return RobloxInstance(0);
            }
            case 1: // UpperTorso (R15) / Torso (R6)
            {
                if (player.RigType == 1)
                {
                    auto ut = player.Upper_Torso;
                    if (ut.address) return ut;
                    auto utAlt = player.Character.FindFirstChild("UpperTorso");
                    if (utAlt.address) return utAlt;
                }
                else
                {
                    auto torso = player.Torso;
                    if (torso.address) return torso;
                    auto torsoAlt = player.Character.FindFirstChild("Torso");
                    if (torsoAlt.address) return torsoAlt;
                }
                return RobloxInstance(0);
            }
            case 2: // LowerTorso (R15 only)
            {
                if (player.RigType == 1)
                {
                    auto lt = player.Lower_Torso;
                    if (lt.address) return lt;
                    auto ltAlt = player.Character.FindFirstChild("LowerTorso");
                    if (ltAlt.address) return ltAlt;
                }
                return RobloxInstance(0);
            }
            case 3: // HumanoidRootPart
            {
                auto hrp = player.HumanoidRootPart;
                if (hrp.address) return hrp;
                auto hrpAlt = player.Character.FindFirstChild("HumanoidRootPart");
                if (hrpAlt.address) return hrpAlt;
                return RobloxInstance(0);
            }
            default:
                return player.Head;
        }
    }

    inline bool IsTargetVisible(const RobloxPlayer& player, const Vectors::Vector3& targetPos)
    {
        if (!Globals::Roblox::Camera.address || !Globals::Viewport::Valid)
            return false;

        auto cameraPos = Globals::Roblox::Camera.Position();
        Vectors::Vector3 direction = targetPos - cameraPos;
        float distance = direction.Magnitude();
        if (distance == 0) return false;
        direction = direction / distance;

        return true;
    }

    inline void UpdateTarget()
    {
        s_Active = false;
        s_TargetAddress = 0;
        s_TargetPosition = {0,0,0};

        if (!Options::Aimbot::SilentAimEnabled)
            return;

        bool keyHeld = false;
        if (Options::Aimbot::SilentAimKey != 0)
        {
            keyHeld = (GetAsyncKeyState(Options::Aimbot::SilentAimKey) & 0x8000) != 0;
        }

        if (Options::Aimbot::SilentAimToggleType == 2)
        {
            Options::Aimbot::SilentAimToggled = true;
        }
        else if (Options::Aimbot::SilentAimToggleType == 1)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = keyHeld;
            if (isKeyPressed && !wasKeyPressed)
                Options::Aimbot::SilentAimToggled = !Options::Aimbot::SilentAimToggled;
            wasKeyPressed = isKeyPressed;
        }
        else
        {
            Options::Aimbot::SilentAimToggled = keyHeld;
        }

        if (!Options::Aimbot::SilentAimToggled)
            return;

        if (!Globals::Roblox::LocalPlayer.address || !Globals::Viewport::Valid)
            return;

        auto localCharacter = Globals::Roblox::LocalPlayer.Character();
        auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
        if (!localHRP.address) localHRP = localCharacter.FindFirstChild("Torso");
        if (!localHRP.address) localHRP = localCharacter.FindFirstChild("UpperTorso");

        Vectors::Vector3 localPos = {0,0,0};
        if (localHRP.address) localPos = localHRP.Position();

        auto cameraPos = Globals::Roblox::Camera.Position();

        float bestFOV = Options::Aimbot::SilentAimFOV;
        uintptr_t bestTarget = 0;
        Vectors::Vector3 bestPos = {0,0,0};

        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
                continue;

            if (player.Health <= 0)
                continue;

            if (Options::Aimbot::SilentAimTeamCheck && IsTeammate(player))
                continue;

            auto targetPart = player.Head;
            if (Options::Aimbot::SilentAimTargetBone == 1)
                targetPart = player.RigType == 1 ? player.Upper_Torso : player.Torso;
            else if (Options::Aimbot::SilentAimTargetBone == 2)
                targetPart = player.Lower_Torso;
            else if (Options::Aimbot::SilentAimTargetBone == 3)
                targetPart = player.HumanoidRootPart;

            if (!targetPart.address)
                continue;

            auto targetPos = targetPart.Position();
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
        
        if (Options::Aimbot::SilentAimPrediction)
        {
            for (const auto& player : Globals::Caches::CachedPlayerObjects)
            {
                if (player.address == s_TargetAddress)
                {
                    targetPos.x += player.Velocity.x * Options::Aimbot::SilentAimPredictionX;
                    targetPos.y += player.Velocity.y * Options::Aimbot::SilentAimPredictionY;
                    targetPos.z += player.Velocity.z * Options::Aimbot::SilentAimPredictionX;
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

        if (Options::Aimbot::SilentAimSmoothness > 0)
        {
            Matrixes::Matrix3x3 currentRot;
            currentRot.r00 = cameraCFrame.r00; currentRot.r01 = cameraCFrame.r01; currentRot.r02 = cameraCFrame.r02;
            currentRot.r10 = cameraCFrame.r10; currentRot.r11 = cameraCFrame.r11; currentRot.r12 = cameraCFrame.r12;
            currentRot.r20 = cameraCFrame.r20; currentRot.r21 = cameraCFrame.r21; currentRot.r22 = cameraCFrame.r22;

            float t = 1.0f - Options::Aimbot::SilentAimSmoothness * 0.01f;
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

        uintptr_t primitiveAddr = Memory->read<uintptr_t>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
        if (primitiveAddr)
        {
            Memory->write<Matrixes::Matrix3x3>(primitiveAddr, newRot);
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