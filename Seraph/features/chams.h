#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include <unordered_map>
#include <thread>
#include <chrono>
#include <functional>

struct MaterialCache {
    int16_t original_material;
    uint32_t original_color;
    bool is_cached;
};

inline uint32_t ConvertColorToBGR(float rgb[3]) {
    auto clamp_value = [](float value) -> uint32_t {
        return static_cast<uint32_t>(fminf(fmaxf(value * 255.0f, 0.0f), 255.0f));
    };
    return (clamp_value(rgb[2]) << 16) | (clamp_value(rgb[1]) << 8) | clamp_value(rgb[0]);
}

inline void ChamsLoop()
{
    static std::unordered_map<uintptr_t, MaterialCache> material_registry;

    while (Globals::running)
    {
        try
        {
            if (!Options::Chams::Enabled)
            {
                if (!material_registry.empty())
                {
                    // Restore original materials and colors
                    for (const auto& [part_address, cached_state] : material_registry)
                    {
                        uintptr_t primitive_address = Memory->read<uintptr_t>(part_address + Offsets::BasePart::Primitive);
                        if (primitive_address)
                        {
                            Memory->write<int16_t>(primitive_address + Offsets::Primitive::Material, cached_state.original_material);
                        }
                        Memory->write<uint32_t>(part_address + Offsets::BasePart::Color3, cached_state.original_color);
                    }
                    material_registry.clear();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            const uint32_t chams_color = ConvertColorToBGR(Options::Chams::Color);
            const int16_t chams_material = static_cast<int16_t>(Options::Chams::Material);
            int nodesVisited = 0;

            auto processPart = [&](uintptr_t partAddr)
            {
                if (!partAddr) return;
                uintptr_t primitive = Memory->read<uintptr_t>(partAddr + Offsets::BasePart::Primitive);
                if (!primitive) return; // Not a BasePart (filter out Folders/Models/etc.)

                auto it = material_registry.find(partAddr);
                if (it == material_registry.end())
                {
                    MaterialCache entry;
                    entry.original_material = Memory->read<int16_t>(primitive + Offsets::Primitive::Material);
                    entry.original_color = Memory->read<uint32_t>(partAddr + Offsets::BasePart::Color3);
                    entry.is_cached = true;
                    material_registry[partAddr] = entry;
                }

                // Apply material and color
                Memory->write<int16_t>(primitive + Offsets::Primitive::Material, chams_material);
                Memory->write<uint32_t>(partAddr + Offsets::BasePart::Color3, chams_color);
            };

            // Apply chams to the entire workspace (world geometry, terrain, models, characters).
            // The BasePart::Primitive check inside processPart filters out non-BasePart instances
            // so we don't accidentally cache/govern instances like Camera or Lighting.
            //
            // Safety: cap recursion depth and total nodes visited per loop so a single
            // corrupted/corrupt Workspace tree (or huge open-world games with thousands of
            // descendants) cannot blow the stack or stall the ChamsLoop thread.
            //
            // Tradeoff (and why the cap is what it is): each tick restarts the walk
            // from the Workspace root and bails when nodesVisited exceeds 4096. There
            // is no resume-from-last-visit, so parts whose path requires >4096 walked
            // ancestors from Workspace root are simply never reached on any tick. 4096
            // is comfortably more than the depth of a typical Roblox game (most worlds
            // have well under 1k BaseParts). For huge open-world games that approach
            // the limit, the cap prevents the chams thread from blocking on giant trees
            // at the cost of leaving some parts at the bottom of the tree untouched.
            std::function<void(const RobloxInstance&, int)> walk;
            walk = [&](const RobloxInstance& inst, int depth)
            {
                if (!inst.address) return;
                if (depth > 12) return;            // hard recursion depth limit
                if (nodesVisited >= 4096) return;  // hard per-tick visit limit
                ++nodesVisited;

                processPart(inst.address);

                const auto children = inst.GetChildren();
                if (children.empty()) return;
                for (const auto& child : children)
                {
                    if (nodesVisited >= 4096) break;
                    walk(child, depth + 1);
                }
            };

            nodesVisited = 0;
            if (Globals::Roblox::Workspace.address)
                walk(Globals::Roblox::Workspace, 0);
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
