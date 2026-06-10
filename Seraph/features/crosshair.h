#pragma once
#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include <windows.h>
#include <cmath>
#include <algorithm>

inline void RenderCrosshair(ImDrawList* drawList)
{
    if (!Options::Crosshair::Enabled)
        return;

    static float fadeParam = 0.0f;
    static int lastStyle = Options::Crosshair::Style;
    static float currentSpeed = 0.0f;
    static float angle = 0.0f;
    static ImVec2 lastPos = ImVec2(0, 0);

    float time = ImGui::GetTime();
    float dt = ImGui::GetIO().DeltaTime;
    
    // Get mouse cursor position
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ImVec2 center = ImVec2(static_cast<float>(cursorPos.x), static_cast<float>(cursorPos.y));

    // Style handling
    if (Options::Crosshair::Style != lastStyle) {
        fadeParam = 0.0f;
        lastStyle = Options::Crosshair::Style;
    }

    float targetSpeed = Options::Crosshair::SpinSpeed * 0.5f;
    switch (Options::Crosshair::Style) {
    case 0: // Static
        targetSpeed = Options::Crosshair::SpinSpeed * 0.5f;
        fadeParam = 0.0f;
        break;
    case 1: // Pulse
        fadeParam = std::clamp(fadeParam + dt / 2.0f, 0.0f, 1.0f);
        targetSpeed = Options::Crosshair::SpinSpeed * (0.1f + 0.9f * (fadeParam * fadeParam));
        break;
    }

    const float accel = 5.0f;
    currentSpeed += (targetSpeed - currentSpeed) * std::clamp(accel * dt, 0.0f, 1.0f);
    angle += currentSpeed * (3.1415926f / 180.0f) * dt;
    if (angle > 6.2831853f) angle -= 6.2831853f;

    float showGap = Options::Crosshair::Gap;
    if (Options::Crosshair::GapTween) {
        float raw = fmodf(time * Options::Crosshair::GapSpeed, 2.0f);
        float e = raw < 1.0f ? (1.0f - (1.0f - raw) * (1.0f - raw)) : 1.0f - ((raw - 1.0f) * (raw - 1.0f));
        showGap = Options::Crosshair::Gap * e;
    }

    ImU32 colLine = IM_COL32(
        (int)(Options::Crosshair::Color[0] * 255),
        (int)(Options::Crosshair::Color[1] * 255),
        (int)(Options::Crosshair::Color[2] * 255),
        (int)(Options::Crosshair::Color[3] * 255)
    );
    ImU32 colOut = IM_COL32(0, 0, 0, 255);
    float thick = Options::Crosshair::Thickness;

    // Draw crosshair lines
    for (int i = 0; i < 4; ++i) {
        float a = angle + i * 3.1415926f * 0.5f;
        ImVec2 d{ cosf(a), sinf(a) };
        ImVec2 p0{ center.x + d.x * showGap, center.y + d.y * showGap };
        ImVec2 p1{ center.x + d.x * (showGap + Options::Crosshair::Size),
                center.y + d.y * (showGap + Options::Crosshair::Size) };

        // Outline
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy)
                    drawList->AddLine({ p0.x + dx, p0.y + dy },
                        { p1.x + dx, p1.y + dy },
                        colOut, thick);

        // Main line
        drawList->AddLine(p0, p1, colLine, thick);
    }

    // Draw "Seraph.gg" text below crosshair
    if (Options::Crosshair::ShowText) {
        const char* Seraph = "Seraph";
        const char* dotWin = ".gg";

        ImFont* font = ImGui::GetFont();
        float SeraphW = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, Seraph).x;
        float dotWinW = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, dotWin).x;
        float totalW = SeraphW + dotWinW;

        ImVec2 textPos{ center.x - totalW * 0.5f, center.y + showGap + Options::Crosshair::Size + 4 };

        ImU32 colWhite = IM_COL32(255, 255, 255, 255);
        ImU32 colPink = IM_COL32(255, 105, 180, 255); // Pink accent for .win
        ImU32 outlineColor = IM_COL32(0, 0, 0, 255);

        // Draw outline for "Seraph"
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx || dy) {
                    drawList->AddText(ImVec2(textPos.x + dx, textPos.y + dy), outlineColor, Seraph);
                }
            }
        }

        // Draw main text for "Seraph"
        drawList->AddText(textPos, colWhite, Seraph);

        // Draw outline for ".win"
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx || dy) {
                    drawList->AddText(ImVec2(textPos.x + SeraphW + dx, textPos.y + dy), outlineColor, dotWin);
                }
            }
        }

        // Draw main text for ".win"
        drawList->AddText(ImVec2(textPos.x + SeraphW, textPos.y), colPink, dotWin);
    }
}
