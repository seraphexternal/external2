#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../features/aimbot.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <cstdint>

// Shared orbit runtime state (single instance across the project).
inline uintptr_t g_orbitLockedPlayer = 0;
inline double g_orbitAngle = 0.0;
inline std::chrono::steady_clock::time_point g_orbitLastTime;
inline bool g_orbitInitTime = false;

inline bool Orbit_IsAlive(const RobloxPlayer& p)
{
    return p.address != 0 && p.HumanoidRootPart.address != 0 && p.Health > 0;
}

// Find a cached player object by its address.
inline RobloxPlayer Orbit_FindCached(uintptr_t addr)
{
    for (auto& p : Globals::Caches::CachedPlayerObjects)
        if (p.address == addr) return p;
    return RobloxPlayer();
}

// Resolve the target by username (TargetMode == 1).
inline RobloxPlayer Orbit_ResolveByName()
{
    std::string name(Options::Orbit::TargetPlayer);
    if (name.empty()) return RobloxPlayer();
    RobloxInstance pl = FindPlayerByName(name);
    if (!pl.address) return RobloxPlayer();
    for (auto& p : Globals::Caches::CachedPlayerObjects)
        if (p.address == pl.address) return p;
    return RobloxPlayer();
}

// Acquire the best target to orbit: the player closest to the crosshair,
// filtered by the orbit WallCheck / KnockedCheck options.
inline RobloxPlayer Orbit_AcquireTarget()
{
    RobloxPlayer best;
    float bestDist = FLT_MAX;

    POINT p;
    GetCursorPos(&p);

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;
        if (player.Health <= 0)
            continue;
        if (Options::Orbit::KnockedCheck && player.Health > 0 && player.Health <= 5.0f)
            continue;
        if (Options::Orbit::WallCheck && Visibility::IsPlayerOccluded(player))
            continue;
        if (!player.HumanoidRootPart.address)
            continue;

        Vectors::Vector3 tp = player.HumanoidRootPart.Position();
        Vectors::Vector2 sp = WorldToScreen(tp);
        if (sp.x == -1.f || sp.y == -1.f)
            continue;

        float d = sp.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });
        if (d < bestDist)
        {
            bestDist = d;
            best = player;
        }
    }
    return best;
}

// ---- Orbit Radius Circle Visualizer ----
inline void RenderOrbitRadiusVisual(ImDrawList* drawList)
{
    if (!Options::Orbit::Enabled || !Options::Orbit::ShowRadius)
        return;

    bool orbitActive = false;
    if (Options::Orbit::ToggleType == 2) orbitActive = true;
    else if (Options::Orbit::OrbitKey != 0) orbitActive = Options::Orbit::Toggled;
    else orbitActive = true;
    if (!orbitActive) return;

    if (!Globals::Roblox::LocalPlayer.address) return;

    RobloxInstance targetHrp(0);

    // Prefer the actually-locked target so the ring follows who we orbit.
    if (g_orbitLockedPlayer != 0)
    {
        RobloxPlayer locked = Orbit_FindCached(g_orbitLockedPlayer);
        if (Orbit_IsAlive(locked))
            targetHrp = locked.HumanoidRootPart;
    }

    if (!targetHrp.address)
    {
        RobloxPlayer resolved = (Options::Orbit::TargetMode == 1)
            ? Orbit_ResolveByName()
            : Orbit_AcquireTarget();
        if (Orbit_IsAlive(resolved))
            targetHrp = resolved.HumanoidRootPart;
    }

    if (!targetHrp.address) return;
    uintptr_t targetPrimitive = Memory->read<uintptr_t>(targetHrp.address + Offsets::BasePart::Primitive);
    if (!targetPrimitive) return;

    Vectors::Vector3 targetPos = Memory->read<Vectors::Vector3>(targetPrimitive + Offsets::Primitive::Position);

    const float radius = Options::Orbit::Radius;
    constexpr int segments = 64;
    constexpr float kTwoPi = 6.28318530718f;

    const ImU32 circleColor = IM_COL32(
        static_cast<int>(Options::Orbit::RadiusColor[0] * 255.f),
        static_cast<int>(Options::Orbit::RadiusColor[1] * 255.f),
        static_cast<int>(Options::Orbit::RadiusColor[2] * 255.f),
        static_cast<int>(Options::Orbit::RadiusAlpha * 255.f));

    ImVec2 prevScreen(-1, -1);
    bool prevValid = false;

    for (int i = 0; i <= segments; ++i)
    {
        const float angle = (kTwoPi * static_cast<float>(i)) / static_cast<float>(segments);
        Vectors::Vector3 worldPoint{
            targetPos.x + radius * std::cos(angle),
            targetPos.y + Options::Orbit::Height,
            targetPos.z + radius * std::sin(angle)
        };
        auto screen = WorldToScreen(worldPoint);
        bool valid = (screen.x >= 0.f && screen.y >= 0.f);

        if (valid && prevValid)
        {
            drawList->AddLine(
                ImVec2(prevScreen.x, prevScreen.y),
                ImVec2(screen.x, screen.y),
                circleColor,
                Options::Orbit::RadiusThickness);
        }
        prevScreen = ImVec2(screen.x, screen.y);
        prevValid = valid;
    }
}

