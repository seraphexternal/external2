#pragma once
#include "../globals/options.h"
#include "../globals/globals.h"
#include <thread>
#include <vector>


inline void TPHandler()
{
	while (Globals::running)
	{
		auto fakeDataModel = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::FakeDataModel::Pointer);
		auto dataModel = RobloxInstance(Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel));
		auto placeId = Memory->read<int>(dataModel.address + Offsets::DataModel::PlaceId);
		uintptr_t visualEngine;

		if (!dataModel || dataModel.address == 0 || dataModel.Name() == "LuaApp" || dataModel.address != Globals::Roblox::DataModel.address || placeId != Globals::Roblox::lastPlaceID) // player left the game or changed servers
		{
			// Reset Roblox globals instantly to prevent other threads from dereferencing dangling/stale pointers
			Globals::Roblox::DataModel = RobloxInstance(0);
			Globals::Roblox::Workspace = RobloxInstance(0);
			Globals::Roblox::Players = RobloxInstance(0);
			Globals::Roblox::Camera = RobloxInstance(0);
			Globals::Roblox::LocalPlayer = RobloxInstance(0);
			Globals::Caches::CachedPlayers.clear();
			Globals::Caches::CachedPlayerObjects.clear();

			while (dataModel.Name() != "Ugc" && dataModel.Name() != "Game" && Globals::running)
			{
				fakeDataModel = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::FakeDataModel::Pointer);
				dataModel = RobloxInstance(Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel));
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}

			if (!Globals::running) break;

			Globals::Roblox::DataModel = dataModel;

			visualEngine = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::VisualEngine::Pointer);

			while (visualEngine == 0 && Globals::running)
			{
				visualEngine = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::VisualEngine::Pointer);
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}

			if (!Globals::running) break;

			Globals::Roblox::VisualEngine = visualEngine;

			Globals::Roblox::Workspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
			Globals::Roblox::Players = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
			Globals::Roblox::Camera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");

			Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));

			// Re-read actual Place ID from the newly loaded DataModel to avoid double-triggering
			Globals::Roblox::lastPlaceID = Memory->read<int>(Globals::Roblox::DataModel.address + Offsets::DataModel::PlaceId);
			Globals::Roblox::isPhantomForces = (Globals::Roblox::lastPlaceID == Globals::Roblox::PHANTOM_FORCES_ID);
			Globals::Roblox::isRivals = (Globals::Roblox::lastPlaceID == Globals::Roblox::RIVALS_ID);
			Globals::Roblox::isOverkill = (Globals::Roblox::lastPlaceID == Globals::Roblox::OVERKILL_ID);

			Globals::Caches::CachedPlayers.clear();
			Globals::Caches::CachedPlayerObjects.clear();

			// Force player caching threads to update immediately using the new pointers
			Globals::Caches::forceRefresh = true;
		}

		// Continuous self-healing checks for cached pointers
		if (Globals::Roblox::DataModel.address != 0)
		{
			auto currentWorkspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
			if (currentWorkspace.address != Globals::Roblox::Workspace.address)
			{
				Globals::Roblox::Workspace = currentWorkspace;
				Globals::Caches::forceRefresh = true;
			}

			auto currentPlayers = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
			if (currentPlayers.address != Globals::Roblox::Players.address)
			{
				Globals::Roblox::Players = currentPlayers;
				Globals::Caches::forceRefresh = true;
			}

			if (Globals::Roblox::Workspace.address != 0)
			{
				auto currentCamera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");
				if (currentCamera.address != Globals::Roblox::Camera.address)
				{
					Globals::Roblox::Camera = currentCamera;
					Globals::Caches::forceRefresh = true;
				}
			}
			else if (Globals::Roblox::Camera.address != 0)
			{
				Globals::Roblox::Camera = RobloxInstance(0);
				Globals::Caches::forceRefresh = true;
			}

			if (Globals::Roblox::Players.address != 0)
			{
				auto currentLocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
				if (currentLocalPlayer.address != Globals::Roblox::LocalPlayer.address)
				{
					Globals::Roblox::LocalPlayer = currentLocalPlayer;
					Globals::Caches::forceRefresh = true;
				}
			}
			else if (Globals::Roblox::LocalPlayer.address != 0)
			{
				Globals::Roblox::LocalPlayer = RobloxInstance(0);
				Globals::Caches::forceRefresh = true;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

