#pragma once

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/ui.h"

#include <thread>
#include <chrono>
#include <cmath>

// ── Rewind: press a key to drop a marker, press it again to teleport back ───
// Ported from the reference implementation. The marker is stored as the HRP's
// world position; teleporting hammers the primitive position so the game
// accepts it (identical technique to Click TP).
namespace Rewind
{
    namespace State
    {
        inline bool markerSet = false;
        inline Vectors::Vector3 markerPos{ 0.f, 0.f, 0.f };
        inline std::chrono::steady_clock::time_point markerTime{};
        inline bool keyWasPressed = false;
    }

    inline void Tick()
    {
        while (Globals::running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));

            if (!Options::Rewind::Enabled || Options::Rewind::Key == 0)
            {
                State::keyWasPressed = false;
                continue;
            }

            bool current = (GetAsyncKeyState(Options::Rewind::Key) & 0x8000) != 0;
            if (current && !State::keyWasPressed)
            {
                auto localPlayer = Globals::Roblox::LocalPlayer;
                auto hrp = localPlayer.Character().FindFirstChild("HumanoidRootPart");
                uintptr_t prim = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
                if (prim)
                {
                    if (!State::markerSet)
                    {
                        // First press: drop the marker at the current position.
                        State::markerPos = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Position);
                        State::markerSet = true;
                        State::markerTime = std::chrono::steady_clock::now();
                    }
                    else
                    {
                        // Second press: teleport back to the marker.
                        Vectors::Vector3 target = State::markerPos;
                        target.y += Options::Rewind::YOffset;
                        for (int i = 0; i < 10000; ++i)
                        {
                            Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::Position, target);
                            Memory->write<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity, { 0.f, 0.f, 0.f });
                        }
                        if (Options::Rewind::AutoClearOnTP)
                            State::markerSet = false;
                    }
                }
            }
            State::keyWasPressed = current;
        }
    }

    inline float DistanceToMarker()
    {
        if (!State::markerSet || !Globals::Roblox::LocalPlayer.address)
            return 0.f;
        auto hrp = Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart");
        uintptr_t prim = Memory->read<uintptr_t>(hrp.address + Offsets::BasePart::Primitive);
        if (!prim)
            return 0.f;
        Vectors::Vector3 cur = Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Position);
        return (cur - State::markerPos).Magnitude();
    }

    inline void DrawOverlay()
    {
        if (!Options::Rewind::Enabled || !Options::Rewind::ShowOverlay || !State::markerSet)
            return;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - State::markerTime).count();
        float elapsedSecs = elapsed / 1000.f;
        float distance = DistanceToMarker();

        char timeText[64];
        snprintf(timeText, sizeof(timeText), "%.1fs ago", elapsedSecs);
        char distText[64];
        snprintf(distText, sizeof(distText), "%.0f studs", distance);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(15, 15, 20, 220));
        ImGui::PushStyleColor(ImGuiCol_Border, UI::U(UI::P.accent));

        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, 50.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

        if (ImGui::Begin("##rewind_overlay", nullptr, flags))
        {
            const ImU32 accent = UI::U(UI::P.accent);

            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::Text("REWIND POINT");
            ImGui::PopStyleColor();

            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 190, 255));
            ImGui::Text("Time:");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("%s", timeText);

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 190, 255));
            ImGui::Text("Distance:");
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImU32 distColor = distance < 50.f ? IM_COL32(80, 255, 120, 255)
                : (distance < 150.f ? IM_COL32(255, 255, 100, 255) : IM_COL32(255, 100, 100, 255));
            ImGui::PushStyleColor(ImGuiCol_Text, distColor);
            ImGui::Text("%s", distText);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 190, 255));
            ImGui::Text("Press");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::Text("[%s]", UI::VKName(Options::Rewind::Key));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 190, 255));
            ImGui::Text("to return");
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    inline void DrawWorldMarker(ImDrawList* drawList)
    {
        if (!Options::Rewind::Enabled || !Options::Rewind::ShowWorldMarker || !State::markerSet)
            return;
        if (!Globals::Roblox::Camera.address)
            return;

        Vectors::Vector2 screen = WorldToScreen(State::markerPos);
        if (screen.x == -1.f || screen.y == -1.f)
            return;

        ImVec2 center(screen.x, screen.y);
        const ImU32 col = IM_COL32(
            (int)(Options::Rewind::MarkerColor[0] * 255),
            (int)(Options::Rewind::MarkerColor[1] * 255),
            (int)(Options::Rewind::MarkerColor[2] * 255),
            255);

        if (Options::Rewind::MarkerStyle == 0) // Circle
        {
            float pulse = (std::sin((float)ImGui::GetTime() * Options::Rewind::MarkerPulseSpeed) + 1.0f) / 2.0f;
            float radius = Options::Rewind::MarkerSize + pulse * 5.0f;

            if (Options::Rewind::MarkerGlow)
            {
                for (int i = 0; i < 3; ++i)
                    drawList->AddCircle(center, radius + (i + 1) * 4.0f, IM_COL32(
                        (int)(Options::Rewind::MarkerColor[0] * 255),
                        (int)(Options::Rewind::MarkerColor[1] * 255),
                        (int)(Options::Rewind::MarkerColor[2] * 255),
                        (int)(100.0f / (i + 1))), 32, 2.0f);
            }
            if (Options::Rewind::MarkerFilled)
            {
                drawList->AddCircleFilled(center, radius, IM_COL32(0, 0, 0, 100), 32);
                drawList->AddCircleFilled(center, radius - 2.0f, col, 32);
            }
            drawList->AddCircle(center, radius, col, 32, Options::Rewind::MarkerThickness);
        }
        else if (Options::Rewind::MarkerStyle == 1) // Cross
        {
            float size = Options::Rewind::MarkerSize;
            if (Options::Rewind::MarkerGlow)
            {
                for (int i = 0; i < 2; ++i)
                {
                    float thick = Options::Rewind::MarkerThickness + (i + 1) * 2.0f;
                    ImU32 glow = IM_COL32(
                        (int)(Options::Rewind::MarkerColor[0] * 255),
                        (int)(Options::Rewind::MarkerColor[1] * 255),
                        (int)(Options::Rewind::MarkerColor[2] * 255),
                        (int)(80.0f / (i + 1)));
                    drawList->AddLine(ImVec2(center.x - size, center.y), ImVec2(center.x + size, center.y), glow, thick);
                    drawList->AddLine(ImVec2(center.x, center.y - size), ImVec2(center.x, center.y + size), glow, thick);
                }
            }
            drawList->AddLine(ImVec2(center.x - size, center.y), ImVec2(center.x + size, center.y), col, Options::Rewind::MarkerThickness);
            drawList->AddLine(ImVec2(center.x, center.y - size), ImVec2(center.x, center.y + size), col, Options::Rewind::MarkerThickness);
        }
        else // Square
        {
            float size = Options::Rewind::MarkerSize;
            ImVec2 mn(center.x - size, center.y - size);
            ImVec2 mx(center.x + size, center.y + size);
            if (Options::Rewind::MarkerFilled)
            {
                drawList->AddRectFilled(mn, mx, IM_COL32(0, 0, 0, 100));
                drawList->AddRectFilled(ImVec2(mn.x + 2, mn.y + 2), ImVec2(mx.x - 2, mx.y - 2), col);
            }
            drawList->AddRect(mn, mx, col, 0.f, 0, Options::Rewind::MarkerThickness);
        }

        if (Options::Rewind::ShowMarkerText)
        {
            char label[64];
            snprintf(label, sizeof(label), "Rewind Point [%.0f studs]", DistanceToMarker());

            ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFont()->FontSize, FLT_MAX, 0.0f, label);
            ImVec2 textPos(center.x - textSize.x / 2.0f, center.y + Options::Rewind::MarkerSize + 15.0f);

            const ImU32 textCol = IM_COL32(
                (int)(Options::Rewind::MarkerTextColor[0] * 255),
                (int)(Options::Rewind::MarkerTextColor[1] * 255),
                (int)(Options::Rewind::MarkerTextColor[2] * 255),
                255);

            if (Options::Rewind::MarkerTextOutline)
            {
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        if (dx == 0 && dy == 0) continue;
                        drawList->AddText(ImVec2(textPos.x + dx, textPos.y + dy), IM_COL32(0, 0, 0, 200), label);
                    }
            }
            drawList->AddText(textPos, textCol, label);
        }
    }
}
