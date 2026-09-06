#pragma once
#include "../rbx/math/math.h"
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../overlay/imgui/KeyBind.h"
#include <thread>
#include <chrono>
#include <cmath>

inline void Spin360Loop()
{
    static bool wasPressed = false;
    static bool isSpinning = false;
    static float accumulatedAngle = 0.0f;

    while (Globals::running)
    {
        try
        {
            if (!Options::Spin360::Enabled || !Globals::Roblox::Camera.address)
            {
                isSpinning = false;
                accumulatedAngle = 0.0f;
                wasPressed = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            bool currentlyPressed = KeyBind::IsPressed(Options::Spin360::HotKey);
            bool justPressed = currentlyPressed && !wasPressed;
            wasPressed = currentlyPressed;

            if (justPressed && !isSpinning)
            {
                isSpinning = true;
                accumulatedAngle = 0.0f;
            }

            if (!isSpinning)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            float step = Options::Spin360::Speed * (3.14159265f / 180.0f);

            if (accumulatedAngle + step >= 2.0f * 3.14159265f)
            {
                step = (2.0f * 3.14159265f) - accumulatedAngle;
                isSpinning = false;
                accumulatedAngle = 0.0f;
            }
            else
            {
                accumulatedAngle += step;
            }

            Matrixes::Matrix3x3 currentRotation = Memory->read<Matrixes::Matrix3x3>(
                Globals::Roblox::Camera.address + Offsets::Camera::Rotation
            );

            float cosA = std::cos(step);
            float sinA = std::sin(step);

            Matrixes::Matrix3x3 rotY {
                cosA,  0.0f, sinA,
                0.0f,  1.0f, 0.0f,
                -sinA, 0.0f, cosA
            };

            Matrixes::Matrix3x3 newRotation {
                currentRotation.r00 * rotY.r00 + currentRotation.r01 * rotY.r10 + currentRotation.r02 * rotY.r20,
                currentRotation.r00 * rotY.r01 + currentRotation.r01 * rotY.r11 + currentRotation.r02 * rotY.r21,
                currentRotation.r00 * rotY.r02 + currentRotation.r01 * rotY.r12 + currentRotation.r02 * rotY.r22,

                currentRotation.r10 * rotY.r00 + currentRotation.r11 * rotY.r10 + currentRotation.r12 * rotY.r20,
                currentRotation.r10 * rotY.r01 + currentRotation.r11 * rotY.r11 + currentRotation.r12 * rotY.r21,
                currentRotation.r10 * rotY.r02 + currentRotation.r11 * rotY.r12 + currentRotation.r12 * rotY.r22,

                currentRotation.r20 * rotY.r00 + currentRotation.r21 * rotY.r10 + currentRotation.r22 * rotY.r20,
                currentRotation.r20 * rotY.r01 + currentRotation.r21 * rotY.r11 + currentRotation.r22 * rotY.r21,
                currentRotation.r20 * rotY.r02 + currentRotation.r21 * rotY.r12 + currentRotation.r22 * rotY.r22
            };

            Memory->write<Matrixes::Matrix3x3>(
                Globals::Roblox::Camera.address + Offsets::Camera::Rotation, newRotation
            );
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
