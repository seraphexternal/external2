#pragma once
#include "../globals/options.h"
#include "../globals/globals.h"
#include <thread>
#include <vector>
#include <algorithm>


inline void CachePlayers()
{
	std::vector<RobloxInstance> tempList;
	std::vector<uintptr_t> playerCharacterAddresses;

	while (Globals::running)
	{
		Globals::Caches::forceRefresh = false;
		tempList.clear();
		playerCharacterAddresses.clear();

		// Dynamically retrieve LocalPlayer to prevent stale pointer issues when rejoining
		if (Globals::Roblox::Players.address != 0)
		{
			Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
		}

		// Get local player's character to exclude it from NPC detection
		auto localCharacter = Globals::Roblox::LocalPlayer.Character();

		// Cache real players from Players service
		auto children = Globals::Roblox::Players.GetChildren();
		if (!children.empty())
		{
			for (auto& player : children)
			{
				tempList.push_back(player);
				auto charInst = player.Character();
				if (charInst.address != 0)
				{
					playerCharacterAddresses.push_back(charInst.address);
				}
			}
		}

		// Cache NPCs from Workspace - look for any Model with a Humanoid
		auto workspace = Globals::Roblox::Workspace;
		if (workspace.address != 0 && Options::Misc::CacheNPCs)
		{
			auto workspaceChildren = workspace.GetChildren();
			
			for (auto& child : workspaceChildren)
			{
				std::string childClass = child.Class();
				
				// If it's a Folder, check inside it
				if (childClass == "Folder")
				{
					auto folderChildren = child.GetChildren();
					for (auto& folderChild : folderChildren)
					{
						std::string folderChildClass = folderChild.Class();
						
						if (folderChildClass == "Model")
						{
							// Skip if this is the local player's character or any real player's character
							if (folderChild.address == localCharacter.address ||
								std::find(playerCharacterAddresses.begin(), playerCharacterAddresses.end(), folderChild.address) != playerCharacterAddresses.end())
								continue;
								
							auto humanoid = folderChild.FindFirstChildWhichIsA("Humanoid");
							if (humanoid.address != 0)
							{
								auto hrp = folderChild.FindFirstChild("HumanoidRootPart");
								if (hrp.address != 0)
								{
									tempList.push_back(folderChild);
								}
							}
						}
					}
				}
				
				// Check if it's a Model directly in workspace
				if (childClass == "Model")
				{
					// Skip if this is the local player's character or any real player's character
					if (child.address == localCharacter.address ||
						std::find(playerCharacterAddresses.begin(), playerCharacterAddresses.end(), child.address) != playerCharacterAddresses.end())
						continue;
						
					auto humanoid = child.FindFirstChildWhichIsA("Humanoid");
					if (humanoid.address != 0)
					{
						auto hrp = child.FindFirstChild("HumanoidRootPart");
						if (hrp.address != 0)
						{
							tempList.push_back(child);
						}
					}
				}
			}
		}

		Globals::Caches::CachedPlayers.clear();
		Globals::Caches::CachedPlayers = tempList;

		// Sleep in small increments to allow instant response to forceRefresh when rejoining
		for (int i = 0; i < 50 && Globals::running; i++)
		{
			if (Globals::Caches::forceRefresh)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}


