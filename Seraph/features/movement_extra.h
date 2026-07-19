#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../features/aimbot.h"
#include <thread>
#include <chrono>
#include <cmath>

// ── Click TP: teleport the local character to the point under the cursor ─────
inline void ClickTPLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        if (!Options::ClickTP::Enabled || Options::ClickTP::Key == 0)
            continue;

        static bool wasDown = false;
        bool isDown = (GetAsyncKeyState(Options::ClickTP::Key) & 0x8000) != 0;
        if (!isDown || wasDown)
        {
            wasDown = isDown;
            continue;
        }
        wasDown = isDown;

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
            auto camera = Globals::Roblox::Camera;
            if (!camera.address)
                continue;

            sCFrame camCFrame = camera.CFrame();
            Vectors::Vector3 camPos = camera.Position();
            Vectors::Vector3 look = camCFrame.GetLookVector();

            // Aim a ray through the current cursor position (use mouse-based
            // direction when available, otherwise straight forward).
            POINT cursor;
            GetCursorPos(&cursor);
            Vectors::Vector2 dims = Memory->read<Vectors::Vector2>(
                Globals::Roblox::VisualEngine + Offsets::VisualEngine::Dimensions);
            Vectors::Vector2 mouse = Memory->read<Vectors::Vector2>(
                Globals::Roblox::Camera.address + Offsets::Camera::Viewport);
            Vectors::Vector2 screen((float)cursor.x, (float)cursor.y);
            // Reconstruct a forward ray biased by the cursor offset from screen center.
            Vectors::Vector3 dir = look;
            if (dims.x > 1.0f && dims.y > 1.0f)
            {
                float nx = (screen.x - dims.x * 0.5f) / (dims.x * 0.5f);
                float ny = (screen.y - dims.y * 0.5f) / (dims.y * 0.5f);
                // Build a right/up basis from the look vector.
                Vectors::Vector3 worldUp(0, 1, 0);
                Vectors::Vector3 right = {
                    look.z, 0, -look.x
                };
                float rl = sqrtf(right.x * right.x + right.z * right.z);
                if (rl > 1e-4f) { right.x /= rl; right.z /= rl; }
                Vectors::Vector3 up = {
                    right.y * look.z - right.z * look.y,
                    right.z * look.x - right.x * look.z,
                    right.x * look.y - right.y * look.x
                };
                float ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
                if (ul > 1e-4f) { up.x /= ul; up.y /= ul; up.z /= ul; }
                dir = {
                    look.x + right.x * nx * 0.6f + up.x * -ny * 0.6f,
                    look.y + right.y * nx * 0.6f + up.y * -ny * 0.6f,
                    look.z + right.z * nx * 0.6f + up.z * -ny * 0.6f
                };
                float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (dl > 1e-4f) { dir.x /= dl; dir.y /= dl; dir.z /= dl; }
            }

            Vectors::Vector3 dest = camPos + dir * Options::ClickTP::MaxDistance;

            uintptr_t primitive = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
            if (primitive)
            {
                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Position, dest);
                Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::AssemblyLinearVelocity, { 0,0,0 });
            }
        }
        catch (...)
        {
        }
    }
}

// ── Hip Height: force the Humanoid HipHeight value ──────────────────────────
inline void HipHeightLoop()
{
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if (Options::HipHeight::Key != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::HipHeight::Key) & 0x8000) != 0;
            if (Options::HipHeight::ToggleType == 2)
                Options::HipHeight::Toggled = true;
            else if (Options::HipHeight::ToggleType == 1)
            {
                if (isKeyPressed && !wasKeyPressed)
                    Options::HipHeight::Toggled = !Options::HipHeight::Toggled;
                wasKeyPressed = isKeyPressed;
            }
            else
                Options::HipHeight::Toggled = isKeyPressed;
        }
        else if (Options::HipHeight::ToggleType == 2)
            Options::HipHeight::Toggled = true;

        if (!Options::HipHeight::Enabled || !Options::HipHeight::Toggled)
            continue;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address)
                continue;
            auto character = localPlayer.Character();
            if (!character.address)
                continue;
            auto humanoid = character.FindFirstChildWhichIsA("Humanoid");
            if (!humanoid.address)
                continue;
            Memory->write<float>(humanoid.address + Offsets::Humanoid::HipHeight, Options::HipHeight::Value);
        }
        catch (...)
        {
        }
    }
}

