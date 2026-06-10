#pragma once

#include "../configs/json.hpp"
#include "../globals/options.h"
#include "../globals/globals.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <type_traits>
#include <cctype>
#include <vector>
#include <mutex>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <objbase.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using json = nlohmann::json;

namespace Config
{
    inline std::string lastError;

    inline std::recursive_mutex& Mutex()
    {
        static std::recursive_mutex mutex;
        return mutex;
    }
}

inline std::string TrimConfigName(std::string name)
{
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    name.erase(name.begin(), std::find_if(name.begin(), name.end(), notSpace));
    name.erase(std::find_if(name.rbegin(), name.rend(), notSpace).base(), name.end());
    return name;
}

inline std::string NormalizeConfigFilename(std::string name)
{
    name = TrimConfigName(std::move(name));
    if (name.empty())
        return name;

    if (name.find('.') == std::string::npos)
        name += ".json";

    return name;
}

inline void MigrateLegacyConfigs(const std::filesystem::path& legacyDir, const std::filesystem::path& targetDir)
{
    if (!std::filesystem::exists(legacyDir) || !std::filesystem::exists(targetDir))
        return;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(legacyDir, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        const std::filesystem::path dest = targetDir / entry.path().filename();
        if (!std::filesystem::exists(dest))
            std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::skip_existing, ec);
    }
}

inline void InitializeConfigPaths()
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    static bool legacyMigrationDone = false;

    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) != 0)
        Globals::executablePath = std::filesystem::path(exePath).parent_path().string();

    if (Globals::configsPath.empty())
    {
        char appData[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
            Globals::configsPath = (std::filesystem::path(appData) / "Seraph" / "configs").string();
        else if (!Globals::executablePath.empty())
            Globals::configsPath = (std::filesystem::path(Globals::executablePath) / "configs").string();
    }

    if (Globals::configsPath.empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(Globals::configsPath, ec);

    if (!legacyMigrationDone && !Globals::executablePath.empty())
    {
        const std::filesystem::path legacyDir = std::filesystem::path(Globals::executablePath) / "configs";
        MigrateLegacyConfigs(legacyDir, Globals::configsPath);
        legacyMigrationDone = true;
    }
}

inline std::filesystem::path GetConfigFilePath(const std::string& configName)
{
    InitializeConfigPaths();
    return std::filesystem::path(Globals::configsPath) / NormalizeConfigFilename(configName);
}

inline std::vector<std::string> ListConfigFiles()
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    std::vector<std::string> files;

    InitializeConfigPaths();

    if (Globals::configsPath.empty())
        return files;

    const std::string searchPath = Globals::configsPath + "\\*.json";
    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        return files;

    do
    {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        files.emplace_back(findData.cFileName);
    } while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);
    std::sort(files.begin(), files.end());
    return files;
}

inline json ToJsonColor(const float* values, size_t count)
{
    json arr = json::array();
    for (size_t i = 0; i < count; ++i)
        arr.push_back(values[i]);
    return arr;
}

inline bool ReadJsonFile(const std::filesystem::path& filePath, json& out)
{
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open())
    {
        Config::lastError = "Could not open file: " + filePath.string();
        return false;
    }

    std::ostringstream buffer;
    buffer << f.rdbuf();
    const std::string content = buffer.str();
    if (content.empty())
    {
        Config::lastError = "Config file is empty";
        return false;
    }

    try
    {
        out = json::parse(content);
        return true;
    }
    catch (const std::exception& e)
    {
        Config::lastError = std::string("Invalid JSON: ") + e.what();
        return false;
    }
}

template <typename T>
inline void LoadVal(const json& j, const std::string& key, T& outVal)
{
    if (!j.is_object() || !j.contains(key))
        return;

    try
    {
        const json& v = j.at(key);

        if constexpr (std::is_same_v<T, bool>)
        {
            if (v.is_boolean())
                outVal = v.get<bool>();
            else if (v.is_number())
                outVal = v.get<int>() != 0;
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if (v.is_number_integer())
                outVal = static_cast<T>(v.get<int64_t>());
            else if (v.is_number_unsigned())
                outVal = static_cast<T>(v.get<uint64_t>());
            else if (v.is_number_float())
                outVal = static_cast<T>(v.get<double>());
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            if (v.is_number())
                outVal = static_cast<T>(v.get<double>());
        }
        else
        {
            outVal = v.get<T>();
        }
    }
    catch (...) {}
}

template <size_t N>
inline void LoadFloatArray(const json& j, const std::string& key, float (&outArray)[N])
{
    if (!j.is_object() || !j.contains(key))
        return;

    try
    {
        const auto& val = j.at(key);
        if (!val.is_array())
            return;

        const size_t count = val.size() < N ? val.size() : N;
        for (size_t i = 0; i < count; i++)
            outArray[i] = val[i].get<float>();
    }
    catch (...) {}
}

