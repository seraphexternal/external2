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

        RobloxInstance targetPart(0);
        if (active)
        {
            const std::string targetName = Options::ESP::AimViewTarget;
            for (const auto& player : Globals::Caches::FramePlayers())
            {
                if (player.Name == targetName && player.HumanoidRootPart.address)
                {
                    if (player.address == Globals::Roblox::LocalPlayer.address)
                        break;
                    targetPart = player.HumanoidRootPart;
                    break;
                }
            }

            if (!targetPart.address)
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

            uintptr_t hrpPrim = Memory->read<uintptr_t>(targetPart.address + Offsets::BasePart::Primitive);
            if (!hrpPrim)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const Vectors::Vector3 hrpPos = Memory->read<Vectors::Vector3>(hrpPrim + Offsets::Primitive::Position);
            const Matrixes::Matrix3x3 hrpRot = Memory->read<Matrixes::Matrix3x3>(hrpPrim + Offsets::Primitive::Rotation);

            float fwdX = -hrpRot.r02;
            float fwdZ = -hrpRot.r22;
            float fwdLen = sqrtf(fwdX * fwdX + fwdZ * fwdZ);
            if (fwdLen < 0.001f)
            {
                fwdX = 0.f;
                fwdZ = 1.f;
                fwdLen = 1.f;
            }
            fwdX /= fwdLen;
            fwdZ /= fwdLen;

            Vectors::Vector3 yawForward{ fwdX, 0.f, fwdZ };
            Vectors::Vector3 yawRight{ fwdZ, 0.f, -fwdX };

            Vectors::Vector3 pos = hrpPos - yawForward * 14.f + Vectors::Vector3{ 0.f, 4.f, 0.f };
            Vectors::Vector3 lookAtPos{ hrpPos.x, hrpPos.y + 2.f, hrpPos.z };

            const float lookDy = lookAtPos.y - pos.y;
            const float dx = pos.x - lookAtPos.x;
            const float dz = pos.z - lookAtPos.z;
            float horiz = sqrtf(dx * dx + dz * dz);
            if (horiz < 0.001f) horiz = 0.001f;
            float pitchLen = sqrtf(horiz * horiz + lookDy * lookDy);
            if (pitchLen < 0.001f) pitchLen = 0.001f;

            Vectors::Vector3 fwdV{
                yawForward.x * (horiz / pitchLen),
                lookDy / pitchLen,
                yawForward.z * (horiz / pitchLen)
            };
            Vectors::Vector3 rV = yawRight;
            Vectors::Vector3 uV = fwdV.cross(rV);

            Matrixes::Matrix3x3 rot;
            rot.r00 = -rV.x;  rot.r01 = uV.x;  rot.r02 = -fwdV.x;
            rot.r10 =  rV.y;  rot.r11 = uV.y;  rot.r12 = -fwdV.y;
            rot.r20 = -rV.z;  rot.r21 = uV.z;  rot.r22 = -fwdV.z;

            // Hammer continuously (~14ms) like the third-person loop to hold
            // the camera without flicker across the inter-frame gap.
            // Same ~14ms coverage window, but batches back off 1ms instead of
            // spinning the driver — the camera script only writes once per
            // frame, so a 1ms re-assert beats it without ~1M IOCTLs/sec.
            auto writeStart = std::chrono::high_resolution_clock::now();
            do {
                Memory->write<int>(cachedCam + Offsets::Camera::CameraType, 7);
                Memory->write<Vectors::Vector3>(cachedCam + Offsets::Camera::Position, pos);
                Memory->write<Matrixes::Matrix3x3>(cachedCam + Offsets::Camera::Rotation, rot);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
