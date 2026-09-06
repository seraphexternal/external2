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
    inline float origExposure = 0.0f;
    inline Vectors::Vector3 origAmbient = { 0.5f, 0.5f, 0.5f };
    inline Vectors::Vector3 origOutdoorAmbient = { 0.5f, 0.5f, 0.5f };
    inline bool origGlobalShadows = true;
    inline float origFogStart = 0.0f;
    inline float origFogEnd = 0.0f;
    inline Vectors::Vector3 origFogColor = { 0.75f, 0.75f, 0.75f };

    inline void SaveOriginals(RobloxInstance& lighting)
    {
        if (savedOriginals || !lighting.address) return;
        origBrightness = Memory->read<float>(lighting.address + Offsets::Lighting::Brightness);
        origExposure = Memory->read<float>(lighting.address + Offsets::Lighting::ExposureCompensation);
        origAmbient = Memory->read<Vectors::Vector3>(lighting.address + Offsets::Lighting::Ambient);
        origOutdoorAmbient = Memory->read<Vectors::Vector3>(lighting.address + Offsets::Lighting::OutdoorAmbient);
        origGlobalShadows = Memory->read<bool>(lighting.address + Offsets::Lighting::GlobalShadows);
        origFogStart = Memory->read<float>(lighting.address + Offsets::Lighting::FogStart);
        origFogEnd = Memory->read<float>(lighting.address + Offsets::Lighting::FogEnd);
        origFogColor = Memory->read<Vectors::Vector3>(lighting.address + Offsets::Lighting::FogColor);
        savedOriginals = true;
    }

    inline void RestoreOriginals(RobloxInstance& lighting)
    {
        if (!savedOriginals || !lighting.address) return;
        Memory->write<float>(lighting.address + Offsets::Lighting::Brightness, origBrightness);
        Memory->write<float>(lighting.address + Offsets::Lighting::ExposureCompensation, origExposure);
        Memory->write<Vectors::Vector3>(lighting.address + Offsets::Lighting::Ambient, origAmbient);
        Memory->write<Vectors::Vector3>(lighting.address + Offsets::Lighting::OutdoorAmbient, origOutdoorAmbient);
        Memory->write<bool>(lighting.address + Offsets::Lighting::GlobalShadows, origGlobalShadows);
        Memory->write<float>(lighting.address + Offsets::Lighting::FogStart, origFogStart);
        Memory->write<float>(lighting.address + Offsets::Lighting::FogEnd, origFogEnd);
        Memory->write<Vectors::Vector3>(lighting.address + Offsets::Lighting::FogColor, origFogColor);
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

    struct SkyboxFaces
    {
        const char* bk;
        const char* dn;
        const char* ft;
        const char* lf;
        const char* rt;
        const char* up;
    };

    inline SkyboxFaces GetSkyboxFaces(int preset)
    {
        switch (preset)
        {
        case 1:  return { "rbxassetid://12635309703", "rbxassetid://12635311686", "rbxassetid://12635312870", "rbxassetid://12635313718", "rbxassetid://12635315817", "rbxassetid://12635316856" };
        case 2:  return { "rbxassetid://12064107",  "rbxassetid://12064152",  "rbxassetid://12064121",  "rbxassetid://12063984",  "rbxassetid://12064115",  "rbxassetid://12064131"  };
        case 3:  return { "rbxassetid://271042516", "rbxassetid://271077243", "rbxassetid://271042556", "rbxassetid://271042310", "rbxassetid://271042467", "rbxassetid://271077958" };
        case 4:  return { "rbxassetid://1876545003", "rbxassetid://1876544331", "rbxassetid://1876542941", "rbxassetid://1876543392", "rbxassetid://1876543764", "rbxassetid://1876544642" };
        case 5:  return { "rbxassetid://116758234", "rbxassetid://116758314", "rbxassetid://116758367", "rbxassetid://116758446", "rbxassetid://116758478", "rbxassetid://116758496" };
        case 6:  return { "rbxassetid://1233158420", "rbxassetid://1233158838", "rbxassetid://1233157105", "rbxassetid://1233157640", "rbxassetid://1233157995", "rbxassetid://1233159158" };
        case 7:  return { "rbxassetid://1327358",    "rbxassetid://1327359",    "rbxassetid://1327355",    "rbxassetid://1327357",    "rbxassetid://1327356",    "rbxassetid://1327360"    };
        case 8:  return { "rbxassetid://570555736", "rbxassetid://570555964", "rbxassetid://570555800", "rbxassetid://570555840", "rbxassetid://570555882", "rbxassetid://570555929" };
        case 9:  return { "rbxassetid://95020137072033", "rbxassetid://92862258103959", "rbxassetid://107665368823185", "rbxassetid://126542804346203", "rbxassetid://103716549795832", "rbxassetid://131036626982613" };
        case 10: return { "rbxassetid://169210090", "rbxassetid://169210108", "rbxassetid://169210121", "rbxassetid://169210133", "rbxassetid://169210143", "rbxassetid://169210149" };
        case 11: return { "rbxassetid://47974894", "rbxassetid://47974690", "rbxassetid://47974821", "rbxassetid://47974776", "rbxassetid://47974859", "rbxassetid://47974909" };
        default: return { "", "", "", "", "", "" };
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

        auto faces = GetSkyboxFaces(preset);
        if (faces.bk[0] == '\0')
            return;

        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxBk, faces.bk);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxDn, faces.dn);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxFt, faces.ft);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxLf, faces.lf);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxRt, faces.rt);
        WriteSkyboxFace(sky.address, Offsets::Sky::SkyboxUp, faces.up);
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

        if (Options::World::Exposure)
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::ExposureCompensation, Options::World::ExposureValue);
        }

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

        // Fang-style separate world effect toggles (override existing settings when enabled)
        if (Options::World::BrightnessEnabled)
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::Brightness, Options::World::BrightnessValue);
        }

        if (Options::World::Ambience)
        {
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::Ambient,
                { Options::World::AmbienceColor[0], Options::World::AmbienceColor[1], Options::World::AmbienceColor[2] });
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::OutdoorAmbient,
                { Options::World::AmbienceColor[0], Options::World::AmbienceColor[1], Options::World::AmbienceColor[2] });
        }

        if (Options::World::FogEnabled)
        {
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::FogStart, 0.0f);
            Memory->write<float>(cachedLighting.address + Offsets::Lighting::FogEnd, Options::World::FogDistance);
            Memory->write<Vectors::Vector3>(cachedLighting.address + Offsets::Lighting::FogColor,
                { Options::World::FogColor2[0], Options::World::FogColor2[1], Options::World::FogColor2[2] });
        }

        if (Options::World::SkyboxChanger)
        {
            cachedSky = RobloxInstance(Memory->read<uintptr_t>(cachedLighting.address + Offsets::Lighting::Sky));

            if (cachedSky.address && lastSkyPreset != Options::World::SkyboxPreset)
            {
                ApplySkybox(cachedSky, Options::World::SkyboxPreset);
                lastSkyPreset = Options::World::SkyboxPreset;
            }

            if (Options::World::RotateSkybox && cachedSky.address)
            {
                static float skyboxRotY = 0.0f;
                skyboxRotY += Options::World::SkyboxRotateSpeed * 0.025f;
                if (skyboxRotY >= 360.0f) skyboxRotY = 0.0f;
                Memory->write<Vectors::Vector3>(cachedSky.address + Offsets::Sky::SkyboxOrientation, { 0.0f, skyboxRotY, 0.0f });
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
