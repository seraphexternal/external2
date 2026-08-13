#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include <thread>
#include <chrono>

void FlyLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Handle keybind toggle
        if (Options::Fly::FlyKey != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::Fly::FlyKey) & 0x8000) != 0;

            if (Options::Fly::ToggleType == 2) // Always On
            {
                Options::Fly::Toggled = true;
            }
            else if (Options::Fly::ToggleType == 1) // Toggle mode
            {
                if (isKeyPressed && !wasKeyPressed)
                {
                    Options::Fly::Toggled = !Options::Fly::Toggled;
                }
                wasKeyPressed = isKeyPressed;
            }
            else // Hold mode
            {
                Options::Fly::Toggled = isKeyPressed;
            }
        }
        else
        {
            if (Options::Fly::ToggleType == 2)
                Options::Fly::Toggled = true;
        }

        bool flyOn = Options::Fly::Enabled && Options::Fly::Toggled;

        // Keep the character from falling while flying: disable gravity via
        // Humanoid::PlatformStand. Restored to 0 whenever fly is off so normal
        // walking/physics resumes.
        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (localPlayer.address)
            {
                auto character = localPlayer.Character();
                if (character.address)
                {
                    auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
                    if (humanoid.address)
                    {
                        Memory->write<uint8_t>(humanoid.address + Offsets::Humanoid::PlatformStand, flyOn ? 1 : 0);
                    }
                }
            }
        }
        catch (...) {}

        if (!flyOn)
            continue;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
                continue;

            auto character = localPlayer.Character();
            if (!character.address)
                continue;

            auto humanoidRootPart = character.FindFirstChild("HumanoidRootPart");
            if (!humanoidRootPart.address)
                continue;

            auto camera = Globals::Roblox::Camera;
            if (!camera.address)
                continue;

            auto cameraMatrix = camera.CFrame();
            Vectors::Vector3 forward = Vectors::Vector3(cameraMatrix.r02, cameraMatrix.r12, cameraMatrix.r22);
            Vectors::Vector3 up = Vectors::Vector3(0, 1, 0);

            uintptr_t primitive = Memory->read<uintptr_t>(humanoidRootPart.address + Offsets::BasePart::Primitive);
            if (!primitive)
                continue;

            // Read, zero out (kills gravity/velocity), then re-apply movement.
            Vectors::Vector3 velocity = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity);
            float speed = Options::Fly::Speed;
            velocity = Vectors::Vector3(0, 0, 0);

            if (GetAsyncKeyState('W') & 0x8000)
                velocity = velocity - forward * speed;
            if (GetAsyncKeyState('S') & 0x8000)
                velocity = velocity + forward * speed;
            if (GetAsyncKeyState('A') & 0x8000)
                velocity = velocity - Vectors::Vector3(forward.z, 0, -forward.x) * speed;
            if (GetAsyncKeyState('D') & 0x8000)
                velocity = velocity + Vectors::Vector3(forward.z, 0, -forward.x) * speed;
            if (GetAsyncKeyState(VK_SPACE) & 0x8000)
                velocity = velocity + up * speed;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                velocity = velocity - up * speed;

            Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, velocity);
        }
        catch (...)
        {
            // Silently handle errors
        }
    }
}
