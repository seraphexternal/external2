#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include <thread>
#include <chrono>

inline void RampFlingLoop()
{
    float lastYVelocity = 0.0f;
    float lastFlingTime = 0.0f;
    bool wasFalling = false;
    int tickCount = 0;

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        // Handle keybind toggle
        if (Options::RampFling::FlingKey != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::RampFling::FlingKey) & 0x8000) != 0;

            if (Options::RampFling::ToggleType == 2)
            {
                Options::RampFling::Toggled = true;
            }
            else if (Options::RampFling::ToggleType == 1)
            {
                if (isKeyPressed && !wasKeyPressed)
                    Options::RampFling::Toggled = !Options::RampFling::Toggled;
                wasKeyPressed = isKeyPressed;
            }
            else
            {
                Options::RampFling::Toggled = isKeyPressed;
            }
        }
        else
        {
            if (Options::RampFling::ToggleType == 2)
                Options::RampFling::Toggled = true;
        }

        if (!Options::RampFling::Enabled || !Options::RampFling::Toggled)
        {
            lastYVelocity = 0.0f;
            wasFalling = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

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

            auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
            if (!humanoid.address)
                continue;

            uintptr_t primitive = Memory->read<uintptr_t>(humanoidRootPart.address + Offsets::BasePart::Primitive);
            if (!primitive)
                continue;

            Vectors::Vector3 velocity = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity);

            uintptr_t stateAddr = humanoid.address + Offsets::Humanoid::HumanoidState;
            int humanoidState = Memory->read<int>(stateAddr + Offsets::Humanoid::HumanoidStateID);

            // 0 = Running, 2 = RunningNoPhysics, 7 = Freefall, 11 = Landed, 12 = Jumping
            bool isFreefalling = (humanoidState == 7);
            bool isGrounded = (humanoidState == 0 || humanoidState == 2 || humanoidState == 11);

            float currentY = velocity.y;
            float horizontalSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
            float yDelta = currentY - lastYVelocity;

            tickCount++;
            float currentTime = tickCount * 0.008f;
            float timeSinceLastFling = currentTime - lastFlingTime;

            if (timeSinceLastFling < Options::RampFling::Cooldown)
            {
                wasFalling = isFreefalling;
                lastYVelocity = currentY;
                continue;
            }

            bool didFling = false;

            // ---- Detection: Ramp/corner fling ----
            // The REAL signature of a ramp hit: Y velocity spikes POSITIVE (upward)
            // while moving fast horizontally. Normal landings bring Y to ~0, not positive.
            // A ramp/corner deflects horizontal momentum into upward momentum.

            // Condition A: Was falling, Y velocity suddenly spiked positive
            // This catches ramp slides and corner bounces where the surface
            // redirected your momentum upward.
            if (wasFalling && yDelta > 8.0f && currentY > 5.0f && horizontalSpeed > 10.0f)
            {
                float force = Options::RampFling::FlingForce;
                float speedMultiplier = std::clamp(horizontalSpeed / 50.0f, 0.5f, 2.5f);
                force *= speedMultiplier;

                velocity.y = force;

                if (horizontalSpeed > 0.1f)
                {
                    float dirX = velocity.x / horizontalSpeed;
                    float dirZ = velocity.z / horizontalSpeed;
                    velocity.x += dirX * force * Options::RampFling::HorizontalBoost;
                    velocity.z += dirZ * force * Options::RampFling::HorizontalBoost;
                }

                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, velocity);
                didFling = true;
            }
            // Condition B: Running on ground at high speed, sudden upward spike
            // This catches hitting a ramp/ledge while grounded at full speed
            // (the physics engine pushes you up when you run into an angled surface)
            else if (isGrounded && horizontalSpeed > 25.0f && yDelta > 5.0f && currentY > 8.0f)
            {
                float force = Options::RampFling::FlingForce * 0.9f;

                velocity.y = force;

                if (horizontalSpeed > 0.1f)
                {
                    float dirX = velocity.x / horizontalSpeed;
                    float dirZ = velocity.z / horizontalSpeed;
                    velocity.x += dirX * force * Options::RampFling::HorizontalBoost;
                    velocity.z += dirZ * force * Options::RampFling::HorizontalBoost;
                }

                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, velocity);
                didFling = true;
            }
            // Condition C: Moving fast in air, Y velocity reverses sharply upward
            // This catches clipping a box corner while airborne
            else if (!isGrounded && horizontalSpeed > 15.0f && lastYVelocity < -5.0f && currentY > 10.0f)
            {
                float force = Options::RampFling::FlingForce * 1.2f;

                velocity.y = force;

                if (horizontalSpeed > 0.1f)
                {
                    float dirX = velocity.x / horizontalSpeed;
                    float dirZ = velocity.z / horizontalSpeed;
                    velocity.x += dirX * force * Options::RampFling::HorizontalBoost;
                    velocity.z += dirZ * force * Options::RampFling::HorizontalBoost;
                }

                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, velocity);
                didFling = true;
            }

            if (didFling)
                lastFlingTime = currentTime;

            wasFalling = isFreefalling;
            lastYVelocity = currentY;
        }
        catch (...)
        {
        }
    }
}
