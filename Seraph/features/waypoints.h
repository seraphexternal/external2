#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/KeyBind.h"

namespace WaypointSystem
{
    constexpr int MAX_WAYPOINTS = 16;

    struct Waypoint
    {
        char name[32];
        Vectors::Vector3 position;
    };

    inline std::vector<Waypoint> waypoints;

    inline void AddWaypoint(const char* name, const Vectors::Vector3& pos)
    {
        if (waypoints.size() >= MAX_WAYPOINTS)
            return;

        Waypoint wp;
        strncpy_s(wp.name, sizeof(wp.name), name, _TRUNCATE);
        wp.position = pos;
        waypoints.push_back(wp);
    }

    inline void RemoveWaypoint(int index)
    {
        if (index >= 0 && index < (int)waypoints.size())
            waypoints.erase(waypoints.begin() + index);
    }

    inline void TeleportToWaypoint(int index)
    {
        if (index < 0 || index >= (int)waypoints.size())
            return;

        if (!Globals::Roblox::LocalPlayer.address)
            return;

        auto character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address)
            return;

        auto hrp = character.FindFirstChild("HumanoidRootPart");
        if (!hrp.address)
            return;

        uintptr_t primitiveAddr = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
        if (!primitiveAddr)
            return;

        Memory->write<Vectors::Vector3>(primitiveAddr + Offsets::Primitive::Position, waypoints[index].position);
    }

    inline int FindNearestWaypointIndex()
    {
        if (waypoints.empty() || !Globals::Roblox::Camera.address)
            return -1;

        Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Position);

        int nearest = -1;
        float bestDist = FLT_MAX;
        for (int i = 0; i < (int)waypoints.size(); i++)
        {
            float d = waypoints[i].position.Distance(camPos);
            if (d < bestDist)
            {
                bestDist = d;
                nearest = i;
            }
        }
        return nearest;
    }

    inline void RunTeleport()
    {
        if (!Options::Waypoints::TeleportKey)
            return;
        if (!KeyBind::IsPressed(Options::Waypoints::TeleportKey))
            return;

        static bool wasPressed = false;
        bool isPressed = KeyBind::IsPressed(Options::Waypoints::TeleportKey);
        if (isPressed && !wasPressed)
        {
            int idx = FindNearestWaypointIndex();
            if (idx >= 0)
                TeleportToWaypoint(idx);
        }
        wasPressed = isPressed;
    }

    inline void Render(ImDrawList* drawList)
    {
        if (!Options::Waypoints::Enabled || !Options::Waypoints::ShowOnESP || waypoints.empty())
            return;

        const ImU32 wpColor = IM_COL32(
            static_cast<int>(Options::Waypoints::Color[0] * 255.f),
            static_cast<int>(Options::Waypoints::Color[1] * 255.f),
            static_cast<int>(Options::Waypoints::Color[2] * 255.f),
            220);

        for (const auto& wp : waypoints)
        {
            auto screen = WorldToScreen(wp.position);
            if (screen.x == -1.f && screen.y == -1.f)
                continue;

            // Draw flag pole
            drawList->AddLine(
                ImVec2(screen.x, screen.y),
                ImVec2(screen.x, screen.y - 30.f),
                wpColor, 2.0f);

            // Draw flag
            drawList->AddTriangleFilled(
                ImVec2(screen.x, screen.y - 30.f),
                ImVec2(screen.x + 12.f, screen.y - 24.f),
                ImVec2(screen.x, screen.y - 18.f),
                wpColor);

            // Draw waypoint name
            ImVec2 textSize = ImGui::CalcTextSize(wp.name);
            drawList->AddText(
                ImVec2(screen.x - textSize.x * 0.5f, screen.y - 45.f - textSize.y),
                wpColor, wp.name);

            // Draw position circle at ground
            drawList->AddCircle(ImVec2(screen.x, screen.y), 4.f, wpColor, 12, 1.5f);
        }
    }

    inline void RenderUI()
    {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetColorU32(ImGuiCol_CheckMark));
        ImGui::Checkbox("Show on ESP", &Options::Waypoints::ShowOnESP);
        ImGui::ColorEdit3("Color", Options::Waypoints::Color, ImGuiColorEditFlags_NoInputs);
        ImGui::PopStyleColor(1);

        ImGui::Dummy(ImVec2(0, 2));
        KeybindSelector(" Teleport Key", &Options::Waypoints::TeleportKey);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6));

        // Add waypoint at current position
        if (ImGui::Button("Add Current Position", ImVec2(-1, 0)))
        {
            if (Globals::Roblox::Camera.address)
            {
                Vectors::Vector3 camPos = Memory->read<Vectors::Vector3>(
                    Globals::Roblox::Camera.address + Offsets::Camera::Position);

                static int wpCounter = 0;
                wpCounter++;
                char buf[32];
                snprintf(buf, sizeof(buf), "WP %d", wpCounter);
                AddWaypoint(buf, camPos);
            }
        }

        ImGui::Dummy(ImVec2(0, 4));

        if (waypoints.empty())
        {
            ImGui::TextDisabled("No waypoints saved");
        }
        else
        {
            ImGui::BeginChild("##wplist", ImVec2(0, 180), true);
            for (int i = 0; i < (int)waypoints.size(); i++)
            {
                ImGui::PushID(i);

                char buf[32];
                snprintf(buf, sizeof(buf), "%s", waypoints[i].name);

                if (ImGui::InputText("##name", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    strncpy_s(waypoints[i].name, sizeof(waypoints[i].name), buf, _TRUNCATE);
                }

                ImGui::SameLine();

                if (ImGui::SmallButton("TP"))
                {
                    TeleportToWaypoint(i);
                }

                ImGui::SameLine();

                if (ImGui::SmallButton("X"))
                {
                    RemoveWaypoint(i);
                    ImGui::PopID();
                    break;
                }

                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }
}
