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

            if (Options::Fly::ToggleType == 1) // Toggle mode
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

        if (!Options::Fly::Enabled || !Options::Fly::Toggled)
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

            // Get camera for forward direction
            auto camera = Globals::Roblox::Camera;
            if (!camera.address)
                continue;

            auto cameraMatrix = camera.CFrame();
            Vectors::Vector3 forward = Vectors::Vector3(cameraMatrix.r02, cameraMatrix.r12, cameraMatrix.r22);
            Vectors::Vector3 up = Vectors::Vector3(0, 1, 0);

            // Get primitive address for velocity
            uintptr_t primitive = Memory->read<uintptr_t>(humanoidRootPart.address + Offsets::BasePart::Primitive);
            if (!primitive)
                continue;

            // Read current velocity
            Vectors::Vector3 velocity = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity);
            float speed = Options::Fly::Speed;

            // Reset velocity to prevent falling
            velocity = Vectors::Vector3(0, 0, 0);

            // Apply movement based on key inputs
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

            // Write the velocity back
            Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, velocity);
        }
        catch (...)
        {
            // Silently handle errors
        }
    }
}
