#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include <thread>
#include <chrono>

static Vectors::Vector3 g_VoidHideLastPos;
static bool g_VoidHideWasActive = false;

inline void VoidHideLoop()
{
    while (Globals::running)
    {
        // ---- Keybind handling (same pattern as Noclip) ----
        if (Options::VoidHide::VoidHideKey != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::VoidHide::VoidHideKey) & 0x8000) != 0;

            if (Options::VoidHide::ToggleType == 0) // Hold
            {
                Options::VoidHide::Toggled = isKeyPressed;
            }
            else if (Options::VoidHide::ToggleType == 1) // Toggle
            {
                if (isKeyPressed && !wasKeyPressed)
                    Options::VoidHide::Toggled = !Options::VoidHide::Toggled;
            }
            // ToggleType == 2 (Always On) = Toggled is ignored; active = Enabled

            wasKeyPressed = isKeyPressed;
        }

        // Determine if voidhide should be active this tick
        bool active = false;
        if (Options::VoidHide::Enabled)
        {
            if (Options::VoidHide::ToggleType == 2)
                active = true;
            else if (Options::VoidHide::VoidHideKey != 0)
                active = Options::VoidHide::Toggled;
            else
                active = true;
        }

        if (!active)
        {
            if (g_VoidHideWasActive)
            {
                // Restore last saved position and zero velocity
                try
                {
                    auto localPlayer = Globals::Roblox::LocalPlayer;
                    if (localPlayer.address)
                    {
                        auto character = localPlayer.Character();
                        if (character.address)
                        {
                            auto hrp = character.FindFirstChild("HumanoidRootPart");
                            if (hrp.address)
                            {
                                uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
                                if (primitive)
                                {
                                    for (int i = 0; i < 10000; i++)
                                    {
                                        Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Position, g_VoidHideLastPos);
                                        Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, Vectors::Vector3(0, 0, 0));
                                    }
                                }
                            }
                        }
                    }
                }
                catch (...) {}
                g_VoidHideWasActive = false;
            }

            // Save current position when not active
            try
            {
                auto localPlayer = Globals::Roblox::LocalPlayer;
                if (localPlayer.address)
                {
                    auto character = localPlayer.Character();
                    if (character.address)
                    {
                        auto hrp = character.FindFirstChild("HumanoidRootPart");
                        if (hrp.address)
                        {
                            uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
                            if (primitive)
                            {
                                g_VoidHideLastPos = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Position);
                            }
                        }
                    }
                }
            }
            catch (...) {}

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // Active: teleport into the void
        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            auto character = localPlayer.Character();
            if (!character.address)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            auto hrp = character.FindFirstChild("HumanoidRootPart");
            if (!hrp.address)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
            if (!primitive)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            // Read current position once, then write void position many times
            Vectors::Vector3 currentPos = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Position);
            Vectors::Vector3 voidPos(currentPos.x + 1e9f, currentPos.y + 1e9f, currentPos.z + 1e9f);

            // Hold the void position with a write per ms for ~40ms rather than
            // 5000 back-to-back driver writes — the game only recomputes
            // position on its ~16ms physics step, so the burst just saturated
            // the shared IO mutex while barely improving hold strength.
            const auto holdEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
            do
            {
                if (!active) break;
                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Position, voidPos);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (Globals::running && std::chrono::steady_clock::now() < holdEnd);

            g_VoidHideWasActive = true;
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // On thread exit, restore if was active
    if (g_VoidHideWasActive)
    {
        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (localPlayer.address)
            {
                auto character = localPlayer.Character();
                if (character.address)
                {
                    auto hrp = character.FindFirstChild("HumanoidRootPart");
                    if (hrp.address)
                    {
                        uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
                        if (primitive)
                        {
                            // Restore with 1ms-spaced writes over ~40ms instead
                            // of 10000 back-to-back (20k IOCTLs) — enough to
                            // win the physics steps without stalling every
                            // other thread on the shared IO mutex.
                            const auto restoreEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
                            do
                            {
                                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Position, g_VoidHideLastPos);
                                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, Vectors::Vector3(0, 0, 0));
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            } while (Globals::running && std::chrono::steady_clock::now() < restoreEnd);
                        }
                    }
                }
            }
        }
        catch (...) {}
        g_VoidHideWasActive = false;
    }
}
