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

    inline bool savedOriginals = false;
    inline float origBrightness = 1.0f;
    inline Vectors::Vector3 origAmbient = { 0.5f, 0.5f, 0.5f };
    inline Vectors::Vector3 origOutdoorAmbient = { 0.5f, 0.5f, 0.5f };
    inline bool origGlobalShadows = true;

    inline void SaveOriginals(RobloxInstance& lighting)
    {
        if (savedOriginals || !lighting.address) return;
        origBrightness = Memory->read<float>(lighting.address + Offsets::Lighting::Brightness);
        origAmbient = Memory->read<Vectors::Vector3>(lighting.address + Offsets::Lighting::Ambient);
        origOutdoorAmbient = Memory->read<Vectors::Vector3>(lighting.address + Offsets::Lighting::OutdoorAmbient);
        origGlobalShadows = Memory->read<bool>(lighting.address + Offsets::Lighting::GlobalShadows);
        savedOriginals = true;
    }

    inline void RestoreOriginals(RobloxInstance& lighting)
    {
        if (!savedOriginals || !lighting.address) return;
        Memory->write<float>(lighting.address + Offsets::Lighting::Brightness, origBrightness);
        Memory->write<Vectors::Vector3>(lighting.address + Offsets::Lighting::Ambient, origAmbient);
        Memory->write<Vectors::Vector3>(lighting.address + Offsets::Lighting::OutdoorAmbient, origOutdoorAmbient);
        Memory->write<bool>(lighting.address + Offsets::Lighting::GlobalShadows, origGlobalShadows);
        savedOriginals = false;
    }

    inline float GetSunHeightAngle(float clockTime)
    {
        float normalized = clockTime - 0.5f;
        return normalized * 2.0f * 3.14159265f;
    }

    inline Vectors::Vector3 ComputeSunPosition(float clockTime)
    {
        float angle = GetSunHeightAngle(clockTime);
        return { std::cos(angle) * -1.0f, std::sin(angle), 0.0f };
    }

    inline Vectors::Vector3 ComputeMoonPosition(float clockTime)
    {
        float angle = GetSunHeightAngle(clockTime) + 3.14159265f;
        return { std::cos(angle) * -1.0f, std::sin(angle), 0.0f };
    }

    inline void ComputeAutoLighting(float clockTime, Vectors::Vector3& outAmbient, Vectors::Vector3& outOutdoor, float& outBrightness)
    {
        float sunHeight = std::sin(GetSunHeightAngle(clockTime));
        float dayFactor = std::clamp((sunHeight + 1.0f) / 2.0f, 0.05f, 1.0f);

        outAmbient = { 0.10f + 0.45f * dayFactor, 0.10f + 0.40f * dayFactor, 0.15f + 0.30f * dayFactor };
        outOutdoor = { 0.15f + 0.50f * dayFactor, 0.15f + 0.45f * dayFactor, 0.20f + 0.35f * dayFactor };
        outBrightness = 0.3f + 0.7f * dayFactor;
    }

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
        {
            cachedLighting = RobloxInstance(0);
            cachedSky = RobloxInstance(0);
            return;
        }

        cachedLighting = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Lighting");
        if (!cachedLighting.address)
        {
            cachedSky = RobloxInstance(0);
            return;
        }

        if (!Options::World::Enabled)
        {
            if (savedOriginals)
                RestoreOriginals(cachedLighting);
            return;
        }

        SaveOriginals(cachedLighting);

        if (Options::World::Fullbright)
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::Brightness, 3.0f);
            const float white[3] = { 1.0f, 1.0f, 1.0f };
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::Ambient, { white[0], white[1], white[2] });
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::OutdoorAmbient, { white[0], white[1], white[2] });
        }
        else if (Options::World::AutoSunPosition)
        {
            float clockTime = Options::World::ClockTime;
            if (clockTime < 0.0f) clockTime = 0.5f;

            Vectors::Vector3 sunPos = ComputeSunPosition(clockTime);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::SunPosition, sunPos);

            Vectors::Vector3 moonPos = ComputeMoonPosition(clockTime);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::MoonPosition, moonPos);

            Vectors::Vector3 autoAmbient, autoOutdoor;
            float autoBrightness;
            ComputeAutoLighting(clockTime, autoAmbient, autoOutdoor, autoBrightness);

            Memory->write<float>(cachedLighting.address + Offsets::Lighting::Brightness, autoBrightness);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::Ambient, autoAmbient);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::OutdoorAmbient, autoOutdoor);
        }
        else
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::Brightness, Options::World::Brightness);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::Ambient,
                { Options::World::Ambient[0], Options::World::Ambient[1], Options::World::Ambient[2] });
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::OutdoorAmbient,
                { Options::World::OutdoorAmbient[0], Options::World::OutdoorAmbient[1], Options::World::OutdoorAmbient[2] });
        }

        Memory->write<bool>(cachedLighting.address + Offsets::Lighting::GlobalShadows, !Options::World::NoShadows);

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
            cachedSky = RobloxInstance(Memory->read<uintptr_t>(cachedLighting.address + Offsets::Lighting::Sky));

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
