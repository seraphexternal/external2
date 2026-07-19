#pragma once

#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"

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

		// Third Person — must write every tick; game scripts override CameraMode every frame
		if (Options::Misc::ThirdPerson && Globals::Roblox::LocalPlayer.address)
		{
			Memory->write<int>(Globals::Roblox::LocalPlayer.address + Offsets::Player::CameraMode, 0);
			Memory->write<float>(Globals::Roblox::LocalPlayer.address + Offsets::Player::MaxZoomDistance, 128.0f);
			Memory->write<float>(Globals::Roblox::LocalPlayer.address + Offsets::Player::MinZoomDistance, 0.5f);

			if (Globals::Roblox::Camera.address)
			{
				Memory->write<int>(Globals::Roblox::Camera.address + Offsets::Camera::CameraType, 5);

				// Point CameraSubject at the local character so any camera script
				// that respects CameraSubject (incl. some custom ones) keeps us in
				// third person. On Overkill the Player.Character() is empty, so fall
				// back to the workspace model we resolved by name.
				RobloxInstance localChar = Globals::Roblox::LocalPlayer.Character();
				if (!localChar.address && Globals::Roblox::isOverkill && g_ResolveCharacterFallback)
					localChar = g_ResolveCharacterFallback(Globals::Roblox::LocalPlayer.address);

			if (localChar.address)
			{
				auto hum = localChar.FindFirstChildWhichIsA("Humanoid");
				if (hum.address)
					Memory->write<uintptr_t>(Globals::Roblox::Camera.address + Offsets::Camera::CameraSubject, hum.address);

				// Force a real third-person transform. Chickynoid ignores CameraType,
				// but the renderer reads Camera.Rotation/Position directly, so writing
				// them every tick pulls the view behind & above the character. Mouse-
				// look still turns the character and the camera trails behind it.
				auto hrp = localChar.FindFirstChild("HumanoidRootPart");
				if (hrp.address)
				{
					sCFrame hrpCFrame = hrp.CFrame();
					Vectors::Vector3 hrpPos = { hrpCFrame.x, hrpCFrame.y, hrpCFrame.z };
					Vectors::Vector3 fwd = hrpCFrame.GetLookVector();

					const float dist = 10.0f;
					const float height = 3.5f;
					Vectors::Vector3 camPos = {
						hrpPos.x - fwd.x * dist,
						hrpPos.y + height,
						hrpPos.z - fwd.z * dist
					};
					Vectors::Vector3 lookAt = { hrpPos.x, hrpPos.y + 1.0f, hrpPos.z };

					sCFrame camCFrame = LookAt(camPos, lookAt);
					Matrixes::Matrix3x3 m;
					m.r00 = camCFrame.r00; m.r01 = camCFrame.r01; m.r02 = camCFrame.r02;
					m.r10 = camCFrame.r10; m.r11 = camCFrame.r11; m.r12 = camCFrame.r12;
					m.r20 = camCFrame.r20; m.r21 = camCFrame.r21; m.r22 = camCFrame.r22;
					Memory->write<Matrixes::Matrix3x3>(Globals::Roblox::Camera.address + Offsets::Camera::Rotation, m);
					Memory->write<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position, camPos);
				}
			}
			}
		}

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