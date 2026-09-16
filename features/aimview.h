#pragma once

#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/utils/W2S.h"
#include "imgui/imgui.h"

#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// Aim View (spectate-follow): when active with a target player, the camera
// follows that one player's HumanoidRootPart (mirror of the third-person loop,
// but the tracked part is the AIMVIEWED player's body instead of the local
// character's). Runs in its own dedicated thread so the camera continuously
// holds without flicker, exactly like the third-person loop.
// ---------------------------------------------------------------------------
inline void AimViewLoop()
{
    uintptr_t cachedCam = 0;
    bool wasActive = false;
    auto lastTick = std::chrono::high_resolution_clock::now();

    while (Globals::running)
    {
        auto nowTick = std::chrono::high_resolution_clock::now();
        float dtSec = std::chrono::duration<float>(nowTick - lastTick).count();
        lastTick = nowTick;

        bool active = Options::ESP::AimView
            && !Options::ESP::AimViewTarget.empty()
            && Globals::Roblox::Camera.address
            && Globals::Roblox::Workspace.address;

        uintptr_t targetHRP = 0;
        if (active)
        {
            const std::string targetName = Options::ESP::AimViewTarget;
            for (const auto& player : Globals::Caches::CachedPlayerObjects)
            {
                if (player.Name == targetName && player.HumanoidRootPart.address)
                {
                    if (player.address == Globals::Roblox::LocalPlayer.address)
                        break;
                    targetHRP = player.HumanoidRootPart.address;
                    break;
                }
            }

            if (!targetHRP)
                active = false;
        }

        if (active)
        {
            if (!cachedCam)
                cachedCam = Globals::Roblox::Camera.address;
            if (!cachedCam)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            wasActive = true;

            Vectors::Vector3 hrpPos = Memory->read<Vectors::Vector3>(targetHRP + Offsets::Misc::Value);

            const Vectors::Vector3 presetOffset{ 0.f, 0.5f, 0.f };
            const float lookHeight = 2.5f; // look slightly above the HRP (head)

            Vectors::Vector3 pos{
                hrpPos.x + presetOffset.x,
                hrpPos.y + presetOffset.y,
                hrpPos.z + presetOffset.z
            };
            Vectors::Vector3 lookAtPos = { hrpPos.x, hrpPos.y + lookHeight, hrpPos.z };

            Vectors::Vector3 fwd = { lookAtPos.x - pos.x, lookAtPos.y - pos.y, lookAtPos.z - pos.z };
            float mag = fwd.Magnitude();
            if (mag > 0.001f)
                fwd = { fwd.x / mag, fwd.y / mag, fwd.z / mag };
            else
                fwd = { 0.f, 0.f, 1.f };

            Vectors::Vector3 yawF{ fwd.x, 0.f, fwd.z };
            float yLen = yawF.Magnitude();
            if (yLen > 0.001f) yawF = { yawF.x / yLen, 0.f, yawF.z / yLen };
            else yawF = { 0.f, 0.f, 1.f };

            Vectors::Vector3 rV = Vectors::Vector3{0.f, 1.f, 0.f}.cross(yawF);
            float rMag = rV.Magnitude();
            if (rMag > 0.001f) rV = { rV.x / rMag, rV.y / rMag, rV.z / rMag };
            else rV = { 1.f, 0.f, 0.f };

            Vectors::Vector3 uV = yawF.cross(rV);
            Matrixes::Matrix3x3 rot{
                -rV.x,  uV.x, -yawF.x,
                -rV.y,  uV.y, -yawF.y,
                -rV.z,  uV.z, -yawF.z
            };

            // Hammer continuously (~14ms) like the third-person loop to hold
            // the camera without flicker across the inter-frame gap.
            auto writeStart = std::chrono::high_resolution_clock::now();
            do {
                Memory->write<int>(cachedCam + Offsets::Camera::CameraType, 7);
                Memory->write<Vectors::Vector3>(cachedCam + Offsets::Camera::Position, pos);
                Memory->write<Matrixes::Matrix3x3>(cachedCam + Offsets::Camera::Rotation, rot);
            } while (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - writeStart).count() < 14000);
        }
        else if (wasActive)
        {
            // Feature just turned off — restore Roblox camera control.
            if (cachedCam)
                Memory->write<int>(cachedCam + Offsets::Camera::CameraType, 0);
            cachedCam = 0;
            wasActive = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