inline json BuildConfigJson()
{
    json j;

    j["ESP"] = {
        { "Team Check", Options::ESP::TeamCheck },
        { "Box Type", Options::ESP::BoxType },
        { "Tracers", Options::ESP::Tracers },
        { "TracersStart", Options::ESP::TracersStart },
        { "Skeleton", Options::ESP::Skeleton },
        { "Name", Options::ESP::Name },
        { "Distance", Options::ESP::Distance },
        { "Health", Options::ESP::Health },
        { "Tracer Thickness", Options::ESP::TracerThickness },
        { "Head Circles", Options::ESP::HeadCircle },
        { "Remove Borders", Options::ESP::RemoveBorders },
        { "Headless", Options::ESP::Headless },
        { "Head Dot", Options::ESP::HeadDot },
        { "Corner ESP", Options::ESP::CornerESP },
        { "Health Text", Options::ESP::HealthText },
        { "Enemy Health Indicator", Options::ESP::EnemyHealthIndicator },
        { "ESP Preview", Options::ESP::ESPPreview },
        { "Box Thickness", Options::ESP::BoxThickness },
        { "Skeleton Thickness", Options::ESP::SkeletonThickness },
        { "3D ESP Thickness", Options::ESP::ESP3DThickness },
        { "Head Circle Thickness", Options::ESP::HeadCircleThickness },
        { "Head Circle Scale", Options::ESP::HeadCircleScale },
        { "Head Circle Max Scale", Options::ESP::HeadCircleMaxScale },
        { "Visibility Check", Options::ESP::VisibilityCheck },
        { "Visibility Chams", Options::ESP::VisibilityChams },
        { "Visibility Max Distance", Options::ESP::VisibilityMaxDistance },
        { "Visible Color", ToJsonColor(Options::ESP::VisibleColor, 3) },
        { "Hidden Color", ToJsonColor(Options::ESP::HiddenColor, 3) },
        { "Name Color", ToJsonColor(Options::ESP::Color, 3) },
        { "Box Color", ToJsonColor(Options::ESP::BoxColor, 3) },
        { "Corner Color", ToJsonColor(Options::ESP::CornerColor, 3) },
        { "Skeleton Color", ToJsonColor(Options::ESP::SkeletonColor, 3) },
        { "Distance Color", ToJsonColor(Options::ESP::DistanceColor, 3) },
        { "Tracers Color", ToJsonColor(Options::ESP::TracerColor, 3) },
        { "3D ESP Color", ToJsonColor(Options::ESP::ESP3DColor, 3) },
        { "Head Circles Color", ToJsonColor(Options::ESP::HeadCircleColor, 3) },
        { "Head Dot Color", ToJsonColor(Options::ESP::HeadDotColor, 3) },
        { "Chams Color", ToJsonColor(Options::ESP::ChamsColor, 3) }
    };

    j["Aimbot"] = {
        { "Aimbot Key", Options::Aimbot::AimbotKey },
        { "Aiming Type", Options::Aimbot::AimingType },
        { "Toggle Type", Options::Aimbot::ToggleType },
        { "Aimbot", Options::Aimbot::Aimbot },
        { "Team Check", Options::Aimbot::TeamCheck },
        { "Downed Check", Options::Aimbot::DownedCheck },
        { "Sticky Aim", Options::Aimbot::StickyAim },
        { "Target Bone", Options::Aimbot::TargetBone },
        { "Air Target Bone", Options::Aimbot::AirTargetBone },
        { "FOV", Options::Aimbot::FOV },
        { "Show FOV", Options::Aimbot::ShowFOV },
        { "Show FOV Fill", Options::Aimbot::ShowFOVFill },
        { "FOV Position Mode", Options::Aimbot::FOVPositionMode },
        { "Silent Aim", Options::Aimbot::SilentAim },
        { "Silent Aim Mode", Options::Aimbot::SilentAimMode },
        { "FOV Color", ToJsonColor(Options::Aimbot::FOVColor, 3) },
        { "FOV Fill Color", ToJsonColor(Options::Aimbot::FOVFillColor, 4) },
        { "FOV Thickness", Options::Aimbot::FOVThickness },
        { "Smoothness", Options::Aimbot::Smoothness },
        { "Smoothness Curve", Options::Aimbot::SmoothnessCurve },
        { "Range", Options::Aimbot::Range },
        { "Prediction", Options::Aimbot::Prediction },
        { "Prediction X", Options::Aimbot::PredictionX },
        { "Prediction Y", Options::Aimbot::PredictionY },
        { "Shake", Options::Aimbot::Shake },
        { "Shake Intensity", Options::Aimbot::ShakeIntensity },
        { "Stutter", Options::Aimbot::Stutter },
        { "Stutter Ticks", Options::Aimbot::StutterTicks },
        { "Custom Curve Enabled", Options::Aimbot::CustomCurveEnabled },
        { "Custom Curve P1", ToJsonColor(Options::Aimbot::CustomCurveP1, 2) },
        { "Custom Curve P2", ToJsonColor(Options::Aimbot::CustomCurveP2, 2) }
    };

    j["Triggerbot"] = {
        { "Triggerbot Key", Options::Triggerbot::TriggerbotKey },
        { "Toggle Type", Options::Triggerbot::ToggleType },
        { "Enabled", Options::Triggerbot::Enabled },
        { "Team Check", Options::Triggerbot::TeamCheck },
        { "Downed Check", Options::Triggerbot::DownedCheck },
        { "Radius", Options::Triggerbot::Radius },
        { "Range", Options::Triggerbot::Range },
        { "Delay", Options::Triggerbot::Delay },
        { "Advanced FOV", Options::Triggerbot::AdvancedFOV },
        { "Show Advanced FOV", Options::Triggerbot::ShowAdvancedFOV },
        { "HeadFOV_X", Options::Triggerbot::HeadFOV_X },
        { "HeadFOV_Y", Options::Triggerbot::HeadFOV_Y },
        { "TorsoFOV_X", Options::Triggerbot::TorsoFOV_X },
        { "TorsoFOV_Y", Options::Triggerbot::TorsoFOV_Y },
        { "UpperTorsoFOV_X", Options::Triggerbot::UpperTorsoFOV_X },
        { "UpperTorsoFOV_Y", Options::Triggerbot::UpperTorsoFOV_Y },
        { "LowerTorsoFOV_X", Options::Triggerbot::LowerTorsoFOV_X },
        { "LowerTorsoFOV_Y", Options::Triggerbot::LowerTorsoFOV_Y },
        { "LeftUpperArmFOV_X", Options::Triggerbot::LeftUpperArmFOV_X },
        { "LeftUpperArmFOV_Y", Options::Triggerbot::LeftUpperArmFOV_Y },
        { "LeftLowerArmFOV_X", Options::Triggerbot::LeftLowerArmFOV_X },
        { "LeftLowerArmFOV_Y", Options::Triggerbot::LeftLowerArmFOV_Y },
        { "LeftHandFOV_X", Options::Triggerbot::LeftHandFOV_X },
        { "LeftHandFOV_Y", Options::Triggerbot::LeftHandFOV_Y },
        { "RightUpperArmFOV_X", Options::Triggerbot::RightUpperArmFOV_X },
        { "RightUpperArmFOV_Y", Options::Triggerbot::RightUpperArmFOV_Y },
        { "RightLowerArmFOV_X", Options::Triggerbot::RightLowerArmFOV_X },
        { "RightLowerArmFOV_Y", Options::Triggerbot::RightLowerArmFOV_Y },
        { "RightHandFOV_X", Options::Triggerbot::RightHandFOV_X },
        { "RightHandFOV_Y", Options::Triggerbot::RightHandFOV_Y },
        { "LeftUpperLegFOV_X", Options::Triggerbot::LeftUpperLegFOV_X },
        { "LeftUpperLegFOV_Y", Options::Triggerbot::LeftUpperLegFOV_Y },
        { "LeftLowerLegFOV_X", Options::Triggerbot::LeftLowerLegFOV_X },
        { "LeftLowerLegFOV_Y", Options::Triggerbot::LeftLowerLegFOV_Y },
        { "LeftFootFOV_X", Options::Triggerbot::LeftFootFOV_X },
        { "LeftFootFOV_Y", Options::Triggerbot::LeftFootFOV_Y },
        { "RightUpperLegFOV_X", Options::Triggerbot::RightUpperLegFOV_X },
        { "RightUpperLegFOV_Y", Options::Triggerbot::RightUpperLegFOV_Y },
        { "RightLowerLegFOV_X", Options::Triggerbot::RightLowerLegFOV_X },
        { "RightLowerLegFOV_Y", Options::Triggerbot::RightLowerLegFOV_Y },
        { "RightFootFOV_X", Options::Triggerbot::RightFootFOV_X },
        { "RightFootFOV_Y", Options::Triggerbot::RightFootFOV_Y }
    };

    j["Macro"] = {
        { "Macro Key", Options::Macro::MacroKey },
        { "Toggle Type", Options::Macro::ToggleType },
        { "Enabled", Options::Macro::Enabled },
        { "Delay", Options::Macro::Delay }
    };

    j["Crosshair"] = {
        { "Enabled", Options::Crosshair::Enabled },
        { "Style", Options::Crosshair::Style },
        { "Size", Options::Crosshair::Size },
        { "Gap", Options::Crosshair::Gap },
        { "Thickness", Options::Crosshair::Thickness },
        { "Spin Speed", Options::Crosshair::SpinSpeed },
        { "Gap Speed", Options::Crosshair::GapSpeed },
        { "Gap Tween", Options::Crosshair::GapTween },
        { "Show Text", Options::Crosshair::ShowText },
        { "Color", ToJsonColor(Options::Crosshair::Color, 4) }
    };

    j["Misc"] = {
        { "Bypass", Options::Misc::Bypass },
        { "FOV Enabled", Options::Misc::FOVEnabled },
        { "FOV", Options::Misc::FOV },
        { "Cache NPCs", Options::Misc::CacheNPCs },
        { "Keybind List", Options::Misc::KeybindList },
        { "Keybind List X", Options::Misc::KeybindListX },
        { "Keybind List Y", Options::Misc::KeybindListY },
        { "Stream Proof", Options::Misc::StreamProof },
        { "Menu Key", Options::Misc::MenuKey },
        { "Menu Accent Color", ToJsonColor(Options::Misc::MenuAccentColor, 3) }
    };

    j["HitboxExpander"] = {
        { "Enabled", Options::HitboxExpander::Enabled },
        { "Horizontal Size", Options::HitboxExpander::HorizontalSize },
        { "Vertical Size", Options::HitboxExpander::VerticalSize },
        { "Show Hitbox", Options::HitboxExpander::ShowHitbox },
        { "Transparency", Options::HitboxExpander::HitboxTransparency },
        { "Walk Through", Options::HitboxExpander::WalkThrough }
    };

    j["Fly"] = {
        { "Fly Key", Options::Fly::FlyKey },
        { "Toggle Type", Options::Fly::ToggleType },
        { "Enabled", Options::Fly::Enabled },
        { "Speed", Options::Fly::Speed }
    };

    j["WalkSpeed"] = {
        { "WalkSpeed Key", Options::WalkSpeed::WalkSpeedKey },
        { "Toggle Type", Options::WalkSpeed::ToggleType },
        { "Enabled", Options::WalkSpeed::Enabled },
        { "Speed", Options::WalkSpeed::Speed }
    };

    j["Combat"] = {
        { "Hit Sounds", Options::Combat::HitSounds },
        { "Hit Sound Type", Options::Combat::HitSoundType },
        { "Hit Notifications", Options::Combat::HitNotifications },
        { "Hit Chams", Options::Combat::HitChams },
        { "Hit Effects", Options::Combat::HitEffects },
        { "Hit Chams Duration", Options::Combat::HitChamsDuration },
        { "Hit Effect Duration", Options::Combat::HitEffectDuration },
        { "Min Damage", Options::Combat::MinDamage },
        { "Hit Chams Color", ToJsonColor(Options::Combat::HitChamsColor, 3) },
        { "Hit Effect Color", ToJsonColor(Options::Combat::HitEffectColor, 3) }
    };

    j["World"] = {
        { "Enabled", Options::World::Enabled },
        { "Fullbright", Options::World::Fullbright },
        { "No Fog", Options::World::NoFog },
        { "Fog Start", Options::World::FogStart },
        { "Fog End", Options::World::FogEnd },
        { "Clock Time", Options::World::ClockTime },
        { "Brightness", Options::World::Brightness },
        { "Ambient", ToJsonColor(Options::World::Ambient, 3) },
        { "Outdoor Ambient", ToJsonColor(Options::World::OutdoorAmbient, 3) },
        { "Fog Color", ToJsonColor(Options::World::FogColor, 3) },
        { "Skybox Changer", Options::World::SkyboxChanger },
        { "Skybox Preset", Options::World::SkyboxPreset }
    };

    j["AntiAim"] = {
        { "Enabled", Options::AntiAim::Enabled },
        { "Mode", Options::AntiAim::Mode },
        { "Speed", Options::AntiAim::Speed },
        { "Strength", Options::AntiAim::Strength }
    };

    j["Weather"] = {
        { "Enabled",        Options::Weather::Enabled },
        { "Type",           Options::Weather::Type },
        { "Intensity",      Options::Weather::Intensity },
        { "Speed",          Options::Weather::Speed },
        { "Wind",           Options::Weather::Wind },
        { "Color",          ToJsonColor(Options::Weather::Color, 3) },
        { "SnowSize",       Options::Weather::SnowSize },
        { "RainThickness",  Options::Weather::RainThickness }
    };

    return j;
}

