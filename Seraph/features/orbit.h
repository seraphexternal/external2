#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../features/aimbot.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <string>

inline void OrbitLoop()
{
    double angle = 0.0;
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
        bool active = false;
        if (Options::Orbit::Enabled)
        {
            if (Options::Orbit::ToggleType == 2) // Always On
                active = true;
            else if (Options::Orbit::OrbitKey != 0)
                active = Options::Orbit::Toggled;
            else
                active = true; // No key set — treat as always on when Enabled
        }

        if (!active)
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

            // ---- Resolve target ----
            RobloxInstance targetHrp(0);

            if (Options::Orbit::TargetMode == 1)
            {
                // By Username — look up in Players service case-insensitively
                std::string name(Options::Orbit::TargetPlayer);
                if (name.empty())
                    continue;

                auto targetPlayer = FindPlayerByName(name);
                if (!targetPlayer.address)
                    continue;

                auto targetChar = targetPlayer.Character();
                if (!targetChar.address)
                    continue;

                targetHrp = targetChar.FindFirstChild("HumanoidRootPart");
            }
            else
            {
                // Aimed At — use same closest-player logic as aimbot
                RobloxPlayer closest = GetClosestPlayer();
                if (!closest.address)
                    continue;

                targetHrp = closest.HumanoidRootPart;
            }

            if (!targetHrp.address)
                continue;

            uintptr_t targetPrimitive = Memory->read<uintptr_t>(targetHrp.address + Offsets::BasePart::Primitive);
            if (!targetPrimitive)
                continue;

            // Read target position
            Vectors::Vector3 targetPos = Memory->read<Vectors::Vector3>(targetPrimitive + Offsets::Primitive::Position);

            // Increment angle
            angle += 0.016 * Options::Orbit::Speed;

            // Circular offset
            float radius = Options::Orbit::Radius;
            Vectors::Vector3 offset{
                static_cast<float>(std::sin(angle) * radius),
                0.0f,
                static_cast<float>(std::cos(angle) * radius)
            };

            Vectors::Vector3 newPos{
                targetPos.x + offset.x,
                targetPos.y + offset.y,
                targetPos.z + offset.z
            };

            Memory->write<Vectors::Vector3>(localPrimitive + Offsets::Primitive::Position, newPos);
        }
        catch (...)
        {
            // Silently handle errors
        }
    }
}
