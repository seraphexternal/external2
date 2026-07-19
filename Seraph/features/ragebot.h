#pragma once
#include <algorithm>
#include <cmath>
#include <chrono>
#include <windows.h>
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/KeyBind.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"
#include "visibility.h"

// Shared state for the Rage tab's "real you orbiting the target" visual.
namespace RageVisual
{
    inline bool hasGhost = false;
    inline Vectors::Vector3 ghostPos = { 0, 0, 0 };
    inline Vectors::Vector3 realPos = { 0, 0, 0 };
}

inline Vectors::Vector3 GetRagebotTargetPosition(const RobloxPlayer& player)
{
    const RobloxInstance* part;
    switch (Options::Ragebot::TargetBone)
    {
    case 0: part = &player.Head; break;
    case 1: part = &player.HumanoidRootPart; break;
    case 2: part = &player.Left_Arm; break;
    case 3: part = &player.Right_Arm; break;
    case 4: part = &player.Left_Leg; break;
    case 5: part = &player.Right_Leg; break;
    case 6: part = &player.Lower_Torso; break;
    case 7: part = &player.Upper_Torso; break;
    default: part = &player.Head; break;
    }

    Vectors::Vector3 pos = part->Position();

    if (Options::Ragebot::Prediction && part->address)
    {
        Vectors::Vector3 vel = Memory->read<Vectors::Vector3>(
            Memory->read<uintptr_t>(part->address + Offsets::BasePart::Primitive) +
            Offsets::Primitive::AssemblyLinearVelocity
        );
        pos.x += vel.x * Options::Ragebot::PredictionX;
        pos.y += vel.y * Options::Ragebot::PredictionY;
        pos.z += vel.z * Options::Ragebot::PredictionX;
    }

    return pos;
}

inline RobloxPlayer GetRagebotTarget()
{
    if (Globals::Roblox::Players.address != 0)
    {
        Globals::Roblox::LocalPlayer = RobloxInstance(
            Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer)
        );
    }

    RobloxPlayer target;
    float bestScore = FLT_MAX;
    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");

    POINT p;
    GetCursorPos(&p);

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        auto HRP = player.HumanoidRootPart;
        if (!HRP.address)
            continue;

        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (Options::Ragebot::TeamCheck && IsTeammate(player))
            continue;

        if (player.Health <= 0)
            continue;

        if (Options::Ragebot::DownedCheck && player.Health > 0 && player.Health <= 5.0f)
            continue;

        if (Options::Ragebot::WallCheck && Visibility::IsPlayerOccluded(player))
            continue;

        Vectors::Vector3 targetPos = GetRagebotTargetPosition(player);
        Vectors::Vector2 targetPos2D = WorldToScreen(targetPos);

        if (targetPos2D.x == -1 && targetPos2D.y == -1)
            continue;

        if (localHRP.address)
        {
            Vectors::Vector3 diff = localHRP.Position() - targetPos;
            float distance3D = diff.Magnitude();
            if (distance3D > Options::Ragebot::Range)
                continue;
        }

        float screenDist = targetPos2D.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });
        if (screenDist < bestScore && screenDist <= Options::Ragebot::FOV)
        {
            bestScore = screenDist;
            target = player;
        }
    }
    return target;
}

inline void RagebotAim(const RobloxPlayer& target)
{
    Vectors::Vector3 targetPos = GetRagebotTargetPosition(target);
    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Position
    );

    sCFrame lookAt = LookAt(camPos, targetPos);
    Matrixes::Matrix3x3 targetRot = {
        lookAt.r00, lookAt.r01, -lookAt.r02,
        lookAt.r10, lookAt.r11, -lookAt.r12,
        lookAt.r20, lookAt.r21, -lookAt.r22
    };

    if (Options::Ragebot::Smoothness <= 0.0f)
    {
        Memory->write<Matrixes::Matrix3x3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Rotation, targetRot
        );
        return;
    }

    Matrixes::Matrix3x3 currentRot = Memory->read<Matrixes::Matrix3x3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Rotation
    );

    Vectors::Vector4 currentQuat = Vectors::Vector4::FromMatrix(currentRot);
    Vectors::Vector4 targetQuat = Vectors::Vector4::FromMatrix(targetRot);
    float t = std::clamp(Options::Ragebot::Smoothness, 0.01f, 1.0f);
    Vectors::Vector4 smoothedQuat = Vectors::Vector4::Slerp(currentQuat, targetQuat, t);
    Matrixes::Matrix3x3 smoothed = smoothedQuat.ToMatrix();

    Memory->write<Matrixes::Matrix3x3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Rotation, smoothed
    );
}

inline void RagebotFire()
{
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
    Sleep(10);
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}