inline void ApplyConfigJson(const json& data)
{
    if (data.is_object() && data.contains("ESP"))
    {
        const auto& esp = data["ESP"];
        LoadVal(esp, "Team Check", Options::ESP::TeamCheck);

        if (esp.contains("Box Type"))
            LoadVal(esp, "Box Type", Options::ESP::BoxType);
        else if (esp.contains("Box"))
        {
            bool oldBox = false;
            LoadVal(esp, "Box", oldBox);
            Options::ESP::BoxType = oldBox ? 1 : 0;
        }

        LoadVal(esp, "Tracers", Options::ESP::Tracers);
        LoadVal(esp, "TracersStart", Options::ESP::TracersStart);
        LoadVal(esp, "Skeleton", Options::ESP::Skeleton);
        LoadVal(esp, "Name", Options::ESP::Name);
        LoadVal(esp, "Distance", Options::ESP::Distance);
        LoadVal(esp, "Health", Options::ESP::Health);
        LoadVal(esp, "Tracer Thickness", Options::ESP::TracerThickness);
        LoadVal(esp, "Head Circles", Options::ESP::HeadCircle);
        LoadVal(esp, "Remove Borders", Options::ESP::RemoveBorders);
        LoadVal(esp, "Headless", Options::ESP::Headless);
        LoadVal(esp, "Head Dot", Options::ESP::HeadDot);
        LoadVal(esp, "Corner ESP", Options::ESP::CornerESP);
        LoadVal(esp, "Health Text", Options::ESP::HealthText);
        LoadVal(esp, "Enemy Health Indicator", Options::ESP::EnemyHealthIndicator);
        LoadVal(esp, "ESP Preview", Options::ESP::ESPPreview);
        LoadVal(esp, "Box Thickness", Options::ESP::BoxThickness);
        LoadVal(esp, "Skeleton Thickness", Options::ESP::SkeletonThickness);
        LoadVal(esp, "3D ESP Thickness", Options::ESP::ESP3DThickness);
        LoadVal(esp, "Head Circle Thickness", Options::ESP::HeadCircleThickness);
        LoadVal(esp, "Head Circle Scale", Options::ESP::HeadCircleScale);
        LoadVal(esp, "Head Circle Max Scale", Options::ESP::HeadCircleMaxScale);
        LoadVal(esp, "Visibility Check", Options::ESP::VisibilityCheck);
        LoadVal(esp, "Visibility Chams", Options::ESP::VisibilityChams);
        LoadVal(esp, "Visibility Max Distance", Options::ESP::VisibilityMaxDistance);
        LoadFloatArray(esp, "Visible Color", Options::ESP::VisibleColor);
        LoadFloatArray(esp, "Hidden Color", Options::ESP::HiddenColor);
        LoadFloatArray(esp, "Name Color", Options::ESP::Color);
        LoadFloatArray(esp, "Box Color", Options::ESP::BoxColor);
        LoadFloatArray(esp, "Corner Color", Options::ESP::CornerColor);
        LoadFloatArray(esp, "Skeleton Color", Options::ESP::SkeletonColor);
        LoadFloatArray(esp, "Distance Color", Options::ESP::DistanceColor);
        LoadFloatArray(esp, "Tracers Color", Options::ESP::TracerColor);
        LoadFloatArray(esp, "3D ESP Color", Options::ESP::ESP3DColor);
        LoadFloatArray(esp, "Head Circles Color", Options::ESP::HeadCircleColor);
        LoadFloatArray(esp, "Head Dot Color", Options::ESP::HeadDotColor);
        LoadFloatArray(esp, "Chams Color", Options::ESP::ChamsColor);
    }

    if (data.is_object() && data.contains("Aimbot"))
    {
        const auto& aim = data["Aimbot"];
        LoadVal(aim, "Aimbot Key", Options::Aimbot::AimbotKey);
        LoadVal(aim, "Aiming Type", Options::Aimbot::AimingType);
        LoadVal(aim, "Toggle Type", Options::Aimbot::ToggleType);
        LoadVal(aim, "Aimbot", Options::Aimbot::Aimbot);
        LoadVal(aim, "Team Check", Options::Aimbot::TeamCheck);
        LoadVal(aim, "Downed Check", Options::Aimbot::DownedCheck);
        LoadVal(aim, "Sticky Aim", Options::Aimbot::StickyAim);
        LoadVal(aim, "Target Bone", Options::Aimbot::TargetBone);
        LoadVal(aim, "Air Target Bone", Options::Aimbot::AirTargetBone);
        LoadVal(aim, "FOV", Options::Aimbot::FOV);
        LoadVal(aim, "Show FOV", Options::Aimbot::ShowFOV);
        LoadVal(aim, "Show FOV Fill", Options::Aimbot::ShowFOVFill);
        LoadVal(aim, "FOV Position Mode", Options::Aimbot::FOVPositionMode);
        LoadVal(aim, "Silent Aim", Options::Aimbot::SilentAim);
        LoadVal(aim, "Silent Aim Mode", Options::Aimbot::SilentAimMode);
        LoadVal(aim, "FOV Thickness", Options::Aimbot::FOVThickness);
        LoadVal(aim, "Smoothness", Options::Aimbot::Smoothness);
        LoadVal(aim, "Smoothness Curve", Options::Aimbot::SmoothnessCurve);
        LoadVal(aim, "Range", Options::Aimbot::Range);
        LoadVal(aim, "Prediction", Options::Aimbot::Prediction);
        LoadVal(aim, "Prediction X", Options::Aimbot::PredictionX);
        LoadVal(aim, "Prediction Y", Options::Aimbot::PredictionY);
        LoadVal(aim, "Shake", Options::Aimbot::Shake);
        LoadVal(aim, "Shake Intensity", Options::Aimbot::ShakeIntensity);
        LoadVal(aim, "Stutter", Options::Aimbot::Stutter);
        LoadVal(aim, "Stutter Ticks", Options::Aimbot::StutterTicks);
        LoadVal(aim, "Custom Curve Enabled", Options::Aimbot::CustomCurveEnabled);
        LoadFloatArray(aim, "FOV Color", Options::Aimbot::FOVColor);
        LoadFloatArray(aim, "FOV Fill Color", Options::Aimbot::FOVFillColor);
        LoadFloatArray(aim, "Custom Curve P1", Options::Aimbot::CustomCurveP1);
        LoadFloatArray(aim, "Custom Curve P2", Options::Aimbot::CustomCurveP2);
    }

    if (data.is_object() && data.contains("Triggerbot"))
    {
        const auto& tb = data["Triggerbot"];
        LoadVal(tb, "Triggerbot Key", Options::Triggerbot::TriggerbotKey);
        LoadVal(tb, "Toggle Type", Options::Triggerbot::ToggleType);
        LoadVal(tb, "Enabled", Options::Triggerbot::Enabled);
        LoadVal(tb, "Team Check", Options::Triggerbot::TeamCheck);
        LoadVal(tb, "Downed Check", Options::Triggerbot::DownedCheck);
        LoadVal(tb, "Radius", Options::Triggerbot::Radius);
        LoadVal(tb, "Range", Options::Triggerbot::Range);
        LoadVal(tb, "Delay", Options::Triggerbot::Delay);
        LoadVal(tb, "Advanced FOV", Options::Triggerbot::AdvancedFOV);
        LoadVal(tb, "Show Advanced FOV", Options::Triggerbot::ShowAdvancedFOV);
        LoadVal(tb, "HeadFOV_X", Options::Triggerbot::HeadFOV_X);
        LoadVal(tb, "HeadFOV_Y", Options::Triggerbot::HeadFOV_Y);
        LoadVal(tb, "TorsoFOV_X", Options::Triggerbot::TorsoFOV_X);
        LoadVal(tb, "TorsoFOV_Y", Options::Triggerbot::TorsoFOV_Y);
        LoadVal(tb, "UpperTorsoFOV_X", Options::Triggerbot::UpperTorsoFOV_X);
        LoadVal(tb, "UpperTorsoFOV_Y", Options::Triggerbot::UpperTorsoFOV_Y);
        LoadVal(tb, "LowerTorsoFOV_X", Options::Triggerbot::LowerTorsoFOV_X);
        LoadVal(tb, "LowerTorsoFOV_Y", Options::Triggerbot::LowerTorsoFOV_Y);
        LoadVal(tb, "LeftUpperArmFOV_X", Options::Triggerbot::LeftUpperArmFOV_X);
        LoadVal(tb, "LeftUpperArmFOV_Y", Options::Triggerbot::LeftUpperArmFOV_Y);
        LoadVal(tb, "LeftLowerArmFOV_X", Options::Triggerbot::LeftLowerArmFOV_X);
        LoadVal(tb, "LeftLowerArmFOV_Y", Options::Triggerbot::LeftLowerArmFOV_Y);
        LoadVal(tb, "LeftHandFOV_X", Options::Triggerbot::LeftHandFOV_X);
        LoadVal(tb, "LeftHandFOV_Y", Options::Triggerbot::LeftHandFOV_Y);
        LoadVal(tb, "RightUpperArmFOV_X", Options::Triggerbot::RightUpperArmFOV_X);
        LoadVal(tb, "RightUpperArmFOV_Y", Options::Triggerbot::RightUpperArmFOV_Y);
        LoadVal(tb, "RightLowerArmFOV_X", Options::Triggerbot::RightLowerArmFOV_X);
        LoadVal(tb, "RightLowerArmFOV_Y", Options::Triggerbot::RightLowerArmFOV_Y);
        LoadVal(tb, "RightHandFOV_X", Options::Triggerbot::RightHandFOV_X);
        LoadVal(tb, "RightHandFOV_Y", Options::Triggerbot::RightHandFOV_Y);
        LoadVal(tb, "LeftUpperLegFOV_X", Options::Triggerbot::LeftUpperLegFOV_X);
        LoadVal(tb, "LeftUpperLegFOV_Y", Options::Triggerbot::LeftUpperLegFOV_Y);
        LoadVal(tb, "LeftLowerLegFOV_X", Options::Triggerbot::LeftLowerLegFOV_X);
        LoadVal(tb, "LeftLowerLegFOV_Y", Options::Triggerbot::LeftLowerLegFOV_Y);
        LoadVal(tb, "LeftFootFOV_X", Options::Triggerbot::LeftFootFOV_X);
        LoadVal(tb, "LeftFootFOV_Y", Options::Triggerbot::LeftFootFOV_Y);
        LoadVal(tb, "RightUpperLegFOV_X", Options::Triggerbot::RightUpperLegFOV_X);
        LoadVal(tb, "RightUpperLegFOV_Y", Options::Triggerbot::RightUpperLegFOV_Y);
        LoadVal(tb, "RightLowerLegFOV_X", Options::Triggerbot::RightLowerLegFOV_X);
        LoadVal(tb, "RightLowerLegFOV_Y", Options::Triggerbot::RightLowerLegFOV_Y);
        LoadVal(tb, "RightFootFOV_X", Options::Triggerbot::RightFootFOV_X);
        LoadVal(tb, "RightFootFOV_Y", Options::Triggerbot::RightFootFOV_Y);
    }

    if (data.is_object() && data.contains("Macro"))
    {
        const auto& mac = data["Macro"];
        LoadVal(mac, "Macro Key", Options::Macro::MacroKey);
        LoadVal(mac, "Toggle Type", Options::Macro::ToggleType);
        LoadVal(mac, "Enabled", Options::Macro::Enabled);
        LoadVal(mac, "Delay", Options::Macro::Delay);
    }

    if (data.is_object() && data.contains("Crosshair"))
    {
        const auto& ch = data["Crosshair"];
        LoadVal(ch, "Enabled", Options::Crosshair::Enabled);
        LoadVal(ch, "Style", Options::Crosshair::Style);
        LoadVal(ch, "Size", Options::Crosshair::Size);
        LoadVal(ch, "Gap", Options::Crosshair::Gap);
        LoadVal(ch, "Thickness", Options::Crosshair::Thickness);
        LoadVal(ch, "Spin Speed", Options::Crosshair::SpinSpeed);
        LoadVal(ch, "Gap Speed", Options::Crosshair::GapSpeed);
        LoadVal(ch, "Gap Tween", Options::Crosshair::GapTween);
        LoadVal(ch, "Show Text", Options::Crosshair::ShowText);
        LoadFloatArray(ch, "Color", Options::Crosshair::Color);
    }

    if (data.is_object() && data.contains("Misc"))
    {
        const auto& ms = data["Misc"];
        LoadVal(ms, "Bypass", Options::Misc::Bypass);
        LoadVal(ms, "FOV Enabled", Options::Misc::FOVEnabled);
        LoadVal(ms, "FOV", Options::Misc::FOV);
        LoadVal(ms, "Cache NPCs", Options::Misc::CacheNPCs);
        LoadVal(ms, "Keybind List", Options::Misc::KeybindList);
        LoadVal(ms, "Keybind List X", Options::Misc::KeybindListX);
        LoadVal(ms, "Keybind List Y", Options::Misc::KeybindListY);
        LoadVal(ms, "Stream Proof", Options::Misc::StreamProof);
        LoadVal(ms, "Menu Key", Options::Misc::MenuKey);
        LoadFloatArray(ms, "Menu Accent Color", Options::Misc::MenuAccentColor);
    }

    if (data.is_object() && data.contains("HitboxExpander"))
    {
        const auto& he = data["HitboxExpander"];
        LoadVal(he, "Enabled", Options::HitboxExpander::Enabled);
        LoadVal(he, "Horizontal Size", Options::HitboxExpander::HorizontalSize);
        LoadVal(he, "Vertical Size", Options::HitboxExpander::VerticalSize);
        LoadVal(he, "Show Hitbox", Options::HitboxExpander::ShowHitbox);
        LoadVal(he, "Transparency", Options::HitboxExpander::HitboxTransparency);
        LoadVal(he, "Walk Through", Options::HitboxExpander::WalkThrough);
    }

    if (data.is_object() && data.contains("Fly"))
    {
        const auto& fl = data["Fly"];
        LoadVal(fl, "Fly Key", Options::Fly::FlyKey);
        LoadVal(fl, "Toggle Type", Options::Fly::ToggleType);
        LoadVal(fl, "Enabled", Options::Fly::Enabled);
        LoadVal(fl, "Speed", Options::Fly::Speed);
    }

    if (data.is_object() && data.contains("WalkSpeed"))
    {
        const auto& ws = data["WalkSpeed"];
        LoadVal(ws, "WalkSpeed Key", Options::WalkSpeed::WalkSpeedKey);
        LoadVal(ws, "Toggle Type", Options::WalkSpeed::ToggleType);
        LoadVal(ws, "Enabled", Options::WalkSpeed::Enabled);
        LoadVal(ws, "Speed", Options::WalkSpeed::Speed);
    }

    if (data.is_object() && data.contains("Combat"))
    {
        const auto& combat = data["Combat"];
        LoadVal(combat, "Hit Sounds", Options::Combat::HitSounds);
        LoadVal(combat, "Hit Sound Type", Options::Combat::HitSoundType);
        LoadVal(combat, "Hit Notifications", Options::Combat::HitNotifications);
        LoadVal(combat, "Hit Chams", Options::Combat::HitChams);
        LoadVal(combat, "Hit Effects", Options::Combat::HitEffects);
        LoadVal(combat, "Hit Chams Duration", Options::Combat::HitChamsDuration);
        LoadVal(combat, "Hit Effect Duration", Options::Combat::HitEffectDuration);
        LoadVal(combat, "Min Damage", Options::Combat::MinDamage);
        LoadFloatArray(combat, "Hit Chams Color", Options::Combat::HitChamsColor);
        LoadFloatArray(combat, "Hit Effect Color", Options::Combat::HitEffectColor);
    }

    if (data.is_object() && data.contains("World"))
    {
        const auto& world = data["World"];
        LoadVal(world, "Enabled", Options::World::Enabled);
        LoadVal(world, "Fullbright", Options::World::Fullbright);
        LoadVal(world, "No Fog", Options::World::NoFog);
        LoadVal(world, "Fog Start", Options::World::FogStart);
        LoadVal(world, "Fog End", Options::World::FogEnd);
        LoadVal(world, "Clock Time", Options::World::ClockTime);
        LoadVal(world, "Brightness", Options::World::Brightness);
        LoadVal(world, "Skybox Changer", Options::World::SkyboxChanger);
        LoadVal(world, "Skybox Preset", Options::World::SkyboxPreset);
        LoadFloatArray(world, "Ambient", Options::World::Ambient);
        LoadFloatArray(world, "Outdoor Ambient", Options::World::OutdoorAmbient);
        LoadFloatArray(world, "Fog Color", Options::World::FogColor);
    }

    if (data.is_object() && data.contains("AntiAim"))
    {
        const auto& aa = data["AntiAim"];
        LoadVal(aa, "Enabled", Options::AntiAim::Enabled);
        LoadVal(aa, "Mode", Options::AntiAim::Mode);
        LoadVal(aa, "Speed", Options::AntiAim::Speed);
        LoadVal(aa, "Strength", Options::AntiAim::Strength);
    }

    if (data.is_object() && data.contains("Weather"))
    {
        const auto& w = data["Weather"];
        LoadVal(w, "Enabled",       Options::Weather::Enabled);
        LoadVal(w, "Type",          Options::Weather::Type);
        LoadVal(w, "Intensity",     Options::Weather::Intensity);
        LoadVal(w, "Speed",         Options::Weather::Speed);
        LoadVal(w, "Wind",          Options::Weather::Wind);
        LoadVal(w, "SnowSize",      Options::Weather::SnowSize);
        LoadVal(w, "RainThickness", Options::Weather::RainThickness);
        LoadFloatArray(w, "Color",   Options::Weather::Color);
    }

    if (Options::Misc::MenuKey == 0)
        Options::Misc::MenuKey = VK_RSHIFT;
}

