#pragma once

#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/utils/W2S.h"
#include "imgui/imgui.h"

namespace LodLineVisual
{
    inline void Render(ImDrawList* drawList)
    {
        if (!Options::ESP::LodLine)
            return;

        if (!drawList)
            return;

        if (Globals::Caches::CachedPlayerObjects.empty())
            return;

        ImU32 lineColor = IM_COL32(
            static_cast<int>(Options::ESP::LodLineColor[0] * 255),
            static_cast<int>(Options::ESP::LodLineColor[1] * 255),
            static_cast<int>(Options::ESP::LodLineColor[2] * 255),
            255
        );

        static const char* possibleNames[] = {
            "MousePos", "mousePos", "AimTarget", "LookDirection", "AimPoint", "TargetPosition"
        };

        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (!player.Head.address || !player.HumanoidRootPart.address)
                continue;

            if (player.address == Globals::Roblox::LocalPlayer.address)
                continue;

            Vectors::Vector3 headPos = player.Head.Position();
            Vectors::Vector3 lookDir{ 0.f, 0.f, 1.f };
            bool hasValidDirection = false;
            Vectors::Vector3 targetPoint{ 0.f, 0.f, 0.f };
            bool hasTargetPoint = false;

            auto character = player.Character;
            if (character.address)
            {
                auto children = character.GetChildren();
                for (const auto& child : children)
                {
                    if (!child.address)
                        continue;

                    std::string childName = child.Name();
                    bool match = false;
                    for (const auto& name : possibleNames)
                    {
                        if (childName == name)
                        {
                            match = true;
                            break;
                        }
                    }

                    if (match)
                    {
                        targetPoint = Memory->read<Vectors::Vector3>(child.address + Offsets::Misc::Value);
                        hasTargetPoint = true;

                        Vectors::Vector3 rawDir = targetPoint - headPos;
                        float mag = rawDir.Magnitude();
                        if (mag > 0.001f)
                        {
                            lookDir = { rawDir.x / mag, rawDir.y / mag, rawDir.z / mag };
                            hasValidDirection = true;
                            break;
                        }
                    }
                }
            }

            if (!hasValidDirection)
            {
                sCFrame headCFrame = player.Head.CFrame();
                Matrixes::Matrix3x3 headRotation{
                    headCFrame.r00, headCFrame.r01, headCFrame.r02,
                    headCFrame.r10, headCFrame.r11, headCFrame.r12,
                    headCFrame.r20, headCFrame.r21, headCFrame.r22
                };
                lookDir = { -headRotation.r02, -headRotation.r12, -headRotation.r22 };
                float mag = lookDir.Magnitude();
                if (mag > 0.001f)
                {
                    lookDir = { lookDir.x / mag, lookDir.y / mag, lookDir.z / mag };
                    hasValidDirection = true;
                }
            }

            if (!hasValidDirection && player.Upper_Torso.address)
            {
                Vectors::Vector3 torsoPos = player.Upper_Torso.Position();
                Vectors::Vector3 dirFromTorso = headPos - torsoPos;
                float mag = dirFromTorso.Magnitude();
                if (mag > 0.1f)
                {
                    lookDir = { dirFromTorso.x / mag, dirFromTorso.y / mag, dirFromTorso.z / mag };
                    hasValidDirection = true;
                }
            }

            if (!hasValidDirection)
                continue;

            Vectors::Vector3 rayEnd;
            if (hasTargetPoint)
            {
                rayEnd = targetPoint;
            }
            else
            {
                rayEnd = {
                    headPos.x + lookDir.x * Options::ESP::LodLineLength,
                    headPos.y + lookDir.y * Options::ESP::LodLineLength,
                    headPos.z + lookDir.z * Options::ESP::LodLineLength
                };
            }

            Vectors::Vector2 headScreen = WorldToScreen(headPos);
            Vectors::Vector2 rayEndScreen = WorldToScreen(rayEnd);

            if (headScreen.x <= 0 && headScreen.y <= 0)
                continue;
            if (rayEndScreen.x <= 0 && rayEndScreen.y <= 0)
                continue;

            drawList->AddLine(
                ImVec2(headScreen.x, headScreen.y),
                ImVec2(rayEndScreen.x, rayEndScreen.y),
                lineColor,
                Options::ESP::LodLineThickness
            );
        }
    }
}
