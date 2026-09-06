#pragma once

#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "../seraph_log.h"
#include "anti_fling.h"

#include <thread>
#include <array>
#include <fstream>

	// Always-on diagnostics for the Animation Changer (independent of the
	// DebugLog toggle) so we can diagnose why it isn't changing animations.
	namespace AnimDebug
	{
		inline std::string Path()
		{
			char* env = nullptr; size_t sz = 0;
			std::string base = ".";
			if (_dupenv_s(&env, &sz, "LOCALAPPDATA") == 0 && env) { base = env; free(env); }
			std::string dir = base + "\\Seraph";
			CreateDirectoryA(dir.c_str(), nullptr);
			return dir + "\\anim_changer_debug.txt";
		}
		inline void Write(const std::string& line)
		{
			FILE* f = nullptr;
			fopen_s(&f, Path().c_str(), "a");
			if (f) { fprintf(f, "%s\n", line.c_str()); fclose(f); }
		}
	}

	// ── Animation Changer ────────────────────────────────────────────────────
	// Writes the local character's "Animate" controller slots (idle, run, walk,
	// jump, fall) to a selected preset style. Every Animation instance under
	// the slot gets its AnimationId content string replaced, mirroring the
	// classic Roblox "animation changer" method.
	//
	// Style table (index == Options::AnimationChanger::<Slot>):
	//   { idle1, idle2, run, walk, jump, fall }
	namespace AnimationChanger
	{
		inline const std::array<std::array<const char*, 6>, 11> Presets = {{
			// 0 - zombie
			{ "http://www.roblox.com/asset/?id=10921344533", "http://www.roblox.com/asset/?id=10921345304", "http://www.roblox.com/asset/?id=616163682", "http://www.roblox.com/asset/?id=616168032", "http://www.roblox.com/asset/?id=616161997", "http://www.roblox.com/asset/?id=616157476" },
			// 1 - ninja
			{ "http://www.roblox.com/asset/?id=656117400", "http://www.roblox.com/asset/?id=656118341", "http://www.roblox.com/asset/?id=656118852", "http://www.roblox.com/asset/?id=656121766", "http://www.roblox.com/asset/?id=656117878", "http://www.roblox.com/asset/?id=656115606" },
			// 2 - robot
			{ "http://www.roblox.com/asset/?id=616136790", "http://www.roblox.com/asset/?id=616138447", "http://www.roblox.com/asset/?id=616140816", "http://www.roblox.com/asset/?id=616146177", "http://www.roblox.com/asset/?id=616139451", "http://www.roblox.com/asset/?id=616134815" },
			// 3 - zombie (variant)
			{ "http://www.roblox.com/asset/?id=616158929", "http://www.roblox.com/asset/?id=616160636", "http://www.roblox.com/asset/?id=616163682", "http://www.roblox.com/asset/?id=616168032", "http://www.roblox.com/asset/?id=616161997", "http://www.roblox.com/asset/?id=616157476" },
			// 4 - levitation
			{ "http://www.roblox.com/asset/?id=616006778", "http://www.roblox.com/asset/?id=616008087", "http://www.roblox.com/asset/?id=616010382", "http://www.roblox.com/asset/?id=616013216", "http://www.roblox.com/asset/?id=616008936", "http://www.roblox.com/asset/?id=616005863" },
			// 5 - stylish
			{ "http://www.roblox.com/asset/?id=616136790", "http://www.roblox.com/asset/?id=616138447", "http://www.roblox.com/asset/?id=616140816", "http://www.roblox.com/asset/?id=616146177", "http://www.roblox.com/asset/?id=616139451", "http://www.roblox.com/asset/?id=616134815" },
			// 6 - cartoony
			{ "http://www.roblox.com/asset/?id=742637544", "http://www.roblox.com/asset/?id=742638445", "http://www.roblox.com/asset/?id=742638842", "http://www.roblox.com/asset/?id=742640026", "http://www.roblox.com/asset/?id=742637942", "http://www.roblox.com/asset/?id=742637151" },
			// 7 - super hero
			{ "http://www.roblox.com/asset/?id=616111295", "http://www.roblox.com/asset/?id=616113536", "http://www.roblox.com/asset/?id=616117076", "http://www.roblox.com/asset/?id=616122287", "http://www.roblox.com/asset/?id=616115533", "http://www.roblox.com/asset/?id=616118211" },
			// 8 - elder
			{ "http://www.roblox.com/asset/?id=845397899", "http://www.roblox.com/asset/?id=845400520", "http://www.roblox.com/asset/?id=845386501", "http://www.roblox.com/asset/?id=845403856", "http://www.roblox.com/asset/?id=845398858", "http://www.roblox.com/asset/?id=845396048" },
			// 9 - toy
			{ "http://www.roblox.com/asset/?id=782841498", "http://www.roblox.com/asset/?id=782845736", "http://www.roblox.com/asset/?id=782842708", "http://www.roblox.com/asset/?id=782843345", "http://www.roblox.com/asset/?id=782847020", "http://www.roblox.com/asset/?id=782846423" },
			// 10 - old school
			{ "http://www.roblox.com/asset/?id=5319828216", "http://www.roblox.com/asset/?id=5319831086", "http://www.roblox.com/asset/?id=5319844329", "http://www.roblox.com/asset/?id=5319847204", "http://www.roblox.com/asset/?id=5319841935", "http://www.roblox.com/asset/?id=5319839762" }
		}};

		inline bool SetAnimationID(RobloxInstance& anim, const char* id)
		{
			if (!anim.address || !id || id[0] == '\0')
				return false;
			const uintptr_t strObj = Memory->read<uintptr_t>(anim.address + Offsets::Misc::AnimationId);
			if (!strObj)
				return false;
			bool ok = Memory->writeString(strObj, id);
			AnimDebug::Write("[AnimChanger] Write " + std::string(ok ? "OK" : "FAIL") + " Animation @" + std::to_string(anim.address) + " -> " + id);
			return ok;
		}

		// Writes every Animation instance under a given Animate slot. Returns the
		// number of Animation children found (debug aid).
		inline int ApplySlot(RobloxInstance& animate, const char* slotName, const std::array<const char*, 6>& preset, int presetSub)
		{
			if (!animate.address || slotName[0] == '\0')
				return 0;
			// presetSub: 0 = idle1, 1 = idle2, 2 = run, 3 = walk, 4 = jump, 5 = fall
			RobloxInstance slot = animate.FindFirstChild(slotName);
			if (!slot.address)
				return -1; // slot not found

			// Debug: dump all children of this slot ONCE
			static bool dumped = false;
			if (!dumped)
			{
				dumped = true;
				auto children = slot.GetChildren();
				for (auto& c : children)
				{
					if (c.address)
					{
						std::string cname = c.Name();
						std::string cclass = c.Class();
						AnimDebug::Write("[AnimChanger]   Slot " + std::string(slotName) + " child: name=" + cname + " class=" + cclass + " addr=" + std::to_string(c.address));
					}
				}
			}

			auto children = slot.GetChildren();
			int found = 0;
			for (auto& anim : children)
			{
				if (!anim.address)
					continue;
				std::string cls = anim.Class();
				if (cls == "Animation")
				{
					found++;
					SetAnimationID(anim, preset[presetSub]);
				}
			}
			return found;
		}
	}

	inline void AnimationChangerLoop()
	{
		AnimDebug::Write("[AnimChanger] thread entered at " + std::to_string(GetTickCount()) + " ms (running=" + std::to_string(Globals::running) + " enabled=" + std::to_string(Options::AnimationChanger::Enabled) + ")");

		bool loggedDiscovery = false;
		bool wasEnabled = false;

		while (Globals::running)
		{
			if (!Options::AnimationChanger::Enabled)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				loggedDiscovery = false;
				if (wasEnabled) { AnimDebug::Write("[AnimChanger] toggle OFF"); wasEnabled = false; }
				continue;
			}
			if (!wasEnabled) { AnimDebug::Write("[AnimChanger] toggle ON (enabled seen)"); wasEnabled = true; }

			auto localplayer = Globals::Roblox::LocalPlayer;
			if (!localplayer.address)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			auto character = localplayer.Character();
			if (!character.address)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			RobloxInstance animate = character.FindFirstChild("Animate");
			if (!animate.address)
			{
				if (!loggedDiscovery)
					AnimDebug::Write("[AnimChanger] 'Animate' not found on local character (class=" + character.Class() + ").");
				loggedDiscovery = true;
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				continue;
			}

			auto& P = AnimationChanger::Presets;

			// sanity-clamp slice indices (0 = none, 1..StyleCount-1 = styles)
			int idleIdx = Options::AnimationChanger::Idle; if (idleIdx < 0 || idleIdx >= Options::AnimationChanger::StyleCount) idleIdx = Options::AnimationChanger::None;
			int runIdx  = Options::AnimationChanger::Run;  if (runIdx  < 0 || runIdx  >= Options::AnimationChanger::StyleCount) runIdx  = Options::AnimationChanger::None;
			int walkIdx = Options::AnimationChanger::Walk; if (walkIdx < 0 || walkIdx >= Options::AnimationChanger::StyleCount) walkIdx = Options::AnimationChanger::None;
			int jumpIdx = Options::AnimationChanger::Jump; if (jumpIdx < 0 || jumpIdx >= Options::AnimationChanger::StyleCount) jumpIdx = Options::AnimationChanger::None;
			int fallIdx = Options::AnimationChanger::Fall; if (fallIdx < 0 || fallIdx >= Options::AnimationChanger::StyleCount) fallIdx = Options::AnimationChanger::None;

			// None (0) means "leave this slot untouched".
			int nIdle = 0, nRun = 0, nWalk = 0, nJump = 0, nFall = 0;
			if (idleIdx != Options::AnimationChanger::None) nIdle = AnimationChanger::ApplySlot(animate, "idle", P[idleIdx - 1], 0);
			if (runIdx  != Options::AnimationChanger::None) nRun  = AnimationChanger::ApplySlot(animate, "run",  P[runIdx  - 1], 2);
			if (walkIdx != Options::AnimationChanger::None) nWalk = AnimationChanger::ApplySlot(animate, "walk", P[walkIdx - 1], 3);
			if (jumpIdx != Options::AnimationChanger::None) nJump = AnimationChanger::ApplySlot(animate, "jump", P[jumpIdx - 1], 4);
			if (fallIdx != Options::AnimationChanger::None) nFall = AnimationChanger::ApplySlot(animate, "fall", P[fallIdx - 1], 5);

			if (!loggedDiscovery)
			{
				std::string slots =
					"[AnimChanger] Animate found. Animation children per slot -> "
					"idle=" + std::to_string(nIdle) + " run=" + std::to_string(nRun) +
					" walk=" + std::to_string(nWalk) + " jump=" + std::to_string(nJump) +
					" fall=" + std::to_string(nFall) +
					" (idle/run styles=" + std::to_string(idleIdx) + "/" + std::to_string(runIdx) + ")";
				AnimDebug::Write(slots);
				loggedDiscovery = true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

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