#pragma once
#include <algorithm>
#include <cmath>
#include <chrono>
#include <vector>
#include <windows.h>
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/KeyBind.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"
#include "visibility.h"
#include "playerfilter.h"

namespace RageVisual
{
    inline bool hasGhost = false;
    inline Vectors::Vector3 ghostPos = { 0, 0, 0 };
    inline Vectors::Vector3 realPos = { 0, 0, 0 };
}

inline Vectors::Vector3 GetRagebotTargetPosition(const RobloxPlayer& player)
{
    const RobloxInstance* part;
    switch (Options::Ragebot::TargetBone)
    {
    case 0: part = &player.Head; break;
    case 1: part = &player.HumanoidRootPart; break;
    case 2: part = &player.Lower_Torso; break;
    case 3: part = &player.Upper_Torso; break;
    default: part = &player.Head; break;
    }

    Vectors::Vector3 pos = part->Position();

    if (Options::Ragebot::Prediction && part->address)
    {
        Vectors::Vector3 vel = Memory->read<Vectors::Vector3>(
            Memory->read<uintptr_t>(part->address + Offsets::BasePart::Primitive) +
            Offsets::Primitive::AssemblyLinearVelocity
        );
        pos.x += vel.x * Options::Ragebot::PredictionX;
        pos.y += vel.y * Options::Ragebot::PredictionY;
        pos.z += vel.z * Options::Ragebot::PredictionX;
    }

    return pos;
}

struct RagebotTarget
{
    RobloxPlayer player;
    Vectors::Vector3 targetPos;
    Vectors::Vector2 targetPos2D;
    float screenDist;
    float distance3D;
    int hitboxIndex;
    float hitChance;
    bool visible;
};