inline bool SaveConfig(std::string configName)
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    InitializeConfigPaths();
    configName = NormalizeConfigFilename(configName);
    if (configName.empty())
    {
        Config::lastError = "Config name is empty";
        return false;
    }

    if (Globals::configsPath.empty())
    {
        Config::lastError = "Config folder path is not set";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(Globals::configsPath, ec);

    const std::filesystem::path filePath = std::filesystem::path(Globals::configsPath) / configName;
    const std::filesystem::path tempPath = filePath.string() + ".tmp";

    try
    {
        const json j = BuildConfigJson();
        const std::string payload = j.dump(4);

        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                Config::lastError = "Could not write file: " + tempPath.string();
                return false;
            }

            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            out.flush();
            if (!out.good())
            {
                Config::lastError = "Failed while writing file: " + tempPath.string();
                std::filesystem::remove(tempPath, ec);
                return false;
            }
        }

        std::filesystem::rename(tempPath, filePath, ec);
        if (ec)
        {
            std::filesystem::remove(filePath, ec);
            ec.clear();
            std::filesystem::rename(tempPath, filePath, ec);
        }

        if (ec || !std::filesystem::exists(filePath))
        {
            Config::lastError = "Could not finalize config file: " + filePath.string();
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        Config::lastError.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        std::filesystem::remove(tempPath, ec);
        Config::lastError = std::string("Save failed: ") + e.what();
        return false;
    }
}

