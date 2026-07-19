#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <algorithm>

#include "../../Memory/MemoryManager.h"
#include "../SDK/sdk.h"

struct RobloxPlayer
{
    uintptr_t address = 0;
    int RigType = 0;
    std::string Name = "";
    float Health = 0.0f;
    float MaxHealth = 0.0f;
    RobloxInstance Team = RobloxInstance(0);
    RobloxInstance Character = RobloxInstance(0);
    RobloxInstance Humanoid = RobloxInstance(0);
    RobloxInstance Head = RobloxInstance(0);
    RobloxInstance HumanoidRootPart = RobloxInstance(0);
    RobloxInstance Left_Arm = RobloxInstance(0);
    RobloxInstance Left_Leg = RobloxInstance(0);
    RobloxInstance Right_Arm = RobloxInstance(0);
    RobloxInstance Right_Leg = RobloxInstance(0);
    RobloxInstance Torso = RobloxInstance(0);
    RobloxInstance Upper_Torso = RobloxInstance(0);
    RobloxInstance Lower_Torso = RobloxInstance(0);
    RobloxInstance Right_Upper_Arm = RobloxInstance(0);
    RobloxInstance Right_Lower_Arm = RobloxInstance(0);
    RobloxInstance Right_Hand = RobloxInstance(0);
    RobloxInstance Left_Upper_Arm = RobloxInstance(0);
    RobloxInstance Left_Lower_Arm = RobloxInstance(0);
    RobloxInstance Left_Hand = RobloxInstance(0);
    RobloxInstance Right_Upper_Leg = RobloxInstance(0);
    RobloxInstance Right_Lower_Leg = RobloxInstance(0);
    RobloxInstance Right_Foot = RobloxInstance(0);
    RobloxInstance Left_Upper_Leg = RobloxInstance(0);
    RobloxInstance Left_Lower_Leg = RobloxInstance(0);
    RobloxInstance Left_Foot = RobloxInstance(0);
    int TeamColor = 0;
    int TeamBrickColor = 0;
    std::string TeamName = "";
    std::string ToolName = "";
    Vectors::Vector3 Velocity = {0.f, 0.f, 0.f};
};

namespace Globals
{
    namespace Roblox
    {
        inline RobloxInstance DataModel(0);
        inline uintptr_t VisualEngine;
        inline RobloxInstance Workspace(0);
        inline RobloxInstance Players(0);
        inline RobloxInstance Camera(0);
        inline RobloxInstance LocalPlayer(0);
        inline RobloxInstance LocalPlayerTeam(0);
        inline int LocalPlayerTeamColor = 0;
        inline int LocalPlayerTeamBrickColor = 0;
        inline std::string LocalPlayerTeamName = "";
        inline int lastPlaceID;
        inline std::string gameName = "Unknown";
        inline bool isPhantomForces = false;
        inline constexpr int PHANTOM_FORCES_ID = 113491250;
		inline bool isRivals = false;
		inline constexpr int RIVALS_ID = (int)17625359962;
		inline bool isOverkill = false;
		inline constexpr int OVERKILL_ID = (int)124842176624983;
    }
    namespace Viewport
    {
        inline Matrixes::Matrix4 ViewMatrix;
        inline Vectors::Vector2 Dimensions;
        inline POINT ScreenPos;
        inline HWND RobloxHWND = nullptr;
        inline bool Valid = false;

        inline void Update()
        {
            if (!RobloxHWND || !IsWindow(RobloxHWND))
            {
                RobloxHWND = FindWindowA(NULL, "Roblox");
                if (!RobloxHWND)
                {
                    DWORD pid = (Memory ? (DWORD)Memory->getProcessId() : 0);
                    if (pid != 0)
                    {
                        HWND found = nullptr;
                        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                            DWORD wpid = 0;
                            GetWindowThreadProcessId(hwnd, &wpid);
                            if (wpid == (DWORD)lParam && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
                                *reinterpret_cast<HWND*>(lParam) = hwnd;
                                return FALSE;
                            }
                            return TRUE;
                        }, reinterpret_cast<LPARAM>(&found));
                        if (found) RobloxHWND = found;
                    }
                }
            }

