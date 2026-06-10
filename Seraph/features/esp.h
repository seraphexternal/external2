#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cfloat>

#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/KeyBind.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "combatfeedback.h"
#include "visibility.h"

inline float EspClamp(float value, float minVal, float maxVal)
{
    if (value < minVal)
        return minVal;
    if (value > maxVal)
        return maxVal;
    return value;
}

inline float EspMin(float a, float b)
{
    return a < b ? a : b;
}

inline float EspMax(float a, float b)
{
    return a > b ? a : b;
}

inline void GetPlayerHealth(const RobloxPlayer& player, float& health, float& maxHealth)
{
    health = player.Health;
    maxHealth = player.MaxHealth;

    if (maxHealth <= 0.f)
        maxHealth = (health > 0.f) ? health : 100.f;
}

inline bool EspAnyEnabled()
{
    return Options::ESP::BoxType != 0
        || Options::ESP::Tracers
        || Options::ESP::Skeleton
        || Options::ESP::Name
        || Options::ESP::Distance
        || Options::ESP::Health
        || Options::ESP::HeadCircle
        || Options::ESP::HeadDot
        || Options::ESP::CornerESP
        || Options::ESP::HealthText
        || Options::ESP::EnemyHealthIndicator
        || Options::Combat::HitChams
        || Options::ESP::VisibilityCheck
        || Options::ESP::VisibilityChams;
}

