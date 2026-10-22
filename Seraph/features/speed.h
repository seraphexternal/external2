#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include <thread>
#include <chrono>

void SpeedLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Handle keybind toggle
        if (Options::WalkSpeed::WalkSpeedKey != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::WalkSpeed::WalkSpeedKey) & 0x8000) != 0;

            if (Options::WalkSpeed::ToggleType == 2) // Always On
            {
                Options::WalkSpeed::Toggled = true;
            }
            else if (Options::WalkSpeed::ToggleType == 1) // Toggle mode
            {
                if (isKeyPressed && !wasKeyPressed)
                {
                    Options::WalkSpeed::Toggled = !Options::WalkSpeed::Toggled;
                }
                wasKeyPressed = isKeyPressed;
            }
            else // Hold mode
            {
                Options::WalkSpeed::Toggled = isKeyPressed;
            }
        }
        else
        {
            if (Options::WalkSpeed::ToggleType == 2)
                Options::WalkSpeed::Toggled = true;
        }

        if (!Options::WalkSpeed::Enabled || !Options::WalkSpeed::Toggled)
            continue;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
                continue;

            auto character = localPlayer.Character();
            if (!character.address)
                continue;

            auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
            if (!humanoid.address)
                continue;

            auto humanoidRootPart = character.FindFirstChild("HumanoidRootPart");
            if (!humanoidRootPart.address)
                continue;

            // Velocity-based speed
            uintptr_t primitive = Memory->read<uintptr_t>(humanoidRootPart.address + Offsets::BasePart::Primitive);
            if (!primitive)
                continue;

            Vectors::Vector3 moveDir = Memory->read<Vectors::Vector3>(humanoid.address + Offsets::Humanoid::MoveDirection);
            Vectors::Vector3 currentVelocity = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity);

            const Vectors::Vector3 newVelocity(
                moveDir.x * Options::WalkSpeed::Speed,
                currentVelocity.y,
                moveDir.z * Options::WalkSpeed::Speed
            );

            // Re-assert velocity once per ms for ~40ms instead of firing 10000
            // back-to-back driver writes. Roblox recomputes velocity on its
            // ~16ms physics step, so a write every 1ms already lands after
            // every one of those steps; the old burst queued up to ~1M
            // IOCTLs/sec behind the shared IO mutex, starving the ESP/aimbot
            // threads and handing kernel time back to the game.
            const auto hammerEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
            do
            {
                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, newVelocity);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (Globals::running && std::chrono::steady_clock::now() < hammerEnd);
        }
        catch (...)
        {
            // Silently handle errors
        }
    }
}
