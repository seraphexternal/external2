#pragma once
#include <thread>
#include "../globals/options.h"
#include "../../rbx/globals/globals.h"

inline void CachePlayerObjects()
{
	std::vector<RobloxPlayer> tempList;

	while (Globals::running)
	{
		tempList.clear();

		if (Globals::Caches::CachedPlayers.empty())
		{
			// Sleep in small increments to allow instant response to forceRefresh
			for (int i = 0; i < 5 && Globals::running; i++)
			{
				if (Globals::Caches::forceRefresh)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			continue;
		}

		for (auto& player : Globals::Caches::CachedPlayers)
		{
			RobloxPlayer p;

			if (!player || player.address == 0)
				continue;

			p.address = player.address;

			// Check if this is a Player object or a Model (NPC)
			std::string className = player.Class();
			bool isNPC = (className == "Model");

			if (isNPC)
			{
				// For NPCs (models), the object IS the character
				p.Name = player.Name();
				p.Team = RobloxInstance(0);
				p.TeamColor = 0;
				p.TeamBrickColor = 0;
				p.TeamName = "";
				p.Character = player;
				p.Humanoid = p.Character.FindFirstChildWhichIsA("Humanoid");
				if (!p.Humanoid.address)
					continue;
				p.Health = Memory->read<float>(p.Humanoid.address + Offsets::Humanoid::Health);
				p.MaxHealth = Memory->read<float>(p.Humanoid.address + Offsets::Humanoid::MaxHealth);
			}
			else
			{
				// For real players
				p.Name = player.Name();
				p.Team = player.Team();
				p.TeamColor = Memory->read<int>(player.address + Offsets::Player::TeamColor);
				if (p.Team.address != 0)
				{
					p.TeamName = p.Team.Name();
					p.TeamBrickColor = Memory->read<int>(p.Team.address + Offsets::Team::BrickColor);
				}
				else
				{
					p.TeamName = "";
					p.TeamBrickColor = 0;
				}
				p.Character = player.Character();
				if (!p.Character.address)
					continue;
				p.Humanoid = p.Character.FindFirstChildWhichIsA("Humanoid");
				if (!p.Humanoid.address)
					continue;
				p.Health = player.Health();
				p.MaxHealth = player.MaxHealth();
			}

			p.RigType = p.Humanoid.RigType();

			p.Head = p.Character.FindFirstChild("Head");
			p.HumanoidRootPart = p.Character.FindFirstChild("HumanoidRootPart");

			if (p.HumanoidRootPart.address)
			{
				uintptr_t primitiveAddr = Memory->read<uintptr_t>(p.HumanoidRootPart.address + Offsets::BasePart::Primitive);
				if (primitiveAddr)
					p.Velocity = Memory->read<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::AssemblyLinearVelocity);
			}

			// Cache equipped tool name. In modern/Rivals games the weapon is a
			// Tool that lives in the player's Backpack (child of the Player), and
			// may be nested inside folders, so we scan the character a couple
			// levels deep and fall back to the Backpack.
			p.ToolName = "";
			if (p.Character.address)
				p.ToolName = FindToolName(p.Character, 0);

			if (p.ToolName.empty())
			{
				auto backpack = player.FindFirstChild("Backpack");
				if (backpack.address)
					p.ToolName = FindToolName(backpack, 0);
			}

			// Rivals keeps every player's currently equipped weapon in
			// Workspace > ViewModels as "<Name> - <Weapon> - <Weapon>" models,
			// visible even in the lobby/hub. Use it as ground truth whenever the
			// character/backpack scan came up empty (the prefix match makes it
			// Rivals-specific, so it is safe to run regardless of place-ID).
			if (p.ToolName.empty() && Globals::Roblox::Workspace.address)
			{
				auto vm = Globals::Roblox::Workspace.FindFirstChild("ViewModels");
				if (vm.address)
				{
					std::string prefix = p.Name + " - ";
					for (auto& c : vm.GetChildren())
					{
						std::string nm = c.Name();
						if (nm.size() > prefix.size() && nm.compare(0, prefix.size(), prefix) == 0)
						{
							std::string rest = nm.substr(prefix.size());
							size_t sep = rest.find(" - ");
							p.ToolName = (sep != std::string::npos) ? rest.substr(0, sep) : rest;
							break;
						}
					}
				}
			}

			switch (p.RigType)
			{
			case 0: // R6
				p.Left_Arm = p.Character.FindFirstChild("Left Arm");
				p.Left_Leg = p.Character.FindFirstChild("Left Leg");

				p.Right_Arm = p.Character.FindFirstChild("Right Arm");
				p.Right_Leg = p.Character.FindFirstChild("Right Leg");

				p.Torso = p.Character.FindFirstChild("Torso");

				break;
			case 1: // R15
				p.Upper_Torso = p.Character.FindFirstChild("UpperTorso");
				p.Lower_Torso = p.Character.FindFirstChild("LowerTorso");

				p.Right_Upper_Arm = p.Character.FindFirstChild("RightUpperArm");
				p.Right_Lower_Arm = p.Character.FindFirstChild("RightLowerArm");
				p.Right_Hand = p.Character.FindFirstChild("RightHand");

				p.Left_Upper_Arm = p.Character.FindFirstChild("LeftUpperArm");
				p.Left_Lower_Arm = p.Character.FindFirstChild("LeftLowerArm");
				p.Left_Hand = p.Character.FindFirstChild("LeftHand");

				p.Right_Upper_Leg = p.Character.FindFirstChild("RightUpperLeg");
				p.Right_Lower_Leg = p.Character.FindFirstChild("RightLowerLeg");
				p.Right_Foot = p.Character.FindFirstChild("RightFoot");

				p.Left_Upper_Leg = p.Character.FindFirstChild("LeftUpperLeg");
				p.Left_Lower_Leg = p.Character.FindFirstChild("LeftLowerLeg");
				p.Left_Foot = p.Character.FindFirstChild("LeftFoot");

				break;
			default:
				break;
			}

			tempList.push_back(p);
		}

		{
			std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
			Globals::Caches::CachedPlayerObjects = std::move(tempList);
		}

		if (Globals::Roblox::LocalPlayer.address != 0)
		{
			Globals::Roblox::LocalPlayerTeam = Globals::Roblox::LocalPlayer.Team();
			Globals::Roblox::LocalPlayerTeamColor = Memory->read<int>(Globals::Roblox::LocalPlayer.address + Offsets::Player::TeamColor);
			
			if (Globals::Roblox::LocalPlayerTeam.address != 0)
			{
				Globals::Roblox::LocalPlayerTeamName = Globals::Roblox::LocalPlayerTeam.Name();
				Globals::Roblox::LocalPlayerTeamBrickColor = Memory->read<int>(Globals::Roblox::LocalPlayerTeam.address + Offsets::Team::BrickColor);
			}
			else
			{
				Globals::Roblox::LocalPlayerTeamName = "";
				Globals::Roblox::LocalPlayerTeamBrickColor = 0;
			}
		}

		// Sleep in small increments to allow instant response to forceRefresh
		for (int i = 0; i < 5 && Globals::running; i++)
		{
			if (Globals::Caches::forceRefresh)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}