inline void RenderESP(ImDrawList* drawList)
{
    if (!Options::ESP::Enabled)
        return;

    if (Options::ESP::ESPKey != 0)
    {
        static bool wasKeyPressed = false;
        bool isKeyPressed = KeyBind::IsPressed(Options::ESP::ESPKey);
        
        if (Options::ESP::ToggleType == 1)
        {
            if (isKeyPressed && !wasKeyPressed)
                Options::ESP::Toggled = !Options::ESP::Toggled;
            wasKeyPressed = isKeyPressed;
            
            if (!Options::ESP::Toggled)
                return;
        }
        else
        {
            if (!isKeyPressed)
            {
                Options::ESP::Toggled = false;
                return;
            }
        }
    }

    if (!drawList || !Globals::Viewport::Valid || !EspAnyEnabled())
        return;

    if (Globals::Caches::CachedPlayerObjects.empty())
        return;

    const auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    if (!localHRP.address) localHRP = localCharacter.FindFirstChild("Torso");
    if (!localHRP.address) localHRP = localCharacter.FindFirstChild("UpperTorso");

    auto localHead = localCharacter.FindFirstChild("Head");
    if (!localHead.address) localHead = localHRP;

    Vectors::Vector3 localPos = {0,0,0};
    if (localHRP.address) localPos = localHRP.Position();
    else if (localHead.address) localPos = localHead.Position();

    Visibility::BeginFrame();

    if (Options::ESP::VisibilityCheck || Options::ESP::VisibilityChams)
        Visibility::RefreshOccludersIfNeeded();

    ImFont* font = ImGui::GetFont();
    const ImGuiIO& io = ImGui::GetIO();

    const ImU32 boxColor = IM_COL32(
        static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[2] * 255.f),
        255);

    const ImU32 nameColor = IM_COL32(
        static_cast<int>(Options::ESP::Color[0] * 255.f),
        static_cast<int>(Options::ESP::Color[1] * 255.f),
        static_cast<int>(Options::ESP::Color[2] * 255.f),
        255);

    const ImU32 distanceColor = IM_COL32(
        static_cast<int>(Options::ESP::DistanceColor[0] * 255.f),
        static_cast<int>(Options::ESP::DistanceColor[1] * 255.f),
        static_cast<int>(Options::ESP::DistanceColor[2] * 255.f),
        255);

    const ImU32 tracerColor = IM_COL32(
        static_cast<int>(Options::ESP::TracerColor[0] * 255.f),
        static_cast<int>(Options::ESP::TracerColor[1] * 255.f),
        static_cast<int>(Options::ESP::TracerColor[2] * 255.f),
        255);

    const ImU32 skeletonColor = IM_COL32(
        static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
        static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
        static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
        255);

    const ImU32 headCircleColor = IM_COL32(
        static_cast<int>(Options::ESP::HeadCircleColor[0] * 255.f),
        static_cast<int>(Options::ESP::HeadCircleColor[1] * 255.f),
        static_cast<int>(Options::ESP::HeadCircleColor[2] * 255.f),
        255);

    const ImU32 headDotColor = IM_COL32(
        static_cast<int>(Options::ESP::HeadDotColor[0] * 255.f),
        static_cast<int>(Options::ESP::HeadDotColor[1] * 255.f),
        static_cast<int>(Options::ESP::HeadDotColor[2] * 255.f),
        255);

    const ImU32 esp3DColor = IM_COL32(
        static_cast<int>(Options::ESP::ESP3DColor[0] * 255.f),
        static_cast<int>(Options::ESP::ESP3DColor[1] * 255.f),
        static_cast<int>(Options::ESP::ESP3DColor[2] * 255.f),
        255);

    const Vectors::Vector3 localHeadPos = localHead.address ? localHead.Position() : localPos;

    auto projectPart = [](const RobloxInstance& part, float& left, float& top, float& right, float& bottom, bool& any) -> bool
    {
        if (!part.address)
            return false;

        const auto screen = WorldToScreen(part.Position());
        if (screen.x < 0.f || screen.y < 0.f)
            return false;

        any = true;
        left = EspMin(left, screen.x);
        top = EspMin(top, screen.y);
        right = EspMax(right, screen.x);
        bottom = EspMax(bottom, screen.y);
        return true;
    };

    for (const auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        float skipHealth = 0.f;
        float skipMaxHealth = 0.f;
        GetPlayerHealth(player, skipHealth, skipMaxHealth);
        if (skipMaxHealth > 0.f && skipHealth <= 0.f)
            continue;

        if (Options::ESP::TeamCheck && IsTeammate(player))
            continue;

        auto targetHead = player.Head;
        if (!targetHead.address) targetHead = player.Character.FindFirstChild("Head");

        auto targetHRP = player.HumanoidRootPart;
        if (!targetHRP.address) targetHRP = player.Torso;
        if (!targetHRP.address) targetHRP = player.Upper_Torso;
        if (!targetHRP.address) targetHRP = player.Character.FindFirstChild("Torso");
        if (!targetHRP.address) targetHRP = player.Character.FindFirstChild("HumanoidRootPart");
        if (!targetHRP.address) targetHRP = player.Character.FindFirstChild("UpperTorso");

        if (!targetHead.address) targetHead = targetHRP;
        if (!targetHRP.address) targetHRP = targetHead;

        if (!targetHead.address || !targetHRP.address)
            continue;

        float left = FLT_MAX;
        float top = FLT_MAX;
        float right = -FLT_MAX;
        float bottom = -FLT_MAX;
        bool hasBounds = false;

        projectPart(targetHead, left, top, right, bottom, hasBounds);
        projectPart(targetHRP, left, top, right, bottom, hasBounds);

        if (player.RigType == 0)
        {
            projectPart(player.Torso, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Arm, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Arm, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Leg, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Leg, left, top, right, bottom, hasBounds);
        }
        else
        {
            projectPart(player.Upper_Torso, left, top, right, bottom, hasBounds);
            projectPart(player.Lower_Torso, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Hand, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Hand, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Foot, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Foot, left, top, right, bottom, hasBounds);
        }

        if (!hasBounds)
            continue;

        float width = right - left;
        float height = bottom - top;

        // Fallback: If only vertically aligned parts (Head/HRP) projected successfully, width will be near 0.
        // Standard character proportion is roughly 1:2 (width:height).
        if (width == 0.f && height == 0.f)
        {
            width = 10.f; // Arbitrary small size to show a dot or box at the point
            height = 10.f;
            left -= width / 2.0f;
            right += width / 2.0f;
            top -= height / 2.0f;
            bottom += height / 2.0f;
        }
        else if (width < height * 0.2f)
        {
            float center_x = left + (width / 2.0f);
            width = height * 0.55f;
            left = center_x - (width / 2.0f);
            right = center_x + (width / 2.0f);
        }

        left -= width * 0.1f;
        right += width * 0.1f;
        top -= height * 0.08f;
        bottom += height * 0.05f;

        const bool validBounds = left < right && top < bottom;
        if (!validBounds)
            continue;

        float liveHealth = 0.f;
        float liveMaxHealth = 0.f;
        GetPlayerHealth(player, liveHealth, liveMaxHealth);

        const float distance3D = localPos.x != 0 ? targetHRP.Position().Distance(localPos) : 0.f;
        if (distance3D > Options::ESP::MaxRenderDistance) continue;
        if (distance3D > 2000.f) continue;
        const float scale = EspClamp(450.f / EspClamp(distance3D, 1.f, 4500.f), 0.65f, 2.5f);

        const bool playerVisible = Visibility::IsPlayerVisible(player);
        const ImU32 activeBoxColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : boxColor;
        const ImU32 activeSkeletonColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : skeletonColor;
        const ImU32 activeTracerColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : tracerColor;
        const ImU32 activeNameColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : nameColor;
        const ImU32 activeHeadCircleColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : headCircleColor;
        const ImU32 activeHeadDotColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : headDotColor;

        if (Options::ESP::VisibilityChams)
            Visibility::DrawPlayerVisibilityChams(drawList, player, distance3D);

        const auto headScreen = WorldToScreen(targetHead.Position());
        const ImVec2 head2D(headScreen.x, headScreen.y);
        ImVec2 newHeadName(head2D.x, top - 4.f);

        const bool hitFlash = Options::Combat::HitChams && CombatFeedback::IsHitFlashing(player.address);
        const float hitAlpha = CombatFeedback::HitFlashAlpha(player.address);

        if (hitFlash)
        {
            const ImU32 hitFill = IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                static_cast<int>(140.f * hitAlpha));
            const ImU32 hitOutline = IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                static_cast<int>(220.f * hitAlpha));

            drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), hitFill);
            drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), hitOutline, 0.f, 0, 2.0f);
        }

        if (Options::ESP::BoxType == 1)
        {
            if (!Options::ESP::RemoveBorders)
                drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(0, 0, 0, 255), 0.f, 0, Options::ESP::BoxThickness + 1.5f);
            drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), activeBoxColor, 0.f, 0, Options::ESP::BoxThickness);
        }

        if (Options::ESP::CornerESP)
        {
            const ImU32 cornerColor = Options::ESP::VisibilityCheck
                ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                : IM_COL32(
                    static_cast<int>(Options::ESP::CornerColor[0] * 255.f),
                    static_cast<int>(Options::ESP::CornerColor[1] * 255.f),
                    static_cast<int>(Options::ESP::CornerColor[2] * 255.f),
                    255);

            const float cornerLen = EspClamp((right - left) * 0.22f, 6.0f, 24.0f);
            const float t = Options::ESP::BoxThickness;

            auto corner = [&](ImVec2 a, ImVec2 b, ImVec2 c)
            {
                drawList->AddLine(a, b, cornerColor, t);
                drawList->AddLine(a, c, cornerColor, t);
            };

            corner(ImVec2(left, top), ImVec2(left + cornerLen, top), ImVec2(left, top + cornerLen));
            corner(ImVec2(right, top), ImVec2(right - cornerLen, top), ImVec2(right, top + cornerLen));
            corner(ImVec2(left, bottom), ImVec2(left + cornerLen, bottom), ImVec2(left, bottom - cornerLen));
            corner(ImVec2(right, bottom), ImVec2(right - cornerLen, bottom), ImVec2(right, bottom - cornerLen));
        }

        if (Options::ESP::BoxType == 2)
        {
            const auto& hrp = player.HumanoidRootPart;
            const Vectors::Vector3 partPos = hrp.Position();
            const Vectors::Vector3 partSize = hrp.Size();
            const sCFrame partCFrame = hrp.CFrame();

            const Vectors::Vector3 rightVec = partCFrame.GetRightVector();
            const Vectors::Vector3 upVec = partCFrame.GetUpVector();
            const Vectors::Vector3 lookVec = partCFrame.GetLookVector();

            const float halfX = partSize.x * 0.5f;
            const float halfY = partSize.y * 0.5f;
            const float halfZ = partSize.z * 0.5f;

            const std::vector<Vectors::Vector3> corners3D = {
                partPos + rightVec * halfX + upVec * halfY + lookVec * halfZ,
                partPos - rightVec * halfX + upVec * halfY + lookVec * halfZ,
                partPos + rightVec * halfX - upVec * halfY + lookVec * halfZ,
                partPos - rightVec * halfX - upVec * halfY + lookVec * halfZ,
                partPos + rightVec * halfX + upVec * halfY - lookVec * halfZ,
                partPos - rightVec * halfX + upVec * halfY - lookVec * halfZ,
                partPos + rightVec * halfX - upVec * halfY - lookVec * halfZ,
                partPos - rightVec * halfX - upVec * halfY - lookVec * halfZ
            };

            std::vector<ImVec2> corners2D;
            corners2D.reserve(8);
            for (const auto& corner : corners3D)
            {
                const auto screenPos = WorldToScreen(corner);
                if (screenPos.x != -1.f && screenPos.y != -1.f)
                    corners2D.emplace_back(screenPos.x, screenPos.y);
            }

            if (corners2D.size() >= 8)
            {
                const float thickness = Options::ESP::ESP3DThickness;
                const ImU32 active3DColor = Options::ESP::VisibilityCheck
                    ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                    : esp3DColor;

                drawList->AddLine(corners2D[0], corners2D[1], active3DColor, thickness);
                drawList->AddLine(corners2D[1], corners2D[3], active3DColor, thickness);
                drawList->AddLine(corners2D[3], corners2D[2], active3DColor, thickness);
                drawList->AddLine(corners2D[2], corners2D[0], active3DColor, thickness);

                drawList->AddLine(corners2D[4], corners2D[5], active3DColor, thickness);
                drawList->AddLine(corners2D[5], corners2D[7], active3DColor, thickness);
                drawList->AddLine(corners2D[7], corners2D[6], active3DColor, thickness);
                drawList->AddLine(corners2D[6], corners2D[4], active3DColor, thickness);

                drawList->AddLine(corners2D[0], corners2D[4], active3DColor, thickness);
                drawList->AddLine(corners2D[1], corners2D[5], active3DColor, thickness);
                drawList->AddLine(corners2D[2], corners2D[6], active3DColor, thickness);
                drawList->AddLine(corners2D[3], corners2D[7], active3DColor, thickness);
            }
        }

        if (Options::ESP::Tracers)
        {
            ImVec2 tracerStart(io.DisplaySize.x * 0.5f, io.DisplaySize.y);
            switch (Options::ESP::TracersStart)
            {
            case 1:
                tracerStart = ImVec2(io.DisplaySize.x * 0.5f, 0.f);
                break;
            case 2:
            {
                POINT cursor{};
                GetCursorPos(&cursor);
                tracerStart = ImVec2(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
                break;
            }
            case 3:
            {
                const auto torsoScreen = WorldToScreen(localPos);
                if (torsoScreen.x != -1.f && torsoScreen.y != -1.f)
                    tracerStart = ImVec2(torsoScreen.x, torsoScreen.y);
                break;
            }
            default:
                break;
            }

            const auto targetScreen = WorldToScreen(targetHRP.Position());
            if (targetScreen.x != -1.f && targetScreen.y != -1.f)
                drawList->AddLine(tracerStart, ImVec2(targetScreen.x, targetScreen.y), activeTracerColor, Options::ESP::TracerThickness);
        }

        if (Options::ESP::Skeleton)
        {
            auto drawBone = [&](const RobloxInstance& a, const RobloxInstance& b)
            {
                if (!a.address || !b.address)
                    return;

                const auto p1 = WorldToScreen(a.Position());
                const auto p2 = WorldToScreen(b.Position());
                if (p1.x == -1.f || p1.y == -1.f || p2.x == -1.f || p2.y == -1.f)
                    return;

                drawList->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), activeSkeletonColor, Options::ESP::SkeletonThickness);
            };

            if (player.RigType == 0)
            {
                drawBone(player.Head, player.Torso);
                drawBone(player.Torso, player.Left_Arm);
                drawBone(player.Torso, player.Right_Arm);
                drawBone(player.Torso, player.Left_Leg);
                drawBone(player.Torso, player.Right_Leg);
            }
            else
            {
                drawBone(player.Head, player.Upper_Torso);
                drawBone(player.Upper_Torso, player.Lower_Torso);
                drawBone(player.Upper_Torso, player.Left_Upper_Arm);
                drawBone(player.Left_Upper_Arm, player.Left_Lower_Arm);
                drawBone(player.Left_Lower_Arm, player.Left_Hand);
                drawBone(player.Upper_Torso, player.Right_Upper_Arm);
                drawBone(player.Right_Upper_Arm, player.Right_Lower_Arm);
                drawBone(player.Right_Lower_Arm, player.Right_Hand);
                drawBone(player.Lower_Torso, player.Left_Upper_Leg);
                drawBone(player.Left_Upper_Leg, player.Left_Lower_Leg);
                drawBone(player.Left_Lower_Leg, player.Left_Foot);
                drawBone(player.Lower_Torso, player.Right_Upper_Leg);
                drawBone(player.Right_Upper_Leg, player.Right_Lower_Leg);
                drawBone(player.Right_Lower_Leg, player.Right_Foot);
            }
        }

        if (Options::ESP::HeadCircle && headScreen.x != -1.f && headScreen.y != -1.f)
        {
            const float boxHeight = bottom - top;
            const float radius = EspClamp(boxHeight * Options::ESP::HeadCircleScale, 3.f, 14.f);
            drawList->AddCircle(head2D, radius, activeHeadCircleColor, 0, Options::ESP::HeadCircleThickness);
        }

        if (Options::ESP::HeadDot && headScreen.x != -1.f && headScreen.y != -1.f)
        {
            const float dotRadius = EspClamp((bottom - top) * 0.04f, 2.f, 5.f);
            drawList->AddCircleFilled(head2D, dotRadius, activeHeadDotColor, 12);
        }

        if (Options::ESP::Name)
        {
            std::string displayName = player.Name;
            displayName += " (C:" + std::to_string(player.TeamColor & 0xFFFF) + " N:" + (player.TeamName.empty() ? "None" : player.TeamName) + " B:" + std::to_string(player.TeamBrickColor & 0xFFFF) + ")";
            
            const float baseSize = Options::ESP::NameSize;
            const float fontSize = (baseSize * scale > 11.f) ? baseSize * scale : 11.f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, displayName.c_str());
            const ImVec2 namePos(head2D.x - textSize.x * 0.5f, top - textSize.y - 2.f);
            newHeadName = namePos;
            
            float t = Options::ESP::NameThickness;
            if (t > 0.0f)
            {
                ImU32 outlineColor = IM_COL32(0, 0, 0, 255);
                drawList->AddText(font, fontSize, ImVec2(namePos.x + t, namePos.y + t), outlineColor, displayName.c_str());
                drawList->AddText(font, fontSize, ImVec2(namePos.x - t, namePos.y - t), outlineColor, displayName.c_str());
                drawList->AddText(font, fontSize, ImVec2(namePos.x + t, namePos.y - t), outlineColor, displayName.c_str());
                drawList->AddText(font, fontSize, ImVec2(namePos.x - t, namePos.y + t), outlineColor, displayName.c_str());
            }

            drawList->AddText(font, fontSize, namePos, activeNameColor, displayName.c_str());
        }

        if (Options::ESP::Distance)
        {
            const float studs = player.Head.address
                ? player.Head.Position().Distance(localHeadPos)
                : distance3D;

            char distText[32];
            snprintf(distText, sizeof(distText), "%.0f studs", studs);

            const float fontSize = (12.f * scale > 10.f) ? 12.f * scale : 10.f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, distText);
            const ImVec2 distPos((left + right) * 0.5f - textSize.x * 0.5f, bottom + 4.f);
            const ImU32 activeDistanceColor = Options::ESP::VisibilityCheck
                ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                : distanceColor;
            drawList->AddText(font, fontSize, distPos, activeDistanceColor, distText);
        }

        if (Options::ESP::Health)
        {
            const float healthPercent = EspClamp(liveHealth / liveMaxHealth, 0.f, 1.f);
            const float barWidth = 4.f;
            const float boxHeight = bottom - top;

            const ImVec2 barTopLeft(right + 3.f, top);
            const ImVec2 barBottomRight(right + 3.f + barWidth, bottom);

            drawList->AddRectFilled(barTopLeft, barBottomRight, IM_COL32(30, 30, 30, 200));

            const float filledHeight = boxHeight * healthPercent;
            const ImVec2 filledTopLeft(barTopLeft.x, barBottomRight.y - filledHeight);
            const ImVec2 filledBottomRight(barBottomRight.x, barBottomRight.y);

            const int r = static_cast<int>((1.0f - healthPercent) * 255.0f);
            const int g = static_cast<int>(healthPercent * 255.0f);
            const ImU32 barColor = IM_COL32(r, g, 0, 230);

            drawList->AddRectFilled(filledTopLeft, filledBottomRight, barColor);
            drawList->AddRect(barTopLeft, barBottomRight, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.2f);
        }

        if (Options::ESP::HealthText)
        {
            char hpText[32];
            snprintf(hpText, sizeof(hpText), "%.0f/%.0f", liveHealth, liveMaxHealth);
            const float fontSize = (11.f * scale > 10.0f) ? 11.f * scale : 10.0f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, hpText);
            const ImVec2 hpPos(right + 10.f, top + (bottom - top) * 0.5f - textSize.y * 0.5f);
            drawList->AddText(font, fontSize, hpPos, IM_COL32(255, 255, 255, 230), hpText);
        }

        if (Options::ESP::EnemyHealthIndicator && liveMaxHealth > 0.f)
        {
            char hpText[24];
            snprintf(hpText, sizeof(hpText), "%.0f HP", liveHealth);
            const float fontSize = (13.f * scale > 11.0f) ? 13.f * scale : 11.0f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, hpText);
            const ImVec2 hpPos(head2D.x - textSize.x * 0.5f, newHeadName.y - textSize.y - 2.0f);
            const float hpPct = EspClamp(liveHealth / liveMaxHealth, 0.f, 1.f);
            const ImU32 hpColor = IM_COL32(
                static_cast<int>((1.0f - hpPct) * 255.0f),
                static_cast<int>(hpPct * 255.0f),
                0,
                255);
            drawList->AddText(font, fontSize, hpPos, hpColor, hpText);
        }
    }
}

