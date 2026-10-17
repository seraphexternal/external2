#pragma once

// Diagnostics Tool
// ----------------
// Automatic game compatibility diagnostics: dumps asset IDs, place info,
// offset validation, anti-cheat detection on game join.
// Files include PlaceId in filename so multiple games don't overwrite.

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <Windows.h>

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/SDK/SDK.h"
#include "../Memory/MemoryManager.h"

namespace Diagnostics
{
    // Auto-diagnostics state
    inline std::atomic<bool> g_autoEnabled{true};
    inline std::atomic<int64_t> g_lastPlaceId{0};
    inline std::thread g_diagThread;
    inline std::atomic<bool> g_threadRunning{false};

    // Generate filename with PlaceId to avoid overwrites
    inline std::string MakeFilename(const std::string& base, int64_t placeId)
    {
        if (placeId <= 0) return base;
        size_t dot = base.find_last_of('.');
        if (dot == std::string::npos) return base + "_" + std::to_string(placeId);
        return base.substr(0, dot) + "_" + std::to_string(placeId) + base.substr(dot);
    }

    // Dump asset IDs from workspace (for game-specific ESP/aimbot)
    inline void DumpAssetIds(const std::string& path = "")
    {
        int64_t placeId = Globals::Roblox::lastPlaceID;
        std::string filename = path.empty() ? MakeFilename("seraph_assetids.txt", placeId) : path;
        
        std::ofstream f(filename);
        if (!f) return;

        f << "=== Asset ID Dump ===\n";
        f << "PlaceId: " << placeId << "\n";
        f << "Timestamp: " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n\n";

        if (!Globals::Roblox::Workspace.address) {
            f << "Workspace not found\n";
            return;
        }

        int count = 0;
        auto dumpAssets = [&](auto&& self, const RobloxInstance& inst, int depth) -> void {
            if (!inst.address || depth > 6) return;
            for (auto& child : inst.GetChildrenBulk()) {
                if (!child.address) continue;

                std::string className = child.Class();
                if (className == "MeshPart" || className == "SpecialMesh" ||
                    className == "Part" || className == "UnionOperation" ||
                    className == "Accessory" || className == "Tool" ||
                    className == "Model" || className == "Folder") {

                    const uintptr_t offsets[] = { 0x280, 0x2A0, 0x2C0, 0x1A8, 0x1B0 };
                    for (uintptr_t off : offsets) {
                        uintptr_t val = Memory->read<uintptr_t>(child.address + off);
                        if (val != 0 && val > 1000 && val < 10000000000ULL) {
                            f << child.Class() << " | " << child.Name()
                              << " | 0x" << std::hex << child.address << std::dec
                              << " | AssetId: " << val << " (offset 0x" << std::hex << off << std::dec << ")\n";
                            count++;
                            break;
                        }
                    }
                }
                self(self, child, depth + 1);
            }
        };

        dumpAssets(dumpAssets, Globals::Roblox::Workspace, 0);
        f << "\nTotal assets found: " << count << "\n";
        f << "=== End ===\n";
    }

    // Dump full environment info
    inline void DumpEnvironment(const std::string& path = "")
    {
        int64_t placeId = Globals::Roblox::lastPlaceID;
        std::string filename = path.empty() ? MakeFilename("seraph_env.txt", placeId) : path;
        
        std::ofstream f(filename);
        if (!f) return;

        f << "=== Game Environment ===\n";
        f << "PlaceId: " << Globals::Roblox::lastPlaceID << "\n";
        f << "GameName: " << Globals::Roblox::gameName << "\n";
        f << "Timestamp: " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n\n";

        if (!Globals::Roblox::Workspace.address) {
            f << "Workspace not found\n";
            return;
        }

        // Anti-cheat detection
        auto ac = Globals::Roblox::Workspace.FindFirstChild("AntiCheat");
        if (ac.address) f << "Found: Workspace.AntiCheat\n";

        auto ss = Globals::Roblox::Workspace.FindFirstChild("ServerScriptService");
        if (ss.address) {
            for (auto& child : ss.GetChildrenBulk()) {
                std::string name = child.Name();
                if (name.find("Anti") != std::string::npos || name.find("Cheat") != std::string::npos)
                    f << "Suspicious script: " << name << "\n";
            }
        }

        auto rs = Globals::Roblox::DataModel.FindFirstChildWhichIsA("ReplicatedStorage");
        if (rs.address) {
            for (auto& child : rs.GetChildrenBulk()) {
                std::string name = child.Name();
                if (name.find("Anti") != std::string::npos || name.find("Cheat") != std::string::npos ||
                    name.find("Krnl") != std::string::npos || name.find("Byfron") != std::string::npos)
                    f << "ReplicatedStorage suspicious: " << name << "\n";
            }
        }

        // Memory stats
        f << "\nProcess ID: " << Memory->getProcessId() << "\n";
        f << "Base address: 0x" << std::hex << Memory->getBaseAddress() << std::dec << "\n";
        f << "\n=== End ===\n";
    }

    // Auto-diagnostics background thread
    inline void StartAutoDiagnostics()
    {
        if (g_threadRunning.exchange(true)) return;
        
        g_diagThread = std::thread([]() {
            int64_t lastSeenPlaceId = 0;
            int frameCount = 0;
            
            while (g_threadRunning.load() && g_autoEnabled.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                if (!Globals::Roblox::LocalPlayer.address) continue;
                auto character = Globals::Roblox::LocalPlayer.Character();
                if (!character.address) continue;

                int64_t currentPlaceId = Globals::Roblox::lastPlaceID;
                
                // Dump on first run or place change
                if (frameCount == 0 || currentPlaceId != lastSeenPlaceId) {
                    DumpAssetIds();
                    DumpEnvironment();
                    lastSeenPlaceId = currentPlaceId;
                }
                
                frameCount++;
            }
        });
        g_diagThread.detach();
    }

    inline void StopAutoDiagnostics()
    {
        g_threadRunning.store(false);
        if (g_diagThread.joinable()) g_diagThread.join();
    }

    // Manual triggers (for buttons if needed)
    inline void DumpAssetIdsManual() { DumpAssetIds(); }
    inline void DumpEnvironmentManual() { DumpEnvironment(); }
}