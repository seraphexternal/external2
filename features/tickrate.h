#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include <thread>
#include <chrono>

// TickRate loop: reads Workspace -> World -> worldStepsPerSec and writes custom rate
inline void TickRateLoop()
{
    while (Globals::running)
    {
        try
        {
            if (Options::TickRate::Enabled && Globals::Roblox::Workspace.address)
            {
                // Read world pointer from Workspace
                uintptr_t worldAddr = Memory->read<uintptr_t>(
                    Globals::Roblox::Workspace.address + Offsets::Workspace::World
                );

                if (worldAddr)
                {
                    float currentRate = Memory->read<float>(worldAddr + Offsets::World::worldStepsPerSec);
                    float targetRate = Options::TickRate::Rate;

                    if (std::abs(currentRate - targetRate) > 0.5f)
                    {
                        Memory->write<float>(worldAddr + Offsets::World::worldStepsPerSec, targetRate);
                    }
                }
            }
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
