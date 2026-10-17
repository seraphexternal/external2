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

namespace PFDiag
{
    inline std::string Path()
    {
        char* env = nullptr; size_t sz = 0;
        std::string base = ".";
        if (_dupenv_s(&env, &sz, "LOCALAPPDATA") == 0 && env) { base = env; free(env); }
        std::string dir = base + "\\Seraph";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\pf_diag.txt";
    }

    inline void Write(const std::string& line)
    {
        OutputDebugStringA(line.c_str());
        OutputDebugStringA("\n");
        FILE* f = nullptr;
        fopen_s(&f, Path().c_str(), "a");
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
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRun).count() < 2000)
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