            if (RobloxHWND)
            {
                RECT clientRect;
                GetClientRect(RobloxHWND, &clientRect);
                Dimensions = { (float)clientRect.right, (float)clientRect.bottom };

                ScreenPos = { 0, 0 };
                ClientToScreen(RobloxHWND, &ScreenPos);

                if (Roblox::VisualEngine != 0)
                {
                    ViewMatrix = Memory->read<Matrixes::Matrix4>(Roblox::VisualEngine + Offsets::VisualEngine::ViewMatrix);
                    Valid = true;
                }
                else
                {
                    Valid = false;
                }
            }
            else
            {
                Valid = false;
            }
        }
    }
    namespace DynamicOffsets
    {
        inline uintptr_t PlayerTeam = 0;
    }

    namespace Caches
    {
        inline std::vector<RobloxInstance> CachedPlayers;
        inline std::vector<RobloxPlayer> CachedPlayerObjects;
        inline bool forceRefresh = false;
        inline bool playerCacheNeedsRefresh = false;
        inline bool playerObjectsCacheNeedsRefresh = false;
        inline std::unordered_map<uintptr_t, RobloxInstance> CharacterFallbackCache;
    }
    inline std::string executablePath;
    inline std::string configsPath;
    inline uintptr_t ResolveTeamOffset()
    {
        if (DynamicOffsets::PlayerTeam != 0)
            return DynamicOffsets::PlayerTeam;

        if (Globals::Roblox::DataModel.address == 0 || Globals::Roblox::Players.address == 0)
            return 0;

        auto teamsService = Globals::Roblox::DataModel.FindFirstChild("Teams");
        if (teamsService.address == 0)
            return 0;

        auto teams = teamsService.GetChildren();
        if (teams.empty())
            return 0;

        std::vector<uintptr_t> teamAddresses;
        for (const auto& t : teams)
            teamAddresses.push_back(t.address);

        auto players = Globals::Roblox::Players.GetChildren();
        for (const auto& player : players)
        {
            if (player.address == 0) continue;

            // Scan player struct for a pointer to a team instance
            for (uintptr_t offset = 0x100; offset <= 0x400; offset += 8)
            {
                uintptr_t ptr = Memory->read<uintptr_t>(player.address + offset);
                for (uintptr_t tAddr : teamAddresses)
                {
                    if (ptr == tAddr)
                    {
                        DynamicOffsets::PlayerTeam = offset;
                        return offset;
                    }
                }
            }
        }

        return 0;
    }

    inline bool running = true;
    inline std::atomic<bool> overlayDone = true;

    namespace HitSounds
    {
        inline std::vector<std::string> Files;
        inline std::string FolderPath;

        inline void ScanFolder()
        {
            Files.clear();
            if (FolderPath.empty()) return;

            WIN32_FIND_DATAA findData;
            std::string searchPath = FolderPath + "\\*";
            HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
            if (hFind == INVALID_HANDLE_VALUE) return;

            do
            {
                std::string name = findData.cFileName;
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

                if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".wav")
                {
                    Files.push_back(name);
                }
            } while (FindNextFileA(hFind, &findData));

            FindClose(hFind);

            std::sort(Files.begin(), Files.end());
        }
    }
}