inline void RenderESPPreview(ImDrawList* drawList, ImVec2 origin, ImVec2 size)
{
    const ImVec2 rectMin = origin;
    const ImVec2 rectMax(origin.x + size.x, origin.y + size.y);

    drawList->PushClipRect(rectMin, rectMax, true);
    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(8, 8, 8, 255), 4.0f);
    drawList->AddRect(rectMin, rectMax, IM_COL32(27, 27, 27, 255), 4.0f);

    const float pad = 22.0f;
    const float left = origin.x + pad;
    const float right = origin.x + size.x - pad;
    const float top = origin.y + pad + 8.0f;
    const float bottom = origin.y + size.y - pad;

    const ImU32 boxColor = IM_COL32(
        static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[2] * 255.f),
        255);

    const int boxType = Options::ESP::BoxType == 0 ? 1 : Options::ESP::BoxType;

    if (boxType == 1)
        drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxColor, 0, 0, Options::ESP::BoxThickness);

    if (Options::ESP::CornerESP || boxType == 1)
    {
        const float cornerLen = 18.0f;
        const ImU32 cornerColor = IM_COL32(
            static_cast<int>(Options::ESP::CornerColor[0] * 255.f),
            static_cast<int>(Options::ESP::CornerColor[1] * 255.f),
            static_cast<int>(Options::ESP::CornerColor[2] * 255.f),
            255);

        auto corner = [&](ImVec2 a, ImVec2 b, ImVec2 c)
        {
            drawList->AddLine(a, b, cornerColor, 1.8f);
            drawList->AddLine(a, c, cornerColor, 1.8f);
        };

        corner(ImVec2(left, top), ImVec2(left + cornerLen, top), ImVec2(left, top + cornerLen));
        corner(ImVec2(right, top), ImVec2(right - cornerLen, top), ImVec2(right, top + cornerLen));
        corner(ImVec2(left, bottom), ImVec2(left + cornerLen, bottom), ImVec2(left, bottom - cornerLen));
        corner(ImVec2(right, bottom), ImVec2(right - cornerLen, bottom), ImVec2(right, bottom - cornerLen));
    }

    if (Options::ESP::Health)
    {
        const float barWidth = 4.0f;
        const float healthPct = 0.65f;
        drawList->AddRectFilled(ImVec2(right + 3.0f, top), ImVec2(right + 3.0f + barWidth, bottom), IM_COL32(30, 30, 30, 200));
        const float filledTop = bottom - (bottom - top) * healthPct;
        drawList->AddRectFilled(ImVec2(right + 3.0f, filledTop), ImVec2(right + 3.0f + barWidth, bottom), IM_COL32(80, 220, 60, 230));
        drawList->AddRect(ImVec2(right + 3.0f, top), ImVec2(right + 3.0f + barWidth, bottom), IM_COL32(0, 0, 0, 255));
    }

    if (Options::ESP::HealthText)
    {
        const char* hpText = "65/100";
        const ImVec2 textSize = ImGui::CalcTextSize(hpText);
        drawList->AddText(ImVec2(right + 10.0f, (top + bottom) * 0.5f - textSize.y * 0.5f), IM_COL32(255, 255, 255, 230), hpText);
    }

    if (Options::ESP::EnemyHealthIndicator)
    {
        const char* hpText = "65 HP";
        const ImVec2 textSize = ImGui::CalcTextSize(hpText);
        drawList->AddText(ImVec2((left + right) * 0.5f - textSize.x * 0.5f, top - 18.0f), IM_COL32(0, 220, 60, 255), hpText);
    }

    if (Options::ESP::Name)
    {
        const char* previewName = "Player";
        const ImVec2 textSize = ImGui::CalcTextSize(previewName);
        drawList->AddText(ImVec2((left + right) * 0.5f - textSize.x * 0.5f, top - 34.0f), IM_COL32(255, 255, 255, 255), previewName);
    }

    if (Options::ESP::Distance)
    {
        const char* distText = "42 studs";
        const ImVec2 textSize = ImGui::CalcTextSize(distText);
        drawList->AddText(ImVec2((left + right) * 0.5f - textSize.x * 0.5f, bottom + 4.0f), IM_COL32(200, 200, 200, 255), distText);
    }

    if (Options::ESP::Skeleton)
    {
        const ImU32 skel = IM_COL32(
            static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
            static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
            static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
            255);
        const float cx = (left + right) * 0.5f;
        const float midY = (top + bottom) * 0.5f;
        drawList->AddLine(ImVec2(cx, top + 10.0f), ImVec2(cx, midY), skel, 1.8f);
        drawList->AddLine(ImVec2(cx, midY), ImVec2(left + 12.0f, bottom - 10.0f), skel, 1.8f);
        drawList->AddLine(ImVec2(cx, midY), ImVec2(right - 12.0f, bottom - 10.0f), skel, 1.8f);
        drawList->AddLine(ImVec2(cx, midY), ImVec2(cx, bottom - 8.0f), skel, 1.8f);
    }

    if (Options::Combat::HitChams)
    {
        drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
            IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                100));
        drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom),
            IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                200),
            0.f, 0, 2.0f);
    }

    drawList->PopClipRect();
}
