#pragma once

#include <thread>
#include <chrono>
#include <string>
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"

namespace WorldVisuals
{
    inline RobloxInstance cachedLighting(0);
    inline RobloxInstance cachedSky(0);
    inline int lastSkyPreset = -1;

    inline const char* GetSkyboxAssetId(int preset)
    {
        switch (preset)
        {
        case 1: return "rbxassetid://692809484";   // Night
        case 2: return "rbxassetid://159454299";   // Space
        case 3: return "rbxassetid://26431257";    // Sunset
        case 4: return "rbxassetid://271042516";   // Storm
        default: return "";
        }
    }

    inline void WriteSkyboxFace(uintptr_t skyAddress, uintptr_t offset, const char* assetId)
    {
        if (!skyAddress || !assetId || assetId[0] == '\0')
            return;

        const uintptr_t stringObject = Memory->read<uintptr_t>(skyAddress + offset);
        if (stringObject)
            Memory->writeString(stringObject, assetId);
    }

    inline void ApplySkybox(RobloxInstance& sky, int preset)
    {
        if (!sky.address || preset == 0)
            return;

        const char* assetId = GetSkyboxAssetId(preset);
        if (!assetId || assetId[0] == '\0')
            return;

        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxBk, assetId);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxDn, assetId);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxFt, assetId);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxLf, assetId);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxRt, assetId);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxUp, assetId);
    }

    inline void ApplyLighting()
    {
        if (!Globals::Roblox::DataModel.address)
            return;

        if (!cachedLighting.address)
            cachedLighting = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Lighting");

        if (!cachedLighting.address)
            return;

        if (Options::World::Fullbright)
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::Brightness, 3.0f);
            const float white[3] = { 1.0f, 1.0f, 1.0f };
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::Ambient, { white[0], white[1], white[2] });
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::OutdoorAmbient, { white[0], white[1], white[2] });
        }
        else
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::Brightness, Options::World::Brightness);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::Ambient,
                { Options::World::Ambient[0], Options::World::Ambient[1], Options::World::Ambient[2] });
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::OutdoorAmbient,
                { Options::World::OutdoorAmbient[0], Options::World::OutdoorAmbient[1], Options::World::OutdoorAmbient[2] });
        }

        Memory->write<float>(cachedLighting.address + Offsets::Lighting::ClockTime, Options::World::ClockTime);

        if (Options::World::NoFog)
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::FogStart, 0.0f);
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::FogEnd, 100000.0f);
        }
        else
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::FogStart, Options::World::FogStart);
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::FogEnd, Options::World::FogEnd);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::FogColor,
                { Options::World::FogColor[0], Options::World::FogColor[1], Options::World::FogColor[2] });
        }

        if (Options::World::SkyboxChanger)
        {
            if (!cachedSky.address)
            {
                const uintptr_t skyPtr = Memory->read<uintptr_t>(cachedLighting.address + Offsets::Lighting::Sky);
                cachedSky = RobloxInstance(skyPtr);
            }

            if (cachedSky.address && lastSkyPreset != Options::World::SkyboxPreset)
            {
                ApplySkybox(cachedSky, Options::World::SkyboxPreset);
                lastSkyPreset = Options::World::SkyboxPreset;
            }
        }
    }
}

inline void WorldLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        if (!Options::World::Enabled)
            continue;

        try
        {
            WorldVisuals::ApplyLighting();
        }
        catch (...)
        {
        }
    }
}
