#pragma once

#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

// ── Player Fling ───────────────────────────────────────────────────────────
// Brief on-demand burst that teleports the local character onto the selected
// target and hurls it with a huge velocity + spin. The local character owns
// its own network simulation, so the launch is replicated by the server and
// the victim is the one who ends up flying.
//
// Started from the player list window; only one fling runs at a time. The
// burst is deliberately short (~650ms) and restores the original velocities
// on exit so nothing is left dirty (Anti-Fling, when enabled, watches the
// local character too and would otherwise fight the writes).
namespace PlayerFling
{
	inline std::atomic<bool> g_running{ false };

	inline bool Busy() { return g_running.load(std::memory_order_relaxed); }

	inline void Start(const std::string& targetName)
	{
		if (targetName.empty())
			return;

		bool expected = false;
		if (!g_running.compare_exchange_strong(expected, true))
			return;

		std::thread([targetName]()
		{
			const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(650);

			auto hrp = Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart");
			uintptr_t localPrim = hrp.address
				? Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive)
				: 0;
			if (!localPrim)
			{
				g_running.store(false);
				return;
			}

			Vectors::Vector3 savedLin = Memory->read<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity);
			Vectors::Vector3 savedAng = Memory->read<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyAngularVelocity);
			const Vectors::Vector3 startPos = Memory->read<Vectors::Vector3>(localPrim + Offsets::Primitive::Position);

			uintptr_t humanoidAddr = 0;
			uint8_t savedPlatformStand = 0;
			int savedStateId = 0;
			{
				auto localChar = Globals::Roblox::LocalPlayer.Character();
				if (localChar.address)
				{
					auto localHumanoid = localChar.FindFirstChildWhichIsA("Humanoid");
					humanoidAddr = localHumanoid.address;
				}
			}
			if (humanoidAddr)
			{
				savedPlatformStand = Memory->read<uint8_t>(humanoidAddr + Offsets::Humanoid::PlatformStand);
				savedStateId = Memory->read<int>(humanoidAddr + Offsets::Humanoid::HumanoidState + Offsets::Humanoid::HumanoidStateID);
			}

			Vectors::Vector3 punchDir{ 0.f, 0.f, 1.f };
			int tick = 0;
			while (std::chrono::steady_clock::now() < end && Globals::running)
			{
				// Resolve the target fresh every tick - players move, leave and rejoin.
				RobloxPlayer target;
				bool found = false;
				{
					std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
					for (const auto& p : Globals::Caches::CachedPlayerObjects)
					{
						if (p.Name == targetName)
						{
							target = p;
							found = true;
							break;
						}
					}
				}
				if (!found || !target.HumanoidRootPart.address)
					break;
				if (target.address == Globals::Roblox::LocalPlayer.address)
					break;

				uintptr_t targetPrim = Memory->read<uintptr_t>(target.HumanoidRootPart.address + Offsets::BasePart::Primitive);
				if (!targetPrim)
					break;

				const Vectors::Vector3 targetPos = Memory->read<Vectors::Vector3>(targetPrim + Offsets::Primitive::Position);
				const Vectors::Vector3 localPos = Memory->read<Vectors::Vector3>(localPrim + Offsets::Primitive::Position);

				Vectors::Vector3 toTarget = targetPos - localPos;
				const float len = toTarget.Magnitude();
				if (tick == 0 && len > 0.001f)
					punchDir = toTarget / len;

				Vectors::Vector3 punch = punchDir * 3500.f;
				punch.y += 500.f;
				const float spin = (tick & 1) ? 1.0e8f : -1.0e8f;
				const Vectors::Vector3 ang{ spin, -spin, spin * 0.6f };

				// Drive the position directly through the victim each window:
				// replication then carries real motion even if the humanoid or
				// physics engine fights the velocity writes.
				const int sweepSteps = 8;
				for (int k = 0; k < sweepSteps; ++k)
				{
					const float along = -15.f + 60.f * ((float)k / (float)(sweepSteps - 1));
					const Vectors::Vector3 pos = targetPos + punchDir * along;

					auto holdStart = std::chrono::steady_clock::now();
					do {
						Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::Position, pos);
						Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity, punch);
						Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyAngularVelocity, ang);
						if (humanoidAddr)
						{
							Memory->write<uint8_t>(humanoidAddr + Offsets::Humanoid::PlatformStand, 1);
							Memory->write<int>(humanoidAddr + Offsets::Humanoid::HumanoidState + Offsets::Humanoid::HumanoidStateID, 16);
						}
					} while (std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - holdStart).count() < 2000);
				}

				++tick;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			// Restore the pre-fling state.
			Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::Position, startPos);
			Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity, savedLin);
			Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyAngularVelocity, savedAng);
			if (humanoidAddr)
			{
				Memory->write<uint8_t>(humanoidAddr + Offsets::Humanoid::PlatformStand, savedPlatformStand);
				Memory->write<int>(humanoidAddr + Offsets::Humanoid::HumanoidState + Offsets::Humanoid::HumanoidStateID, savedStateId);
			}

			g_running.store(false);
		}).detach();
	}
}