inline bool LoadConfig(std::string configName)
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    InitializeConfigPaths();
    configName = NormalizeConfigFilename(configName);
    if (configName.empty())
    {
        Config::lastError = "Config name is empty";
        return false;
    }

    const std::filesystem::path filePath = std::filesystem::path(Globals::configsPath) / configName;
    if (!std::filesystem::exists(filePath))
    {
        Config::lastError = "Config not found: " + filePath.string();
        return false;
    }

    json data;
    if (!ReadJsonFile(filePath, data))
        return false;

    try
    {
        ApplyConfigJson(data);
        Config::lastError.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        Config::lastError = std::string("Load failed: ") + e.what();
        return false;
    }
}

struct AutoloadSettings
{
    bool enabled = false;
    std::string configName;
};

inline std::filesystem::path GetSeraphDataPath()
{
    InitializeConfigPaths();
    if (!Globals::configsPath.empty())
        return std::filesystem::path(Globals::configsPath).parent_path();

    char appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
        return std::filesystem::path(appData) / "Seraph";

    if (!Globals::executablePath.empty())
        return std::filesystem::path(Globals::executablePath);

    return {};
}

inline std::filesystem::path GetSettingsFilePath()
{
    return GetSeraphDataPath() / "settings.json";
}

inline AutoloadSettings LoadAutoloadSettings()
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    AutoloadSettings settings;

    const std::filesystem::path settingsPath = GetSettingsFilePath();
    if (std::filesystem::exists(settingsPath))
    {
        json data;
        if (ReadJsonFile(settingsPath, data))
        {
            if (data.contains("autoload") && data["autoload"].is_object())
            {
                const json& autoload = data["autoload"];
                if (autoload.contains("enabled") && autoload["enabled"].is_boolean())
                    settings.enabled = autoload["enabled"].get<bool>();
                if (autoload.contains("config") && autoload["config"].is_string())
                    settings.configName = NormalizeConfigFilename(autoload["config"].get<std::string>());
            }
            return settings;
        }
    }

    // Legacy: autoload default.json if present and no settings file yet
    if (std::filesystem::exists(GetConfigFilePath("default")))
    {
        settings.enabled = true;
        settings.configName = "default.json";
    }

    return settings;
}