inline void OrbitLoop()
{
    g_orbitAngle = 0.0;
    g_orbitLockedPlayer = 0;
    g_orbitInitTime = false;

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        // ---- Keybind handling ----
        if (Options::Orbit::OrbitKey != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::Orbit::OrbitKey) & 0x8000) != 0;

            if (Options::Orbit::ToggleType == 0) // Hold
            {
                Options::Orbit::Toggled = isKeyPressed;
            }
            else if (Options::Orbit::ToggleType == 1) // Toggle
            {
                if (isKeyPressed && !wasKeyPressed)
                    Options::Orbit::Toggled = !Options::Orbit::Toggled;
            }
            // ToggleType == 2 (Always On) ignores the key; active = Enabled

            wasKeyPressed = isKeyPressed;
        }

        // Determine whether orbit should run this tick
        bool featureOn = false;
        if (Options::Orbit::Enabled)
        {
            if (Options::Orbit::ToggleType == 2) // Always On
                featureOn = true;
            else if (Options::Orbit::OrbitKey != 0)
                featureOn = Options::Orbit::Toggled;
            else
                featureOn = true; // No key set — treat as always on when Enabled
        }

        if (!featureOn)
        {
            // Reset lock so the next activation grabs a fresh target.
            g_orbitLockedPlayer = 0;
            g_orbitAngle = 0.0;
            g_orbitInitTime = false;
            continue;
        }

        // ---- Resolve / validate the target ----
        RobloxPlayer target;

        if (Options::Orbit::TargetMode == 2) // Lock Until Death
        {
            bool reacquire = (g_orbitLockedPlayer == 0);
            if (!reacquire)
            {
                target = Orbit_FindCached(g_orbitLockedPlayer);
                if (!Orbit_IsAlive(target)) // dead / left
                {
                    if (Options::Orbit::OrbitUntilDeath)
                        reacquire = true; // grab a new one and keep going
                    else
                        target = RobloxPlayer(); // stop; keep stale lock so we don't re-grab until toggled
                }
            }
            if (reacquire)
            {
                target = Orbit_AcquireTarget();
                g_orbitLockedPlayer = target.address;
            }
        }
        else if (Options::Orbit::TargetMode == 1) // By Username
        {
            target = Orbit_ResolveByName();
            g_orbitLockedPlayer = target.address;
        }
        else // Aimed At — follow whoever is closest to the crosshair
        {
            target = Orbit_AcquireTarget();
            g_orbitLockedPlayer = target.address;
        }

        if (!target.address || !target.HumanoidRootPart.address)
            continue;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
                continue;

            auto localChar = localPlayer.Character();
            if (!localChar.address)
                continue;

            auto localHrp = localChar.FindFirstChild("HumanoidRootPart");
            if (!localHrp.address)
                continue;

            uintptr_t localPrimitive = Memory->read<uintptr_t>(localHrp.address + Offsets::BasePart::Primitive);
            if (!localPrimitive)
                continue;

            uintptr_t targetPrimitive = Memory->read<uintptr_t>(target.HumanoidRootPart.address + Offsets::BasePart::Primitive);
            if (!targetPrimitive)
                continue;

            Vectors::Vector3 targetPos = Memory->read<Vectors::Vector3>(targetPrimitive + Offsets::Primitive::Position);

            // Advance the orbit angle using real elapsed time so the speed is
            // framerate-independent and the motion stays smooth (full 360 circles).
            auto now = std::chrono::steady_clock::now();
            double dt = 0.016;
            if (g_orbitInitTime)
                dt = std::chrono::duration<double>(now - g_orbitLastTime).count();
            g_orbitLastTime = now;
            g_orbitInitTime = true;
            if (dt > 0.1) dt = 0.1; // clamp after stalls

            g_orbitAngle += dt * Options::Orbit::Speed;

            // Desired position: a point on a ring (radius) around the target.
            double r = Options::Orbit::Radius;
            Vectors::Vector3 desired{
                targetPos.x + static_cast<float>(std::sin(g_orbitAngle) * r),
                targetPos.y + Options::Orbit::Height,
                targetPos.z + static_cast<float>(std::cos(g_orbitAngle) * r)
            };

            // Glide toward the desired point via velocity instead of snapping the
            // position. This makes the character actually circle the enemy instead
            // of teleporting next to them, and naturally holds height (no-fall).
            Vectors::Vector3 cur = Memory->read<Vectors::Vector3>(localPrimitive + Offsets::Primitive::Position);

            Vectors::Vector3 vel{
                (desired.x - cur.x) * Options::Orbit::Follow,
                (desired.y - cur.y) * Options::Orbit::Follow,
                (desired.z - cur.z) * Options::Orbit::Follow
            };

            Memory->write<Vectors::Vector3>(localPrimitive + Offsets::Primitive::AssemblyLinearVelocity, vel);

            // Make the character face the enemy we're orbiting.
            if (Globals::Roblox::Camera.address)
            {
                Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
                    Globals::Roblox::Camera.address + Offsets::Camera::Position);

                sCFrame lookAtCFrame = LookAt(camPos, targetPos);
                Vectors::Vector3 lookVec = lookAtCFrame.GetLookVector();

                Matrixes::Matrix3x3 curRot = Memory->read<Matrixes::Matrix3x3>(
                    Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
                curRot.r02 = lookVec.x;
                curRot.r12 = lookVec.y;
                curRot.r22 = lookVec.z;
                Memory->write<Matrixes::Matrix3x3>(
                    Globals::Roblox::Camera.address + Offsets::Camera::Rotation, curRot);
            }
        }
        catch (...)
        {
            // Silently handle errors
        }
    }
}