inline void RunRagebot()
{
    static std::chrono::steady_clock::time_point lastFireTime;
    static std::chrono::steady_clock::time_point lastToggleTime;

    if (!Options::Ragebot::Enabled)
    {
        Options::Ragebot::Toggled = false;
        return;
    }

    if (!Globals::Roblox::Camera.address)
        return;

    if (Globals::Roblox::Players.address != 0)
    {
        Globals::Roblox::LocalPlayer = RobloxInstance(
            Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer)
        );
    }

    if (!Globals::Roblox::LocalPlayer.address)
        return;

    bool keyActive = KeyBind::IsPressed(Options::Ragebot::RagebotKey);

    switch (Options::Ragebot::ToggleType)
    {
    case 0:
        if (!keyActive) return;
        break;
    case 1:
        if (keyActive)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastToggleTime).count() > 200)
            {
                Options::Ragebot::Toggled = !Options::Ragebot::Toggled;
                lastToggleTime = now;
            }
        }
        if (!Options::Ragebot::Toggled) return;
        break;
    case 2:
        break;
    }

    RobloxPlayer target = GetRagebotTarget();
    if (!target.address || target.Health <= 0)
        return;

    RagebotAim(target);

    if (Options::Ragebot::AutoFire)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFireTime).count();
        if (elapsed >= Options::Ragebot::FireRate)
        {
            RagebotFire();
            lastFireTime = now;
        }
    }
}

// Rage tab kill/orbit loop. The "real you" visually orbits the locked target
// (drawn as a ghost by RenderRageGhost) while the actual character is left free
// to walk around. While orbiting we keep the target damaged by briefly snapping
// the real character next to the target (teleport-to-hit) and auto-firing, then
// restoring it so the player can still move normally.
inline RobloxPlayer GetRageKillTarget()
{
    if (Options::Rage::TargetMode == 1)
    {
        std::string name(Options::Rage::TargetPlayer);
        if (!name.empty())
        {
            auto tp = FindPlayerByName(name);
            if (tp.address)
            {
                RobloxPlayer p;
                p.address = tp.address;
                p.Character = tp.Character();
                if (p.Character.address)
                {
                    p.HumanoidRootPart = p.Character.FindFirstChild("HumanoidRootPart");
                    if (p.HumanoidRootPart.address)
                        return p;
                }
            }
        }
        return RobloxPlayer();
    }

    RobloxPlayer best;
    float bestDist = FLT_MAX;
    auto localChar = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localChar.FindFirstChild("HumanoidRootPart");
    POINT p; GetCursorPos(&p);

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address) continue;
        if (Globals::Roblox::isOverkill && !player.Name.empty() &&
            player.Name == Globals::Roblox::LocalPlayer.Name()) continue;
        if (player.Health <= 0.f) continue;

        auto hrp = player.HumanoidRootPart;
        if (!hrp.address) continue;

        Vectors::Vector3 tp = hrp.Position();
        Vectors::Vector2 t2d = WorldToScreen(tp);
        if (t2d.x == -1 && t2d.y == -1) continue;

        // Prefer the aimed-at / closest-to-crosshair enemy
        float d = t2d.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });
        if (d < bestDist) { bestDist = d; best = player; }
    }
    return best;
}