inline bool SaveAutoloadSettings(const AutoloadSettings& settings)
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    const std::filesystem::path settingsPath = GetSettingsFilePath();
    const std::filesystem::path dataPath = settingsPath.parent_path();

    if (dataPath.empty())
    {
        Config::lastError = "Could not resolve Seraph data folder";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dataPath, ec);

    json data;
    const std::filesystem::path existingPath = settingsPath;
    if (std::filesystem::exists(existingPath))
    {
        json existing;
        if (ReadJsonFile(existingPath, existing))
            data = std::move(existing);
    }

    std::string configName = settings.configName;
    if (!configName.empty())
        configName = NormalizeConfigFilename(configName);

    data["autoload"] = {
        {"enabled", settings.enabled},
        {"config", configName}
    };

    const std::filesystem::path tempPath = settingsPath.string() + ".tmp";
    try
    {
        const std::string payload = data.dump(4);
        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                Config::lastError = "Could not write settings file";
                return false;
            }
            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            out.flush();
            if (!out.good())
            {
                std::filesystem::remove(tempPath, ec);
                Config::lastError = "Failed while writing settings file";
                return false;
            }
        }

        std::filesystem::rename(tempPath, settingsPath, ec);
        if (ec)
        {
            std::filesystem::remove(settingsPath, ec);
            ec.clear();
            std::filesystem::rename(tempPath, settingsPath, ec);
        }

        if (ec)
        {
            Config::lastError = "Could not finalize settings file";
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        Config::lastError.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        std::filesystem::remove(tempPath, ec);
        Config::lastError = std::string("Save settings failed: ") + e.what();
        return false;
    }
}