inline float CalculateHitChance(const RobloxPlayer& player, const Vectors::Vector3& targetPos, int hitbox)
{
    float baseChance = 100.0f;

    if (!Options::Ragebot::HitChanceEnabled)
        return baseChance;

    float distance = 0.0f;
    if (Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart").address)
    {
        distance = Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart").Position().Distance(targetPos);
    }

    float distancePenalty = (std::min)(distance / 100.0f * 20.0f, 30.0f);
    baseChance -= distancePenalty;

    if (Options::Ragebot::WallCheck && Visibility::IsPlayerOccluded(player))
    {
        baseChance -= 50.0f;
    }

    if (hitbox == 0)
    {
        baseChance += 10.0f;
    }
    else if (hitbox == 1)
    {
        baseChance += 5.0f;
    }

    float velocity = 0.0f;
    if (player.HumanoidRootPart.address)
    {
        Vectors::Vector3 vel = Memory->read<Vectors::Vector3>(
            Memory->read<uintptr_t>(player.HumanoidRootPart.address + Offsets::BasePart::Primitive) +
            Offsets::Primitive::AssemblyLinearVelocity
        );
        velocity = vel.Magnitude();
    }

    if (velocity > 50.0f)
    {
        baseChance -= (std::min)((velocity - 50.0f) / 10.0f, 30.0f);
    }

    return std::clamp(baseChance, 0.0f, 100.0f);
}

inline std::vector<RagebotTarget> GetRagebotTargets()
{
    std::vector<RagebotTarget> targets;

    if (Globals::Roblox::Players.address != 0)
    {
        Globals::Roblox::LocalPlayer = RobloxInstance(
            Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer)
        );
    }

    auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");

    POINT p;
    GetCursorPos(&p);

    const int hitboxes[] = { 0, 1, 2, 3 };
    const int hitboxCount = 4;

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        auto HRP = player.HumanoidRootPart;
        if (!HRP.address)
            continue;

        if (player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (!player.Name.empty() && !PlayerFilter::AimbotAllowed(player.Name))
            continue;

        if (Options::Ragebot::TeamCheck && IsTeammate(player))
            continue;

        if (player.Health <= 0)
            continue;

        if (Options::Ragebot::DownedCheck && player.Health > 0 && player.Health <= 5.0f)
            continue;

        if (Options::Rivals::AntiKatana && IsHoldingKatana(player))
            continue;

        Vectors::Vector3 basePos = GetRagebotTargetPosition(player);
        Vectors::Vector3 headPos = player.Head.Position();
        Vectors::Vector3 torsoPos = player.HumanoidRootPart.Position();

        for (int hb = 0; hb < hitboxCount; hb++)
        {
            Vectors::Vector3 targetPos;
            switch (hb)
            {
            case 0: targetPos = headPos; break;
            case 1: targetPos = torsoPos; break;
            case 2: targetPos = player.Lower_Torso.Position(); break;
            case 3: targetPos = player.Upper_Torso.Position(); break;
            default: targetPos = basePos; break;
            }

            if (Options::Ragebot::Prediction)
            {
                Vectors::Vector3 vel = Memory->read<Vectors::Vector3>(
                    Memory->read<uintptr_t>(HRP.address + Offsets::BasePart::Primitive) +
                    Offsets::Primitive::AssemblyLinearVelocity
                );
                targetPos.x += vel.x * Options::Ragebot::PredictionX;
                targetPos.y += vel.y * Options::Ragebot::PredictionY;
                targetPos.z += vel.z * Options::Ragebot::PredictionX;
            }

            Vectors::Vector2 targetPos2D = WorldToScreen(targetPos);
            if (targetPos2D.x == -1 && targetPos2D.y == -1)
                continue;

            bool visible = true;
            if (Options::Ragebot::WallCheck)
            {
                visible = !Visibility::IsPlayerOccluded(player);
            }

            if (!visible && Options::Ragebot::WallCheck)
                continue;

            float distance3D = FLT_MAX;
            if (localHRP.address)
            {
                Vectors::Vector3 diff = localHRP.Position() - targetPos;
                distance3D = diff.Magnitude();
                if (distance3D > Options::Ragebot::Range)
                    continue;
            }

            float screenDist = targetPos2D.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });
            if (screenDist <= Options::Ragebot::FOV)
            {
                float hc = 100.0f;
                if (Options::Ragebot::HitChanceEnabled)
                {
                    hc = CalculateHitChance(player, targetPos, hb);
                }

                targets.push_back({ player, targetPos, targetPos2D, screenDist, distance3D, hb, hc, visible });
            }
        }
    }

    std::sort(targets.begin(), targets.end(), [](const RagebotTarget& a, const RagebotTarget& b) {
        if (Options::Ragebot::TargetPriority == 0)
            return a.screenDist < b.screenDist;
        else if (Options::Ragebot::TargetPriority == 1)
            return a.distance3D < b.distance3D;
        else
            return a.hitChance > b.hitChance;
    });

    return targets;
}

inline bool ShouldFire(const RagebotTarget& target)
{
    if (!Options::Ragebot::AutoFire)
        return false;

    if (Options::Ragebot::HitChanceEnabled && target.hitChance < Options::Ragebot::MinHitChance)
        return false;

    if (target.distance3D > Options::Ragebot::Range)
        return false;

    return true;
}

inline void RagebotAim(const Vectors::Vector3& targetPos)
{
    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Position
    );

    sCFrame lookAt = LookAt(camPos, targetPos);
    Matrixes::Matrix3x3 targetRot = {
        lookAt.r00, lookAt.r01, -lookAt.r02,
        lookAt.r10, lookAt.r11, -lookAt.r12,
        lookAt.r20, lookAt.r21, -lookAt.r22
    };

    if (Options::Ragebot::Smoothness <= 0.0f)
    {
        Memory->write<Matrixes::Matrix3x3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Rotation, targetRot
        );
        return;
    }

    Matrixes::Matrix3x3 currentRot = Memory->read<Matrixes::Matrix3x3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Rotation
    );

    Vectors::Vector4 currentQuat = Vectors::Vector4::FromMatrix(currentRot);
    Vectors::Vector4 targetQuat = Vectors::Vector4::FromMatrix(targetRot);
    float t = std::clamp(Options::Ragebot::Smoothness, 0.01f, 1.0f);
    Vectors::Vector4 smoothedQuat = Vectors::Vector4::Slerp(currentQuat, targetQuat, t);
    Matrixes::Matrix3x3 smoothed = smoothedQuat.ToMatrix();

    Memory->write<Matrixes::Matrix3x3>(
        Globals::Roblox::Camera.address + Offsets::Camera::Rotation, smoothed
    );
}

