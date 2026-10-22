#pragma once

// Game Profile System
// -------------------
// Loads dumped asset IDs and environment data to provide game-specific
// ESP/aimbot configurations per PlaceId.

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <optional>
#include <functional>

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/SDK/SDK.h"

namespace GameProfile
{
    struct AssetEntry
    {
        std::string className;
        std::string name;
        uintptr_t address;
        uintptr_t assetId;
        uintptr_t offset;
    };

    struct GameConfig
    {
        int64_t placeId = 0;
        std::string gameName;
        std::vector<AssetEntry> assets;
        
        // ESP settings per asset type
        struct ESPConfig
        {
            bool enabled = true;
            ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            bool showName = true;
            bool showDistance = true;
            bool showHealth = true;
            float maxDistance = 500.0f;
        };
        std::unordered_map<std::string, ESPConfig> espConfigs;

        // Aimbot settings per asset type
        struct AimbotConfig
        {
            bool enabled = true;
            float fov = 10.0f;
            float smoothness = 5.0f;
            std::vector<std::string> targetParts;
            int priority = 0;
        };
        std::unordered_map<std::string, AimbotConfig> aimbotConfigs;

        // General settings
        bool enabled = true;
        bool useAssetIdsForESP = true;
        bool useAssetIdsForAimbot = true;
    };

    // Global game profile registry
    inline std::unordered_map<int64_t, GameConfig> g_gameConfigs;
    inline GameConfig* g_currentConfig = nullptr;
    inline std::string g_profilesDir = "";
    inline std::atomic<bool> g_loaded = {false};

    // Initialize profiles directory
    inline void Initialize()
    {
        char exeDir[MAX_PATH];
        GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
        std::string dir = std::string(exeDir).substr(0, std::string(exeDir).find_last_of('\\') + 1);
        g_profilesDir = dir + "gameprofiles\\";
        std::filesystem::create_directory(g_profilesDir);
    }

    // Load all game profiles from disk
    inline void LoadAllProfiles()
    {
        if (g_loaded.exchange(true)) return;
        
        for (const auto& entry : std::filesystem::directory_iterator(g_profilesDir))
        {
            if (entry.path().extension() == ".json")
            {
                try
                {
                    std::ifstream f(entry.path());
                    if (!f) continue;
                }
                catch (...)
                {
                }
            }
        }
    }

    // Generate filename with PlaceId to avoid overwrites
    inline std::string MakeFilename(const std::string& base, int64_t placeId)
    {
        if (placeId <= 0) return base;
        size_t dot = base.find_last_of('.');
        if (dot == std::string::npos) return base + "_" + std::to_string(placeId);
        return base.substr(0, dot) + "_" + std::to_string(placeId) + base.substr(dot);
    }

    // Load asset IDs from dumped text file
    inline bool LoadAssetIdsFromDump(int64_t placeId, GameConfig& config)
    {
        std::string filename = "seraph_assetids_" + std::to_string(placeId) + ".txt";
        std::string path = g_profilesDir + filename;
        
        std::ifstream f(path);
        if (!f) return false;

        config.placeId = placeId;
        config.assets.clear();

        std::string line;
        bool inData = false;
        while (std::getline(f, line))
        {
            if (line.find("PlaceId:") != std::string::npos)
            {
                config.placeId = std::stoll(line.substr(line.find(":") + 1));
                continue;
            }
            if (line.find("===") != std::string::npos)
            {
                inData = true;
                continue;
            }
            if (!inData || line.empty()) continue;

            // Parse: Class | Name | Address | AssetId: X (offset 0xOFF)
            size_t pos1 = line.find(" | ");
            if (pos1 == std::string::npos) continue;
            std::string className = line.substr(0, pos1);
            
            size_t pos2 = line.find(" | ", pos1 + 3);
            if (pos2 == std::string::npos) continue;
            std::string name = line.substr(pos1 + 3, pos2 - pos1 - 3);
            
            size_t pos3 = line.find(" | ", pos2 + 3);
            if (pos3 == std::string::npos) continue;
            std::string addrStr = line.substr(pos2 + 3, pos3 - pos2 - 3);
            
            size_t assetPos = line.find("AssetId: ");
            if (assetPos == std::string::npos) continue;
            size_t assetEnd = line.find(" (offset", assetPos);
            if (assetEnd == std::string::npos) continue;
            std::string assetIdStr = line.substr(assetPos + 9, assetEnd - assetPos - 9);
            
            size_t offsetPos = line.find("offset 0x", assetEnd);
            if (offsetPos == std::string::npos) continue;
            size_t offsetEnd = line.find(")", offsetPos);
            if (offsetEnd == std::string::npos) continue;
            std::string offsetStr = line.substr(offsetPos + 9, offsetEnd - offsetPos - 9);

            AssetEntry entry;
            entry.className = className;
            entry.name = name;
            entry.address = std::stoull(addrStr.substr(2), nullptr, 16);
            entry.assetId = std::stoull(assetIdStr);
            entry.offset = std::stoull(offsetStr, nullptr, 16);

            config.assets.push_back(entry);
        }

        return !config.assets.empty();
    }

