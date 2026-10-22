#pragma once

// ── Anti-Fling ───────────────────────────────────────────────────────────────
// Detects fling attempts on the local character by watching BOTH linear and
// angular velocity on the HumanoidRootPart primitive, and when a spike is
// detected hammers the saved position + zero velocities with the same
// tight-loop pattern fling scripts use, so the restore wins the race.
// Call AntiFling::Tick() from the misc loop.

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"

#include <cmath>
#include <chrono>
#include <thread>

namespace AntiFling
{
    inline void Tick()
    {
        if (!Options::AntiFling::Enabled)
            return;
        if (!Globals::Roblox::LocalPlayer.address)
            return;

        auto character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address)
            return;

        // Character() + FindFirstChild() walks every child reading a name per
        // child, and this tick runs ~1000x/sec from the misc loop — that was
        // tens of thousands of driver reads/sec for as long as Anti-Fling was
        // enabled. The rig only changes on respawn, so re-resolve against the
        // character address instead of on every call.
        static uintptr_t s_cachedChar = 0;
        static uintptr_t s_cachedHrp = 0;
        static std::chrono::steady_clock::time_point s_rescanAt = std::chrono::steady_clock::now();
        if (character.address != s_cachedChar ||
            std::chrono::steady_clock::now() - s_rescanAt > std::chrono::seconds(2))
        {
            s_cachedChar = character.address;
            s_rescanAt = std::chrono::steady_clock::now();
            s_cachedHrp = character.FindFirstChild("HumanoidRootPart").address;
        }
        if (!s_cachedHrp)
            return;
        const RobloxInstance hrp(s_cachedHrp);

        uintptr_t prim = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
        if (!prim)
            return;

        static Vectors::Vector3 s_savedPos = {};
        static bool s_hasSaved = false;
        static auto s_lastSave = std::chrono::steady_clock::now();

        // ── Periodically save a safe position ────────────────────────────────
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - s_lastSave).count();

        if (!s_hasSaved || elapsed >= Options::AntiFling::SaveInterval)
        {
            Vectors::Vector3 lin = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
            Vectors::Vector3 ang = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyAngularVelocity);

            float linSpd = std::sqrtf(lin.x * lin.x + lin.y * lin.y + lin.z * lin.z);
            float angSpd = std::sqrtf(ang.x * ang.x + ang.y * ang.y + ang.z * ang.z);

            // Only snapshot when both look normal. Angular velocity is in
            // radians/s so its threshold is naturally far lower than linear.
            if (linSpd < 50.f && angSpd < 5.f)
            {
                Vectors::Vector3 pos = hrp.Position();
                if (!std::isnan(pos.x) && !std::isnan(pos.y) && !std::isnan(pos.z) &&
                    (std::abs(pos.x) > 0.1f || std::abs(pos.y) > 0.1f || std::abs(pos.z) > 0.1f))
                {
                    s_savedPos = pos;
                    s_hasSaved = true;
                    s_lastSave = now;
                }
            }
        }

        if (!s_hasSaved)
            return;

        // ── Detect fling ─────────────────────────────────────────────────────
        Vectors::Vector3 lin = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
        Vectors::Vector3 ang = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyAngularVelocity);

        float linSpd = std::sqrtf(lin.x * lin.x + lin.y * lin.y + lin.z * lin.z);
        float angSpd = std::sqrtf(ang.x * ang.x + ang.y * ang.y + ang.z * ang.z);

        bool flung = (linSpd >= Options::AntiFling::VelocityThreshold)
            || (angSpd >= Options::AntiFling::VelocityThreshold * 100.f);

        if (flung)
        {
            // Hammer restore with position AND both velocity types zeroed.
            Vectors::Vector3 zeroVec = { 0.f, 0.f, 0.f };

            // Restore writes are spaced 1ms apart over the same 14ms window:
            // the game can only write these once per physics step, so a batch
            // per ms already lands after each of them instead of spinning the
            // shared IO mutex for the full window at full speed.
            auto hammerStart = std::chrono::steady_clock::now();
            while (std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - hammerStart).count() < 14.f)
            {
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::Position, s_savedPos);
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity, zeroVec);
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyAngularVelocity, zeroVec);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            // Invalidate saved pos — re-save once velocity settles.
            s_hasSaved = false;
        }
    }
}
