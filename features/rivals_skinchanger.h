#pragma once
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <functional>
#include <Windows.h>
#include <fstream>
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/SDK/SDK.h"

namespace RivalsSkinChanger
{
    inline bool appliedThisSession = false;
    inline uintptr_t lastDataModelAddress = 0;
    inline std::string lastStatus = "Not applied";

    struct ContentBlock {
        uintptr_t dataPtr;
        uintptr_t metadata;
        uint64_t size;
        uint64_t capacity;
    };

    inline bool CopyContent(uintptr_t fromPart, uintptr_t toPart, uintptr_t offset)
    {
        if (!fromPart || !toPart) return false;

        HANDLE h = Memory->openTransientHandle(true);
        if (!h) return false;

        ContentBlock fromC, toC;
        ReadProcessMemory(h, (LPCVOID)(fromPart + offset), &fromC, sizeof(fromC), nullptr);
        ReadProcessMemory(h, (LPCVOID)(toPart + offset), &toC, sizeof(toC), nullptr);

        if (!fromC.dataPtr || !toC.dataPtr || fromC.size == 0 || fromC.size >= 500) { CloseHandle(h); return false; }
        if (toC.capacity < fromC.size) { CloseHandle(h); return false; }

        std::string buf; buf.resize(fromC.size);
        if (!ReadProcessMemory(h, (LPCVOID)fromC.dataPtr, &buf[0], fromC.size, nullptr)) { CloseHandle(h); return false; }

        if (!WriteProcessMemory(h, (LPVOID)toC.dataPtr, buf.data(), buf.size(), nullptr)) { CloseHandle(h); return false; }
        if (!WriteProcessMemory(h, (LPVOID)(toPart + offset + 16), &fromC.size, sizeof(fromC.size), nullptr)) { CloseHandle(h); return false; }

        CloseHandle(h);
        return true;
    }

    inline bool CopyMeshId(uintptr_t fromPart, uintptr_t toPart)
    {
        return CopyContent(fromPart, toPart, Offsets::MeshPart::MeshId);
    }

    inline bool CopyTexture(uintptr_t fromPart, uintptr_t toPart)
    {
        return CopyContent(fromPart, toPart, Offsets::MeshPart::Texture);
    }

    inline void MatchAndCopy(RobloxInstance& skin, RobloxInstance& weapon, int& count)
    {
        for (auto& sc : skin.GetChildren())
        {
            auto wc = weapon.FindFirstChild(sc.Name());
            if (!wc.address) continue;

            auto scParts = sc.GetChildren();
            auto wcParts = wc.GetChildren();

            size_t si = 0, wi = 0;
            while (si < scParts.size() && wi < wcParts.size())
            {
                while (si < scParts.size() && scParts[si].Class() != "MeshPart") si++;
                while (wi < wcParts.size() && wcParts[wi].Class() != "MeshPart") wi++;
                if (si >= scParts.size() || wi >= wcParts.size()) break;

                if (CopyMeshId(scParts[si].address, wcParts[wi].address))
                    count++;
                if (CopyTexture(scParts[si].address, wcParts[wi].address))
                    count++;
                si++; wi++;
            }
        }
    }
}

inline void RivalsSkinChangerLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        try
        {
            // Detect game rejoin: DataModel address changed
            if (Globals::Roblox::DataModel.address != RivalsSkinChanger::lastDataModelAddress)
            {
                RivalsSkinChanger::lastDataModelAddress = Globals::Roblox::DataModel.address;
                RivalsSkinChanger::appliedThisSession = false;
            }

            if (!Globals::Roblox::isRivals)
            {
                RivalsSkinChanger::appliedThisSession = false;
                continue;
            }

            if (!Options::RivalsSkinChanger::Enabled)
            {
                RivalsSkinChanger::appliedThisSession = false;
                continue;
            }

            auto& lp = Globals::Roblox::LocalPlayer;
            if (!lp.address) continue;

            auto ps = lp.FindFirstChild("PlayerScripts");
            if (!ps.address) continue;

            auto vms = ps.FindFirstChild("Assets").FindFirstChild("ViewModels");
            if (!vms.address) continue;

            auto bundles = vms.FindFirstChild("Bundles");
            auto weapons = vms.FindFirstChild("Weapons");
            if (!bundles.address || !weapons.address) continue;

            std::string sn(Options::RivalsSkinChanger::SkinName);
            std::string wn(Options::RivalsSkinChanger::WeaponName);

            auto skin = bundles.FindFirstChild(sn);
            auto weapon = weapons.FindFirstChild(wn);
            if (!skin.address || !weapon.address) continue;

            auto sChildren = skin.GetChildren();
            auto wChildren = weapon.GetChildren();
            if (sChildren.empty() || wChildren.empty()) continue;

            // Always try to copy — re-applies on every equip cycle
            int copyCount = 0;
            RivalsSkinChanger::MatchAndCopy(skin, weapon, copyCount);

            if (copyCount > 0)
            {
                RivalsSkinChanger::appliedThisSession = true;
                RivalsSkinChanger::lastStatus = "Applied " + sn + " to " + wn + " (" + std::to_string(copyCount) + " copies)";
            }
        }
        catch (...) {}
    }
}