inline bool IsTeammate(const RobloxPlayer& player)
{
    if (Globals::Roblox::LocalPlayer.address == 0 || player.address == 0)
        return false;

    if (player.address == Globals::Roblox::LocalPlayer.address)
        return true;

    // Prioritize Player.TeamColor as it's not encrypted and very reliable in games like Arsenal.
    // Mask with 0xFFFF because BrickColor is a 16-bit integer, preventing garbage in upper bits.
    auto localPlayerColor = Globals::Roblox::LocalPlayerTeamColor & 0xFFFF;
    auto playerPlayerColor = player.TeamColor & 0xFFFF;
    
    if (localPlayerColor == playerPlayerColor && localPlayerColor != 194 && localPlayerColor != 0 && localPlayerColor != 0xFFFF)
    {
        return true;
    }

    // If TeamColor didn't match (or was neutral), try Team object comparison.
    // Note: Team pointers can be encrypted in some games, causing false negative matches.
    auto localTeam = Globals::Roblox::LocalPlayerTeam;
    auto playerTeam = player.Team;

    if (localTeam.address != 0 && playerTeam.address != 0)
    {
        // If team color is neutral, treat as FFA (enemies)
        if (Globals::Roblox::LocalPlayerTeamBrickColor == 194 || Globals::Roblox::LocalPlayerTeamBrickColor == 0 || Globals::Roblox::LocalPlayerTeamBrickColor == -1)
            return false;

        if (localTeam.address == playerTeam.address)
            return true;
            
        if (!Globals::Roblox::LocalPlayerTeamName.empty() && Globals::Roblox::LocalPlayerTeamName == player.TeamName)
            return true;
            
        if (Globals::Roblox::LocalPlayerTeamBrickColor != 0 && Globals::Roblox::LocalPlayerTeamBrickColor != 194 && Globals::Roblox::LocalPlayerTeamBrickColor != -1 && 
            Globals::Roblox::LocalPlayerTeamBrickColor == player.TeamBrickColor)
            return true;
            
        // If pointers were valid but didn't match anything, they are enemies.
        return false;
    }
    else if (localTeam.address != 0 || playerTeam.address != 0)
    {
        return false; // One has a team, the other doesn't
    }

    return false;
}

#include <algorithm>
#include <string>

inline RobloxInstance FindPlayerByName(const std::string& name)
{
    if (name.empty()) return RobloxInstance(0);
    
    auto playersService = Globals::Roblox::Players;
    if (!playersService.address) return RobloxInstance(0);
    
    std::string searchName = name;
    std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);
    
    for (auto& child : playersService.GetChildren())
    {
        std::string childName = child.Name();
        std::string lowerChildName = childName;
        std::transform(lowerChildName.begin(), lowerChildName.end(), lowerChildName.begin(), ::tolower);
        if (lowerChildName.find(searchName) != std::string::npos)
        {
            return child;
        }
    }
    return RobloxInstance(0);
}

inline RobloxInstance FindCharacterInWorkspace(const std::string& playerName)
{
    auto workspace = Globals::Roblox::Workspace;
    if (!workspace.address) return RobloxInstance(0);

    auto children = workspace.GetChildren();
    for (auto& child : children)
    {
        if (!child.address) continue;
        std::string childClass = child.Class();

        auto checkModel = [&](RobloxInstance& model) -> RobloxInstance
        {
            if (!model.address) return RobloxInstance(0);
            std::string modelName = model.Name();
            if (modelName != playerName) return RobloxInstance(0);
            auto humanoid = model.FindFirstChildWhichIsA("Humanoid");
            if (!humanoid.address) return RobloxInstance(0);
            auto hrp = model.FindFirstChild("HumanoidRootPart");
            if (!hrp.address) return RobloxInstance(0);
            return model;
        };

        if (childClass == "Model")
        {
            auto result = checkModel(child);
            if (result.address) return result;
        }

        if (childClass == "Folder" || childClass == "Model")
        {
            auto grandchildren = child.GetChildren();
            for (auto& gc : grandchildren)
            {
                if (!gc.address) continue;
                std::string gcClass = gc.Class();

                if (gcClass == "Model")
                {
                    auto result = checkModel(gc);
                    if (result.address) return result;
                }

                if (gcClass == "Folder" || gcClass == "Model")
                {
                    auto ggchildren = gc.GetChildren();
                    for (auto& ggc : ggchildren)
                    {
                        if (!ggc.address) continue;
                        if (ggc.Class() == "Model")
                        {
                            auto result = checkModel(ggc);
                            if (result.address) return result;
                        }
                    }
                }
            }
        }
    }
    return RobloxInstance(0);
}

