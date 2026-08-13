#pragma once

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"

#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

// ── Third Person: force the camera behind the local player using only the HRP's ─
// yaw (horizontal look direction). Pitch and roll are intentionally ignored so
// the camera stays stable and the side offset never causes spinning.
//
// Runs in its own dedicated thread (like FreeCam) so it continuously holds the
// camera without gaps, preventing the flicker that comes from sleeping inside
// a shared tick thread. Ported from the reference implementation.
inline void ThirdPersonLoop()
{
    uintptr_t cachedCam = 0;
    bool wasActive = false;

    // Cinematic mode smoothing state.
    Vectors::Vector3 smoothCamPos{ 0.f, 0.f, 0.f };
    Vectors::Vector3 smoothLookPos{ 0.f, 0.f, 0.f };
    bool smoothInit = false;

    auto lastTick = std::chrono::high_resolution_clock::now();

    while (Globals::running)
    {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;

        // ── Keybind / toggle handling ───────────────────────────────────────
        if (Options::ThirdPerson::Key != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::ThirdPerson::Key) & 0x8000) != 0;
            if (Options::ThirdPerson::ToggleType == 2)
                Options::ThirdPerson::Toggled = true;
            else if (Options::ThirdPerson::ToggleType == 1)
            {
                if (isKeyPressed && !wasKeyPressed)
                    Options::ThirdPerson::Toggled = !Options::ThirdPerson::Toggled;
                wasKeyPressed = isKeyPressed;
            }
            else
                Options::ThirdPerson::Toggled = isKeyPressed;
        }
        else if (Options::ThirdPerson::ToggleType == 2)
            Options::ThirdPerson::Toggled = true;

        bool active = Options::Misc::ThirdPerson
            && (Options::ThirdPerson::Key == 0 || Options::ThirdPerson::Toggled)
            && !Globals::freecamOwnsCamera.load()   // don't fight freecam
            && Globals::Roblox::Workspace.address
            && Globals::Roblox::LocalPlayer.address;

        if (active)
        {
            if (!cachedCam)
                cachedCam = Globals::Roblox::Camera.address;
            if (!cachedCam)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto character = Globals::Roblox::LocalPlayer.Character();
            auto hrp = character.FindFirstChild("HumanoidRootPart");
            if (!hrp.address)
            {
                if (wasActive)
                {
                    Memory->write<int>(cachedCam + Offsets::Camera::CameraType, 0);
                    wasActive = false;
                    smoothInit = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            wasActive = true;

            // ── Cinematic Mode: track head/neck instead of HRP ──────────────
            sCFrame trackCF = hrp.CFrame();
            Vectors::Vector3 trackPos = trackCF.Position();
            if (Options::ThirdPerson::CinematicMode && Options::ThirdPerson::UseHeadTracking)
            {
                auto head = character.FindFirstChild("Head");
                if (head.address)
                    trackPos = head.Position();
                else
                {
                    auto upperTorso = character.FindFirstChild("UpperTorso");
                    if (upperTorso.address)
                        trackPos = upperTorso.Position();
                }
            }

            // Extract the forward vector from the rotation matrix, keeping only
            // the horizontal (XZ) component so camera distance / side offset
            // never cause the view to spin when the character looks up/down.
            float fwdX, fwdZ;
            if (Options::ThirdPerson::CinematicMode && Options::ThirdPerson::PortraitMode)
            {
                // Portrait: camera looks AT the character (reverse forward vector).
                fwdX = trackCF.r02;
                fwdZ = trackCF.r22;
            }
            else
            {
                fwdX = -trackCF.r02;
                fwdZ = -trackCF.r22;
            }
            float fwdLen = sqrtf(fwdX * fwdX + fwdZ * fwdZ);
            if (fwdLen < 0.001f) fwdLen = 0.001f;
            fwdX /= fwdLen;
            fwdZ /= fwdLen;

            // Yaw-only axes (world up = (0,1,0) is always stable).
            Vectors::Vector3 yawForward{ fwdX, 0.f, fwdZ };
            Vectors::Vector3 yawRight{ fwdZ, 0.f, -fwdX };

            // Camera position: start at the tracking point, step back along
            // yaw_forward, lift by offset_up.
            Vectors::Vector3 targetCamPos = trackPos
                + yawForward * Options::ThirdPerson::OffsetForward
                + Vectors::Vector3(0.f, 1.f, 0.f) * Options::ThirdPerson::OffsetUp;
            Vectors::Vector3 targetLookPos = trackPos;
            targetLookPos.y += Options::ThirdPerson::LookHeight;

            // ── Cinematic Mode: smooth camera movement with easing ──────────
            Vectors::Vector3 camPos, lookAtPos;
            if (Options::ThirdPerson::CinematicMode && Options::ThirdPerson::CinematicSmoothing > 0.001f)
            {
                if (!smoothInit)
                {
                    smoothCamPos = targetCamPos;
                    smoothLookPos = targetLookPos;
                    smoothInit = true;
                }

                Vectors::Vector3 dCam = targetCamPos - smoothCamPos;
                float dist = dCam.Magnitude();
                float baseT = std::clamp(1.0f - powf(Options::ThirdPerson::CinematicSmoothing, dt * 60.f), 0.f, 1.f);
                float easedT = 1.0f - powf(1.0f - baseT, 3.0f);
                float distFactor = std::clamp(dist / 5.0f, 0.1f, 1.0f);
                easedT *= distFactor;

                smoothCamPos = smoothCamPos + dCam * easedT;
                smoothLookPos = smoothLookPos + (targetLookPos - smoothLookPos) * easedT;
                camPos = smoothCamPos;
                lookAtPos = smoothLookPos;
            }
            else
            {
                camPos = targetCamPos;
                lookAtPos = targetLookPos;
                smoothInit = false;
            }

            // ── Build the camera's look direction (pitch toward the target) ──
            float lookDy = lookAtPos.y - camPos.y;
            float horiz = sqrtf(
                (camPos.x - lookAtPos.x) * (camPos.x - lookAtPos.x) +
                (camPos.z - lookAtPos.z) * (camPos.z - lookAtPos.z));
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

            // Apply the side offset (ignored in cinematic mode for framing).
            float sideOffset = Options::ThirdPerson::CinematicMode ? 0.f : Options::ThirdPerson::OffsetRight;
            if (sideOffset != 0.f)
            {
                float angle = sideOffset * 0.05f;
                float ca = cosf(angle), sa = sinf(angle);
                Vectors::Vector3 newFwd{
                    fwdV.x * ca - fwdV.z * sa,
                    fwdV.y,
                    fwdV.x * sa + fwdV.z * ca
                };
                fwdV = newFwd.Normalize();
                rV = Vectors::Vector3(0.f, 1.f, 0.f).cross(fwdV).Normalize();
                if (rV.x == 0.f && rV.y == 0.f && rV.z == 0.f)
                    rV = yawRight;
                uV = fwdV.cross(rV);
            }

            Matrixes::Matrix3x3 rot;
            rot.r00 = -rV.x;  rot.r01 = uV.x;  rot.r02 = -fwdV.x;
            rot.r10 =  rV.y;  rot.r11 = uV.y;  rot.r12 = -fwdV.y;
            rot.r20 = -rV.z;  rot.r21 = uV.z;  rot.r22 = -fwdV.z;

            // Hammer continuously for ~14ms — identical to FreeCam's inner
            // loop. Prevents the game's camera script from winning the race
            // during the inter-frame gap, eliminating flicker entirely.
            auto writeStart = std::chrono::high_resolution_clock::now();
            do {
                Memory->write<int>(cachedCam + Offsets::Camera::CameraType, 7);
                Memory->write<Vectors::Vector3>(cachedCam + Offsets::Camera::Position, camPos);
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
            smoothInit = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