inline void RageKillLoop()
{
    double angle = 0.0;
    static std::chrono::steady_clock::time_point lastFire;
    static std::chrono::steady_clock::time_point lastToggle;

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        // Keybind handling
        if (Options::Rage::RageKey != 0)
        {
            static bool wasKey = false;
            bool isKey = (GetAsyncKeyState(Options::Rage::RageKey) & 0x8000) != 0;
            if (Options::Rage::ToggleType == 0) Options::Rage::Toggled = isKey;
            else if (Options::Rage::ToggleType == 1)
            {
                if (isKey && !wasKey)
                {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastToggle).count() > 200)
                    {
                        Options::Rage::Toggled = !Options::Rage::Toggled;
                        lastToggle = now;
                    }
                }
            }
            wasKey = isKey;
        }

        bool active = false;
        if (Options::Rage::Enabled)
        {
            if (Options::Rage::ToggleType == 2) active = true;
            else if (Options::Rage::RageKey != 0) active = Options::Rage::Toggled;
            else active = true;
        }

        if (!active)
        {
            RageVisual::hasGhost = false;
            continue;
        }

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address) { RageVisual::hasGhost = false; continue; }

            auto localChar = localPlayer.Character();
            if (!localChar.address && g_ResolveCharacterFallback)
                localChar = g_ResolveCharacterFallback(localPlayer.address);
            if (!localChar.address) { RageVisual::hasGhost = false; continue; }

            auto localHrp = localChar.FindFirstChild("HumanoidRootPart");
            if (!localHrp.address) { RageVisual::hasGhost = false; continue; }

            uintptr_t localPrim = Memory->read<uintptr_t>(localHrp.address + Offsets::BasePart::Primitive);
            if (!localPrim) { RageVisual::hasGhost = false; continue; }

            RobloxPlayer target = GetRageKillTarget();
            if (!target.address || !target.HumanoidRootPart.address)
            {
                RageVisual::hasGhost = false;
                continue;
            }

            Vectors::Vector3 realPos = localHrp.Position();
            Vectors::Vector3 tgtPos = target.HumanoidRootPart.Position();

            // Orbit position around the target
            angle += 0.008 * Options::Rage::OrbitSpeed;
            float r = Options::Rage::OrbitRadius;
            Vectors::Vector3 ghost{
                tgtPos.x + static_cast<float>(std::sin(angle) * r),
                tgtPos.y,
                tgtPos.z + static_cast<float>(std::cos(angle) * r)
            };

            RageVisual::ghostPos = ghost;
            RageVisual::realPos = realPos;
            RageVisual::hasGhost = Options::Rage::ShowGhost;

            if (Options::Rage::KillOnOrbit)
            {
                // Briefly teleport the real character next to the target so the
                // hit registers, auto-fire, then restore so the player can walk.
                Vectors::Vector3 savedPos = realPos;
                Vectors::Vector3 savedVel = Memory->read<Vectors::Vector3>(
                    localPrim + Offsets::Primitive::AssemblyLinearVelocity);

                Vectors::Vector3 killPos{
                    tgtPos.x,
                    tgtPos.y,
                    tgtPos.z
                };

                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::Position, killPos);
                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity, { 0,0,0 });

                if (Options::Rage::AutoKillAim && Globals::Roblox::Camera.address)
                {
                    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
                        Globals::Roblox::Camera.address + Offsets::Camera::Position);
                    sCFrame lookAt = LookAt(camPos, tgtPos);
                    Matrixes::Matrix3x3 rot{ lookAt.r00, lookAt.r01, -lookAt.r02,
                                             lookAt.r10, lookAt.r11, -lookAt.r12,
                                             lookAt.r20, lookAt.r21, -lookAt.r22 };
                    Memory->write<Matrixes::Matrix3x3>(
                        Globals::Roblox::Camera.address + Offsets::Camera::Rotation, rot);
                }

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFire).count() >= Options::Ragebot::FireRate)
                {
                    RagebotFire();
                    lastFire = now;
                }

                // Restore after a short window so the player keeps moving freely
                std::this_thread::sleep_for(std::chrono::milliseconds(12));
                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::Position, savedPos);
                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity, savedVel);
            }
        }
        catch (...) { RageVisual::hasGhost = false; }
    }
}

inline void RenderRageGhost(ImDrawList* drawList)
{
    if (!Options::Rage::Enabled || !Options::Rage::ShowGhost || !RageVisual::hasGhost)
        return;

    auto g = WorldToScreen(RageVisual::ghostPos);
    auto r = WorldToScreen(RageVisual::realPos);
    if (g.x == -1.f || g.y == -1.f) return;

    const ImU32 col = IM_COL32(
        static_cast<int>(Options::Rage::GhostColor[0] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[1] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[2] * 255.f),
        static_cast<int>(Options::Rage::GhostAlpha * 255.f));

    float viewDist = RageVisual::realPos.Distance(RageVisual::ghostPos);
    if (viewDist < 1.f) viewDist = 200.f;
    const float scale = 450.f / fmaxf(viewDist, 1.f);
    const float s = fminf(fmaxf(scale, 0.3f), 3.0f);
    const float boxW = 12.f * s, boxH = 24.f * s;

    ImVec2 bbMin(g.x - boxW * 0.5f, g.y - boxH * 0.5f);
    ImVec2 bbMax(g.x + boxW * 0.5f, g.y + boxH * 0.5f);

    drawList->AddRectFilled(bbMin, bbMax, IM_COL32(
        static_cast<int>(Options::Rage::GhostColor[0] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[1] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[2] * 255.f),
        static_cast<int>(40.f * Options::Rage::GhostAlpha)), 4.0f);
    drawList->AddRect(bbMin, bbMax, col, 4.0f, 0, 2.0f);

    const float hr = boxW * 0.35f;
    ImVec2 head(g.x, bbMin.y - hr - 2.f);
    drawList->AddCircle(head, hr, col, 16, 1.5f);
    drawList->AddCircleFilled(head, hr * 0.4f, col, 12);

    if (Options::Rage::ShowGhostLine && r.x != -1.f && r.y != -1.f)
    {
        drawList->AddLine(ImVec2(r.x, r.y), ImVec2(g.x, g.y), col, 2.0f);
    }
}
