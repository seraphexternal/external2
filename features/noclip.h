#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>

// Parts we disable collision for
static const std::vector<std::string> g_NoclipParts = {
    "Head", "Torso", "UpperTorso", "LowerTorso", "HumanoidRootPart"
};

// Track addresses where we cleared the CanCollide bit so we can restore
static std::vector<uintptr_t> g_ModifiedNoclipAddrs;
static bool g_NoclipWasActive = false;

// Restore collision flags on all parts we previously modified
static void RestoreNoclipCollision()
{
    for (uintptr_t addr : g_ModifiedNoclipAddrs)
    {
        try
        {
            uint8_t flags = Memory->read<uint8_t>(addr);
            constexpr uint8_t canCollideBit = 0x8;
            if (!(flags & canCollideBit))
            {
                Memory->write<uint8_t>(addr, flags | canCollideBit);
            }
        }
        catch (...) {}
    }
    g_ModifiedNoclipAddrs.clear();
}

inline void NoclipLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // ---- Keybind handling ----
        if (Options::Noclip::NoclipKey != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::Noclip::NoclipKey) & 0x8000) != 0;

            if (Options::Noclip::ToggleType == 0) // Hold
            {
                Options::Noclip::Toggled = isKeyPressed;
            }
            else if (Options::Noclip::ToggleType == 1) // Toggle
            {
                if (isKeyPressed && !wasKeyPressed)
                    Options::Noclip::Toggled = !Options::Noclip::Toggled;
            }
            // ToggleType == 2 (Always On) = Toggled is ignored; active = Enabled

            wasKeyPressed = isKeyPressed;
        }

        // Determine if noclip should be active this tick
        bool active = false;
        if (Options::Noclip::Enabled)
        {
            if (Options::Noclip::ToggleType == 2) // Always On
                active = true;
            else if (Options::Noclip::NoclipKey != 0)
                active = Options::Noclip::Toggled;
            else
                active = true; // No key set — treat as always on when Enabled
        }

        // If we just became inactive, restore collision
        if (!active)
        {
            if (g_NoclipWasActive)
            {
                RestoreNoclipCollision();
                g_NoclipWasActive = false;
            }
            continue;
        }

        g_NoclipWasActive = true;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
                continue;

            auto character = localPlayer.Character();
            if (!character.address)
                continue;

            g_ModifiedNoclipAddrs.clear(); // rebuild each tick (character may respawn)

            for (const auto& partName : g_NoclipParts)
            {
                auto part = character.FindFirstChild(partName);
                if (!part.address)
                    continue;

                uintptr_t primitive = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
                if (!primitive)
                    continue;

                const uintptr_t canCollideAddr = primitive + Offsets::PrimitiveFlags::CanCollide;
                uint8_t currentFlags = Memory->read<uint8_t>(canCollideAddr);
                constexpr uint8_t canCollideBit = 0x8;

                // Track this address so we can restore later
                g_ModifiedNoclipAddrs.push_back(canCollideAddr);

                // Clear the CanCollide bit to disable collisions
                if (currentFlags & canCollideBit)
                {
                    uint8_t newFlags = currentFlags & ~canCollideBit;
                    Memory->write<uint8_t>(canCollideAddr, newFlags);
                }
            }
        }
        catch (...)
        {
            // Silently handle errors
        }
    }

    // On thread exit, restore any lingering modifications
    RestoreNoclipCollision();
}
