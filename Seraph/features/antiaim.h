#pragma once

#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"

inline void AntiAimLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        if (!Options::AntiAim::Enabled)
            continue;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
                continue;

            auto character = localPlayer.Character();
            if (!character.address)
                continue;

            auto hrp = character.FindFirstChild("HumanoidRootPart");
            if (!hrp.address)
                continue;

            uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
            if (!primitive)
                continue;

            sCFrame cf = Memory->read<sCFrame>(primitive + Offsets::Primitive::Rotation);
            const float speed = Options::AntiAim::Speed * 0.01f;
            static float spinAngle = 0.0f;
            spinAngle += speed;

            float yawOffset = 0.0f;
            switch (Options::AntiAim::Mode)
            {
            case 0: // Spin
                yawOffset = spinAngle;
                break;
            case 1: // Jitter
                yawOffset = (fmodf(spinAngle, 2.0f) < 1.0f ? 1.0f : -1.0f) * (Options::AntiAim::Strength * 0.0174533f);
                break;
            case 2: // Random
                yawOffset = ((static_cast<float>(rand() % 360)) - 180.0f) * 0.0174533f;
                break;
            default:
                break;
            }

            const float c = cosf(yawOffset);
            const float s = sinf(yawOffset);

            sCFrame rotated = cf;
            const float r00 = cf.r00 * c - cf.r02 * s;
            const float r02 = cf.r00 * s + cf.r02 * c;
            const float r20 = cf.r20 * c - cf.r22 * s;
            const float r22 = cf.r20 * s + cf.r22 * c;

            rotated.r00 = r00;
            rotated.r02 = r02;
            rotated.r20 = r20;
            rotated.r22 = r22;

            Memory->write<sCFrame>(primitive + Offsets::Primitive::Rotation, rotated);
        }
        catch (...)
        {
        }
    }
}
