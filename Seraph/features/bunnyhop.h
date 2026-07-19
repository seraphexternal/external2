#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/SDK/SDK.h"
#include "../rbx/offsets.h"
#include <thread>
#include <chrono>

static RobloxInstance cachedLocalChar;
static DWORD lastLocalCharRefresh = 0;

static RobloxInstance GetBhopLocalCharacter()
{
	DWORD now = GetTickCount();
	if (cachedLocalChar.address && (now - lastLocalCharRefresh) < 500)
		return cachedLocalChar;

	lastLocalCharRefresh = now;

	auto ch = Globals::Roblox::LocalPlayer.Character();
	if (ch.address)
	{
		cachedLocalChar = ch;
		return ch;
	}

	if (Globals::Roblox::isOverkill)
	{
		std::string localName = Globals::Roblox::LocalPlayer.Name();
		if (!localName.empty() && Globals::Roblox::Workspace.address)
		{
			auto characters = FindAllCharactersInWorkspace();
			for (auto& c : characters)
			{
				if (c.model.Name() == localName)
				{
					cachedLocalChar = c.model;
					return c.model;
				}
			}
		}
	}

	cachedLocalChar = RobloxInstance(0);
	return cachedLocalChar;
}

static bool BhopIsGrounded()
{
	auto character = GetBhopLocalCharacter();
	if (!character.address)
		return false;

	auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
	if (!humanoid.address)
		return false;

	uint32_t floorMat = Memory->read<uint32_t>(humanoid.address + Offsets::Humanoid::FloorMaterial);
	return (floorMat != 0);
}

static void BhopForceJump()
{
	auto character = GetBhopLocalCharacter();
	if (!character.address)
		return;

	auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
	if (!humanoid.address)
		return;

	Memory->write<bool>(humanoid.address + Offsets::Humanoid::Jump, true);
}

inline void BhopLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (!Options::Bhop::Enabled)
            continue;

        if (!Options::Bhop::BhopKey)
            continue;

        if (!(GetAsyncKeyState(Options::Bhop::BhopKey) & 0x8000))
            continue;

        if (!BhopIsGrounded())
            continue;

        BhopForceJump();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
