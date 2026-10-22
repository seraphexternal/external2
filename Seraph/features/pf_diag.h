#pragma once
#include "../rbx/SDK/SDK.h"
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/RobloxVersion.h"
#include "../seraph_log.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <set>
#include <utility>

namespace PFDiag
{
    inline std::string Path()
    {
        // Diagnostics go to the temp directory, NOT %LOCALAPPDATA%\Seraph.
        // That folder is removed when the process exits, which repeatedly
        // destroyed the log before it could be read - every live measurement
        // was lost the moment Seraph was uninjected. The temp path survives.
        char tmp[MAX_PATH] = {};
        std::string dir;
        if (GetTempPathA(MAX_PATH, tmp))
            dir = std::string(tmp) + "SeraphDiag";
        else
            dir = ".\\SeraphDiag";

        // CreateDirectoryA only makes the final component, so fall back to the
        // LOCALAPPDATA folder if the temp directory is not writable.
        if (!CreateDirectoryA(dir.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
        {
            char* env = nullptr; size_t sz = 0;
            std::string base = ".";
            if (_dupenv_s(&env, &sz, "LOCALAPPDATA") == 0 && env) { base = env; free(env); }
            dir = base + "\\Seraph";
            CreateDirectoryA(dir.c_str(), nullptr);
        }
        return dir + "\\pf_diag.txt";
    }

    inline void Write(const std::string& line)
    {
        OutputDebugStringA(line.c_str());
        OutputDebugStringA("\n");
        // Path() resolves the directory with GetTempPath plus CreateDirectoryA.
        // It used to be called per line, so a diagnostic burst paid that cost
        // hundreds of times. Resolve it once.
        static const std::string path = Path();
        FILE* f = nullptr;
        fopen_s(&f, path.c_str(), "a");
        if (f) { fprintf(f, "%s\n", line.c_str()); fclose(f); }
    }

    inline std::string Hex(uintptr_t v)
    {
        char b[24];
        snprintf(b, sizeof(b), "%llx", (unsigned long long)v);
        return std::string(b);
    }

    inline void WalkEx(RobloxInstance inst, int depth, int& budget, int& humanoids, int& models, int& parts,
                       std::vector<std::string>& humanoidParents, std::vector<std::string>& modelNames,
                       std::vector<std::string>& rootPartSamples)
    {
        if (!inst.address || depth > 8 || budget <= 0)
            return;
        auto children = inst.GetChildren();
        for (auto& ch : children)
        {
            if (budget <= 0)
                return;
            --budget;
            std::string cls = ch.Class();
            std::string nm = ch.Name();
            if (cls == "Humanoid")
            {
                ++humanoids;
                if ((int)humanoidParents.size() < 20)
                {
                    char b[512];
                    snprintf(b, sizeof(b), "[PFDIAG]   humanoid under name='%s' cls='%s' addr=%s",
                        inst.Name().c_str(), inst.Class().c_str(), Hex(inst.address).c_str());
                    humanoidParents.push_back(b);
                }
            }
            else if (cls == "Model")
            {
                ++models;
                if ((int)modelNames.size() < 40)
                    modelNames.push_back(nm);
            }
            else if (cls == "Part" || cls == "MeshPart" || cls == "WedgePart" || cls == "TrussPart" || cls == "SpawnLocation")
            {
                ++parts;
                if ((int)rootPartSamples.size() < 25)
                {
                    std::string lower = nm;
                    for (auto& c : lower)
                        c = (char)tolower((unsigned char)c);
                    if (lower.find("root") != std::string::npos)
                        rootPartSamples.push_back(nm);
                }
            }
            WalkEx(ch, depth + 1, budget, humanoids, models, parts, humanoidParents, modelNames, rootPartSamples);
        }
    }

    inline void DumpWalk(const char* label, RobloxInstance root)
    {
        char buf2[1024];
        int budget = 40000;
        int humanoids = 0, models = 0, parts = 0;
        std::vector<std::string> humanoidParents;
        std::vector<std::string> modelNames;
        std::vector<std::string> rootPartSamples;
        WalkEx(root, 0, budget, humanoids, models, parts, humanoidParents, modelNames, rootPartSamples);
        snprintf(buf2, sizeof(buf2),
            "[PFDIAG] walk(%s) humanoids=%d models=%d parts=%d budgetLeft=%d",
            label, humanoids, models, parts, budget);
        Write(buf2);
        for (auto& h : humanoidParents)
            Write(h);
        for (auto& n : modelNames)
        {
            snprintf(buf2, sizeof(buf2), "[PFDIAG]   %s modelName='%s'", label, n.c_str());
            Write(buf2);
        }
        for (auto& n : rootPartSamples)
        {
            snprintf(buf2, sizeof(buf2), "[PFDIAG]   %s rootPart='%s'", label, n.c_str());
            Write(buf2);
        }
    }

    inline void Tick()
    {
        static auto lastRun = std::chrono::steady_clock::now();
        static int tickCount = 0;
        const auto now = std::chrono::steady_clock::now();
        // 15s, not 2s: each pass walks Workspace/FindAllCharactersInWorkspace
        // and re-opens the log file per line, so at 2s it was a steady IO burst
        // (hundreds of driver reads + a dozen file opens) in every game, all
        // year round. Still frequent enough to diagnose from.
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRun).count() < 15000)
            return;
        lastRun = now;
        ++tickCount;

        char buf[1400];

        {
            Vectors::Vector3 cp = Globals::Roblox::Camera.Position();
            int wsPlayerCount = 0, wsPlayerSubTotal = 0, rootsCount = 0;
            for (auto& c : Globals::Roblox::Workspace.GetChildren())
            {
                if (c.Name() == "Players")
                {
                    auto sub = c.GetChildren();
                    wsPlayerCount = (int)sub.size();
                    for (auto& s : sub)
                        wsPlayerSubTotal += (int)s.GetChildren().size();
                }
                if (c.Name() == "Roots")
                    rootsCount = (int)c.GetChildren().size();
            }
            snprintf(buf, sizeof(buf),
                "[PFDIAG] heartbeat place=%lld isPF=%d campos=(%.0f,%.0f,%.0f) wsPlayers=%d wsPlayerSub=%d roots=%d",
                (long long)Globals::Roblox::lastPlaceID,
                (int)Globals::Roblox::isPhantomForces,
                cp.x, cp.y, cp.z,
                wsPlayerCount, wsPlayerSubTotal, rootsCount);
            Write(buf);

            // --- ESP / aimbot acquisition diagnostics (runs in every game) -----
            // The old heartbeat only counted Phantom Forces' Workspace.Players /
            // Workspace.Roots folders, so it said nothing about whether the list
            // ESP and the aimbot actually iterate was populated.
            Vectors::Vector3 camPos{ 0.f, 0.f, 0.f };
            if (Globals::Roblox::Camera.address)
                camPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);

            auto wsChars = FindAllCharactersInWorkspace();
            int npcObjs = 0, playerObjs = 0;
            for (const auto& p : Globals::Caches::FramePlayers())
            {
                if (p.address != 0 && p.address == p.Character.address) ++npcObjs;
                else ++playerObjs;
            }
            snprintf(buf, sizeof(buf),
                "[PFDIAG] acq dm=%llu cam=%llu camPos=(%.0f,%.0f,%.0f) viewport=%d localChar=%llu cachedPlayers=%zu cachedObjects=%zu npc=%d playerObjs=%d cacheNPCs=%d wsChars=%zu",
                (unsigned long long)Globals::Roblox::DataModel.address,
                (unsigned long long)Globals::Roblox::Camera.address,
                camPos.x, camPos.y, camPos.z,
                (int)Globals::Viewport::Valid,
                (unsigned long long)Globals::Roblox::LocalPlayer.Character().address,
                Globals::Caches::CachedPlayers.size(),
                Globals::Caches::CachedPlayerObjects.size(),
                npcObjs, playerObjs, (int)Options::Misc::CacheNPCs,
                wsChars.size());
            Write(buf);

            int printed = 0;
            int printed2 = 0;
            for (const auto& p : Globals::Caches::FramePlayers())
            {
                if (printed >= 8) break;
                const auto hpPos = p.HumanoidRootPart.Position();
                const float dx = hpPos.x - camPos.x, dy = hpPos.y - camPos.y, dz = hpPos.z - camPos.z;
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                const RobloxInstance charParent(p.Character.address
                    ? Memory->read<uintptr_t>(p.Character.address + Offsets::Instance::Parent) : 0);
                snprintf(buf, sizeof(buf),
                    "[PFDIAG] obj[%d] name=%.20s char=%llu hum=%llu head=%llu hrp=%llu hp=%.0f pos=(%.0f,%.0f,%.0f) dist=%.0f team=%.16s tm=%d parent=%.20s bc=%d pc=%d tcOpt=%d maxD=%.0f",
                    printed, p.Name.c_str(),
                    (unsigned long long)p.Character.address,
                    (unsigned long long)p.Humanoid.address,
                    (unsigned long long)p.Head.address,
                    (unsigned long long)p.HumanoidRootPart.address,
                    p.Health,
                    hpPos.x, hpPos.y, hpPos.z,
                    dist, p.TeamName.c_str(), (int)IsTeammate(p), charParent.Name().c_str(), p.TeamBrickColor, p.TeamColor,
                    (int)Options::ESP::TeamCheck, Options::ESP::MaxRenderDistance);
                Write(buf);
                ++printed;
            }

            // Local player team probe: check character's AttributeStorage for
            // "Team" attribute, and see if our own character has a Highlight
            // (possibly disabled). Also dump TeamSelect model under Reactor.
            {
                static uint64_t lastLocal = 0;
                const uint64_t nowL = GetTickCount64();
                if (nowL - lastLocal > 20000)
                {
                    lastLocal = nowL;
                    const auto lpChar = Globals::Roblox::LocalPlayer.Character();
                    if (lpChar.address)
                    {
                        const uintptr_t attrStore = Memory->read<uintptr_t>(lpChar.address + 0x40);
                        snprintf(buf, sizeof(buf), "[PFDIAG] localChar attrStore=%llu",
                            (unsigned long long)attrStore);
                        Write(buf);
                        if (attrStore > 0x10000 && attrStore < 0x7fffffffffffULL)
                        {
                            unsigned char w[0x100]{};
                            Memory->readRaw(attrStore, w, sizeof(w));
                            char txt[0x101]{};
                            for (size_t b = 0; b < sizeof(w); ++b)
                                txt[b] = (w[b] >= 32 && w[b] < 127) ? (char)w[b] : '.';
                            txt[sizeof(w)] = 0;
                            snprintf(buf, sizeof(buf), "[PFDIAG] localChar attrRaw=%.120s", txt);
                            Write(buf);
                        }

                        // Check for Highlight on local character (maybe disabled)
                        const auto kids = lpChar.GetChildren();
                        for (auto& k : kids)
                        {
                            if (k.Class() == "Highlight")
                            {
                                const bool en = Memory->read<bool>(k.address + 0xF4);
                                const float fc[3] = { Memory->read<float>(k.address + 0xC8),
                                    Memory->read<float>(k.address + 0xCC), Memory->read<float>(k.address + 0xD0) };
                                snprintf(buf, sizeof(buf),
                                    "[PFDIAG] localChar Highlight en=%d fill=(%.2f,%.2f,%.2f)",
                                    (int)en, fc[0], fc[1], fc[2]);
                                Write(buf);
                            }
                        }
                    }

                    // TeamSelect model under Reactor
                    const auto reactor = Globals::Roblox::DataModel.FindFirstChild("Reactor");
                    if (reactor.address)
                    {
                        const auto ts = reactor.FindFirstChild("TeamSelect");
                        if (ts.address)
                        {
                            const auto tsk = ts.GetChildren();
                            for (auto& c : tsk)
                            {
                                snprintf(buf, sizeof(buf),
                                    "[PFDIAG] TeamSelect kid=%.24s cls=%.16s kids=%zu",
                                    c.Name().c_str(), c.Class().c_str(), c.GetChildren().size());
                                Write(buf);
                            }
                        }
                    }
                }
            }

            // Team discriminator probe: for the first objects dump the character's
            // child count, its Highlight (if any) and the torso colour, so a
            // teammate and an enemy can be told apart by their signatures.
            {
                int sig = 0;
                for (const auto& p : Globals::Caches::FramePlayers())
                {
                    if (printed2 >= 8) break;
                    ++printed2;
                    if (!p.Character.address) continue;

                    const auto kids = p.Character.GetChildren();
                    RobloxInstance hl(0);
                    for (auto& k : kids)
                        if (k.Class() == "Highlight") { hl = k; break; }
                    char hlCol[80] = "none";
                    if (hl.address)
                        snprintf(hlCol, sizeof(hlCol),
                            "fill=(%.2f,%.2f,%.2f) out=(%.2f,%.2f,%.2f) ft=%.2f ot=%.2f",
                            Memory->read<float>(hl.address + 0xC8), Memory->read<float>(hl.address + 0xCC),
                            Memory->read<float>(hl.address + 0xD0), Memory->read<float>(hl.address + 0xD4),
                            Memory->read<float>(hl.address + 0xD8), Memory->read<float>(hl.address + 0xDC),
                            Memory->read<float>(hl.address + 0xE4), Memory->read<float>(hl.address + 0xEC));

                    const auto torso = p.Character.FindFirstChild("UpperTorso");
                    int tbc = -1;
                    float tc3[3] = { 0.f, 0.f, 0.f };
                    if (torso.address)
                    {
                        tbc = Memory->read<int>(torso.address + 0x1A8);
                        tc3[0] = Memory->read<float>(torso.address + 0x1A8);
                        tc3[1] = Memory->read<float>(torso.address + 0x1AC);
                        tc3[2] = Memory->read<float>(torso.address + 0x1B0);
                    }

                    snprintf(buf, sizeof(buf),
                        "[PFDIAG] sig[%d] name=%.20s nk=%zu hl=%d %s torsoBC=%d torsoC=(%.3f,%.3f,%.3f)",
                        sig, p.Name.c_str(), kids.size(), (int)(hl.address ? 1 : 0), hlCol, tbc, tc3[0], tc3[1], tc3[2]);
                    Write(buf);

                    // Rivals team probe: dump character AttributeStorage and look for Team attribute
                    if (Globals::Roblox::isRivals && p.Character.address)
                    {
                        const uintptr_t charAttrStore = Memory->read<uintptr_t>(p.Character.address + 0x40);
                        if (charAttrStore > 0x10000)
                        {
                            unsigned char w[0x100]{};
                            Memory->readRaw(charAttrStore, w, sizeof(w));
                            char txt[0x101]{};
                            for (size_t b = 0; b < sizeof(w); ++b)
                                txt[b] = (w[b] >= 32 && w[b] < 127) ? (char)w[b] : '.';
                            txt[sizeof(w)] = 0;
                            snprintf(buf, sizeof(buf), "[PFDIAG] charAttr %s store=%llu raw=%.120s",
                                p.Name.c_str(), (unsigned long long)charAttrStore, txt);
                            Write(buf);
                        }

                        // Dump all character children looking for team indicators
                        for (auto& k : kids)
                        {
                            const std::string kname = k.Name();
                            const std::string kclass = k.Class();
                            std::string klow = kname;
                            for (char& c : klow) c = (char)tolower((unsigned char)c);
                            if (klow.find("team") != std::string::npos || klow.find("side") != std::string::npos ||
                                kclass == "BoolValue" || kclass == "IntValue" || kclass == "StringValue" || kclass == "NumberValue")
                            {
                                snprintf(buf, sizeof(buf), "[PFDIAG] charChild %s kid=%.24s cls=%.16s",
                                    p.Name.c_str(), kname.c_str(), kclass.c_str());
                                Write(buf);
                            }
                        }
                    }
                    ++sig;
                }
            }

            // Look for a team roster: any DataModel node whose children are named
            // after cached players (Blox Strike's real team source may be a data
            // object rather than Player.Team / a player attribute).
            {
                static uint64_t lastRoster = 0;
                const uint64_t nowR = GetTickCount64();
                if (nowR - lastRoster > 20000)
                {
                    lastRoster = nowR;
                    std::set<std::string> pnames;
                    for (const auto& pl : Globals::Caches::CachedPlayers)
                        if (!pl.Name().empty()) pnames.insert(pl.Name());

                    std::vector<std::pair<int, RobloxInstance>> frontier;
                    frontier.push_back({ 0, Globals::Roblox::DataModel });
                    int visited = 0, logged = 0;
                    while (!frontier.empty() && visited < 600 && logged < 30)
                    {
                        const auto cur = frontier.back();
                        frontier.pop_back();
                        if (!cur.second.address) continue;
                        ++visited;

                        auto kk = cur.second.GetChildren();
                        int matches = 0;
                        std::string names;
                        for (auto& c : kk)
                        {
                            const std::string nm = c.Name();
                            const std::string cl = c.Class();
                            std::string low;
                            for (char ch2 : nm) low += (char)tolower((unsigned char)ch2);
                            if (low.find("team") != std::string::npos ||
                                low.find("terror") != std::string::npos ||
                                low.find("counter") != std::string::npos)
                            {
                                snprintf(buf, sizeof(buf),
                                    "[PFDIAG] roster node=%.20s kid=%.24s cls=%.16s kids=%zu",
                                    cur.second.Name().c_str(), nm.c_str(), cl.c_str(), c.GetChildren().size());
                                Write(buf);
                                ++logged;
                            }
                            if (pnames.count(nm))
                            {
                                ++matches;
                                if (names.size() < 60) names += nm + ",";
                            }
                        }
                        if (matches >= 2)
                        {
                            snprintf(buf, sizeof(buf),
                                "[PFDIAG] rosterHIT d=%d node=%.20s cls=%.16s kids=%zu matches=%d [%.60s]",
                                cur.first, cur.second.Name().c_str(), cur.second.Class().c_str(),
                                kk.size(), matches, names.c_str());
                            Write(buf);
                            ++logged;
                        }

                        if (cur.first >= 3) continue;
                        for (auto& c : kk)
                        {
                            const std::string ccl = c.Class();
                            if (cur.first == 0 || ccl == "Folder" || ccl == "Model" ||
                                ccl == "Configuration" || ccl == "PlayerGui" || ccl == "ScreenGui" ||
                                ccl == "Frame" || ccl == "ScrollingFrame" || ccl == "Teams" ||
                                ccl == "TextLabel" || ccl == "TextButton" || ccl == "GuiObject" ||
                                ccl == "ReplicatedStorage")
                                frontier.push_back({ cur.first + 1, c });
                        }
                    }
                    snprintf(buf, sizeof(buf), "[PFDIAG] roster scan visited=%d", visited);
                    Write(buf);
                }
            }

            // GUI roster scan: the game's HUD/scoreboard spells out player names
            // and team names, which is the one place the team split is certain.
            {
                static uint64_t lastGui = 0;
                const uint64_t nowG = GetTickCount64();
                if (nowG - lastGui > 20000)
                {
                    lastGui = nowG;
                    std::vector<RobloxInstance> roots;
                    if (Globals::Roblox::LocalPlayer.address)
                    {
                        auto pg = Globals::Roblox::LocalPlayer.FindFirstChild("PlayerGui");
                        if (pg.address) roots.push_back(pg);
                    }
                    if (Globals::Roblox::DataModel.address)
                    {
                        auto cg = Globals::Roblox::DataModel.FindFirstChild("CoreGui");
                        if (cg.address) roots.push_back(cg);
                    }

                    std::vector<std::pair<int, RobloxInstance>> frontier;
                    for (auto& r : roots) frontier.push_back({ 0, r });
                    int visited = 0, logged2 = 0;
                    while (!frontier.empty() && visited < 500 && logged2 < 60)
                    {
                        const auto curNode = frontier.back();
                        frontier.pop_back();
                        if (!curNode.second.address) continue;
                        ++visited;
                        auto kk = curNode.second.GetChildren();

                        const std::string cl = curNode.second.Class();
                        if (cl == "TextLabel" || cl == "TextButton" || cl == "TextBox")
                        {
                            const std::string t = Memory->readString(curNode.second.address + 0xE00);
                            if (!t.empty())
                            {
                                snprintf(buf, sizeof(buf), "[PFDIAG] gui d=%d cls=%.10s text=%.40s",
                                    curNode.first, cl.c_str(), t.c_str());
                                Write(buf);
                                ++logged2;
                            }
                        }

                        if (curNode.first >= 5) continue;
                        for (auto& c : kk)
                            frontier.push_back({ curNode.first + 1, c });
                    }
                    snprintf(buf, sizeof(buf), "[PFDIAG] gui scan visited=%d logged=%d", visited, logged2);
                    Write(buf);
                }
            }

            // Hunt for the real team marker: dump a live character model's
            // children and the Workspace.Characters team folders.
            // children and the Workspace.Characters team folders.
            {
                const RobloxInstance& first = Globals::Caches::CachedPlayerObjects.empty()
                    ? RobloxInstance(0) : Globals::Caches::CachedPlayerObjects.front().Character;
                if (first.address)
                {
                    snprintf(buf, sizeof(buf), "[PFDIAG] charKids begin=%.20s", first.Name().c_str());
                    Write(buf);
                    int k = 0;
                    for (auto& ch : first.GetChildren())
                    {
                        if (k >= 30) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG]   ck[%d] name=%.24s cls=%.16s",
                            k, ch.Name().c_str(), ch.Class().c_str());
                        Write(buf);
                        ++k;
                    }
                }
                const auto charsFolder = Globals::Roblox::Workspace.FindFirstChild("Characters");
                if (charsFolder.address)
                {
                    snprintf(buf, sizeof(buf), "[PFDIAG] charsFolder kids=%d",
                        (int)charsFolder.GetChildren().size());
                    Write(buf);
                    int k = 0;
                    for (auto& ch : charsFolder.GetChildren())
                    {
                        if (k >= 8) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG]   cf[%d] name=%.24s cls=%.16s kids=%d",
                            k, ch.Name().c_str(), ch.Class().c_str(), (int)ch.GetChildren().size());
                        Write(buf);
                        ++k;
                    }
                }

                // Rivals: dump ShootingRangeEntities Target characters to see if they have player refs
                if (Globals::Roblox::isRivals)
                {
                    const auto sre = Globals::Roblox::Workspace.FindFirstChild("ShootingRangeEntities");
                    if (sre.address)
                    {
                        snprintf(buf, sizeof(buf), "[PFDIAG] SRE kids=%d",
                            (int)sre.GetChildren().size());
                        Write(buf);
                        int k = 0;
                        for (auto& ch : sre.GetChildren())
                        {
                            if (k >= 10) break;
                            auto kids = ch.GetChildren();
                            bool hasPlayerRef = false;
                            for (auto& gc : kids)
                            {
                                std::string low = gc.Name();
                                for (char& c : low) c = (char)tolower((unsigned char)c);
                                if (low.find("player") != std::string::npos || low.find("owner") != std::string::npos)
                                    hasPlayerRef = true;
                            }
                            snprintf(buf, sizeof(buf), "[PFDIAG] SRE[%d] name=%.24s cls=%.16s kids=%d hasPlayerRef=%d",
                                k, ch.Name().c_str(), ch.Class().c_str(), (int)kids.size(), hasPlayerRef);
                            Write(buf);
                            if (hasPlayerRef)
                            {
                                for (auto& gc : kids)
                                {
                                    std::string low = gc.Name();
                                    for (char& c : low) c = (char)tolower((unsigned char)c);
                                    if (low.find("player") != std::string::npos || low.find("owner") != std::string::npos)
                                    {
                                        snprintf(buf, sizeof(buf), "[PFDIAG]   SRE.ref kid=%.24s cls=%.16s",
                                            gc.Name().c_str(), gc.Class().c_str());
                                        Write(buf);
                                    }
                                }
                            }
                            ++k;
                        }
                    }
                }
            }

            // Are the toggles even on? ESP/Aimbot silently draw nothing when
            // the master switch (or every sub-element) is off, which is
            // indistinguishable from "no targets" in the log.
            snprintf(buf, sizeof(buf),
                "[PFDIAG] opts espEn=%d espTog=%d box=%d boxType=%d skel=%d name=%d health=%d tcOpt=%d aimEn=%d aim=%d aimTc=%d addrTC=%p",
                (int)Options::ESP::Enabled, (int)Options::ESP::Toggled,
                (int)Options::ESP::Box, (int)Options::ESP::BoxType,
                (int)Options::ESP::Skeleton, (int)Options::ESP::Name,
                (int)Options::ESP::Health, (int)Options::ESP::TeamCheck,
                (int)Options::Aimbot::Enabled, (int)Options::Aimbot::Aimbot,
                (int)Options::Aimbot::TeamCheck, &Options::ESP::TeamCheck);
            Write(buf);

            // Every Players-service entry, so we can see WHICH players got
            // dropped for a null Character (playerobjectscache.h skips them).
            {
                int i = 0;
                for (const auto& pl : Globals::Caches::CachedPlayers)
                {
                    if (i >= 12) break;
                    if (!pl.address) { ++i; continue; }
                    const auto ch = pl.Character();
                    snprintf(buf, sizeof(buf),
                        "[PFDIAG] plr[%d] name=%.24s class=%.16s char=%llu",
                        i, pl.Name().c_str(), pl.Class().c_str(),
                        (unsigned long long)ch.address);
                    Write(buf);
                    ++i;
                }
            }

            // Where does the team actually come from?  Player.Team is often
            // stripped by these games, in which case the team lives somewhere
            // else: the Teams service, a StringValue child, or TeamColor.
            {
                const auto teamsSvc = Globals::Roblox::DataModel.FindFirstChild("Teams");
                if (teamsSvc.address)
                {
                    int i = 0;
                    for (auto& t : teamsSvc.GetChildren())
                    {
                        if (i >= 8) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG] svcTeam[%d] name=%.20s cls=%.12s addr=%llu",
                            i, t.Name().c_str(), t.Class().c_str(), (unsigned long long)t.address);
                        Write(buf);
                        ++i;
                    }
                }
                else
                    Write("[PFDIAG] svcTeam: no Teams service");

                int i = 0;
                for (const auto& pl : Globals::Caches::CachedPlayers)
                {
                    if (i >= 6) break;
                    if (!pl.address) { ++i; continue; }
                    const auto team = pl.Team();
                    const int rawColor = Memory->read<int>(pl.address + Offsets::Player::TeamColor);
                    snprintf(buf, sizeof(buf),
                        "[PFDIAG] plrTeam[%d] name=%.20s team=%llu teamName=%.20s rawColor=%d",
                        i, pl.Name().c_str(), (unsigned long long)team.address,
                        team.Name().c_str(), rawColor);
                    Write(buf);
                    int k = 0;
                    for (auto& ch : pl.GetChildren())
                    {
                        if (k >= 6) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG]   plrChild[%d.%d] name=%.20s cls=%.12s",
                            i, k, ch.Name().c_str(), ch.Class().c_str());
                        Write(buf);
                        ++k;
                    }
                    ++i;
                }

                // Rivals: deep probe for ANY client-visible team signal.
                // Team/TeamColor/attributes all read neutral in Rivals, so find where
                // the game actually encodes teammate-vs-enemy (Highlight, BillboardGui
                // name-tag color, leaderstats values, team folders in Workspace).
                {
                    static uint64_t lastDeepTeam = 0;
                    if (Globals::Roblox::isRivals && GetTickCount64() - lastDeepTeam > 15000)
                    {
                        lastDeepTeam = GetTickCount64();

                        // (a) Workspace scan (depth<=2) for Team folders / Team instances / Highlight / BillboardGui
                        int tseen = 0;
                        {
                            std::vector<std::pair<RobloxInstance, int>> stk;
                            for (auto& c : Globals::Roblox::Workspace.GetChildren())
                                stk.push_back({ c, 0 });
                            while (!stk.empty() && tseen < 10)
                            {
                                auto cur = stk.back(); stk.pop_back();
                                if (!cur.first.address || cur.second > 2) continue;
                                std::string cls = cur.first.Class();
                                std::string nm = cur.first.Name();
                                std::string low = nm;
                                for (char& q : low) q = (char)tolower((unsigned char)q);
                                if (cls == "Team" || cls == "Highlight" || cls == "BillboardGui" ||
                                    low.find("team") != std::string::npos)
                                {
                                    snprintf(buf, sizeof(buf), "[PFDIAG] steam name=%.40s cls=%.16s d=%d",
                                        nm.c_str(), cls.c_str(), cur.second);
                                    Write(buf);
                                    ++tseen;
                                }
                                if (cur.second < 2)
                                    for (auto& c2 : cur.first.GetChildren())
                                        stk.push_back({ c2, cur.second + 1 });
                            }
                        }
                        if (tseen == 0)
                            Write("[PFDIAG] steam none (no Team/Highlight/Billboard in Workspace d<=2)");

                        // (b) leaderstats values per real player
                        for (const auto& pl : Globals::Caches::CachedPlayers)
                        {
                            if (!pl.address) continue;
                            std::string pnm = pl.Name();
                            if (pnm.empty()) continue;
                            auto ls = pl.FindFirstChild("CustomLeaderstats");
                            if (!ls.address) ls = pl.FindFirstChild("leaderstats");
                            if (!ls.address) continue;
                            int k = 0;
                            for (auto& v : ls.GetChildren())
                            {
                                if (k >= 6) break;
                                snprintf(buf, sizeof(buf), "[PFDIAG] ls %s|%.20s|cls=%.16s",
                                    pnm.substr(0, 12).c_str(), v.Name().c_str(), v.Class().c_str());
                                Write(buf);
                                ++k;
                            }
                        }

                        // (c) non-part descendants of each cached character (GUI / values / Highlight)
                        for (const auto& p : Globals::Caches::FramePlayers())
                        {
                            if (!p.Character.address || p.Name.empty()) continue;
                            std::vector<std::pair<RobloxInstance, int>> stk2;
                            stk2.push_back({ p.Character, 0 });
                            int budget = 40;
                            while (!stk2.empty() && budget > 0)
                            {
                                auto cur = stk2.back(); stk2.pop_back();
                                --budget;
                                if (!cur.first.address || cur.second > 1) continue;
                                for (auto& c2 : cur.first.GetChildren())
                                {
                                    if (!c2.address) continue;
                                    std::string cc = c2.Class();
                                    bool isPart = (cc == "Part" || cc == "MeshPart" || cc == "WedgePart" ||
                                        cc == "TrussPart" || cc == "CornerWedgePart" || cc == "UnionOperation" ||
                                        cc == "SkinnedMeshPart" || cc == "Seat" || cc == "VehicleSeat");
                                    if (!isPart)
                                    {
                                        snprintf(buf, sizeof(buf), "[PFDIAG] chd %s|%.28s|cls=%.20s d=%d",
                                            p.Name.substr(0, 12).c_str(), c2.Name().c_str(), cc.c_str(), cur.second + 1);
                                        Write(buf);
                                    }
                                    if (cur.second < 1 && (isPart || cc == "Model"))
                                        stk2.push_back({ c2, cur.second + 1 });
                                }
                            }
                        }
                    }
                }

                // DEBUG: per-player teammate-label resolution. Reports whether the
                // cached flag is set AND, if a TeammateLabel exists, which part owns
                // it. This distinguishes "detection missed the part" from "ESP never
                // consumed the flag" in a single run.
                static uint64_t lastTld = 0;
                if (Globals::Roblox::isRivals && GetTickCount64() - lastTld > 8000)
                {
                    lastTld = GetTickCount64();
                    for (const auto& p : Globals::Caches::FramePlayers())
                    {
                        if (p.Name.empty()) continue;
                        std::string owner = "none";
                        if (p.Character.address)
                        {
                            for (auto& k : p.Character.GetChildren())
                            {
                                if (!k.address) continue;
                                if (k.Name() == "TeammateLabel") { owner = "CHAR:" + k.Class(); break; }
                                for (auto& gc : k.GetChildren())
                                {
                                    if (gc.address && gc.Name() == "TeammateLabel")
                                    { owner = "PART:" + k.Name(); break; }
                                }
                                if (owner != "none") break;
                            }
                        }
                        snprintf(buf, sizeof(buf), "[PFDIAG] tld %s|flag=%d|owner=%s|self=%d",
                            p.Name.substr(0, 14).c_str(), (int)p.HasTeammateLabel,
                            owner.c_str(), (int)(p.address == Globals::Roblox::LocalPlayer.address));
                        Write(buf);
                    }
                }

                // DEBUG: dump isRivals flag
                static uint64_t lastDbg = 0;
                if (GetTickCount64() - lastDbg > 5000)
                {
                    lastDbg = GetTickCount64();
                    snprintf(buf, sizeof(buf), "[PFDIAG] DBG isRivals=%d placeID=%lld",
                        (int)Globals::Roblox::isRivals, (long long)Globals::Roblox::lastPlaceID);
                    Write(buf);
                }

                // DEBUG: verify plrAttr block executes
                static uint64_t lastPlrAttr = 0;
                if (GetTickCount64() - lastPlrAttr > 5000)
                {
                    lastPlrAttr = GetTickCount64();
                    Write("[PFDIAG] plrAttrBlock entered");

                    // Dump player attribute stores
                    for (const auto& pl : Globals::Caches::CachedPlayers)
                    {
                        if (!pl.address) continue;
                        const uintptr_t attrStore = Memory->read<uintptr_t>(pl.address + 0x40);
                        snprintf(buf, sizeof(buf), "[PFDIAG] plrAttrRaw %s store=%llu",
                            pl.Name().c_str(), (unsigned long long)attrStore);
                        Write(buf);
                    }

                    // Dump character attribute stores
                    for (const auto& p : Globals::Caches::FramePlayers())
                    {
                        if (!p.Character.address) continue;
                        const uintptr_t attrStore = Memory->read<uintptr_t>(p.Character.address + 0x40);
                        snprintf(buf, sizeof(buf), "[PFDIAG] charAttrRaw %s store=%llu",
                            p.Name.c_str(), (unsigned long long)attrStore);
                        Write(buf);
                    }

                    // Dump Duels folder
                    const auto duels = Globals::Roblox::DataModel.FindFirstChild("Duels");
                    if (duels.address)
                    {
                        for (auto& d : duels.GetChildren())
                        {
                            snprintf(buf, sizeof(buf), "[PFDIAG] Duels kid=%.24s cls=%.16s kids=%d",
                                d.Name().c_str(), d.Class().c_str(), (int)d.GetChildren().size());
                            Write(buf);
                        }
                    }
                }

                // Rivals-specific: check Duels folder and player/character attributes
                if (Globals::Roblox::isRivals)
                {
                    const auto duels = Globals::Roblox::DataModel.FindFirstChild("Duels");
                    if (duels.address)
                    {
                        for (auto& d : duels.GetChildren())
                        {
                            snprintf(buf, sizeof(buf), "[PFDIAG] Duels kid=%.24s cls=%.16s kids=%d",
                                d.Name().c_str(), d.Class().c_str(), (int)d.GetChildren().size());
                            Write(buf);
                            for (auto& gc : d.GetChildren())
                            {
                                snprintf(buf, sizeof(buf), "[PFDIAG]   Duels.gc=%.24s cls=%.16s", gc.Name().c_str(), gc.Class().c_str());
                                Write(buf);
                            }
                        }
                    }

                    // Check player AttributeStorage for Team attribute
                    for (const auto& pl : Globals::Caches::CachedPlayers)
                    {
                        if (!pl.address) continue;
                        const uintptr_t attrStore = Memory->read<uintptr_t>(pl.address + 0x40);
                        if (attrStore > 0x10000)
                        {
                            unsigned char w[0x100]{};
                            Memory->readRaw(attrStore, w, sizeof(w));
                            char txt[0x101]{};
                            for (size_t b = 0; b < sizeof(w); ++b)
                                txt[b] = (w[b] >= 32 && w[b] < 127) ? (char)w[b] : '.';
                            txt[sizeof(w)] = 0;
                            snprintf(buf, sizeof(buf), "[PFDIAG] plrAttr %s store=%llu raw=%.120s",
                                pl.Name().c_str(), (unsigned long long)attrStore, txt);
                            Write(buf);
                        }
                    }
                }
            }

            // Every character model actually sitting in Workspace, so we can
            // compare against the player list above (matched by name?).
            {
                int i = 0;
                for (const auto& w : wsChars)
                {
                    if (i >= 12) break;
                    const auto hrp = w.model.FindFirstChild("HumanoidRootPart");
                    auto pp = hrp.address ? hrp.Position() : Vectors::Vector3{ 0,0,0 };
                    // Parent chain up to Workspace, so we can tell a live rig in
                    // Characters from a culled/pooled copy parked elsewhere.
                    auto up = RobloxInstance(Memory->read<uintptr_t>(w.model.address + Offsets::Instance::Parent));
                    std::string path;
                    for (int d = 0; d < 4 && up.address && up.address != Globals::Roblox::Workspace.address; ++d)
                    {
                        path = up.Name() + "/" + path;
                        up = RobloxInstance(Memory->read<uintptr_t>(up.address + Offsets::Instance::Parent));
                    }
                    snprintf(buf, sizeof(buf),
                        "[PFDIAG] ws[%d] name=%.24s char=%llu hrp=%llu pos=(%.0f,%.0f,%.0f) path=%.32s",
                        i, w.model.Name().c_str(),
                        (unsigned long long)w.model.address,
                        (unsigned long long)hrp.address,
                        pp.x, pp.y, pp.z, path.c_str());
                    Write(buf);
                    ++i;
                }
            }

            // Attribute probe: Blox Strike keeps the team in the Player attribute
            // "Team" (Player.Team / TeamColor and the Teams service are all dead
            // ends). Roblox hangs the attribute container off every Instance at
            // AttributeStorage (0x40) -- dump it so we can find "Team".
            {
                static uint64_t lastAttr = 0;
                const uint64_t nowAttr = GetTickCount64();
                if (nowAttr - lastAttr > 20000)
                {
                    lastAttr = nowAttr;

                    auto ascii = [](const unsigned char* w, size_t n, char* out) {
                        for (size_t b = 0; b < n; ++b)
                            out[b] = (w[b] >= 32 && w[b] < 127) ? (char)w[b] : '.';
                        out[n] = 0;
                    };

                    struct AT { const char* tag; uintptr_t addr; };
                    std::vector<AT> ats;
                    if (Globals::Roblox::LocalPlayer.address)
                        ats.push_back({ "local", Globals::Roblox::LocalPlayer.address });
                    for (const auto& pl : Globals::Caches::CachedPlayers)
                    {
                        if (pl.address && pl.address != Globals::Roblox::LocalPlayer.address)
                        {
                            ats.push_back({ "other", pl.address });
                            if (ats.size() >= 3) break;
                        }
                    }

                    for (const auto& a : ats)
                    {
                        const uintptr_t storage = Memory->read<uintptr_t>(a.addr + 0x40);
                        snprintf(buf, sizeof(buf), "[PFDIAG] attr %s player=%llu storage=%llu",
                            a.tag, (unsigned long long)a.addr, (unsigned long long)storage);
                        Write(buf);
                        if (storage < 0x10000 || storage > 0x7fffffffffffULL) continue;

                        char txt0[0x101]{};
                        {
                            unsigned char w0[0x100]{};
                            Memory->readRaw(storage, w0, sizeof(w0));
                            for (size_t b = 0; b < sizeof(w0); ++b)
                                txt0[b] = (w0[b] >= 32 && w0[b] < 127) ? (char)w0[b] : '.';
                            txt0[sizeof(w0)] = 0;
                        }
                        snprintf(buf, sizeof(buf), "[PFDIAG] attr0 raw=%.120s", txt0);
                        Write(buf);

                        // Bounded walk of the container looking for the "Team" key.
                        std::vector<std::pair<int, uintptr_t>> frontier;
                        frontier.push_back({ 0, storage });
                        int visited = 0;
                        while (!frontier.empty() && visited < 60)
                        {
                            const std::pair<int, uintptr_t> curNode = frontier.back();
                            frontier.pop_back();
                            const int depth = curNode.first;
                            const uintptr_t node = curNode.second;
                            if (node < 0x10000 || node > 0x7fffffffffffULL) continue;
                            ++visited;

                            unsigned char w[0x100]{};
                            Memory->readRaw(node, w, sizeof(w));
                            int key = -1;
                            for (size_t b = 0; b + 5 <= sizeof(w); ++b)
                                if (w[b] == 'T' && w[b + 1] == 'e' && w[b + 2] == 'a' &&
                                    w[b + 3] == 'm' && w[b + 4] == 0)
                                { key = (int)b; break; }

                            if (key >= 0)
                            {
                                char txt[0x101]{};
                                for (size_t b = 0; b < sizeof(w); ++b)
                                    txt[b] = (w[b] >= 32 && w[b] < 127) ? (char)w[b] : '.';
                                txt[sizeof(w)] = 0;
                                snprintf(buf, sizeof(buf),
                                    "[PFDIAG] attrKey d=%d at=0x%x node=%llu txt=%.120s",
                                    depth, key, (unsigned long long)node, txt);
                                Write(buf);
                            }

                            if (depth >= 3) continue;
                            for (int k = 0; k < 8; ++k)
                            {
                                const uintptr_t p = Memory->read<uintptr_t>(node + (uintptr_t)k * 8);
                                if (p >= 0x10000 && p <= 0x7fffffffffffULL)
                                    frontier.push_back({ depth + 1, p });
                            }
                        }
                        snprintf(buf, sizeof(buf), "[PFDIAG] attrWalk done visited=%d", visited);
                        Write(buf);
                    }
                }
            }

        }

        {
            snprintf(buf, sizeof(buf),
                "[PFDIAG] fovfill FOV=%d showFOV=%d showFill=%d grad=%d shape=%d fill=(%.2f,%.2f,%.2f,%.2f) top=(%.2f,%.2f,%.2f,%.2f) bot=(%.2f,%.2f,%.2f,%.2f)",
                (int)Options::Aimbot::FOV, (int)Options::Aimbot::ShowFOV, (int)Options::Aimbot::ShowFOVFill,
                (int)Options::Aimbot::FOVFillGradient, (int)Options::Aimbot::FOVShape,
                Options::Aimbot::FOVFillColor[0], Options::Aimbot::FOVFillColor[1], Options::Aimbot::FOVFillColor[2], Options::Aimbot::FOVFillColor[3],
                Options::Aimbot::FOVFillTopColor[0], Options::Aimbot::FOVFillTopColor[1], Options::Aimbot::FOVFillTopColor[2], Options::Aimbot::FOVFillTopColor[3],
                Options::Aimbot::FOVFillBottomColor[0], Options::Aimbot::FOVFillBottomColor[1], Options::Aimbot::FOVFillBottomColor[2], Options::Aimbot::FOVFillBottomColor[3]);
            Write(buf);
        }

        if (!Globals::Roblox::isPhantomForces)
            return;
        if (true)
            return;

        Write("[PFDIAG] ===== dump =====");

        snprintf(buf, sizeof(buf),
            "[PFDIAG] place=%lld isPF=%d ws=%s players=%s cam=%s lp=%s",
            (long long)Globals::Roblox::lastPlaceID,
            (int)Globals::Roblox::isPhantomForces,
            Hex(Globals::Roblox::Workspace.address).c_str(),
            Hex(Globals::Roblox::Players.address).c_str(),
            Hex(Globals::Roblox::Camera.address).c_str(),
            Hex(Globals::Roblox::LocalPlayer.address).c_str());
        Write(buf);

        {
            std::string inst = RobloxVersion::GetClientVersion();
            snprintf(buf, sizeof(buf),
                "[PFDIAG] bundled=%s installed=%s status=%d",
                Offsets::ClientVersion.c_str(), inst.c_str(), RobloxVersion::GetStatus());
            Write(buf);
        }

        {
            auto lc = Globals::Roblox::LocalPlayer.Character();
            snprintf(buf, sizeof(buf),
                "[PFDIAG] local name='%s' char=%s modelRaw=%s",
                Globals::Roblox::LocalPlayer.Name().c_str(),
                Hex(lc.address).c_str(),
                Hex(Memory->read<uintptr_t>(Globals::Roblox::LocalPlayer.address + Offsets::Player::ModelInstance)).c_str());
            Write(buf);
        }

        auto players = Globals::Roblox::Players.GetChildren();
        snprintf(buf, sizeof(buf), "[PFDIAG] Players.children=%zu", players.size());
        Write(buf);
        {
            int i = 0;
            for (auto& pl : players)
            {
                if (i >= 6) break;
                snprintf(buf, sizeof(buf),
                    "[PFDIAG]  player[%d] name='%s' char=%s modelRaw=%s",
                    i, pl.Name().c_str(),
                    Hex(pl.Character().address).c_str(),
                    Hex(Memory->read<uintptr_t>(pl.address + Offsets::Player::ModelInstance)).c_str());
                Write(buf);
                ++i;
            }
        }

        auto wsChildren = Globals::Roblox::Workspace.GetChildren();
        snprintf(buf, sizeof(buf), "[PFDIAG] Workspace.children=%zu", wsChildren.size());
        Write(buf);
        {
            int i = 0;
            for (auto& c : wsChildren)
            {
                if (i >= 24) break;
                snprintf(buf, sizeof(buf),
                    "[PFDIAG]  ws[%d] name='%s' cls='%s'",
                    i, c.Name().c_str(), c.Class().c_str());
                Write(buf);
                ++i;
            }
        }

        RobloxInstance pfPlayersFolder(0);
        for (auto& c : wsChildren)
        {
            if (c.Name() == "Players")
            {
                pfPlayersFolder = c;
                break;
            }
        }
        if (pfPlayersFolder.address)
        {
            auto pfChildren = pfPlayersFolder.GetChildren();
            snprintf(buf, sizeof(buf), "[PFDIAG] pfPlayers folder='%s' cls='%s' children=%zu",
                pfPlayersFolder.Name().c_str(), pfPlayersFolder.Class().c_str(), pfChildren.size());
            Write(buf);
            int i = 0;
            for (auto& ch : pfChildren)
            {
                if (i >= 20) break;
                snprintf(buf, sizeof(buf),
                    "[PFDIAG]  pfchild[%d] name='%s' cls='%s'",
                    i, ch.Name().c_str(), ch.Class().c_str());
                Write(buf);
                auto subc = ch.GetChildren();
                snprintf(buf, sizeof(buf),
                    "[PFDIAG]   pfchild[%d] subchildren=%zu",
                    i, subc.size());
                Write(buf);
                int j = 0;
                for (auto& sc : subc)
                {
                    if (j >= 6) break;
                    auto p = sc.Position();
                    snprintf(buf, sizeof(buf),
                        "[PFDIAG]    sub[%d] name='%s' cls='%s' pos=(%.0f,%.0f,%.0f)",
                        j, sc.Name().c_str(), sc.Class().c_str(), p.x, p.y, p.z);
                    Write(buf);
                    ++j;
                }
                ++i;
            }
        }
        else
        {
            Write("[PFDIAG] pfPlayers folder NOT FOUND in workspace");
        }

        {
            auto rsRoot = Globals::Roblox::DataModel.FindFirstChildWhichIsA("ReplicatedStorage");
            snprintf(buf, sizeof(buf), "[PFDIAG] ReplicatedStorage root=%s",
                rsRoot.address ? Hex(rsRoot.address).c_str() : "0");
            Write(buf);
            if (rsRoot.address)
            {
                auto rs = rsRoot.GetChildren();
                snprintf(buf, sizeof(buf), "[PFDIAG] ReplicatedStorage children=%zu", rs.size());
                Write(buf);
                int i = 0;
                for (auto& r : rs)
                {
                    if (i >= 25) break;
                    snprintf(buf, sizeof(buf), "[PFDIAG]  rs[%d] name='%s' cls='%s'",
                        i, r.Name().c_str(), r.Class().c_str());
                    Write(buf);
                    ++i;
                }
            }
        }

        {
            for (auto& c : wsChildren)
            {
                if (c.Name() == "Map")
                {
                    snprintf(buf, sizeof(buf), "[PFDIAG] Map name='%s' cls='%s'",
                        c.Name().c_str(), c.Class().c_str());
                    Write(buf);
                    auto mc = c.GetChildren();
                    int i = 0;
                    for (auto& cc : mc)
                    {
                        if (i >= 12) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG]  mapchild[%d] name='%s' cls='%s'",
                            i, cc.Name().c_str(), cc.Class().c_str());
                        Write(buf);
                        ++i;
                    }
                }
                if (c.Name() == "Ignore")
                {
                    snprintf(buf, sizeof(buf), "[PFDIAG] Ignore top children=%zu", c.GetChildren().size());
                    Write(buf);
                    auto mc = c.GetChildren();
                    int i = 0;
                    for (auto& cc : mc)
                    {
                        if (i >= 20) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG]  ignorechild[%d] name='%s' cls='%s'",
                            i, cc.Name().c_str(), cc.Class().c_str());
                        Write(buf);
                        ++i;
                    }
                }
                if (c.Name() == "Roots")
                {
                    auto rc = c.GetChildren();
                    snprintf(buf, sizeof(buf), "[PFDIAG] Roots children=%zu", rc.size());
                    Write(buf);
                    int i = 0;
                    for (auto& rr : rc)
                    {
                        if (i >= 30) break;
                        auto p = rr.Position();
                        Vectors::Vector3 sz = {0,0,0};
                        Vectors::Vector3 vel = {0,0,0};
                        uintptr_t prim = Memory->read<uintptr_t>(rr.address + Offsets::BasePart::Primitive);
                        if (prim)
                        {
                            sz = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Size);
                            vel = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
                        }
                        snprintf(buf, sizeof(buf),
                            "[PFDIAG]  root[%d] name='%s' cls='%s' pos=(%.0f,%.0f,%.0f) size=(%.2f,%.2f,%.2f) vel=(%.0f,%.0f,%.0f) prim=%s",
                            i, rr.Name().c_str(), rr.Class().c_str(), p.x, p.y, p.z,
                            sz.x, sz.y, sz.z, vel.x, vel.y, vel.z, prim ? Hex(prim).c_str() : "0");
                        Write(buf);
                        ++i;
                    }
                }
            }
        }

        {
            auto psvc = Globals::Roblox::Players;
            auto psc = psvc.GetChildren();
            snprintf(buf, sizeof(buf), "[PFDIAG] PlayersService children=%zu", psc.size());
            Write(buf);
            int i = 0;
            for (auto& pl : psc)
            {
                if (i >= 12) break;
                auto t = Memory->read<uintptr_t>(pl.address + Offsets::Player::Team);
                auto tc = Memory->read<int>(pl.address + Offsets::Player::TeamColor);
                auto isLocal = Memory->read<bool>(pl.address + Offsets::Player::LocalPlayer);
                auto mdl = Memory->read<uintptr_t>(pl.address + Offsets::Player::ModelInstance);
                snprintf(buf, sizeof(buf),
                    "[PFDIAG]  ps[%d] name='%s' local=%d team=%s teamColor=%d model=%s",
                    i, pl.Name().c_str(), (int)isLocal, t ? Hex(t).c_str() : "0", tc,
                    mdl ? Hex(mdl).c_str() : "0");
                Write(buf);
                ++i;
            }
        }

        {
            for (auto& c : wsChildren)
            {
                if (c.Name() != "Ignore")
                    continue;
                int k = 0;
                for (auto& ig : c.GetChildren())
                {
                    if (k >= 8) break;
                    snprintf(buf, sizeof(buf), "[PFDIAG]  ign* name='%s' cls='%s'", ig.Name().c_str(), ig.Class().c_str());
                    Write(buf);
                    if (ig.Class() == "Model")
                    {
                        uintptr_t hid = 0;
                        std::function<void(const RobloxInstance&)> findH =
                            [&](const RobloxInstance& node) {
                                if (hid) return;
                                if (node.Class() == "Humanoid") { hid = node.address; return; }
                                for (auto& s : node.GetChildren()) findH(s);
                            };
                        findH(ig);
                        snprintf(buf, sizeof(buf), "[PFDIAG]  ign model '%s' humanoid=%s",
                            ig.Name().c_str(), hid ? Hex(hid).c_str() : "0");
                        Write(buf);
                        if (hid)
                        {
                            float hp = Memory->read<float>(hid + Offsets::Humanoid::Health);
                            float maxHp = Memory->read<float>(hid + Offsets::Humanoid::MaxHealth);
                            uintptr_t hroot = Memory->read<uintptr_t>(hid + Offsets::Humanoid::HumanoidRootPart);
                            snprintf(buf, sizeof(buf), "[PFDIAG]  ign humanoid hp=%.1f max=%.1f root=%s",
                                hp, maxHp, hroot ? Hex(hroot).c_str() : "0");
                            Write(buf);
                            if (hroot)
                            {
                                RobloxInstance rp(hroot);
                                auto pp = rp.Position();
                                snprintf(buf, sizeof(buf), "[PFDIAG]  ign hroot name='%s' cls='%s' pos=(%.0f,%.0f,%.0f)",
                                    rp.Name().c_str(), rp.Class().c_str(), pp.x, pp.y, pp.z);
                                Write(buf);
                            }
                        }
                    }
                    ++k;
                }
            }
        }

        {
            snprintf(buf, sizeof(buf), "[PFDIAG] LocalPlayer addr=%s", Hex(Globals::Roblox::LocalPlayer.address).c_str());
            Write(buf);
            auto psc = Globals::Roblox::Players.GetChildren();
            int i = 0;
            for (auto& pl : psc)
            {
                if (i >= 12) break;
                snprintf(buf, sizeof(buf), "[PFDIAG]  psMatch[%d] name='%s' same=%d",
                    i, pl.Name().c_str(), (int)(pl.address == Globals::Roblox::LocalPlayer.address));
                Write(buf);
                ++i;
            }
        }

        {
            auto taddr = Memory->read<uintptr_t>(Globals::Roblox::LocalPlayer.address + Offsets::Player::Team);
            auto tcol = Memory->read<int>(Globals::Roblox::LocalPlayer.address + Offsets::Player::TeamColor);
            snprintf(buf, sizeof(buf), "[PFDIAG] localTeam=%s localTeamColor=%d cached=%s/%d/%s",
                taddr ? Hex(taddr).c_str() : "0", tcol,
                Globals::Roblox::LocalPlayerTeam.address ? Hex(Globals::Roblox::LocalPlayerTeam.address).c_str() : "0",
                Globals::Roblox::LocalPlayerTeamColor, Globals::Roblox::LocalPlayerTeamName.c_str());
            Write(buf);
        }

        {
            for (auto& c : wsChildren)
            {
                if (c.Name() != "Roots")
                    continue;
                int i = 0;
                for (auto& rr : c.GetChildren())
                {
                    if (i >= 8) break;
                    uintptr_t prim = Memory->read<uintptr_t>(rr.address + Offsets::BasePart::Primitive);
                    uintptr_t owner = prim ? Memory->read<uintptr_t>(prim + Offsets::Primitive::Owner) : 0;
                    std::string ownName = "?", ownCls = "?";
                    std::string ownKids = "-";
                    std::string ownHum = "0";
                    if (owner)
                    {
                        RobloxInstance oi(owner);
                        ownName = oi.Name().empty() ? "?" : oi.Name();
                        ownCls = oi.Class();
                        std::vector<std::string> kids;
                        for (auto& sc : oi.GetChildren())
                        {
                            if (kids.size() >= 6) break;
                            kids.push_back(sc.Class() + ":'" + sc.Name() + "'");
                        }
                        ownKids = kids.empty() ? "-" : std::to_string((int)(oi.GetChildren().size()));
                        std::function<void(const RobloxInstance&)> findH =
                            [&](const RobloxInstance& node) {
                                if (ownHum != "0") return;
                                if (node.Class() == "Humanoid") { ownHum = Hex(node.address); return; }
                                for (auto& s : node.GetChildren()) findH(s);
                            };
                        findH(oi);
                    }
                    snprintf(buf, sizeof(buf), "[PFDIAG]  rootO[%d] get=%s own=%s ownName='%s' cls='%s' kids=%s hum=%s",
                        i, prim ? Hex(prim).c_str() : "0", Hex(owner).c_str(),
                        ownName.c_str(), ownCls.c_str(), ownKids.c_str(), ownHum.c_str());
                    Write(buf);
                    if (owner)
                    {
                        float hp = -1, mx = -1;
                        uintptr_t hid = 0;
                        std::string hs = ownHum;
                        if (hs != "0")
                        {
                            sscanf_s(hs.c_str(), "%llx", &hid);
                            hp = Memory->read<float>(hid + Offsets::Humanoid::Health);
                            mx = Memory->read<float>(hid + Offsets::Humanoid::MaxHealth);
                        }
                        snprintf(buf, sizeof(buf), "[PFDIAG]   rootO[%d] ownerHp=%.1f ownerMax=%.1f",
                            i, hp, mx);
                        Write(buf);
                    }
                    ++i;
                }
            }
        }

        {
            auto psc = Globals::Roblox::Players.GetChildren();
            int c23 = 0, c106 = 0, cOther = 0;
            for (auto& pl : psc)
            {
                auto tcol = Memory->read<int>(pl.address + Offsets::Player::TeamColor);
                if (tcol == 23) c23++;
                else if (tcol == 106) c106++;
                else cOther++;
            }
            snprintf(buf, sizeof(buf), "[PFDIAG]  psColors c23=%d c106=%d cOther=%d total=%zu",
                c23, c106, cOther, psc.size());
            Write(buf);
            uintptr_t cam = Globals::Roblox::Workspace.address
                ? Memory->read<uintptr_t>(Globals::Roblox::Workspace.address + Offsets::Workspace::CurrentCamera) : 0;
            snprintf(buf, sizeof(buf), "[PFDIAG]  cam=0x%llx cachedCam=%s",
                (unsigned long long)cam,
                Globals::Roblox::Camera.address ? Hex(Globals::Roblox::Camera.address).c_str() : "0");
            Write(buf);
            if (cam)
            {
                Vectors::Vector3 cp = Memory->read<Vectors::Vector3>(cam + Offsets::Camera::Position);
                snprintf(buf, sizeof(buf), "[PFDIAG]  camPos=(%.0f,%.0f,%.0f)", cp.x, cp.y, cp.z);
                Write(buf);
            }
        }

        {
            RobloxInstance pf0(0), pf1(0);
            for (auto& c : wsChildren)
            {
                if (c.Name() != "Players")
                    continue;
                auto chs = c.GetChildren();
                if (chs.size() >= 1) pf0 = chs[0];
                if (chs.size() >= 2) pf1 = chs[1];
            }
            auto countModels = [&](RobloxInstance& f) -> int
            {
                int n = 0;
                for (auto& m : f.GetChildren())
                    if (m.Class() == "Model") n++;
                return n;
            };
            snprintf(buf, sizeof(buf), "[PFDIAG]  fold0 mods=%d fold1 mods=%d",
                countModels(pf0), countModels(pf1));
            Write(buf);
        }

        {
            RobloxInstance pff(0);
            for (auto& c : wsChildren)
            {
                if (c.Name() == "Players") { pff = c; break; }
            }
            if (pff.address)
            {
                int fc = 0;
                for (auto& ch : pff.GetChildren())
                {
                    if (fc >= 4) break;
                    auto subc = ch.GetChildren();
                    snprintf(buf, sizeof(buf), "[PFDIAG]  pfchild '%s' kids=%zu", ch.Name().c_str(), subc.size());
                    Write(buf);
                    int j = 0;
                    for (auto& sc : subc)
                    {
                        if (j >= 6) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG]   pfmod[%d] name='%s' cls='%s' addr=%s",
                            j, sc.Name().c_str(), sc.Class().c_str(), Hex(sc.address).c_str());
                        Write(buf);
                        ++j;
                    }
                    ++fc;
                }
            }
        }

        {
            RobloxInstance pf0(0), pf1(0);
            for (auto& c : wsChildren)
            {
                if (c.Name() != "Players")
                    continue;
                auto chs = c.GetChildren();
                if (chs.size() >= 1) pf0 = chs[0];
                if (chs.size() >= 2) pf1 = chs[1];
            }
            auto modelPos = [&](const RobloxInstance& m) -> Vectors::Vector3
            {
                Vectors::Vector3 acc = {0,0,0};
                int n = 0;
                std::function<void(const RobloxInstance&)> accParts =
                    [&](const RobloxInstance& node) {
                        if (n >= 4) return;
                        std::string cl = node.Class();
                        if (cl == "Part" || cl == "MeshPart" || cl == "Union")
                        {
                            auto pp = node.Position();
                            acc.x += pp.x; acc.y += pp.y; acc.z += pp.z;
                            ++n;
                            return;
                        }
                        for (auto& s : node.GetChildren()) accParts(s);
                    };
                accParts(m);
                if (n == 0) return acc;
                return Vectors::Vector3{ acc.x / (float)n, acc.y / (float)n, acc.z / (float)n };
            };
            auto nearestInFolder = [&](RobloxInstance& folder, const Vectors::Vector3& p,
                Vectors::Vector3& out, int& outN) -> float
            {
                float best = 1e9f;
                outN = 0;
                for (auto& m : folder.GetChildren())
                {
                    if (m.Class() != "Model") continue;
                    auto mp = modelPos(m);
                    float d2 = (mp.x - p.x) * (mp.x - p.x) + (mp.y * 0.1f - p.y * 0.1f) * (mp.y * 0.1f - p.y * 0.1f) + (mp.z - p.z) * (mp.z - p.z);
                    if (d2 < best)
                    {
                        best = d2;
                        out = mp;
                        outN++;
                    }
                }
                return best;
            };
            for (auto& c : wsChildren)
            {
                if (c.Name() != "Roots")
                    continue;
                int i = 0;
                for (auto& rr : c.GetChildren())
                {
                    if (i >= 16) break;
                    auto p = rr.Position();
                    Vectors::Vector3 m0, m1;
                    int n0 = 0, n1 = 0;
                    float d0 = nearestInFolder(pf0, p, m0, n0);
                    float d1 = nearestInFolder(pf1, p, m1, n1);
                    snprintf(buf, sizeof(buf),
                        "[PFDIAG]  geo[%d] pos=(%.0f,%.0f,%.0f) dF0=%.1f dF1=%.1f f0mods=%d f1mods=%d fold0='%s'",
                        i, p.x, p.y, p.z, d0, d1, n0, n1, pf0.Name().c_str());
                    Write(buf);
                    ++i;
                }
            }
        }

        {
            double bestRoot2 = 1e18f;
            Vectors::Vector3 bestRoot{-1,-1,-1};
            uintptr_t cam = Globals::Roblox::Workspace.address
                ? Memory->read<uintptr_t>(Globals::Roblox::Workspace.address + Offsets::Workspace::CurrentCamera) : 0;
            if (cam)
            {
                Vectors::Vector3 cp = Memory->read<Vectors::Vector3>(cam + Offsets::Camera::Position);
                for (auto& c : wsChildren)
                {
                    if (c.Name() != "Roots")
                        continue;
                    for (auto& rr : c.GetChildren())
                    {
                        auto p = rr.Position();
                        double dx = p.x - cp.x, dy = p.y - cp.y, dz = p.z - cp.z;
                        double d2 = dx*dx + dy*dy + dz*dz;
                        if (d2 < bestRoot2) { bestRoot2 = d2; bestRoot = p; }
                    }
                }
                snprintf(buf, sizeof(buf), "[PFDIAG]  nearestRoot2cam dist=%.1f root=(%.0f,%.0f,%.0f)",
                    sqrt(bestRoot2), bestRoot.x, bestRoot.y, bestRoot.z);
                Write(buf);
            }
        }

        {
            uintptr_t cam = Globals::Roblox::Workspace.address
                ? Memory->read<uintptr_t>(Globals::Roblox::Workspace.address + Offsets::Workspace::CurrentCamera) : 0;
            if (cam)
            {
                Vectors::Vector3 cp = Memory->read<Vectors::Vector3>(cam + Offsets::Camera::Position);
                RobloxInstance pf0(0), pf1(0);
                for (auto& c : wsChildren)
                {
                    if (c.Name() != "Players")
                        continue;
                    auto chs = c.GetChildren();
                    if (chs.size() >= 1) pf0 = chs[0];
                    if (chs.size() >= 2) pf1 = chs[1];
                }
                auto modelPos = [&](const RobloxInstance& m) -> Vectors::Vector3
                {
                    Vectors::Vector3 acc = {0,0,0};
                    int n = 0;
                    std::function<void(const RobloxInstance&)> accParts =
                        [&](const RobloxInstance& node) {
                            if (n >= 4) return;
                            std::string cl = node.Class();
                            if (cl == "Part" || cl == "MeshPart" || cl == "Union")
                            {
                                auto pp = node.Position();
                                acc.x += pp.x; acc.y += pp.y; acc.z += pp.z;
                                ++n;
                                return;
                            }
                            for (auto& s : node.GetChildren()) accParts(s);
                        };
                    accParts(m);
                    if (n == 0) return acc;
                    return Vectors::Vector3{ acc.x / (float)n, acc.y / (float)n, acc.z / (float)n };
                };
                auto nearestToCam = [&](RobloxInstance& f, const char* tag)
                {
                    double best = 1e18f;
                    std::string bname = "-";
                    for (auto& m : f.GetChildren())
                    {
                        if (m.Class() != "Model") continue;
                        auto mp = modelPos(m);
                        double dx = mp.x - cp.x, dy = mp.y - cp.y, dz = mp.z - cp.z;
                        double d2 = dx*dx + dy*dy + dz*dz;
                        if (d2 < best) { best = d2; bname = m.Name(); }
                    }
                    snprintf(buf, sizeof(buf), "[PFDIAG]  %s nearestModel2cam dist=%.1f name='%s'",
                        tag, sqrt(best), bname.c_str());
                    Write(buf);
                };
                nearestToCam(pf0, "fold0");
                nearestToCam(pf1, "fold1");
            }
        }

        {
            snprintf(buf, sizeof(buf), "[PFDIAG] LocalPlayer addr=%s teamColor=%d",
                Hex(Globals::Roblox::LocalPlayer.address).c_str(),
                Memory->read<int>(Globals::Roblox::LocalPlayer.address + Offsets::Player::TeamColor));
            Write(buf);
        }

        {
            auto hexOf = [&](uintptr_t v) -> std::string {
                char b[24]; snprintf(b, sizeof(b), "%llx", (unsigned long long)v); return std::string(b);
            };
            auto kidsOf = [&](uintptr_t addr) -> std::string {
                RobloxInstance n(addr);
                std::string s; int k = 0;
                for (auto& sc : n.GetChildren()) { if (k++ >= 8) break; s += sc.Class() + ":'" + sc.Name() + "' "; }
                return s.empty() ? std::string("-") : s;
            };
            for (auto& c : wsChildren)
            {
                if (c.Name() == "Roots")
                {
                    int i = 0;
                    for (auto& rr : c.GetChildren())
                    {
                        if (i >= 6) break;
                        uintptr_t par = Memory->read<uintptr_t>(rr.address + Offsets::Instance::Parent);
                        uintptr_t gp = par ? Memory->read<uintptr_t>(par + Offsets::Instance::Parent) : 0;
                        std::string parN = "?", parC = "?", gpN = "?", gpC = "?";
                        if (par) { RobloxInstance pi(par); parN = pi.Name(); parC = pi.Class(); }
                        if (gp) { RobloxInstance gi(gp); gpN = gi.Name(); gpC = gi.Class(); }
                        auto pp = rr.Position();
                        snprintf(buf, sizeof(buf),
                            "[PFDIAG] PROBE root[%d] pos=(%.0f,%.0f,%.0f) par='%s'/%s gp='%s'/%s nkids=%d [%s]",
                            i, pp.x, pp.y, pp.z, parN.c_str(), parC.c_str(), gpN.c_str(), gpC.c_str(),
                            (int)rr.GetChildren().size(), kidsOf(rr.address).c_str());
                        Write(buf);
                        ++i;
                    }
                }
                if (c.Name() == "Ignore")
                {
                    std::vector<RobloxInstance> models;
                    std::function<void(const RobloxInstance&, int)> collect =
                        [&](const RobloxInstance& node, int depth) {
                            if (depth > 4 || models.size() >= 6) return;
                            if (node.Class() == "Model") { models.push_back(node); return; }
                            for (auto& s : node.GetChildren()) collect(s, depth + 1);
                        };
                    collect(c, 0);
                    for (size_t mi = 0; mi < models.size(); ++mi)
                    {
                        auto& m = models[mi];
                        uintptr_t par = Memory->read<uintptr_t>(m.address + Offsets::Instance::Parent);
                        std::string parN = "?", parC = "?";
                        if (par) { RobloxInstance pi(par); parN = pi.Name(); parC = pi.Class(); }
                        Vectors::Vector3 mp = {0,0,0};
                        for (auto& ch : m.GetChildren())
                        {
                            std::string cl = ch.Class();
                            if (cl == "Part" || cl == "MeshPart") { mp = ch.Position(); break; }
                        }
                        snprintf(buf, sizeof(buf),
                            "[PFDIAG] PROBE ignM[%d] name='%s' pos=(%.0f,%.0f,%.0f) par='%s'/%s nkids=%d [%s]",
                            (int)mi, m.Name().c_str(), mp.x, mp.y, mp.z, parN.c_str(), parC.c_str(),
                            (int)m.GetChildren().size(), kidsOf(m.address).c_str());
                        Write(buf);
                    }
                }
            }
        }

        {
            auto pathOf = [&](uintptr_t addr) -> std::string {
                std::string s; int guard = 0; uintptr_t cur = addr;
                while (cur && guard++ < 8)
                {
                    RobloxInstance ci(cur);
                    s = "/" + ci.Name() + "[" + ci.Class() + "]" + s;
                    if (cur == Globals::Roblox::Workspace.address) break;
                    cur = Memory->read<uintptr_t>(cur + Offsets::Instance::Parent);
                }
                return s;
            };
            Vectors::Vector3 cam = {0,0,0};
            uintptr_t camp = Globals::Roblox::Workspace.address
                ? Memory->read<uintptr_t>(Globals::Roblox::Workspace.address + Offsets::Workspace::CurrentCamera) : 0;
            if (camp) cam = Memory->read<Vectors::Vector3>(camp + Offsets::Camera::Position);
            int localTC = Globals::Roblox::LocalPlayer.address
                ? (Memory->read<int>(Globals::Roblox::LocalPlayer.address + Offsets::Player::TeamColor) & 0xFFFF) : -1;
            snprintf(buf, sizeof(buf), "[PFDIAG] CAM=(%.0f,%.0f,%.0f) localTC=%d", cam.x, cam.y, cam.z, localTC);
            Write(buf);

            auto dist2cam = [&](const Vectors::Vector3& p) -> float {
                float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
                return sqrtf(dx*dx + dy*dy + dz*dz);
            };

            for (auto& c : wsChildren)
            {
                if (c.Name() == "Roots")
                {
                    int i = 0;
                    for (auto& rr : c.GetChildren())
                    {
                        int en = 0, fr = 0;
                        for (auto& sc : rr.GetChildren())
                        {
                            std::string nm = sc.Name();
                            if (nm.rfind("enemy_", 0) == 0) ++en;
                            else if (nm.rfind("friendly_", 0) == 0) ++fr;
                        }
                        auto pp = rr.Position();
                        snprintf(buf, sizeof(buf), "[PFDIAG] RROOT[%d] pos=(%.0f,%.0f,%.0f) d2c=%.1f en=%d fr=%d",
                            i, pp.x, pp.y, pp.z, dist2cam(pp), en, fr);
                        Write(buf);
                        ++i;
                    }
                }
                if (c.Name() != "Players") continue;
                for (auto& fold : c.GetChildren())
                {
                    std::string fc; int k = 0;
                    for (auto& fch : fold.GetChildren()) { if (k++ >= 8) break; fc += fch.Class() + ":" + fch.Name() + " "; }
                    snprintf(buf, sizeof(buf), "[PFDIAG] FOLD '%s' n=%d kids=[%s]",
                        fold.Name().c_str(), (int)fold.GetChildren().size(), fc.c_str());
                    Write(buf);
                    int mi = 0;
                    for (auto& mm : fold.GetChildren())
                    {
                        if (mm.Class() != "Model") continue;
                        Vectors::Vector3 acc = {0,0,0}; int pc = 0;
                        for (auto& sc : mm.GetChildren())
                        {
                            std::string cl = sc.Class();
                            if (cl == "Part" || cl == "MeshPart") { auto pp = sc.Position(); acc.x += pp.x; acc.y += pp.y; acc.z += pp.z; ++pc; }
                        }
                        if (!pc) continue;
                        acc.x /= pc; acc.y /= pc; acc.z /= pc;
                        if (mi++ >= 10) break;
                        snprintf(buf, sizeof(buf), "[PFDIAG] FMODEL fold='%s' center=(%.0f,%.0f,%.0f) parts=%d d2c=%.1f",
                            fold.Name().c_str(), acc.x, acc.y, acc.z, pc, dist2cam(acc));
                        Write(buf);
                    }
                }
            }

            for (auto& c : wsChildren)
            {
                if (c.Name() != "Players") continue;
                for (auto& fold : c.GetChildren())
                {
                    int n = 0;
                    for (auto& mm : fold.GetChildren())
                    {
                        if (n++ >= 15) break;
                        Vectors::Vector3 p = {0,0,0};
                        if (mm.Class() == "Model")
                        {
                            auto hrp = mm.FindFirstChild("HumanoidRootPart");
                            if (hrp.address) p = hrp.Position();
                            else {
                                auto h = mm.FindFirstChild("Head");
                                if (h.address) p = h.Position();
                                else for (auto& pc : mm.GetChildren())
                                    if (pc.Class() == "Part" || pc.Class() == "MeshPart") { p = pc.Position(); break; }
                            }
                        }
                        snprintf(buf, sizeof(buf), "[PFDIAG] PFC fold='%s' name='%s' cls='%s' pos=(%.0f,%.0f,%.0f)",
                            fold.Name().c_str(), mm.Name().c_str(), mm.Class().c_str(), p.x, p.y, p.z);
                        Write(buf);
                    }
                }
            }
        }

        {
            size_t n = 0;
            {
                std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
                n = Globals::Caches::CachedPlayerObjects.size();
            }
            snprintf(buf, sizeof(buf), "[PFDIAG] CachedPlayerObjects=%zu fallbackCache=%zu",
                n, Globals::Caches::CharacterFallbackCache.size());
            Write(buf);
        }
    }
}
