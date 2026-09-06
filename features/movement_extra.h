#pragma once
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../features/aimbot.h"
#include <thread>
#include <chrono>
#include <cmath>

// ── Click TP: teleport the local character to the ground point under the cursor ─
// Unprojects the cursor through the real view-projection matrix and intersects
// the ray with the ground plane (same Y as the character), then hammers the
// primitive position so the game accepts the teleport.
inline void ClickTPLoop()
{
    bool lastClick = false;

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        if (!Options::ClickTP::Enabled || Options::ClickTP::Key == 0)
            continue;

        // Optional hold key: clicking only teleports while it is held down.
        if (Options::ClickTP::HoldKey != 0 &&
            !(GetAsyncKeyState(Options::ClickTP::HoldKey) & 0x8000))
            continue;

        // Block while typing in a text field / using the menu.
        if (ImGui::GetIO().WantTextInput)
            continue;

        bool curClick = (GetAsyncKeyState(Options::ClickTP::Key) & 0x8000) != 0;
        bool justClicked = curClick && !lastClick;
        lastClick = curClick;
        if (!justClicked)
            continue;

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address) continue;
            auto character = localPlayer.Character();
            if (!character.address) continue;
            auto hrp = character.FindFirstChild("HumanoidRootPart");
            if (!hrp.address) continue;
            uintptr_t prim = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
            if (!prim) continue;

            const Vectors::Vector2 dims = Globals::Viewport::Dimensions;
            if (dims.x < 1.f || dims.y < 1.f || !Globals::Viewport::Valid)
                continue;

            POINT cursor;
            GetCursorPos(&cursor);
            float cx = (float)cursor.x - (float)Globals::Viewport::ScreenPos.x;
            float cy = (float)cursor.y - (float)Globals::Viewport::ScreenPos.y;
            float ndc_x = (2.f * cx / dims.x) - 1.f;
            float ndc_y = -(2.f * cy / dims.y) + 1.f;

            const float* m = Globals::Viewport::ViewMatrix.data;

            // Invert the view-projection matrix (row-major, 4x4).
            float inv[16];
            inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
            inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
            inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
            inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
            inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
            inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
            inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
            inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
            inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
            inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
            inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
            inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
            inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
            inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
            inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]  + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
            inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]  - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

            float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
            if (std::abs(det) < 1e-8f)
                continue;
            float invDet = 1.f / det;
            for (int i = 0; i < 16; ++i)
                inv[i] *= invDet;

            auto unproj = [&](float nx, float ny, float nz) -> Vectors::Vector3
            {
                float cw = inv[12]*nx + inv[13]*ny + inv[14]*nz + inv[15];
                if (std::abs(cw) < 1e-8f)
                    return { 0.f, 0.f, 0.f };
                return {
                    (inv[0]*nx + inv[1]*ny + inv[2]*nz + inv[3]) / cw,
                    (inv[4]*nx + inv[5]*ny + inv[6]*nz + inv[7]) / cw,
                    (inv[8]*nx + inv[9]*ny + inv[10]*nz + inv[11]) / cw
                };
            };

            Vectors::Vector3 nearPt = unproj(ndc_x, ndc_y, 0.f);
            Vectors::Vector3 farPt  = unproj(ndc_x, ndc_y, 1.f);
            Vectors::Vector3 rayDir = (farPt - nearPt).Normalize();
            if (std::abs(rayDir.y) < 1e-6f)
                continue;

            Vectors::Vector3 curPos = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Position);
            float t = (curPos.y - nearPt.y) / rayDir.y;
            if (t < 0.f)
                continue;

            Vectors::Vector3 target = nearPt + rayDir * t;
            target.y += Options::ClickTP::YOffset;

            if (Options::ClickTP::MaxDistanceCheck)
            {
                float dx = curPos.x - target.x, dz = curPos.z - target.z;
                if (std::sqrt(dx * dx + dz * dz) > Options::ClickTP::MaxDist)
                    continue;
            }

            for (int i = 0; i < 10000; ++i)
            {
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::Position, target);
                Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity, { 0.f, 0.f, 0.f });
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
// Reference-style implementation: locks CameraType to 7, holds walkspeed at 0,
// RMB drag to look, WASD/space/ctrl to move, and hammers the camera transform
// continuously (~14ms bursts) so the game's camera script can never win the race.
inline void FreeCamLoop()
{
    static Vectors::Vector3 savedPos{ 0,0,0 };
    static Matrixes::Matrix3x3 savedRot{};
    static bool hasSaved = false;
    static float yaw = 0.f, pitch = 0.f;
    static bool initialized = false;
    static bool wasRmb = false;
    static bool movementLocked = false;
    static float savedWalkSpeed = 16.f;
    static float savedFOV = 0.f;
    static bool wasActive = false;

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

        bool active = Options::FreeCam::Enabled && Options::FreeCam::Toggled;

        if (!active)
        {
            // Restore the real camera when turning off.
            if (wasActive)
            {
                Globals::freecamOwnsCamera = false;
                if (hasSaved && Options::FreeCam::SaveRealCamera)
                {
                    Memory->write<Vectors::Vector3>(cam.address + Offsets::Camera::Position, savedPos);
                    Memory->write<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation, savedRot);
                    hasSaved = false;
                }
                if (initialized && savedFOV > 0.f && Options::FreeCam::FOVOverride)
                {
                    Memory->write<float>(cam.address + Offsets::Camera::FieldOfView, savedFOV);
                    savedFOV = 0.f;
                }
                if (movementLocked)
                {
                    auto character = Globals::Roblox::LocalPlayer.Character();
                    auto hum = character.FindFirstChildWhichIsA("Humanoid");
                    if (hum.address)
                    {
                        hum.SetWalkspeed(savedWalkSpeed);
                    }
                    movementLocked = false;
                }
                Memory->write<int>(cam.address + Offsets::Camera::CameraType, 0);
                initialized = false;
                wasActive = false;
                wasRmb = false;
            }
            continue;
        }

        wasActive = true;
        Globals::freecamOwnsCamera = true;

        // Save the real camera once on activation.
        if (!hasSaved && Options::FreeCam::SaveRealCamera)
        {
            savedPos = Memory->read<Vectors::Vector3>(cam.address + Offsets::Camera::Position);
            savedRot = Memory->read<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation);
            hasSaved = true;
        }
        if (!initialized)
        {
            yaw = 0.f;
            pitch = 0.f;
            if (Options::FreeCam::FOVOverride)
                savedFOV = Memory->read<float>(cam.address + Offsets::Camera::FieldOfView);
            wasRmb = false;
            initialized = true;
        }

        // Lock walkspeed so the character can't walk away.
        if (Globals::Roblox::LocalPlayer.address)
        {
            auto character = Globals::Roblox::LocalPlayer.Character();
            auto hum = character.FindFirstChildWhichIsA("Humanoid");
            if (hum.address)
            {
                if (!movementLocked)
                {
                    savedWalkSpeed = hum.GetWalkspeed();
                    movementLocked = true;
                }
                Memory->write<float>(hum.address + Offsets::Humanoid::Walkspeed, 0.f);
            }
        }

        // ── Mouse look: hold RMB and drag to rotate (cursor-delta based) ─────
        bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (rmb)
        {
            static POINT lastCursor{};
            if (!wasRmb)
            {
                GetCursorPos(&lastCursor);
            }
            else
            {
                POINT cur;
                GetCursorPos(&cur);
                float dx = (float)(cur.x - lastCursor.x);
                float dy = (float)(cur.y - lastCursor.y);
                yaw -= dx * Options::FreeCam::Sensitivity;
                pitch -= dy * Options::FreeCam::Sensitivity;
                pitch = std::clamp(pitch, -1.5f, 1.5f);
                SetCursorPos(cur.x, cur.y);
                lastCursor = cur;
            }
        }
        wasRmb = rmb;

        // ── Build rotation (Roblox left-handed convention) ───────────────────
        float cy = std::cos(yaw),  sy = std::sin(yaw);
        float cp = std::cos(pitch), sp = std::sin(pitch);
        Matrixes::Matrix3x3 rot;
        rot.r00 = cy;    rot.r01 = sy * sp;  rot.r02 = sy * cp;
        rot.r10 = 0.f;   rot.r11 = cp;       rot.r12 = -sp;
        rot.r20 = -sy;   rot.r21 = cy * sp;  rot.r22 = cy * cp;

        // ── Movement ─────────────────────────────────────────────────────────
        Vectors::Vector3 move(0.f, 0.f, 0.f);
        if (GetAsyncKeyState('W') & 0x8000) move.z -= 1.f;
        if (GetAsyncKeyState('S') & 0x8000) move.z += 1.f;
        if (GetAsyncKeyState('A') & 0x8000) move.x -= 1.f;
        if (GetAsyncKeyState('D') & 0x8000) move.x += 1.f;
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) move.y += 1.f;
        if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) move.y -= 1.f;

        if (move.x != 0.f || move.y != 0.f || move.z != 0.f)
        {
            move = move.Normalize();
            float spd = Options::FreeCam::Speed;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                spd *= Options::FreeCam::ShiftMultiplier;

            Vectors::Vector3 right{ rot.r00, rot.r10, rot.r20 };
            Vectors::Vector3 up{ rot.r01, rot.r11, rot.r21 };
            Vectors::Vector3 forward{ -rot.r02, -rot.r12, -rot.r22 };

            savedPos = savedPos
                + (right * move.x + up * move.y + forward * -move.z) * (spd * 0.016f);
        }

        // ── Hammer the camera transform (~14ms burst) so nothing can override ─
        auto writeStart = std::chrono::high_resolution_clock::now();
        do {
            Memory->write<int>(cam.address + Offsets::Camera::CameraType, 7);
            Memory->write<Vectors::Vector3>(cam.address + Offsets::Camera::Position, savedPos);
            Memory->write<Matrixes::Matrix3x3>(cam.address + Offsets::Camera::Rotation, rot);
        } while (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - writeStart).count() < 14000);

        if (Options::FreeCam::FOVOverride)
            Memory->write<float>(cam.address + Offsets::Camera::FieldOfView, Options::FreeCam::FOVValue);
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