    // Load environment info
    inline bool LoadEnvironmentFromDump(int64_t placeId, GameConfig& config)
    {
        std::string filename = "seraph_env_" + std::to_string(placeId) + ".txt";
        std::string path = g_profilesDir + filename;
        
        std::ifstream f(path);
        if (!f) return false;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.find("GameName:") != std::string::npos)
            {
                config.gameName = line.substr(line.find(":") + 1);
                if (!config.gameName.empty() && config.gameName[0] == ' ')
                    config.gameName = config.gameName.substr(1);
            }
        }

        return true;
    }

    // Get or create config for current place
    inline GameConfig* GetCurrentConfig()
    {
        if (!g_currentConfig && Globals::Roblox::lastPlaceID > 0)
        {
            auto& config = g_gameConfigs[Globals::Roblox::lastPlaceID];
            if (config.assets.empty())
            {
                LoadAssetIdsFromDump(Globals::Roblox::lastPlaceID, config);
                LoadEnvironmentFromDump(Globals::Roblox::lastPlaceID, config);
            }
            g_currentConfig = &config;
        }
        return g_currentConfig;
    }

    // Get ESP config for an asset
    inline const GameConfig::ESPConfig* GetESPConfig(const std::string& className, const std::string& name)
    {
        auto config = GetCurrentConfig();
        if (!config) return nullptr;

        auto it = config->espConfigs.find(name);
        if (it != config->espConfigs.end()) return &it->second;

        it = config->espConfigs.find(className);
        if (it != config->espConfigs.end()) return &it->second;

        static GameConfig::ESPConfig defaultConfig;
        return &defaultConfig;
    }

    // Get Aimbot config for an asset
    inline const GameConfig::AimbotConfig* GetAimbotConfig(const std::string& className, const std::string& name)
    {
        auto config = GetCurrentConfig();
        if (!config) return nullptr;

        auto it = config->aimbotConfigs.find(name);
        if (it != config->aimbotConfigs.end()) return &it->second;

        it = config->aimbotConfigs.find(className);
        if (it != config->aimbotConfigs.end()) return &it->second;

        static GameConfig::AimbotConfig defaultConfig;
        return &defaultConfig;
    }

    // Check if an instance matches a known asset
    inline bool IsKnownAsset(uintptr_t assetId, const std::string& className, const std::string& name, AssetEntry* outEntry = nullptr)
    {
        auto config = GetCurrentConfig();
        if (!config) return false;

        for (const auto& asset : config->assets)
        {
            if (asset.assetId == assetId && 
                (asset.className == className || asset.name == name))
            {
                if (outEntry) *outEntry = asset;
                return true;
            }
        }
        return false;
    }

    // Get best aimbot target from known assets
    inline std::optional<RobloxInstance> GetBestAimbotTarget()
    {
        auto config = GetCurrentConfig();
        if (!config || !config->enabled || !config->useAssetIdsForAimbot)
            return std::nullopt;

        if (!Globals::Roblox::LocalPlayer.address) return std::nullopt;
        auto localChar = Globals::Roblox::LocalPlayer.Character();
        if (!localChar.address) return std::nullopt;

        auto localHRP = localChar.FindFirstChild("HumanoidRootPart");
        if (!localHRP.address) return std::nullopt;

        std::optional<RobloxInstance> bestTarget;
        float bestFOV = FLT_MAX;
        int bestPriority = -1;

        if (!Globals::Roblox::Workspace.address) return std::nullopt;

        // Scan workspace for known assets
        std::function<void(const RobloxInstance&, int)> scanForTargets = [&](const RobloxInstance& inst, int depth) {
            if (!inst.address || depth > 4) return;
            for (auto& child : inst.GetChildrenBulk())
            {
                if (!child.address) continue;

                AssetEntry assetEntry;
                if (IsKnownAsset(0, child.Class(), child.Name(), &assetEntry))
                {
                    const auto* aimConfig = GetAimbotConfig(child.Class(), child.Name());
                    if (aimConfig && aimConfig->enabled)
                    {
                        auto humanoid = child.FindFirstChildWhichIsA("Humanoid");
                        if (humanoid.address)
                        {
                            if (Options::Aimbot::TeamCheck)
                            {
                                // Add team check logic here
                            }

                            auto targetPos = child.Position();
                            auto localPos = localHRP.Position();
                            
                            float dist = (targetPos - localPos).Magnitude();
                            if (dist < aimConfig->fov && dist < bestFOV)
                            {
                                if (aimConfig->priority > bestPriority)
                                {
                                    bestFOV = dist;
                                    bestPriority = aimConfig->priority;
                                    bestTarget = child;
                                }
                            }
                        }
                    }
                }
                scanForTargets(child, depth + 1);
            };
        };

        scanForTargets(Globals::Roblox::Workspace, 0);
        return bestTarget;
    }

    // Menu UI for game profile settings
    inline void RenderMenu()
    {
        auto config = GetCurrentConfig();
        if (!config) return;

        ImGui::Text("Game Profile: %s (PlaceId: %lld)", 
            config->gameName.empty() ? "Unknown" : config->gameName.c_str(),
            config->placeId);
        ImGui::Text("Assets loaded: %zu", config->assets.size());
        ImGui::Separator();

        ImGui::Checkbox("Enable Game Profile", &config->enabled);
        ImGui::Checkbox("Use Asset IDs for ESP", &config->useAssetIdsForESP);
        ImGui::Checkbox("Use Asset IDs for Aimbot", &config->useAssetIdsForAimbot);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Asset ESP Configs"))
        {
            for (auto& [name, esp] : config->espConfigs)
            {
                if (ImGui::TreeNode(name.c_str()))
                {
                    ImGui::Checkbox("Enabled", &esp.enabled);
                    ImGui::ColorEdit4("Color", &esp.color.x);
                    ImGui::Checkbox("Show Name", &esp.showName);
                    ImGui::Checkbox("Show Distance", &esp.showDistance);
                    ImGui::Checkbox("Show Health", &esp.showHealth);
                    ImGui::SliderFloat("Max Distance", &esp.maxDistance, 10.0f, 2000.0f);
                    ImGui::TreePop();
                }
            }
        }

        if (ImGui::CollapsingHeader("Asset Aimbot Configs"))
        {
            for (auto& [name, aim] : config->aimbotConfigs)
            {
                if (ImGui::TreeNode(name.c_str()))
                {
                    ImGui::Checkbox("Enabled", &aim.enabled);
                    ImGui::SliderFloat("FOV", &aim.fov, 1.0f, 180.0f);
                    ImGui::SliderFloat("Smoothness", &aim.smoothness, 1.0f, 20.0f);
                    ImGui::InputInt("Priority", &aim.priority);
                    ImGui::TreePop();
                }
            }
        }

        if (ImGui::Button("Reload Current Profile"))
        {
            if (Globals::Roblox::lastPlaceID > 0)
            {
                auto& config = g_gameConfigs[Globals::Roblox::lastPlaceID];
                config.assets.clear();
                LoadAssetIdsFromDump(Globals::Roblox::lastPlaceID, config);
                LoadEnvironmentFromDump(Globals::Roblox::lastPlaceID, config);
            }
        }
    }
}