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
        auto hrp = character.FindFirstChild("HumanoidRootPart");
        if (!hrp.address)
            return;

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

            auto hammerStart = std::chrono::steady_clock::now();
            while (std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - hammerStart).count() < 14.f)
            {
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::Position, s_savedPos);
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity, zeroVec);
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyAngularVelocity, zeroVec);
            }

            // Invalidate saved pos — re-save once velocity settles.
            s_hasSaved = false;
        }
    }
}
