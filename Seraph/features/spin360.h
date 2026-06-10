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
    while (Globals::running)
    {
        try
        {
            if (Options::Spin360::Enabled
                && Globals::Roblox::Camera.address
                && KeyBind::IsPressed(Options::Spin360::HotKey))
            {
                // Read current camera rotation matrix
                Matrixes::Matrix3x3 currentRotation = Memory->read<Matrixes::Matrix3x3>(
                    Globals::Roblox::Camera.address + Offsets::Camera::Rotation
                );

                // Build incremental yaw rotation matrix (around Y-axis)
                float angle = Options::Spin360::Speed * (3.14159265f / 180.0f);
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);

                Matrixes::Matrix3x3 rotY {
                    cosA,  0.0f, sinA,
                    0.0f,  1.0f, 0.0f,
                    -sinA, 0.0f, cosA
                };

                // Matrix multiply: newRotation = currentRotation * rotY
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
        }
        catch (...) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
