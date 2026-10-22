#pragma once
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

// Last successfully read progression per player instance, keyed by address.
// CustomLeaderstats is not reliably resident, so a frame that fails to read it
// reuses the previous value instead of blanking the ESP.
struct ProgressionCacheEntry
{
    std::int64_t Level = -1;
    std::int64_t Elo = -1;
    bool LevelResolved = false;
    bool RankResolved = false;
};

inline std::unordered_map<uintptr_t, ProgressionCacheEntry>& ProgressionCache()
{
    static std::unordered_map<uintptr_t, ProgressionCacheEntry> cache;
    return cache;
}

inline void PruneProgressionCache(const std::unordered_set<uintptr_t>& live)
{
    auto& cache = ProgressionCache();
    for (auto it = cache.begin(); it != cache.end();)
    {
        if (live.count(it->first) == 0)
            it = cache.erase(it);
        else
            ++it;
    }
}
#include <thread>
#include <functional>
#include <cmath>
#include "../globals/options.h"
#include "../../rbx/globals/globals.h"
#include "../../features/pf_diag.h"

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

		if (Globals::Roblox::isPhantomForces)
		{
			RobloxInstance wsRoots(0), wsPlayers(0);
			for (auto& c : Globals::Roblox::Workspace.GetChildren())
			{
				std::string cn = c.Name();
				if (cn == "Roots") wsRoots = c;
				else if (cn == "Players") wsPlayers = c;
			}

			// Phantom Forces runs on Chickynoid: Player.ModelInstance is unlinked, so
			// the replicated characters instead live under
			// Workspace.Players/<teamFolder>/<Model> (each a 6-part hitbox rig with the
			// held weapon). Workspace.Roots holds one bare anchor part per spawned
			// player; its Sound children are named "enemy_*"/"friendly_*" relative to
			// us, which is the only team signal the client exposes.
			struct PFModel
			{
				std::string fold;
				Vectors::Vector3 center = { 0.f, 0.f, 0.f };
				std::vector<RobloxInstance> parts;
			};
			std::vector<PFModel> models;
			std::vector<std::string> foldNames;
			if (wsPlayers.address)
			{
				for (auto& fold : wsPlayers.GetChildren())
				{
					std::string foldName = fold.Name();
					foldNames.push_back(foldName);
					for (auto& mm : fold.GetChildren())
					{
						if (mm.Class() != "Model") continue;
						PFModel m;
						m.fold = foldName;
						Vectors::Vector3 acc = { 0.f, 0.f, 0.f };
						int pc = 0;
						for (auto& sc : mm.GetChildren())
						{
							std::string cl = sc.Class();
							if (cl != "Part" && cl != "MeshPart") continue;
							Vectors::Vector3 pp = sc.Position();
							acc.x += pp.x; acc.y += pp.y; acc.z += pp.z;
							m.parts.push_back(sc);
							++pc;
						}
						if (!pc) continue;
						m.center.x = acc.x / pc;
						m.center.y = acc.y / pc;
						m.center.z = acc.z / pc;
						models.push_back(std::move(m));
					}
				}
			}

			// Team resolution is driven by the footstep sounds attached to each
			// root ("enemy_*"/"friendly_*"), which are relative to us and reliable.
			// Scores decay every pass so a mapping learned on one team or round
			// cannot survive a swap and invert (which drew enemies as teammates).
			// The folder with the higher score is that team; with exactly two
			// folders the other one is inferred.
			static std::unordered_map<std::string, float> foldEnScore;
			static std::unordered_map<std::string, float> foldFrScore;
			static std::unordered_map<std::string, int> foldTeam;
			static std::unordered_map<uintptr_t, int> rootTeam;
			static std::string foldSig;
			static bool lastFrameHadSound = false;

			// Fade stale votes. Aggressive only while sounds are actually heard, so
			// the mapping self-heals during a live swap but survives a quiet camp
			// instead of collapsing back to the default.
			const float decay = lastFrameHadSound ? 0.97f : 0.995f;
			for (auto& kv : foldEnScore) kv.second *= decay;
			for (auto& kv : foldFrScore) kv.second *= decay;
			lastFrameHadSound = false;

			// Joining a different server replaces the local Player object. Folder
			// names and the local TeamColor can both look identical across servers,
			// which previously let the enemy/friendly mapping carry over and invert
			// (enemies drawn as teammates). A new local player always means a new
			// session, so throw every team vote and per-root cache away.
			static uintptr_t lastLocalPlayerAddr = 0;
			uintptr_t curLocalAddr = Globals::Roblox::LocalPlayer.address;
			if (curLocalAddr != lastLocalPlayerAddr)
			{
				if (lastLocalPlayerAddr != 0)
					PFDiag::Write("[PFRESET] localaddr old=" + PFDiag::Hex(lastLocalPlayerAddr) +
						" new=" + PFDiag::Hex(curLocalAddr));
				lastLocalPlayerAddr = curLocalAddr;
				foldSig.clear();
			}

			std::string sig;
			for (auto& fn : foldNames) sig += fn + "|";
			if (sig != foldSig)
			{
				if (!foldSig.empty())
					PFDiag::Write("[PFRESET] foldsig old=[" + foldSig + "] new=[" + sig + "]");
				foldSig = sig; foldEnScore.clear(); foldFrScore.clear(); foldTeam.clear(); rootTeam.clear();
			}

			// The folder->team mapping is resolved once (with a clear margin) and
			// then LOCKED until a reset. Legitimate team/session changes always
			// trip one of the resets above, so a locked mapping cannot go stale,
			// while a transient burst of mislabelled sounds can no longer flip it
			// and invert the ESP (which showed teammates as enemies and back).
			{
				bool resolved = false;
				for (auto& fn : foldNames)
				{
					if (foldTeam.count(fn)) continue;
					float ev = foldEnScore.count(fn) ? foldEnScore[fn] : 0.f;
					float fr = foldFrScore.count(fn) ? foldFrScore[fn] : 0.f;
					if (ev - fr >= 3.f) { foldTeam[fn] = -1; resolved = true; }
					else if (fr - ev >= 3.f) { foldTeam[fn] = 1; resolved = true; }
				}
				// Two teams, two folders: resolve the other from whichever is known.
				if (resolved && foldNames.size() == 2)
				{
					std::string enemyFold, friendFold;
					for (auto& fn : foldNames)
					{
						auto it = foldTeam.find(fn);
						if (it == foldTeam.end()) continue;
						if (it->second == -1) enemyFold = fn;
						else if (it->second == 1) friendFold = fn;
					}
					if (!enemyFold.empty() && friendFold.empty())
					{
						for (auto& fn : foldNames)
							if (fn != enemyFold) foldTeam[fn] = 1;
					}
					else if (enemyFold.empty() && !friendFold.empty())
					{
						for (auto& fn : foldNames)
							if (fn != friendFold) foldTeam[fn] = -1;
					}
				}
			}

			auto nearestModel = [&](const Vectors::Vector3& p, float& outD, float& outD2) -> int
			{
				int best = -1;
				float bd = 1e18f;
				float bd2 = 1e18f;
				for (size_t i = 0; i < models.size(); ++i)
				{
					float dx = models[i].center.x - p.x;
					float dy = models[i].center.y - p.y;
					float dz = models[i].center.z - p.z;
					float d = dx * dx + dy * dy + dz * dz;
					if (d < bd) { bd2 = bd; bd = d; best = (int)i; }
					else if (d < bd2) { bd2 = d; }
				}
				outD = sqrtf(bd);
				outD2 = (bd2 < 1e17f) ? sqrtf(bd2) : 1e9f;
				return best;
			};

			int localTC = 0;
			if (Globals::Roblox::LocalPlayer.address)
				localTC = Memory->read<int>(Globals::Roblox::LocalPlayer.address + Offsets::Player::TeamColor) & 0xFFFF;
			int enemyTC = (localTC == 23) ? 106 : 23;

			// Auto-balance can move us to the other team mid-round; the footstep
			// labels (and therefore the folder mapping) invert at that point, so the
			// accumulated votes must be thrown away.
			static int lastLocalTC = -1;
			if (localTC != 0 && localTC != lastLocalTC)
			{
				if (lastLocalTC != -1)
					PFDiag::Write("[PFRESET] localtc old=" + std::to_string(lastLocalTC) +
						" new=" + std::to_string(localTC));
				lastLocalTC = localTC;
				// foldFrVotes removed
				foldEnScore.clear();
				foldFrScore.clear();
				foldTeam.clear();
				rootTeam.clear();
			}

			int svcPlayersSeen = 0, rootsSeen = 0, friendlySeen = 0, enemySeen = 0, limbsSeen = 0;
			int soundEnRoots = 0, soundFrRoots = 0;
			std::string sndInfo;
			std::string rootInfo;
			std::unordered_set<uintptr_t> liveRootAddrs;
			Vectors::Vector3 camPos = { 0.f, 0.f, 0.f };
			if (Globals::Roblox::Camera.address)
				camPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);

			for (auto& pl : Globals::Roblox::Players.GetChildren())
				if (pl.address) ++svcPlayersSeen;

			if (wsRoots.address)
			{
				for (auto& rr : wsRoots.GetChildren())
				{
					if (!rr.address) continue;
					liveRootAddrs.insert(rr.address);

					// Only a clean, one-sided footstep label counts as a team signal;
					// a root that emitted both is pooled/stale and is ignored.
					int en = 0, fr = 0;
					for (auto& sc : rr.GetChildren())
					{
						std::string nm = sc.Name();
						if (nm.rfind("enemy_", 0) == 0) ++en;
						else if (nm.rfind("friendly_", 0) == 0) ++fr;
					}
					int soundSign = (fr > 0 && en == 0) ? 1 : ((en > 0 && fr == 0) ? -1 : 0);
					if (soundSign > 0) ++soundFrRoots; else if (soundSign < 0) ++soundEnRoots;
					if (soundSign != 0) lastFrameHadSound = true;

					Vectors::Vector3 rp = rr.Position();

					float d = 0.f;
					float d2 = 0.f;
					int mi = nearestModel(rp, d, d2);

					int modelSign = 0;
					if (mi >= 0 && d < 1.5f)
					{
						const std::string& fn = models[mi].fold;
						auto it = foldTeam.find(fn);
						if (it != foldTeam.end()) modelSign = it->second;

						// Only an unambiguous match (nearest model clearly closer
						// than the runner-up) may cast a team vote, so two players
						// standing together cannot pollute the folder mapping.
						if (d2 - d > 0.5f)
						{
							if (soundSign < 0) { foldEnScore[fn] += 1.f; if (foldEnScore[fn] > 8.f) foldEnScore[fn] = 8.f; }
							else if (soundSign > 0) { foldFrScore[fn] += 1.f; if (foldFrScore[fn] > 8.f) foldFrScore[fn] = 8.f; }
						}
					}

					// Priority: direct enemy sound > folder inference > per-root
					// cache. A lone friendly label is deliberately NOT trusted to
					// hide a root: a stray/pooled "friendly_*" child on an enemy
					// root was occluding enemies, and friendly labels already feed
					// the folder mapping. Anything unresolved stays visible.
					int sign = 0;
					if (soundSign < 0)
					{
						sign = -1;
					}
					else if (modelSign != 0)
					{
						sign = modelSign;
					}
					else
					{
						auto rt = rootTeam.find(rr.address);
						if (rt != rootTeam.end()) sign = rt->second;
					}
					if (sign != 0) rootTeam[rr.address] = sign;
					if (sign == 0)
					{
						// No signal at all: show as enemy. A teammate with no
						// streamed model and no footstep sound cannot be told apart
						// from an enemy, and hiding it also hides nearby enemies.
						sign = -1;
					}

					if (rootInfo.size() < 1400)
					{
						float cdx = rp.x - camPos.x, cdy = rp.y - camPos.y, cdz = rp.z - camPos.z;
						float cd = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz);
						std::string rn = rr.Name();
						if (rn.size() > 14) rn.resize(14);
						rootInfo += rn + "|cd" + std::to_string((int)cd) +
							"|s" + std::to_string(soundSign) +
							"|m" + std::to_string(modelSign) +
							"|f" + std::to_string(sign) +
							"|d" + std::to_string((int)d) + "  ";
					}

					if (soundSign != 0 && sndInfo.size() < 500)
					{
						std::string fn = (mi >= 0 && d < 3.f) ? models[mi].fold : std::string("?");
						sndInfo += std::string(soundSign > 0 ? "F" : "E") + ":" + fn +
							"@" + std::to_string((int)(d * 10)) + " ";
					}

					RobloxPlayer p;
					p.address = rr.address;
					p.Name = "";
					p.Team = RobloxInstance(0);
					p.Character = rr;
					p.HumanoidRootPart = rr;
					p.Head = rr;
					p.UseAnchor = true;
					p.AnchorPos = rp;
					p.Health = 100.f;
					p.MaxHealth = 100.f;
					p.TeamColor = (sign > 0) ? localTC : enemyTC;
					p.TeamBrickColor = 0;
					p.TeamName = "";

					// Build a drawable skeleton from the matched 6-part rig: highest
					// part is the head, the two lowest are the legs, the two widest are
					// the arms, the remainder is the torso.
					if (mi >= 0 && d < 4.f && models[mi].parts.size() >= 5)
					{
						auto& parts = models[mi].parts;
						const size_t ps = parts.size();
						if (ps < 5) continue;
						Vectors::Vector3 c = models[mi].center;

						// Pre-compute positions to avoid any reference/iterator invalidation
						// during sort, and to guard against stale RobloxInstance addresses.
						std::vector<Vectors::Vector3> pos(ps);
						for (size_t i = 0; i < ps; ++i)
							pos[i] = parts[i].Position();

						int headI = 0;
						for (size_t i = 1; i < ps; ++i)
							if (pos[i].y > pos[headI].y) headI = (int)i;

						std::vector<int> rest;
						for (int i = 0; i < (int)ps; ++i)
							if (i != headI) rest.push_back(i);
						std::sort(rest.begin(), rest.end(), [&](int a, int b) {
							if (a < 0 || a >= (int)ps || b < 0 || b >= (int)ps) return false;
							return pos[a].y < pos[b].y;
						});
						if (rest.size() < 4) continue;
						int legA = rest[0], legB = rest[1];

						// Remaining parts: torso + 2 arms. Arms are the two widest from center.
						std::vector<int> mid;
						for (size_t i = 2; i < rest.size(); ++i) mid.push_back(rest[i]);
						std::sort(mid.begin(), mid.end(), [&](int a, int b) {
							if (a < 0 || a >= (int)ps || b < 0 || b >= (int)ps) return false;
							return fabsf(pos[a].x - c.x) > fabsf(pos[b].x - c.x);
						});
						if (mid.size() < 3) continue;
						int armA = mid[0], armB = mid[1];
						int torsoI = mid[2];

						// Left/Right by X
						bool leftIsMin = pos[armA].x <= pos[armB].x;
						int lArm = leftIsMin ? armA : armB, rArm = leftIsMin ? armB : armA;
						leftIsMin = pos[legA].x <= pos[legB].x;
						int lLeg = leftIsMin ? legA : legB, rLeg = leftIsMin ? legB : legA;

						RobloxInstance torso = parts[torsoI];
						p.Head = parts[headI];
						p.Upper_Torso = torso;
						p.Lower_Torso = torso;
						p.Left_Upper_Arm = parts[lArm]; p.Left_Lower_Arm = parts[lArm]; p.Left_Hand = parts[lArm];
						p.Right_Upper_Arm = parts[rArm]; p.Right_Lower_Arm = parts[rArm]; p.Right_Hand = parts[rArm];
						p.Left_Upper_Leg = parts[lLeg]; p.Left_Lower_Leg = parts[lLeg]; p.Left_Foot = parts[lLeg];
						p.Right_Upper_Leg = parts[rLeg]; p.Right_Lower_Leg = parts[rLeg]; p.Right_Foot = parts[rLeg];
						++limbsSeen;
					}

					if (sign > 0) ++friendlySeen; else ++enemySeen;
					tempList.push_back(p);
					++rootsSeen;
				}
			}

			// Drop cache entries for roots that no longer exist so a recycled
			// address cannot inherit a previous player's (or previous round's) team.
			for (auto it = rootTeam.begin(); it != rootTeam.end();)
			{
				if (liveRootAddrs.count(it->first) == 0) it = rootTeam.erase(it);
				else ++it;
			}

			if (Globals::Roblox::LocalPlayer.address)
			{
				Globals::Roblox::LocalPlayerTeam = RobloxInstance(0);
				Globals::Roblox::LocalPlayerTeamName = "";
				Globals::Roblox::LocalPlayerTeamColor = localTC;
			}

			std::string foldInfo;
			for (auto& fn : foldNames)
			{
				int fs = 0; auto it = foldTeam.find(fn); if (it != foldTeam.end()) fs = it->second;
				int cnt = 0; for (auto& m : models) if (m.fold == fn) ++cnt;
				float ev = foldEnScore.count(fn) ? foldEnScore[fn] : 0.f;
				float frv = foldFrScore.count(fn) ? foldFrScore[fn] : 0.f;
				foldInfo += fn + ":" + std::to_string(fs) + "(" + std::to_string(cnt) + ")e" +
					std::to_string((int)ev) + "f" + std::to_string((int)frv) + " ";
			}

			PFDiag::Write("[PFDIAG] pf svcPlayers=" + std::to_string(svcPlayersSeen) +
				" roots=" + std::to_string(rootsSeen) +
				" friendly=" + std::to_string(friendlySeen) +
				" enemy=" + std::to_string(enemySeen) +
				" models=" + std::to_string(models.size()) +
				" limbs=" + std::to_string(limbsSeen) +
				" soundEn=" + std::to_string(soundEnRoots) +
				" soundFr=" + std::to_string(soundFrRoots) +
				" localTC=" + std::to_string(localTC) +
				" folds=[" + foldInfo + "]" +
				" snd=[" + sndInfo + "]");

			{
				static int pfRootTick = 0;
				if (++pfRootTick >= 10)
				{
					pfRootTick = 0;
					PFDiag::Write("[PFROOT] tc=" + std::to_string(localTC) + " " + rootInfo);
				}
			}

			{
				std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
				Globals::Caches::CachedPlayerObjects = std::move(tempList);
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		// Workspace character lookup, used ONLY for players whose
		// Player.Character link came back null. Some shooters keep their rigs in
		// Workspace under the player's own name instead of linking them back to
		// the Player object, which made every such player silently drop out of
		// CachedPlayerObjects below — and both ESP and the aimbot iterate that
		// list, so they showed nothing at all for those players.
		//
		// This used to call FindAllCharactersInWorkspace() here, which walks the
		// ENTIRE Workspace - including the map model, several thousand nodes on
		// the test place - and this loop runs every 500ms. That single call was
		// the largest read consumer in the program. CachePlayers() already
		// discovers exactly the same rigs every 5s with the same rules, so the
		// fallback map is built from that list instead of re-walking the tree.
		std::unordered_map<std::string, RobloxInstance> wsCharByName;
		{
			const uintptr_t localCharAddr = Globals::Roblox::LocalPlayer.Character().address;
			for (const auto& pl : Globals::Caches::CachedPlayers)
			{
				// NPC entries are the Workspace rigs (player entries carry a
				// Player object address, not one of these models).
				if (!pl.address || pl.address == localCharAddr)
					continue;
				const std::string n = pl.Name();
				if (n.empty() || n.find("Dummy") != std::string::npos || n == "Noob")
					continue;
				wsCharByName.emplace(n, pl);
			}
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

			// The character's child list and a name->child map, fetched ONCE
			// per player per rebuild and shared by both branches and by the
			// part/limb resolution below. Every FindFirstChild call re-read the
			// child array and then read a name per child, and this player loop
			// runs over the whole lobby every 500ms - it was the single largest
			// source of memory reads in the program.
			std::vector<RobloxInstance> charChildren;
			std::unordered_map<std::string, RobloxInstance> charByName;
			auto byName = [&](const char* n) -> RobloxInstance
			{
				auto it = charByName.find(n);
				return (it != charByName.end()) ? it->second : RobloxInstance(0);
			};
			auto byClass = [&](const char* cls) -> RobloxInstance
			{
				for (const auto& c : charChildren)
					if (c.Class() == cls)
						return c;
				return RobloxInstance(0);
			};

			if (isNPC)
			{
				// For NPCs (models), the object IS the character
				p.Name = player.Name();
				p.Team = RobloxInstance(0);
				p.TeamColor = 0;
				p.TeamBrickColor = 0;
				p.TeamName = "";
				p.Character = player;
				charChildren = p.Character.GetChildren();
				charByName.reserve(charChildren.size() * 2);
				for (const auto& c : charChildren)
					charByName.emplace(c.Name(), c);

				p.Humanoid = byClass("Humanoid");
				// A Humanoid is not required: playercache.h accepts NPC rigs on
				// HumanoidRootPart alone (custom-controller and dummy rigs ship no
				// Humanoid). Demanding one here silently dropped every such NPC
				// right after it had been discovered, so the NPC ESP toggle still
				// showed nothing. ESP draws off the root part; default the health.
				p.HumanoidRootPart = byName("HumanoidRootPart");
				if (!p.HumanoidRootPart.address)
					continue;
				// ESP falls back to the root part when Head is null, but resolving it
				// gives name-tag/head-dot ESP the correct anchor.
				p.Head = byName("Head");
				if (p.Humanoid.address)
				{
					p.Health = Memory->read<float>(p.Humanoid.address + Offsets::Humanoid::Health);
					p.MaxHealth = Memory->read<float>(p.Humanoid.address + Offsets::Humanoid::MaxHealth);
				}
				else
				{
					p.Health = 100.f;
					p.MaxHealth = 100.f;
				}
				// Resolve R6 vs R15 so skeleton/box ESP draws the right rig. Without
				// this RigType stayed 0 (R6), so every R15 NPC had its limbs mismatched.
				p.RigType = byName("UpperTorso").address ? 1 : 0;

				// A rig with no Head and no torso part is a viewmodel or a prop,
				// not a character - skip it rather than filling the cache and the
				// ESP loop with junk.
				if (!p.Head.address &&
					!byName("Torso").address &&
					!byName("UpperTorso").address)
					continue;
				// Dead NPCs (corpses) carry health 0. ESP already refuses to draw
				// them, but keeping them cost a full limb scan per corpse per
				// rebuild, which is pure read traffic for something never rendered.
				if (p.Humanoid.address && p.Health <= 0.f)
					continue;
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
						if (Globals::Roblox::isRivals && p.TeamName.empty())
						{
							// Rivals strips the Team object for non-local players (Team()
							// comes back empty/0), so TeamBrickColor was never populated
							// and every Rivals player was reading as "no team" == enemy.
							// Rivals keeps the real affiliation on the player's own
							// TeamColor offset, so fall back to that here.
							p.TeamBrickColor = p.TeamColor & 0xFFFF;
						}
						else
						{
							p.TeamBrickColor = Memory->read<int>(p.Team.address + Offsets::Team::BrickColor);
						}
					}
					else
					{
						p.TeamName = "";
						// Rivals: Team object stripped, use TeamColor from player offset
						if (Globals::Roblox::isRivals)
							p.TeamBrickColor = p.TeamColor & 0xFFFF;
						else
							p.TeamBrickColor = 0;
					}
				p.Character = player.Character();
				if (!p.Character.address)
				{
					// No link from the Player object. Fall back to a Workspace
					// character carrying this player's name rather than dropping
					// the player entirely.
					auto it = wsCharByName.find(p.Name);
					if (it != wsCharByName.end())
						p.Character = it->second;
				}
				if (!p.Character.address)
					continue;

				// Populate the shared child list/map declared above the branch.
				charChildren = p.Character.GetChildren();
				charByName.reserve(charChildren.size() * 2);
				for (const auto& c : charChildren)
					charByName.emplace(c.Name(), c);

				p.Humanoid = byClass("Humanoid");
				if (p.Humanoid.address)
				{
					// Read straight off the humanoid resolved above.
					// player.Health()/player.MaxHealth() each re-ran
					// Character() + FindFirstChildWhichIsA() - two more full
					// child-list scans per player per rebuild just to reach the
					// same humanoid we already had in hand.
					p.Health = Memory->read<float>(p.Humanoid.address + Offsets::Humanoid::Health);
					p.MaxHealth = Memory->read<float>(p.Humanoid.address + Offsets::Humanoid::MaxHealth);
				}
				else
				{
					// Humanoid-less character (Blox Strike and other games that
					// drive their rigs with a custom controller). ESP and the
					// aimbot only need the HumanoidRootPart, so keep the player
					// instead of dropping it silently - which is exactly what made
					// those games show no ESP at all.
					if (!byName("HumanoidRootPart").address)
						continue;
					p.Health = 100.f;
					p.MaxHealth = 100.f;
				}

				// ── Rivals progression ──
				// CustomLeaderstats, NOT "leaderstats". Rank is not a name either:
				// it is an ELO number under "Current ELO", and the tier comes from
				// a threshold ladder. Both values are int64 at Misc::Value.
				//
				// Gated on the toggles: this is a per-player scan on top of an
				// already busy cache rebuild, and there is no reason to pay for it
				// when neither Rank nor Level is switched on. Reads are cached per
				// player and reused when a read misses, because CustomLeaderstats
				// is not reliably resident and level would otherwise flicker.
				p.Level = -1;
				p.Elo = -1;
				p.LevelResolved = false;
				p.RankResolved = false;
				p.RankStem.clear();

				if (Options::ESP::Rank || Options::ESP::Level)
				{
					// Two bulk scans total: the Player's children to locate
					// CustomLeaderstats, then that folder's children picking up
					// both Level and Current ELO in a single pass.
					const auto siblings = player.GetChildrenBulk();
					const RobloxInstance stats = player.FindInChildren(siblings, "CustomLeaderstats");

					if (stats.address)
					{
						const auto stats_children = stats.GetChildrenBulk();

						if (const RobloxInstance lvl = stats.FindInChildren(stats_children, "Level"))
						{
							std::int64_t v = 0;
							if (lvl.ProgressionValue(v) && v >= 0)
							{
								p.Level = v;
								p.LevelResolved = true;
							}
						}

						if (const RobloxInstance elo = stats.FindInChildren(stats_children, "Current ELO"))
						{
							std::int64_t v = -1;
							if (elo.ProgressionValue(v))
							{
								p.Elo = v;
								p.RankResolved = true;
							}
						}
					}

					if (p.RankResolved)
						p.RankStem = RankIconStem(RankFromElo(p.Elo));

					const uintptr_t id = player.address;
					auto& cached = ProgressionCache()[id];

					if (!p.LevelResolved && cached.LevelResolved)
					{
						p.Level = cached.Level;
						p.LevelResolved = true;
					}
					if (!p.RankResolved && cached.RankResolved)
					{
						p.Elo = cached.Elo;
						p.RankResolved = true;
						p.RankStem = RankIconStem(RankFromElo(p.Elo));
					}

					if (p.LevelResolved)
					{
						cached.Level = p.Level;
						cached.LevelResolved = true;
					}
					if (p.RankResolved)
					{
						cached.Elo = p.Elo;
						cached.RankResolved = true;
					}
				}
			}

			if (p.Humanoid.address)
				p.RigType = p.Humanoid.RigType();
			else
				// No Humanoid to ask, so infer the rig from the part names the
				// character actually has (Humanoid-less rigs use R15 names).
				p.RigType = byName("UpperTorso").address ? 1 : 0;

			p.Head = byName("Head");
			p.HumanoidRootPart = byName("HumanoidRootPart");

			// Rivals team signal: a "TeammateLabel" BillboardGui parented to a
			// body part of YOUR teammates' characters (probe-confirmed: it is the
			// only non-cosmetic descendant that differs between teammate and
			// enemy). The probe only showed it at depth 2, not which part, so scan
			// every direct child of the character (parts and models) plus their
			// children rather than assuming the Head. Cached here because this is
			// a per-player multi-read and the cache rebuild only runs ~every 500ms
			// -- doing it per ESP frame would tank framerate. Enemies never carry
			// it, so its presence is the whole answer.
			p.HasTeammateLabel = false;
			if (Globals::Roblox::isRivals && p.Character.address)
			{
				// Reuses the child list already fetched above instead of reading
				// it a second time.
				for (auto& k : charChildren)
				{
					if (!k.address) continue;
					if (k.Name() == "TeammateLabel")
					{
						p.HasTeammateLabel = true;
						break;
					}
					// depth-2: label hangs off a body part / nested model
					for (auto& gc : k.GetChildren())
					{
						if (gc.address && gc.Name() == "TeammateLabel")
						{
							p.HasTeammateLabel = true;
							break;
						}
					}
					if (p.HasTeammateLabel) break;
				}
			}

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

			if (p.RigType == 0 || p.RigType == 1)
			{
				// Reuses the name->child map built once at the top of this
				// player's processing. It used to GetChildren() a third time
				// here and read a name per child again.
				auto limb = [&](const char* n) -> RobloxInstance
				{
					auto it = charByName.find(n);
					return (it != charByName.end()) ? it->second : RobloxInstance(0);
				};

				switch (p.RigType)
				{
				case 0: // R6
					p.Left_Arm = limb("Left Arm");
					p.Left_Leg = limb("Left Leg");

					p.Right_Arm = limb("Right Arm");
					p.Right_Leg = limb("Right Leg");

					p.Torso = limb("Torso");

					break;
				case 1: // R15
					p.Upper_Torso = limb("UpperTorso");
					p.Lower_Torso = limb("LowerTorso");

					p.Right_Upper_Arm = limb("RightUpperArm");
					p.Right_Lower_Arm = limb("RightLowerArm");
					p.Right_Hand = limb("RightHand");

					p.Left_Upper_Arm = limb("LeftUpperArm");
					p.Left_Lower_Arm = limb("LeftLowerArm");
					p.Left_Hand = limb("LeftHand");

					p.Right_Upper_Leg = limb("RightUpperLeg");
					p.Right_Lower_Leg = limb("RightLowerLeg");
					p.Right_Foot = limb("RightFoot");

					p.Left_Upper_Leg = limb("LeftUpperLeg");
					p.Left_Lower_Leg = limb("LeftLowerLeg");
					p.Left_Foot = limb("LeftFoot");

					break;
				default:
					break;
				}
			}

			tempList.push_back(p);
		}

		// Rivals: add "Target" characters from ShootingRangeEntities as enemy players
		// (In duels, enemy players control these dummies but have no character link).
		// Gated on Cache NPCs: there are ~200 of them and caching them all drags
		// ESP/aimbot hard, so they only exist when the user opted into NPCs.
		if (Globals::Roblox::isRivals && Options::Misc::CacheNPCs && Globals::Roblox::Workspace.address)
		{
			const auto sre = Globals::Roblox::Workspace.FindFirstChild("ShootingRangeEntities");
			if (sre.address)
			{
				for (auto& ch : sre.GetChildren())
				{
					if (ch.Name() != "Target") continue;
					if (!ch.FindFirstChild("HumanoidRootPart").address) continue;

					RobloxPlayer target;
					target.address = ch.address; // use character address as player ID
					target.Name = "Target";
					target.Team = RobloxInstance(0);
					target.TeamColor = 0;
					target.TeamBrickColor = 0;
					target.TeamName = "";
					target.Character = ch;
					target.Humanoid = ch.FindFirstChildWhichIsA("Humanoid");
					target.HumanoidRootPart = ch.FindFirstChild("HumanoidRootPart");
					target.Head = ch.FindFirstChild("Head");
					target.Health = 100.f;
					target.MaxHealth = 100.f;
					target.RigType = 1; // R15
					target.UseAnchor = true;
					target.AnchorPos = target.HumanoidRootPart.address ? target.HumanoidRootPart.Position() : Vectors::Vector3{0,0,0};

					// Rig parts for R15
					target.Upper_Torso = ch.FindFirstChild("UpperTorso");
					target.Lower_Torso = ch.FindFirstChild("LowerTorso");
					target.Right_Upper_Arm = ch.FindFirstChild("RightUpperArm");
					target.Right_Lower_Arm = ch.FindFirstChild("RightLowerArm");
					target.Right_Hand = ch.FindFirstChild("RightHand");
					target.Left_Upper_Arm = ch.FindFirstChild("LeftUpperArm");
					target.Left_Lower_Arm = ch.FindFirstChild("LeftLowerArm");
					target.Left_Hand = ch.FindFirstChild("LeftHand");
					target.Right_Upper_Leg = ch.FindFirstChild("RightUpperLeg");
					target.Right_Lower_Leg = ch.FindFirstChild("RightLowerLeg");
					target.Right_Foot = ch.FindFirstChild("RightFoot");
					target.Left_Upper_Leg = ch.FindFirstChild("LeftUpperLeg");
					target.Left_Lower_Leg = ch.FindFirstChild("LeftLowerLeg");
					target.Left_Foot = ch.FindFirstChild("LeftFoot");

					tempList.push_back(target);
				}
			}
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
				// Rivals: Team object is stripped for local player too, fall back to TeamColor offset
				if (Globals::Roblox::isRivals)
				{
					Globals::Roblox::LocalPlayerTeamBrickColor = Globals::Roblox::LocalPlayerTeamColor & 0xFFFF;
				}
				else
				{
					Globals::Roblox::LocalPlayerTeamBrickColor = 0;
				}
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