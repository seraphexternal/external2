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
		RobloxInstance localCharacter(0);
		if (Globals::Roblox::LocalPlayer.address)
			localCharacter = Globals::Roblox::LocalPlayer.Character();

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

		// If any player has no Character, try to find characters in Workspace by memory scanning
		bool needsFallback = false;
		for (auto& player : tempList)
		{
			if (!player.Character().address)
			{
				needsFallback = true;
				break;
			}
		}
		if (needsFallback)
		{
			PopulateCharacterFallbacks();
			playerCharacterAddresses.clear();
			for (auto& player : tempList)
			{
				auto charInst = player.Character();
				if (charInst.address != 0)
				{
					playerCharacterAddresses.push_back(charInst.address);
				}
			}
		}

		// Character bypass: some games manage characters separately from the
		// Player objects (Chickynoid-style), so every Player.Character() comes back
		// null even though the rigs are sitting in Workspace. This only ever fires
		// when NOT A SINGLE player has a character — i.e. exactly when the standard
		// path has already failed — so it cannot double-add characters in normal
		// games where Player.Character links work.
		bool overkillBypass = false;
		if (playerCharacterAddresses.empty() && !tempList.empty())
		{
			tempList.clear();
			overkillBypass = true;

			std::string localName;
			if (Globals::Roblox::LocalPlayer.address)
				localName = Globals::Roblox::LocalPlayer.Name();

			auto characters = FindAllCharactersInWorkspace();
			for (auto& ch : characters)
			{
				std::string name = ch.model.Name();
				if (name.find("Dummy") != std::string::npos || name == "Noob")
					continue;
				if (!localName.empty() && name == localName)
					continue;
				tempList.push_back(ch.model);
			}
		}

		// Cache NPCs from Workspace - any Model carrying a character rig.
		//
		// This used to inspect Workspace's direct children plus exactly ONE level
		// inside a Folder, and demanded a Humanoid on top of that. Both limits
		// silently produced zero NPCs in most games:
		//   - NPCs are overwhelmingly nested deeper than one folder deep
		//     (Workspace\Enemies\Wave1\Grunt, Workspace\Map\Spawns\Dummy, ...), so
		//     the scan never even saw them and toggling "Cache NPCs" did nothing.
		//   - Rigid/custom-controller rigs (and plenty of dummy setups) ship a
		//     HumanoidRootPart with no Humanoid at all. ESP only needs the root
		//     part - it already falls back to a default health for those - so
		//     requiring a Humanoid dropped rigs that would have drawn fine.
		// Now: bounded recursive walk of the whole Workspace, accept on
		// HumanoidRootPart, and let the object cache fill in health/limbs.
		auto workspace = Globals::Roblox::Workspace;
		if (workspace.address != 0 && Options::Misc::CacheNPCs && !overkillBypass)
		{
			// Every address we must not treat as an NPC: us and any real player.
			std::vector<uintptr_t> exclude = playerCharacterAddresses;
			if (localCharacter.address != 0)
				exclude.push_back(localCharacter.address);

			// Also exclude anything parented under the Players service - those are
			// player characters that failed to link, not NPCs, and pulling them in
			// here would duplicate (and misfile) real players as NPCs.
			uintptr_t playersServiceAddr = Globals::Roblox::Players.address;

			// Already-added guard, so a Model reached twice (or a rig whose parent
			// also looks like a rig) is only pushed once.
			std::vector<uintptr_t> npcAddrs;

			// Depth-first walk. Depth cap and a node budget keep this bounded on
			// huge maps: the old code was shallow but the whole-Workspace walk has
			// to be capped or a big place would stall the cache thread.
			int budget = 60000;
			const int maxDepth = 15;

			struct WalkNode { RobloxInstance inst; int depth; };
			std::vector<WalkNode> pending;
			pending.push_back({ workspace, 0 });

			while (!pending.empty() && budget > 0)
			{
				WalkNode node = pending.back();
				pending.pop_back();
				if (node.depth >= maxDepth)
					continue;

				for (auto& child : node.inst.GetChildren())
				{
					if (--budget <= 0)
						break;
					if (!child.address)
						continue;

					const std::string cls = child.Class();

					// A Model with a root part is a drawable character rig.
					if (cls == "Model")
					{
						const bool excluded =
							std::find(exclude.begin(), exclude.end(), child.address) != exclude.end() ||
							std::find(npcAddrs.begin(), npcAddrs.end(), child.address) != npcAddrs.end();

						if (!excluded && child.FindFirstChild("HumanoidRootPart").address != 0)
						{
							npcAddrs.push_back(child.address);
							tempList.push_back(child);
							// Don't descend into a rig we've already accepted; its
							// parts are not characters.
							continue;
						}
					}

					// Recurse through containers. Models that were NOT accepted are
					// still descended into (a rig can be nested inside a non-rig
					// Model), and Folders/other containers always are.
					if (cls == "Model" || cls == "Folder" ||
						cls == "Actor" || cls == "Workspace")
					{
						pending.push_back({ child, node.depth + 1 });
					}
				}
			}

			// Keep the Players-service rigs out of the NPC list. Without this, a
			// game that parks player characters under Players without linking them
			// (Chickynoid-style) would list every real player twice.
			if (playersServiceAddr != 0 && !tempList.empty())
			{
				std::vector<uintptr_t> playerRigAddrs;
				for (auto& pl : Globals::Roblox::Players.GetChildren())
				{
					auto ch = pl.Character();
					if (ch.address != 0)
						playerRigAddrs.push_back(ch.address);
				}
				if (!playerRigAddrs.empty())
				{
					tempList.erase(
						std::remove_if(tempList.begin(), tempList.end(),
							[&](const RobloxInstance& inst) {
								return std::find(playerRigAddrs.begin(), playerRigAddrs.end(),
									inst.address) != playerRigAddrs.end();
							}),
						tempList.end());
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


