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

            if (Options::WalkSpeed::ToggleType == 1) // Toggle mode
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

            for (int i = 0; i < 10000; i++)
            {
                Vectors::Vector3 newVelocity(
                    moveDir.x * Options::WalkSpeed::Speed,
                    currentVelocity.y,
                    moveDir.z * Options::WalkSpeed::Speed
                );
                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, newVelocity);
            }
        }
        catch (...)
        {
            // Silently handle errors
        }
    }
}
