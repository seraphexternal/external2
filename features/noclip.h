#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>

// Track flag addresses we cleared so we can restore them later.
static std::vector<uintptr_t> g_ModifiedNoclipAddrs;
static bool g_NoclipWasActive = false;

// Solid BasePart classes we are willing to clear CanCollide on.
static bool NoclipIsBasePartClass(const std::string& cls)
{
    return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
           cls == "WedgePart" || cls == "CornerWedgePart" || cls == "TrussPart" ||
           cls == "Seat" || cls == "VehicleSeat";
}

// Recursively clear CanCollide on every BasePart under `inst`, collecting the
// flags addresses it touches. Nesting matters: accessory/tool handles live
// under an intermediate instance, so a flat name lookup misses them.
static void NoclipClearRecursive(const RobloxInstance& inst, std::vector<uintptr_t>& out, int depth)
{
    if (!inst.address || depth > 3 || out.size() > 256)
        return;

    try
    {
        for (auto& child : inst.GetChildrenBulk())
        {
            if (!child.address)
                continue;

            if (NoclipIsBasePartClass(child.Class()))
            {
                const uintptr_t primitive = Memory->read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
                if (primitive)
                {
                    const uintptr_t flagsAddr = primitive + Offsets::Primitive::Flags;
                    const uint8_t canCollideBit = (uint8_t)Offsets::PrimitiveFlags::CanCollide;
                    const uint8_t flags = Memory->read<uint8_t>(flagsAddr);
                    out.push_back(flagsAddr);
                    if (flags & canCollideBit)
                        Memory->write<uint8_t>(flagsAddr, (uint8_t)(flags & ~canCollideBit));
                }
            }

            NoclipClearRecursive(child, out, depth + 1);
        }
    }
    catch (...) {}
}

// Restore collision flags on all parts we previously modified.
static void RestoreNoclipCollision()
{
    const uint8_t canCollideBit = (uint8_t)Offsets::PrimitiveFlags::CanCollide;
    for (uintptr_t addr : g_ModifiedNoclipAddrs)
    {
        try
        {
            const uint8_t flags = Memory->read<uint8_t>(addr);
            if (!(flags & canCollideBit))
                Memory->write<uint8_t>(addr, (uint8_t)(flags | canCollideBit));
        }
        catch (...) {}
    }
    g_ModifiedNoclipAddrs.clear();
}

inline void NoclipLoop()
{
    static std::vector<uintptr_t> touched;
    touched.reserve(128);

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));

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

            // Rebuild every tick: the character may respawn and nested parts
            // (tools/accessories) can appear or disappear.
            touched.clear();
            NoclipClearRecursive(character, touched, 0);
            g_ModifiedNoclipAddrs = touched;
        }
        catch (...)
        {
            // Silently handle errors
        }
    }

    // On thread exit, restore any lingering modifications
    RestoreNoclipCollision();
}
