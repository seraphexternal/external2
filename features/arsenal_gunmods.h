#pragma once
#include <vector>
#include <thread>
#include <chrono>
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"

namespace ArsenalGunmods
{
    inline std::vector<uintptr_t> fireRateAddrs;
    inline std::vector<uintptr_t> recoilAddrs;
    inline std::vector<uintptr_t> autoAddrs;
    inline uintptr_t currentCurseAddr = 0;
    inline bool wasInfiniteAmmo = false;
    inline int scanCooldown = 0;

    inline void Clear()
    {
        fireRateAddrs.clear();
        recoilAddrs.clear();
        autoAddrs.clear();
        currentCurseAddr = 0;
        wasInfiniteAmmo = false;
        scanCooldown = 0;
    }

    inline void RestoreInfiniteAmmo()
    {
        if (!currentCurseAddr)
            return;
        wasInfiniteAmmo = false;
        uintptr_t base = currentCurseAddr + Offsets::Misc::Value;
        Memory->write<uint64_t>(base, 0);
        Memory->write<uint64_t>(base + 8, 0);
        Memory->write<int32_t>(base + 0x10, 0);
    }

    inline void Scan()
    {
        fireRateAddrs.clear();
        recoilAddrs.clear();
        autoAddrs.clear();
        currentCurseAddr = 0;

        if (!Globals::Roblox::DataModel.address)
            return;
        auto replicatedStorage = Globals::Roblox::DataModel.FindFirstChildWhichIsA("ReplicatedStorage");
        if (!replicatedStorage.address)
            return;

        auto weapons = replicatedStorage.FindFirstChild("Weapons");
        if (weapons.address)
        {
            for (const auto& weapon : weapons.GetChildren())
            {
                if (auto fr = weapon.FindFirstChild("FireRate"))
                    fireRateAddrs.push_back(fr.address);
                if (auto rc = weapon.FindFirstChild("RecoilControl"))
                    recoilAddrs.push_back(rc.address);
                if (auto av = weapon.FindFirstChild("Auto"))
                    autoAddrs.push_back(av.address);
            }
        }

        auto wkspc = replicatedStorage.FindFirstChild("wkspc");
        if (wkspc.address)
        {
            if (auto cc = wkspc.FindFirstChild("CurrentCurse"))
                currentCurseAddr = cc.address;
        }
    }

    inline void WriteInfiniteAmmo()
    {
        if (!currentCurseAddr)
            return;
        uintptr_t base = currentCurseAddr + Offsets::Misc::Value;
        const char src[16] = "Infinite Ammo";
        Memory->write<uint64_t>(base, *reinterpret_cast<const uint64_t*>(src));
        Memory->write<uint64_t>(base + 8, *reinterpret_cast<const uint64_t*>(src + 8));
        Memory->write<int32_t>(base + 0x10, 13);
    }
}

inline void ArsenalGunmodsLoop()
{
    while (Globals::running)
    {
        bool anyOn = Options::ArsenalGunmods::FastFireRate
            || Options::ArsenalGunmods::NoRecoil
            || Options::ArsenalGunmods::AllAuto
            || Options::ArsenalGunmods::InfiniteAmmo
            || ArsenalGunmods::wasInfiniteAmmo;

        if (!anyOn)
        {
            ArsenalGunmods::Clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (Globals::Roblox::lastPlaceID != 286090429)
        {
            ArsenalGunmods::Clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (++ArsenalGunmods::scanCooldown >= 4)
        {
            ArsenalGunmods::scanCooldown = 0;
            ArsenalGunmods::Scan();
        }

        if (Options::ArsenalGunmods::FastFireRate)
            for (uintptr_t addr : ArsenalGunmods::fireRateAddrs)
                Memory->write<double>(addr + Offsets::Misc::Value, 0.01);

        if (Options::ArsenalGunmods::NoRecoil)
            for (uintptr_t addr : ArsenalGunmods::recoilAddrs)
                Memory->write<double>(addr + Offsets::Misc::Value, 0.0);

        if (Options::ArsenalGunmods::AllAuto)
            for (uintptr_t addr : ArsenalGunmods::autoAddrs)
                Memory->write<bool>(addr + Offsets::Misc::Value, true);

        if (Options::ArsenalGunmods::InfiniteAmmo)
        {
            ArsenalGunmods::wasInfiniteAmmo = true;
            ArsenalGunmods::WriteInfiniteAmmo();
        }
        else if (ArsenalGunmods::wasInfiniteAmmo)
        {
            ArsenalGunmods::RestoreInfiniteAmmo();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
