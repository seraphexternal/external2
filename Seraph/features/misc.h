#pragma once

#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"

#include <thread>

inline void MiscLoop()
{
	while (Globals::running)
	{
		auto character = Globals::Roblox::LocalPlayer.Character();
		auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
		
		// Camera FOV
		if (Options::Misc::FOVEnabled)
		{
			Globals::Roblox::Camera.SetFOV(Options::Misc::FOV);
		}
		
		// Headless feature
		if (Options::ESP::Headless)
		{
			auto head = character.FindFirstChild("Head");
			if (head.address != 0)
			{
				Memory->write<float>(head.address + Offsets::BasePart::Transparency, 1.0f);
			}
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}