#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>

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