// ── Free Cam: detach the camera from the player and fly it freely ───────────
inline void FreeCamLoop()
{
    static Vectors::Vector3 savedPos{ 0,0,0 };
    static Matrixes::Matrix3x3 savedRot{};
    static bool hasSaved = false;

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (Options::FreeCam::Key != 0)
        {
            static bool wasKeyPressed = false;
            bool isKeyPressed = (GetAsyncKeyState(Options::FreeCam::Key) & 0x8000) != 0;
            if (Options::FreeCam::ToggleType == 2)
                Options::FreeCam::Toggled = true;
            else if (Options::FreeCam::ToggleType == 1)
            {
                if (isKeyPressed && !wasKeyPressed)
                {
                    Options::FreeCam::Toggled = !Options::FreeCam::Toggled;
                    if (Options::FreeCam::Toggled && Options::FreeCam::SaveRealCamera)
                    {
                        auto cam = Globals::Roblox::Camera;
                        if (cam.address)
                        {
                            savedPos = Memory->read<Vectors::Vector3>(cam.address + Offsets::Camera::Position);
                            savedRot = Memory->read<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation);
                            hasSaved = true;
                        }
                    }
                }
                wasKeyPressed = isKeyPressed;
            }
            else
                Options::FreeCam::Toggled = isKeyPressed;
        }
        else if (Options::FreeCam::ToggleType == 2)
            Options::FreeCam::Toggled = true;

        auto cam = Globals::Roblox::Camera;
        if (!cam.address)
            continue;

        if (!Options::FreeCam::Enabled || !Options::FreeCam::Toggled)
        {
            // Restore the real camera when turning off.
            if (hasSaved && Options::FreeCam::SaveRealCamera)
            {
                Memory->write<Vectors::Vector3>(cam.address + Offsets::Camera::Position, savedPos);
                Memory->write<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation, savedRot);
                hasSaved = false;
            }
            continue;
        }

        // Save once on activation.
        if (!hasSaved && Options::FreeCam::SaveRealCamera)
        {
            savedPos = Memory->read<Vectors::Vector3>(cam.address + Offsets::Camera::Position);
            savedRot = Memory->read<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation);
            hasSaved = true;
        }

        Matrixes::Matrix3x3 rot = Memory->read<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation);
        Vectors::Vector3 pos = Memory->read<Vectors::Vector3>(cam.address + Offsets::Camera::Position);
        Vectors::Vector3 fwd(rot.r02, rot.r12, rot.r22);
        Vectors::Vector3 right(fwd.z, 0, -fwd.x);
        float rl = sqrtf(right.x * right.x + right.z * right.z);
        if (rl > 1e-4f) { right.x /= rl; right.z /= rl; }
        Vectors::Vector3 up(0, 1, 0);
        float sp = Options::FreeCam::Speed;

        Vectors::Vector3 vel(0, 0, 0);
        if (GetAsyncKeyState('W') & 0x8000) vel = vel - fwd * sp;
        if (GetAsyncKeyState('S') & 0x8000) vel = vel + fwd * sp;
        if (GetAsyncKeyState('A') & 0x8000) vel = vel - right * sp;
        if (GetAsyncKeyState('D') & 0x8000) vel = vel + right * sp;
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) vel = vel + up * sp;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) vel = vel - up * sp;

        pos = pos + vel * 0.016f;
        Memory->write<Vectors::Vector3>(cam.address + Offsets::Camera::Position, pos);
    }
}

// ── Stretch Res: vertical/horizontal stretch by modulating camera FOV ───────
inline void StretchResLoop()
{
    static float savedFOV = -1.0f;
    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto cam = Globals::Roblox::Camera;
        if (!cam.address)
            continue;

        if (!Options::StretchRes::Enabled || (Options::StretchRes::ScaleX == 1.0f && Options::StretchRes::ScaleY == 1.0f))
        {
            if (savedFOV > 0.0f)
            {
                Memory->write<float>(cam.address + Offsets::Camera::FieldOfView, savedFOV);
                savedFOV = -1.0f;
            }
            continue;
        }

        float baseFOV = Memory->read<float>(cam.address + Offsets::Camera::FieldOfView);
        if (savedFOV < 0.0f)
            savedFOV = baseFOV;

        // Vertical stretch lowers the effective FOV; horizontal stretch raises it.
        float targetFOV = savedFOV / Options::StretchRes::ScaleY * Options::StretchRes::ScaleX;
        Memory->write<float>(cam.address + Offsets::Camera::FieldOfView, targetFOV);
    }
}
