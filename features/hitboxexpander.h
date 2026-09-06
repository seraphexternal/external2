#pragma once
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include <thread>

inline void RunHitboxExpander()
{
    while (Globals::running)
    {
        try
        {
            if (!Options::HitboxExpander::Enabled)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (!Globals::Roblox::Players.address)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            auto players = Globals::Roblox::Players.GetChildren();
            for (auto player : players)
            {
                if (!Options::HitboxExpander::Enabled)
                    break;

                if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
                    continue;

                auto character = player.Character();
                if (!character.address) 
                    continue;

                auto hrp = character.FindFirstChild("HumanoidRootPart");
                if (!hrp.address) 
                    continue;

                uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
                if (!primitive) 
                    continue;

                // Set hitbox size (horizontal for X/Z, vertical for Y)
                Vectors::Vector3 newSize = { 
                    Options::HitboxExpander::HorizontalSize, 
                    Options::HitboxExpander::VerticalSize, 
                    Options::HitboxExpander::HorizontalSize 
                };
                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Size, newSize);

                // Set CanCollide using proper bit manipulation (from primitive, not hrp)
                const uintptr_t canCollideAddr = primitive + Offsets::PrimitiveFlags::CanCollide;
                uint8_t currentFlags = Memory->read<uint8_t>(canCollideAddr);
                constexpr uint8_t canCollideBit = 0x8;
                
                if (!Options::HitboxExpander::WalkThrough)
                {
                    // Enable collision - set bit
                    if (!(currentFlags & canCollideBit))
                    {
                        uint8_t newFlags = currentFlags | canCollideBit;
                        Memory->write<uint8_t>(canCollideAddr, newFlags);
                    }
                }
                else
                {
                    // Disable collision (walk through) - clear bit
                    if (currentFlags & canCollideBit)
                    {
                        uint8_t newFlags = currentFlags & ~canCollideBit;
                        Memory->write<uint8_t>(canCollideAddr, newFlags);
                    }
                }

                // Set transparency if show hitbox is enabled
                if (Options::HitboxExpander::ShowHitbox)
                {
                    Memory->write<float>(hrp.address + Offsets::BasePart::Transparency, Options::HitboxExpander::HitboxTransparency);
                }
                else
                {
                    Memory->write<float>(hrp.address + Offsets::BasePart::Transparency, 1.0f); // Fully transparent
                }
            }
        }
        catch (...)
        {
            // Silently catch any exceptions to prevent crashes
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
