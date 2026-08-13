#pragma once

#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "anti_fling.h"

#include <thread>

	inline void MiscLoop()
	{
		int tickCount = 0;

		while (Globals::running)
		{
			// Camera FOV — write every tick
			if (Options::Misc::FOVEnabled && Globals::Roblox::Camera.address)
			{
				Globals::Roblox::Camera.SetFOV(Options::Misc::FOV);
			}

			// Anti-Fling — restores the local position/velocity when a fling
			// spike is detected (runs every tick so it can win the race).
			AntiFling::Tick();

		// Third Person is handled by the dedicated ThirdPersonLoop thread
		// (features/thirdperson.h) — it hammers the camera transform every
		// frame so the game's camera script can never override it.

		// Slower path for headless / transparency features (every ~500 ticks = ~500ms)
			if (++tickCount % 500 == 0 && Globals::Roblox::LocalPlayer.address)
			{
				auto character = Globals::Roblox::LocalPlayer.Character();
				if (character.address)
				{
					if (Options::ESP::Headless)
					{
						auto head = character.FindFirstChild("Head");
						if (head.address != 0)
							Memory->write<float>(head.address + Offsets::BasePart::Transparency, 1.0f);
					}

					if (Globals::Roblox::isPhantomForces || Globals::Roblox::isOverkill)
					{
						auto head = character.FindFirstChild("Head");
						if (head.address != 0)
							Memory->write<float>(head.address + Offsets::BasePart::Transparency, 1.0f);
					}
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}