inline bool TryLoadAutoloadConfig()
{
    const AutoloadSettings settings = LoadAutoloadSettings();
    if (!settings.enabled || settings.configName.empty())
        return false;

    if (!std::filesystem::exists(GetConfigFilePath(settings.configName)))
        return false;

    return LoadConfig(settings.configName);
}

inline void ClearAutoloadIfMatches(const std::string& deletedConfigName)
{
    AutoloadSettings settings = LoadAutoloadSettings();
    if (settings.configName.empty())
        return;

    if (NormalizeConfigFilename(deletedConfigName) != settings.configName)
        return;

    settings.configName.clear();
    settings.enabled = false;
    SaveAutoloadSettings(settings);
}

// Backward-compatible alias used by the UI
inline bool CreateConfig(std::string configName)
{
    return SaveConfig(std::move(configName));
}

// ----- UTF-8 <-> UTF-16 helpers used by the file dialog (shobjidl.h) -----
inline std::wstring UTF8ToWide(const std::string& in)
{
    if (in.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), (int)in.size(), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.c_str(), (int)in.size(), out.data(), needed);
    return out;
}

inline std::string WideToUTF8(const std::wstring& in)
{
    if (in.empty()) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), (int)in.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, in.c_str(), (int)in.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

// Globally exposed so the file dialog helpers in configs.h can parent the
// OS dialog to the overlay window. Set by ShowImgui once the overlay HWND
// is created and cleared back to nullptr on shutdown. Reading from the
// file dialog is safe because dialogs are only invoked from the UI loop
// while the overlay is alive.
// Globally exposed so renderer.cpp / main loop can publish the overlay HWND
// once the WS_EX_LAYERED window is created. Read by the file-dialog helpers
// to send a foreground-activation nudge before IFileDialog::Show().
inline HWND g_OverlayHWND = nullptr;

// Dedicated owner window used by IFileDialog::Show(). A layered / tool-window
// overlay HWND cannot reliably own a modal dialog on Windows -- WS_EX_LAYERED
// breaks modal focus routing and WS_EX_TOOLWINDOW blocks foreground status,
// so the dialog ends up visually present yet unclickable. To work around
// this, we maintain a separate invisible top-level window:
//   * WS_OVERLAPPED (real top-level window, can own modal dialogs)
//   * NO WS_EX_LAYERED, NO WS_EX_TOOLWINDOW (so focus routes correctly)
//   * minimized + placed off-screen at (32000, 32000) so it's invisible
// Created lazily on the first dialog call and re-used thereafter.
inline HWND g_FileDialogOwnerHWND = nullptr;

inline LRESULT CALLBACK SeraphDialogOwnerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

inline HWND EnsureDialogOwnerWindow()
{
    if (g_FileDialogOwnerHWND && IsWindow(g_FileDialogOwnerHWND))
        return g_FileDialogOwnerHWND;

    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SeraphDialogOwnerWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SeraphDialogOwnerClass";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;
        classRegistered = true;
    }

    // WS_OVERLAPPED + small dimensions + offscreen position. We don't call
    // ShowWindow so it stays invisible; the IFileDialog::Show() call will
    // activate it as the modal owner.
    HWND h = CreateWindowExW(
        0,                              // no ex-style: keep focus-friendly
        L"SeraphDialogOwnerClass",
        L"Seraph Dialog Owner",
        WS_OVERLAPPED,
        0, 0, 1, 1,                     // 1x1 px, off-screen
        nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!h)
        return nullptr;

    // Belt-and-braces: move it to a never-seen coordinate + ensure hidden.
    SetWindowPos(h, nullptr, 32000, 32000, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW | SWP_NOACTIVATE);

    g_FileDialogOwnerHWND = h;
    return h;
}

