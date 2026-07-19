#pragma once
#include "visibility.h"
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include <vector>
#include <string>
#include <thread>
#include <chrono>

namespace Chams
{
    struct MaterialEntry
    {
        const char* name;
        int16_t id;
    };

    inline MaterialEntry materialList[] =
    {
        { "Plastic",        272 },
        { "Wood",           273 },
        { "Slate",          274 },
        { "Concrete",       275 },
        { "Metal",          276 },
        { "Grass",          277 },
        { "Sand",           278 },
        { "Brick",          283 },
        { "Cobblestone",    284 },
        { "Ice",            288 },
        { "Rock",           293 },
        { "Snow",           296 },
        { "WoodPlanks",     297 },
        { "Glass",          298 },
        { "Marble",         299 },
        { "Granite",        300 },
        { "SmoothPlastic",  302 },
        { "Neon",           303 },
        { "DiamondPlate",   304 },
        { "Foil",           306 },
        { "ForceField",     1584 },
    };

    inline constexpr int materialCount = sizeof(materialList) / sizeof(materialList[0]);

    inline int16_t GetSelectedMaterial()
    {
        int idx = Options::Chams::Material;
        if (idx < 0 || idx >= materialCount)
            idx = materialCount - 1;
        return materialList[idx].id;
    }

    inline uint32_t ConvertColorToBGR(float r, float g, float b, float a = 1.0f)
    {
        auto clampVal = [](float v) -> uint32_t {
            float clamped = v * 255.0f;
            if (clamped < 0.0f) clamped = 0.0f;
            if (clamped > 255.0f) clamped = 255.0f;
            return static_cast<uint32_t>(clamped);
        };
        uint32_t alpha = clampVal(a) << 24;
        return alpha | (clampVal(b) << 16) | (clampVal(g) << 8) | clampVal(r);
    }

    inline void ApplyToPart(const RobloxInstance& part, uint32_t chamsColor)
    {
        if (!part.address || part.address > 0x7FFFFFFFFFFF)
            return;

        Memory->write<uint32_t>(part.address + Offsets::BasePart::Color3, chamsColor);
    }

    inline void ApplyToPartMaterial(const RobloxInstance& part, int16_t materialID, uint32_t chamsColor)
        {
            if (!part.address || part.address > 0x7FFFFFFFFFFF)
                return;

            uintptr_t primitiveAddr = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
            if (!primitiveAddr || primitiveAddr > 0x7FFFFFFFFFFF)
                return;

            Memory->write<int16_t>(primitiveAddr + Offsets::Primitive::Material, materialID);
            Memory->write<uint32_t>(part.address + Offsets::BasePart::Color3, chamsColor);
        }

    inline void CacheChamsLoop()
    {
        while (Globals::running)
        {
            if (!Options::Chams::Enabled)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            try
            {
                int16_t materialID = GetSelectedMaterial();
                uint32_t chamsColor = ConvertColorToBGR(
                    Options::Chams::VisibleColor[0],
                    Options::Chams::VisibleColor[1],
                    Options::Chams::VisibleColor[2],
                    Options::Chams::VisibleColor[3]
                );

                for (auto& player : Globals::Caches::CachedPlayerObjects)
                {
                    if (player.address == 0 || player.address > 0x7FFFFFFFFFFF)
                        continue;
                    if (player.address == Globals::Roblox::LocalPlayer.address)
                        continue;

                    if (Options::Chams::TeamCheck && IsTeammate(player))
                        continue;

                    // Engine chams forces the selected material (Neon / ForceField / etc.)
                    // onto every part for the classic glowing look. Otherwise we just tint
                    // the part color.
                    auto apply = [&](const RobloxInstance& part)
                    {
                        if (Options::Chams::EngineChams)
                            ApplyToPartMaterial(part, materialID, chamsColor);
                        else
                            ApplyToPart(part, chamsColor);
                    };

                    apply(player.Head);
                    apply(player.HumanoidRootPart);

                    if (player.RigType == 0)
                    {
                        apply(player.Torso);
                        apply(player.Left_Arm);
                        apply(player.Right_Arm);
                        apply(player.Left_Leg);
                        apply(player.Right_Leg);
                    }
                    else
                    {
                        apply(player.Upper_Torso);
                        apply(player.Lower_Torso);
                        apply(player.Left_Upper_Arm);
                        apply(player.Left_Lower_Arm);
                        apply(player.Left_Hand);
                        apply(player.Right_Upper_Arm);
                        apply(player.Right_Lower_Arm);
                        apply(player.Right_Hand);
                        apply(player.Left_Upper_Leg);
                        apply(player.Left_Lower_Leg);
                        apply(player.Left_Foot);
                        apply(player.Right_Upper_Leg);
                        apply(player.Right_Lower_Leg);
                        apply(player.Right_Foot);
                    }
                }
            }
            catch (...) {}

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    inline void RenderChams() {}
}