inline void RagebotFire()
{
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
    Sleep(10);
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}

static std::chrono::steady_clock::time_point lastFireTime;
static std::chrono::steady_clock::time_point lastDoubleTapTime;
static bool doubleTapReady = false;
static int shotsFired = 0;

inline void RunRagebot()
{
    if (!Options::Ragebot::Enabled)
    {
        Options::Ragebot::Toggled = false;
        return;
    }

    if (AntiKatanaFiringBlocked())
        return;

    if (!Globals::Roblox::Camera.address)
        return;

    if (Globals::Roblox::Players.address != 0)
    {
        Globals::Roblox::LocalPlayer = RobloxInstance(
            Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer)
        );
    }

    if (!Globals::Roblox::LocalPlayer.address)
        return;

    bool keyActive = KeyBind::IsPressed(Options::Ragebot::RagebotKey);

    switch (Options::Ragebot::ToggleType)
    {
    case 0:
        if (!keyActive) return;
        break;
    case 1:
        if (keyActive)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFireTime).count() > 200)
            {
                Options::Ragebot::Toggled = !Options::Ragebot::Toggled;
                lastFireTime = now;
            }
        }
        if (!Options::Ragebot::Toggled) return;
        break;
    case 2:
        break;
    }

    auto targets = GetRagebotTargets();
    if (targets.empty())
        return;

    RagebotTarget bestTarget = targets[0];

    if (Options::Ragebot::HitChanceEnabled && bestTarget.hitChance < Options::Ragebot::MinHitChance)
        return;

    RagebotAim(bestTarget.targetPos);

    if (Options::Ragebot::AutoFire)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFireTime).count();

        bool canFire = elapsed >= Options::Ragebot::FireRate;

        if (Options::Ragebot::DoubleTap && shotsFired < 2 && doubleTapReady)
        {
            canFire = true;
        }

        if (canFire)
        {
            RagebotFire();
            lastFireTime = now;
            shotsFired++;

            if (Options::Ragebot::DoubleTap && shotsFired == 1)
            {
                doubleTapReady = true;
                lastDoubleTapTime = now;
            }
            else if (shotsFired >= 2)
            {
                shotsFired = 0;
                doubleTapReady = false;
            }
        }

        if (Options::Ragebot::DoubleTap && doubleTapReady)
        {
            auto dtElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDoubleTapTime).count();
            if (dtElapsed > Options::Ragebot::DoubleTapDelay)
            {
                doubleTapReady = false;
                shotsFired = 0;
            }
        }
    }
}