// ----- Native file dialog (Import / Export) -----
// Opens a Windows file dialog (open or save). Returns true and writes the
// picked file path (UTF-8) into `outPath` on success.
//
// Internally uses IFileDialog (the common base interface) so a single
// code path can dispatch to either CLSID_FileOpenDialog (Import) or
// CLSID_FileSaveDialog (Export). Save mode receives FOS_OVERWRITEPROMPT
// so the OS prompts before overwriting an existing file.
//
// Show() blocks until the user picks a file or cancels. The dialog is
// OWNED by the Seraph overlay HWND (when available) so Windows routes
// focus / mouse input correctly above the WS_EX_LAYERED overlay. We
// also activate the overlay to foreground BEFORE Show() to make sure
// the system treats it as the modal owner (Show() can otherwise push
// the dialog above the TOPMOST overlay without giving it active focus).
//
// The filter string format matches the Win32 OPENFILENAME convention:
// e.g. "*.json\0*.json\0All Files\0*.*\0" (each entry is name\0pattern\0,
// terminated by an extra \0).
inline bool OpenWindowsFileDialog(bool openDialog, std::string& outPath,
                                  const char* fileTypesFilter, const char* title,
                                  const std::string& defaultFileName = std::string())
{
    outPath.clear();

    // Make sure COM is initialised on this thread before touching shell APIs.
    // We tolerate being called from a context where COM apartment was
    // already set (the main UI loop calls CoInitializeEx once at startup).
    HRESULT initHr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool weInitialised = (initHr == S_OK);
    (void)initHr;

    // Use the IFileDialog base interface (shared by both IFileOpenDialog
    // and IFileSaveDialog), then dispatch via the appropriate CLSID so
    // we don't pass invalid flag combinations like FOS_OVERWRITEPROMPT
    // to an open dialog (which returns E_INVALIDARG).
    IFileDialog* pDialog = nullptr;
    REFCLSID clsid = openDialog ? CLSID_FileOpenDialog : CLSID_FileSaveDialog;
    HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDialog));
    if (FAILED(hr) || !pDialog)
    {
        if (weInitialised) CoUninitialize();
        return false;
    }

    DWORD baseFlags = FOS_NOCHANGEDIR | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    // FOS_OVERWRITEPROMPT only meaningful for save dialogs; the open
    // dialog ignores it, but setting it explicitly is benign.
    if (!openDialog)
        baseFlags |= FOS_OVERWRITEPROMPT;

    pDialog->SetOptions(baseFlags);

    // Default the dialog to a sensible starting folder so users don't have
    // to navigate from the OS's last-used folder. For Save mode, start in
    // the user's Documents folder (most common place to dump exported
    // configs). For Open mode, start in the Seraph configs folder itself
    // so users who already have configs can pick them up immediately;
    // fall back to Documents if we can't resolve the configs path.
    if (!openDialog)
    {
        PWSTR docs = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &docs)) && docs)
        {
            IShellItem* pFolder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(docs, NULL, IID_PPV_ARGS(&pFolder)) && pFolder))
            {
                pDialog->SetFolder(pFolder);
                pFolder->Release();
            }
            CoTaskMemFree(docs);
        }
    }
    else
    {
        const std::filesystem::path configsPath = GetConfigFilePath(std::string{}).parent_path();
        std::error_code ec;
        if (!configsPath.empty() && std::filesystem::exists(configsPath, ec) && !ec)
        {
            IShellItem* pFolder = nullptr;
            std::wstring widePath = UTF8ToWide(configsPath.string());
            if (!widePath.empty())
            {
                IShellItem* pFolder = nullptr;
                HRESULT folderHr = SHCreateItemFromParsingName(widePath.c_str(), NULL, IID_PPV_ARGS(&pFolder));
                if (SUCCEEDED(folderHr) && pFolder)
                {
                    pDialog->SetFolder(pFolder);
                    pFolder->Release();
                }
            }
        }
    }

    if (fileTypesFilter && *fileTypesFilter)
    {
        // SetFileTypes retains pointers to the COMDLG_FILTERSPEC structs
        // and the wchar strings inside them. We hand it references into
        // local wstrings that outlive Show().
        std::vector<std::pair<std::wstring, std::wstring>> specs;
        std::string filter = fileTypesFilter;
        size_t pos = 0;
        while (pos < filter.size())
        {
            size_t nul = filter.find('\0', pos);
            if (nul == std::string::npos) break;
            std::string name = filter.substr(pos, nul - pos);
            pos = nul + 1;
            if (pos >= filter.size()) break;
            size_t nul2 = filter.find('\0', pos);
            if (nul2 == std::string::npos) break;
            std::string pattern = filter.substr(pos, nul2 - pos);
            pos = nul2 + 1;
            specs.emplace_back(UTF8ToWide(name), UTF8ToWide(pattern));
        }
        if (!specs.empty())
        {
            std::vector<COMDLG_FILTERSPEC> nativeSpecs;
            nativeSpecs.reserve(specs.size());
            for (auto& s : specs)
                nativeSpecs.push_back({ s.first.c_str(), s.second.c_str() });
            pDialog->SetFileTypes((UINT)nativeSpecs.size(), nativeSpecs.data());
        }
    }

    if (title && *title)
        pDialog->SetTitle(UTF8ToWide(title).c_str());

    if (!defaultFileName.empty() && !openDialog)
        pDialog->SetFileName(UTF8ToWide(defaultFileName).c_str());

    // Choose the dialog owner. The overlay HWND is WS_EX_LAYERED +
    // WS_EX_TOOLWINDOW which cannot reliably own a modal dialog (the OS
    // routes mouse / focus through the dialog visually but the call
    // chain fails to deliver clicks). We fall back to a dedicated,
    // regular WS_OVERLAPPED owner window that is invisible + off-screen.
    // That window is focus-friendly and Windows treats the dialog as
    // truly modal-to-owner, restoring clickability.
    HWND owner = EnsureDialogOwnerWindow();
    if (!owner && g_OverlayHWND && IsWindow(g_OverlayHWND))
        owner = g_OverlayHWND;

    if (owner)
        SetForegroundWindow(owner);

    // While the modal dialog is up, the renderer's ::PeekMessage and
    // ::DispatchMessage keep spinning on the same thread. ImGui's WndProc
    // handler inside the overlay HWND would otherwise compete with the
    // dialog's modal loop for mouse events, which can manifest as the
    // dialog appearing visually on top yet not receiving clicks. We force
    // WS_EX_TRANSPARENT on the overlay for the duration of Show() so the
    // OS mouse-routing ignores the overlay HWND entirely. The previous
    // ex-style is captured and restored after Show() returns so the menu
    // is interactive again immediately when the dialog closes.
    LONG prevExStyle = 0;
    bool toggledOverlayTransparency = false;
    if (g_OverlayHWND && IsWindow(g_OverlayHWND))
    {
        prevExStyle = GetWindowLong(g_OverlayHWND, GWL_EXSTYLE);
        SetWindowLong(g_OverlayHWND, GWL_EXSTYLE, prevExStyle | WS_EX_TRANSPARENT);
        SetWindowPos(g_OverlayHWND, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        toggledOverlayTransparency = true;
    }

    hr = pDialog->Show(owner);

    if (toggledOverlayTransparency)
    {
        SetWindowLong(g_OverlayHWND, GWL_EXSTYLE, prevExStyle);
        SetWindowPos(g_OverlayHWND, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    bool result = false;
    if (hr == S_OK)
    {
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(pDialog->GetResult(&pItem)) && pItem)
        {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
            {
                outPath = WideToUTF8(pszPath);
                CoTaskMemFree(pszPath);
                result = true;
            }
            pItem->Release();
        }
    }

    pDialog->Release();
    if (weInitialised) CoUninitialize();
    return result;
}

// Copy an external config file (.json) into the configs folder. Used by the
// "Import" button which surfaces a Windows file picker so the user can drag
// configs from any folder (Desktop, Downloads, etc.) into Seraph without
// having to copy/paste manually.
inline bool ImportConfigFromFile(const std::filesystem::path& sourcePath)
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    InitializeConfigPaths();

    if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
    {
        Config::lastError = "Source file does not exist";
        return false;
    }

    if (sourcePath.extension() != ".json")
    {
        Config::lastError = "Config files must be .json";
        return false;
    }

    json data;
    if (!ReadJsonFile(sourcePath, data))
        return false;   // lastError already populated by ReadJsonFile

    const std::filesystem::path destPath = GetConfigFilePath(sourcePath.stem().string());
    std::error_code ec;
    std::filesystem::create_directories(destPath.parent_path(), ec);
    ec.clear();

    if (std::filesystem::exists(destPath, ec))
    {
        Config::lastError = "A config with that name already exists";
        return false;
    }

    std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::none, ec);
    if (ec)
    {
        Config::lastError = "Could not copy file: " + ec.message();
        return false;
    }
    return true;
}

// Copy one of the Seraph configs out to an arbitrary destination on disk.
// Used by the "Export" button so users can share configs (e.g. save to
// Desktop / Downloads / a Discord-attached location).
inline bool ExportConfigToFile(const std::string& configName, const std::filesystem::path& destinationPath)
{
    std::lock_guard<std::recursive_mutex> lock(Config::Mutex());
    InitializeConfigPaths();

    const std::filesystem::path srcPath = GetConfigFilePath(configName);
    if (!std::filesystem::exists(srcPath))
    {
        Config::lastError = "Source config does not exist";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destinationPath.parent_path(), ec);
    ec.clear();

    if (std::filesystem::exists(destinationPath, ec))
    {
        Config::lastError = "Destination file already exists";
        return false;
    }

    std::filesystem::copy_file(srcPath, destinationPath, std::filesystem::copy_options::none, ec);
    if (ec)
    {
        Config::lastError = "Could not copy file: " + ec.message();
        return false;
    }
    return true;
}