struct FoundCharacter
{
    RobloxInstance model;
    RobloxInstance humanoid;
    RobloxInstance hrp;
};

inline std::vector<FoundCharacter> FindAllCharactersInWorkspace()
{
    std::vector<FoundCharacter> results;
    auto workspace = Globals::Roblox::Workspace;
    if (!workspace.address) return results;

    std::function<void(RobloxInstance&)> search = [&](RobloxInstance& inst) {
        auto children = inst.GetChildren();
        for (auto& child : children)
        {
            if (!child.address) continue;
            std::string cls = child.Class();

            if (cls == "Model")
            {
                auto humanoid = child.FindFirstChildWhichIsA("Humanoid");
                auto hrp = child.FindFirstChild("HumanoidRootPart");
                if (humanoid.address && hrp.address)
                {
                    results.push_back({ child, humanoid, hrp });
                }
            }

            auto grandChildren = child.GetChildren();
            if (!grandChildren.empty())
            {
                search(child);
            }
        }
    };

    search(workspace);
    return results;
}

inline void PopulateCharacterFallbacks()
{
    auto& cache = Globals::Caches::CharacterFallbackCache;
    cache.clear();

    auto players = Globals::Roblox::Players.GetChildren();
    if (players.empty()) return;

    auto characters = FindAllCharactersInWorkspace();
    if (characters.empty()) return;

    std::vector<uintptr_t> playerAddrs;
    for (auto& p : players)
        playerAddrs.push_back(p.address);

    for (auto& ch : characters)
    {
        // Strategy 1: Scan character Model's children for an ObjectValue referencing a Player
        auto chChildren = ch.model.GetChildren();
        for (auto& child : chChildren)
        {
            if (!child.address) continue;
            std::string cls = child.Class();
            if (cls == "ObjectValue" || cls == "ValueBase")
            {
                uintptr_t val = Memory->read<uintptr_t>(child.address + 0x78);
                for (size_t i = 0; i < playerAddrs.size(); i++)
                {
                    if (val == playerAddrs[i] && !cache.count(playerAddrs[i]))
                    {
                        cache[playerAddrs[i]] = ch.model;
                        goto next_character;
                    }
                }
            }
        }

        // Strategy 2: Scan Humanoid memory for Player pointers
        for (uintptr_t off = 0x80; off <= 0x300; off += 0x8)
        {
            uintptr_t val = Memory->read<uintptr_t>(ch.humanoid.address + off);
            for (size_t i = 0; i < playerAddrs.size(); i++)
            {
                if (val == playerAddrs[i] && !cache.count(playerAddrs[i]))
                {
                    cache[playerAddrs[i]] = ch.model;
                    goto next_character;
                }
            }
        }

        // Strategy 3: Scan character Model's own memory for Player pointers
        for (uintptr_t off = 0x80; off <= 0x300; off += 0x8)
        {
            uintptr_t val = Memory->read<uintptr_t>(ch.model.address + off);
            for (size_t i = 0; i < playerAddrs.size(); i++)
            {
                if (val == playerAddrs[i] && !cache.count(playerAddrs[i]))
                {
                    cache[playerAddrs[i]] = ch.model;
                    goto next_character;
                }
            }
        }

    next_character:;
    }
}

inline RobloxInstance ResolveCharacterFallback(uintptr_t playerAddress)
{
    auto& cache = Globals::Caches::CharacterFallbackCache;
    auto it = cache.find(playerAddress);
    if (it != cache.end()) return it->second;
    return RobloxInstance(0);
}