// Rage tab kill/orbit loop with ghost visual
inline RobloxPlayer GetRageKillTarget()
{
    if (Options::Rage::TargetMode == 1)
    {
        std::string name(Options::Rage::TargetPlayer);
        if (!name.empty())
        {
            auto tp = FindPlayerByName(name);
            if (tp.address)
            {
                RobloxPlayer p;
                p.address = tp.address;
                p.Character = tp.Character();
                if (p.Character.address)
                {
                    p.HumanoidRootPart = p.Character.FindFirstChild("HumanoidRootPart");
                    if (p.HumanoidRootPart.address)
                        return p;
                }
            }
        }
        return RobloxPlayer();
    }

    RobloxPlayer best;
    float bestDist = FLT_MAX;
    auto localChar = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localChar.FindFirstChild("HumanoidRootPart");
    POINT p; GetCursorPos(&p);

    for (auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (player.address == Globals::Roblox::LocalPlayer.address) continue;
        if (Globals::Roblox::isOverkill && !player.Name.empty() &&
            player.Name == Globals::Roblox::LocalPlayer.Name()) continue;
        if (!player.Name.empty() && !PlayerFilter::AimbotAllowed(player.Name)) continue;
        if (player.Health <= 0.f) continue;

        auto hrp = player.HumanoidRootPart;
        if (!hrp.address) continue;

        Vectors::Vector3 tp = hrp.Position();
        Vectors::Vector2 t2d = WorldToScreen(tp);
        if (t2d.x == -1 && t2d.y == -1) continue;

        float d = t2d.Distance({ static_cast<float>(p.x), static_cast<float>(p.y) });
        if (d < bestDist) { bestDist = d; best = player; }
    }
    return best;
}

inline void RageKillLoop()
{
    double angle = 0.0;
    static std::chrono::steady_clock::time_point lastFire;
    static std::chrono::steady_clock::time_point lastToggle;

    while (Globals::running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        if (Options::Rage::RageKey != 0)
        {
            static bool wasKey = false;
            bool isKey = (GetAsyncKeyState(Options::Rage::RageKey) & 0x8000) != 0;
            if (Options::Rage::ToggleType == 0) Options::Rage::Toggled = isKey;
            else if (Options::Rage::ToggleType == 1)
            {
                if (isKey && !wasKey)
                {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastToggle).count() > 200)
                    {
                        Options::Rage::Toggled = !Options::Rage::Toggled;
                        lastToggle = now;
                    }
                }
            }
            wasKey = isKey;
        }

        bool active = false;
        if (Options::Rage::Enabled)
        {
            if (Options::Rage::ToggleType == 2) active = true;
            else if (Options::Rage::RageKey != 0) active = Options::Rage::Toggled;
            else active = true;
        }

        if (!active)
        {
            RageVisual::hasGhost = false;
            continue;
        }

        try
        {
            auto localPlayer = Globals::Roblox::LocalPlayer;
            if (!localPlayer.address) { RageVisual::hasGhost = false; continue; }

            auto localChar = localPlayer.Character();
            if (!localChar.address && g_ResolveCharacterFallback)
                localChar = g_ResolveCharacterFallback(localPlayer.address);
            if (!localChar.address) { RageVisual::hasGhost = false; continue; }

            auto localHrp = localChar.FindFirstChild("HumanoidRootPart");
            if (!localHrp.address) { RageVisual::hasGhost = false; continue; }

            uintptr_t localPrim = Memory->read<uintptr_t>(localHrp.address + Offsets::BasePart::Primitive);
            if (!localPrim) { RageVisual::hasGhost = false; continue; }

            RobloxPlayer target = GetRageKillTarget();
            if (!target.address || !target.HumanoidRootPart.address)
            {
                RageVisual::hasGhost = false;
                continue;
            }

            Vectors::Vector3 realPos = localHrp.Position();
            Vectors::Vector3 tgtPos = target.HumanoidRootPart.Position();

            angle += 0.008 * Options::Rage::OrbitSpeed;
            float r = Options::Rage::OrbitRadius;
            Vectors::Vector3 ghost{
                tgtPos.x + static_cast<float>(std::sin(angle) * r),
                tgtPos.y + Options::Rage::OrbitHeight,
                tgtPos.z + static_cast<float>(std::cos(angle) * r)
            };

            RageVisual::ghostPos = ghost;
            RageVisual::realPos = realPos;
            RageVisual::hasGhost = Options::Rage::ShowGhost;

            if (Options::Rage::KillOnOrbit && !AntiKatanaFiringBlocked())
            {
                Vectors::Vector3 savedPos = realPos;
                Vectors::Vector3 savedVel = Memory->read<Vectors::Vector3>(
                    localPrim + Offsets::Primitive::AssemblyLinearVelocity);

                Matrixes::Matrix3x3 savedCamRot;
                bool hasSavedCamRot = false;
                if (Options::Rage::AutoKillAim && Globals::Roblox::Camera.address)
                {
                    savedCamRot = Memory->read<Matrixes::Matrix3x3>(
                        Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
                    hasSavedCamRot = true;
                }

                Vectors::Vector3 killPos{
                    tgtPos.x,
                    tgtPos.y,
                    tgtPos.z
                };

                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::Position, killPos);
                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity, { 0,0,0 });

                if (Options::Rage::AutoKillAim && Globals::Roblox::Camera.address)
                {
                    Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
                        Globals::Roblox::Camera.address + Offsets::Camera::Position);
                    sCFrame lookAt = LookAt(camPos, tgtPos);
                    Matrixes::Matrix3x3 rot{ lookAt.r00, lookAt.r01, -lookAt.r02,
                                             lookAt.r10, lookAt.r11, -lookAt.r12,
                                             lookAt.r20, lookAt.r21, -lookAt.r22 };
                    Memory->write<Matrixes::Matrix3x3>(
                        Globals::Roblox::Camera.address + Offsets::Camera::Rotation, rot);
                }

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFire).count() >= Options::Ragebot::FireRate)
                {
                    RagebotFire();
                    lastFire = now;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(12));
                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::Position, savedPos);
                Memory->write<Vectors::Vector3>(localPrim + Offsets::Primitive::AssemblyLinearVelocity, savedVel);

                if (hasSavedCamRot && Globals::Roblox::Camera.address)
                {
                    Memory->write<Matrixes::Matrix3x3>(
                        Globals::Roblox::Camera.address + Offsets::Camera::Rotation, savedCamRot);
                }
            }
        }
        catch (...) { RageVisual::hasGhost = false; }
    }
}

inline void RenderRageGhost(ImDrawList* drawList)
{
    if (!Options::Rage::Enabled || !Options::Rage::ShowGhost || !RageVisual::hasGhost)
        return;

    auto g = WorldToScreen(RageVisual::ghostPos);
    auto r = WorldToScreen(RageVisual::realPos);
    if (g.x == -1.f || g.y == -1.f) return;

    const ImU32 col = IM_COL32(
        static_cast<int>(Options::Rage::GhostColor[0] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[1] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[2] * 255.f),
        static_cast<int>(Options::Rage::GhostAlpha * 255.f));

    float viewDist = RageVisual::realPos.Distance(RageVisual::ghostPos);
    if (viewDist < 1.f) viewDist = 200.f;
    const float scale = 450.f / fmaxf(viewDist, 1.f);
    const float s = fminf(fmaxf(scale, 0.3f), 3.0f);
    const float boxW = 12.f * s, boxH = 24.f * s;

    ImVec2 bbMin(g.x - boxW * 0.5f, g.y - boxH * 0.5f);
    ImVec2 bbMax(g.x + boxW * 0.5f, g.y + boxH * 0.5f);

    drawList->AddRectFilled(bbMin, bbMax, IM_COL32(
        static_cast<int>(Options::Rage::GhostColor[0] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[1] * 255.f),
        static_cast<int>(Options::Rage::GhostColor[2] * 255.f),
        static_cast<int>(40.f * Options::Rage::GhostAlpha)), 4.0f);
    drawList->AddRect(bbMin, bbMax, col, 4.0f, 0, 2.0f);

    const float hr = boxW * 0.35f;
    ImVec2 head(g.x, bbMin.y - hr - 2.f);
    drawList->AddCircle(head, hr, col, 16, 1.5f);
    drawList->AddCircleFilled(head, hr * 0.4f, col, 12);

    if (Options::Rage::ShowGhostLine && r.x != -1.f && r.y != -1.f)
    {
        drawList->AddLine(ImVec2(r.x, r.y), ImVec2(g.x, g.y), col, 2.0f);
    }
}