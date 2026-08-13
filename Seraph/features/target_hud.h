#pragma once

// ── Target HUD ───────────────────────────────────────────────────────────────
// Draws a floating panel showing the current aimbot/silent target's avatar,
// name, health bar (with health text), held tool and distance. Ported from the
// reference implementation and adapted to this codebase: it reads the shared
// g_AimInfo state filled by RunAimCore and fetches avatars via AvatarManager.
// Called from the render loop; drawn on the background draw list so the menu
// always stays on top.

#include "../overlay/imgui/imgui.h"
#include "../overlay/ui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "aimbot.h"
#include "avatar_manager.h"
#include "hud_editor.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace TargetHud
{
    // Health gradient color matching ESP's health bar logic:
    // green (50,205,50) -> yellow (255,255,0) -> dark red (139,0,0)
    inline ImU32 HealthGradientCol(float frac)
    {
        const ImVec4 cg{ 50.f / 255.f, 205.f / 255.f, 50.f / 255.f, 1.f };
        const ImVec4 cy{ 255.f / 255.f, 255.f / 255.f, 0.f / 255.f, 1.f };
        const ImVec4 cr{ 139.f / 255.f, 0.f / 255.f, 0.f / 255.f, 1.f };

        auto lerp_col = [](ImVec4 a, ImVec4 b, float t) -> ImU32 {
            return IM_COL32(
                (int)((a.x + (b.x - a.x) * t) * 255.f),
                (int)((a.y + (b.y - a.y) * t) * 255.f),
                (int)((a.z + (b.z - a.z) * t) * 255.f),
                255);
        };

        return frac >= 0.5f
            ? lerp_col(cy, cg, (frac - 0.5f) / 0.5f)
            : lerp_col(cr, cy, frac / 0.5f);
    }

    inline void Render()
    {
        if (!Options::Aimbot::TargetHud)
            return;

        ImGuiIO& io = ImGui::GetIO();
        const float dt = io.DeltaTime;

        // Hold-on-loss state: keeps the last target visible briefly to prevent
        // flicker when the aimbot swaps targets between frames.
        static AimInfoState s_cached;
        static bool s_has_cached = false;
        static float s_hold_timer = 0.f;
        constexpr float k_hold_secs = 0.15f;

        if (g_AimInfo.valid)
        {
            if (g_AimInfo.name != s_cached.name)
                s_hold_timer = 0.f;
            s_cached = g_AimInfo;
            s_has_cached = true;
            s_hold_timer = k_hold_secs;
        }
        else
        {
            s_hold_timer -= dt;
            if (s_hold_timer <= 0.f)
            {
                s_has_cached = false;
                s_hold_timer = 0.f;
            }
        }

        if (!s_has_cached)
            return;

        // ── Layout constants ──────────────────────────────────────────────
        // Scale with the menu so the HUD is the same apparent size as the menu
        // at any DPI/MenuScale (previously hardcoded → tiny + pixelated).
        const float sc = UI::sc * Options::Aimbot::TargetHudScale;
        const float win_w     = 250.f * sc;
        const float pad_x     = 10.f * sc;
        const float pad_y     = 8.f * sc;
        const float avatar_sz = 38.f * sc;
        const float bar_h     = 6.f * sc;
        const float row_gap   = 3.f * sc;

        ImFont* font = ImGui::GetFont();
        if (!font) return;
        const float fs = font->FontSize * sc; // ~13px scaled

        const float text_area_h = fs + row_gap + fs; // name row + tool row
        const float content_h   = (avatar_sz > text_area_h ? avatar_sz : text_area_h) + row_gap + bar_h;
        const float win_h       = pad_y * 2.f + content_h;

        ImVec2 display = io.DisplaySize;

        // ── Position (preset or draggable) ────────────────────────────────
        const bool draggable = Options::Aimbot::TargetHudPositionMode == 0;
        ImVec2 pos;
        if (!draggable)
        {
            switch (Options::Aimbot::TargetHudPositionMode)
            {
            case 1: pos = ImVec2(display.x * 0.5f - win_w * 0.5f, 60.f); break;
            case 2: pos = ImVec2(display.x - win_w - 20.f, 60.f); break;
            case 3: pos = ImVec2(20.f, 60.f); break;
            case 4: pos = ImVec2(display.x * 0.5f - win_w * 0.5f, display.y - win_h - 60.f); break;
            default: pos = ImVec2(display.x * 0.5f - win_w * 0.5f, 60.f); break;
            }
        }
        else
        {
            if (Options::Aimbot::TargetHudPos[0] < 0.f)
                pos = ImVec2(display.x * 0.5f - win_w * 0.5f, 60.f);
            else
                pos = ImVec2(Options::Aimbot::TargetHudPos[0], Options::Aimbot::TargetHudPos[1]);
        }

        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // In HUD edit mode a grab-handle repositions the panel (replaces header
        // drag). Preset positions disable the handle by design – switch
        // "Position" to "Draggable" to move it.
        if (HudEditor::Enabled() && draggable)
        {
            float hx = pos.x, hy = pos.y;
            HudEditor::Handle("##targethud", hx, hy, dl);
            Options::Aimbot::TargetHudPos[0] = hx;
            Options::Aimbot::TargetHudPos[1] = hy;
            pos.x = hx;
            pos.y = hy;
        }
        // Drag the panel by its header (draggable mode only).
        else if (draggable)
        {
            ImVec2 hdrMin(pos.x, pos.y);
            ImVec2 hdrMax(pos.x + win_w, pos.y + 24.f);
            if (ImGui::IsMouseHoveringRect(hdrMin, hdrMax) &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                pos.x += io.MouseDelta.x;
                pos.y += io.MouseDelta.y;
            }
            Options::Aimbot::TargetHudPos[0] = pos.x;
            Options::Aimbot::TargetHudPos[1] = pos.y;
        }

        const ImVec2 sz(win_w, win_h);

        // ── Panel background + stroke ─────────────────────────────────────
        dl->AddRectFilled(ImVec2(pos.x - 1.f, pos.y - 1.f),
            ImVec2(pos.x + sz.x + 1.f, pos.y + sz.y + 1.f), IM_COL32(0, 0, 0, 115), 5.f);
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), IM_COL32(16, 18, 24, 235), 4.f);
        dl->AddRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), IM_COL32(90, 120, 180, 200), 4.f, 0, 1.2f);

        // Accent top bar (2px)
        const ImU32 accent = UI::U(UI::P.accent);
        dl->AddRectFilled(ImVec2(pos.x + 2, pos.y + 2), ImVec2(pos.x + sz.x - 2, pos.y + 4), accent, 2.f);

        // ── Avatar ────────────────────────────────────────────────────────
        const uint64_t uid = s_cached.userId;
        if (uid)
            AvatarManager::Request(uid);

        const float content_top = pos.y + pad_y;
        const float av_x0 = pos.x + pad_x;
        const float av_y0 = content_top + (text_area_h - avatar_sz) * 0.5f;
        const float av_x1 = av_x0 + avatar_sz;
        const float av_y1 = av_y0 + avatar_sz;

        dl->AddRectFilled(ImVec2(av_x0, av_y0), ImVec2(av_x1, av_y1), IM_COL32(18, 18, 22, 220), 4.f);
        dl->AddRect(ImVec2(av_x0, av_y0), ImVec2(av_x1, av_y1), accent, 4.f, 0, 1.2f);

        ImTextureID av_tex = uid ? AvatarManager::Get(uid) : nullptr;
        if (av_tex)
        {
             dl->AddImageRounded(av_tex,
                 ImVec2(av_x0 + 1.f, av_y0 + 1.f),
                 ImVec2(av_x1 - 1.f, av_y1 - 1.f),
                 ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                 IM_COL32(255, 255, 255, 255), avatar_sz * 0.5f);
        }
        else
        {
            // Placeholder silhouette (no target or still loading).
            const float cx_av = (av_x0 + av_x1) * 0.5f;
            dl->AddCircleFilled(ImVec2(cx_av, av_y0 + avatar_sz * 0.35f), avatar_sz * 0.22f, IM_COL32(200, 200, 200, 100), 16);
            dl->AddCircleFilled(ImVec2(cx_av, av_y0 + avatar_sz * 0.72f), avatar_sz * 0.20f, IM_COL32(200, 200, 200, 80), 16);
        }

        // ── Text block (right of avatar) ──────────────────────────────────
        const float text_x   = av_x1 + 8.f;
        const float text_max = pos.x + sz.x - pad_x;
        float ty = content_top + (text_area_h - (fs + row_gap + fs)) * 0.5f;

        const bool hasTarget = !s_cached.name.empty();

        if (Options::Aimbot::TargetHudName)
        {
            const char* name_str = hasTarget ? s_cached.name.c_str() : "No target found";
            const ImU32 name_col = hasTarget ? IM_COL32(235, 235, 235, 255) : IM_COL32(150, 150, 150, 180);

            ImVec2 np(text_x, ty);
            if (Options::Aimbot::TargetHudOutline)
            {
                const ImU32 out = IM_COL32(0, 0, 0, 255);
                dl->AddText(font, fs, ImVec2(np.x - 1.f, np.y - 1.f), out, name_str);
                dl->AddText(font, fs, ImVec2(np.x + 1.f, np.y + 1.f), out, name_str);
                dl->AddText(font, fs, ImVec2(np.x + 1.f, np.y - 1.f), out, name_str);
                dl->AddText(font, fs, ImVec2(np.x - 1.f, np.y + 1.f), out, name_str);
            }
            dl->AddText(font, fs, np, name_col, name_str);
        }
        ty += fs + row_gap;

        // Tool + distance row (only when we have a real target).
        if (hasTarget && (Options::Aimbot::TargetHudTool || Options::Aimbot::TargetHudDistance))
        {
            float dist_w = 0.f;
            char dist[32] = {};
            if (Options::Aimbot::TargetHudDistance)
            {
                snprintf(dist, sizeof(dist), "%.0f studs", s_cached.distance);
                dist_w = font->CalcTextSizeA(fs, FLT_MAX, -1.f, dist).x + 4.f;
                ImVec2 dp(text_max - dist_w + 4.f, ty);
                dl->AddText(font, fs, dp, IM_COL32(180, 200, 255, 220), dist);
            }

            if (Options::Aimbot::TargetHudTool)
            {
                const char* tool_str = (!s_cached.tool.empty() && s_cached.tool != "[none]")
                    ? s_cached.tool.c_str() : "No Tool";
                const char* tb = tool_str;
                const char* te = tb + strlen(tool_str);
                const float tool_max_w = (text_max - dist_w) - text_x;
                font->CalcTextSizeA(fs, tool_max_w, -1.f, tb, te, &te);

                ImVec2 tp(text_x, ty);
                if (Options::Aimbot::TargetHudOutline)
                {
                    const ImU32 out = IM_COL32(0, 0, 0, 220);
                    dl->AddText(font, fs, ImVec2(tp.x - 1.f, tp.y - 1.f), out, tb, te);
                    dl->AddText(font, fs, ImVec2(tp.x + 1.f, tp.y + 1.f), out, tb, te);
                    dl->AddText(font, fs, ImVec2(tp.x + 1.f, tp.y - 1.f), out, tb, te);
                    dl->AddText(font, fs, ImVec2(tp.x - 1.f, tp.y + 1.f), out, tb, te);
                }
                dl->AddText(font, fs, tp, IM_COL32(255, 210, 150, 235), tb, te);
            }
        }

        // ── Health bar (full width at the bottom) ─────────────────────────
        if (Options::Aimbot::TargetHudHealth && hasTarget && s_cached.maxHealth > 0.f)
        {
            const float bar_y0 = content_top + text_area_h + row_gap;
            const float bar_y1 = bar_y0 + bar_h;
            const float bx0 = pos.x + pad_x;
            const float bx1 = pos.x + sz.x - pad_x;

            float frac = s_cached.maxHealth > 0.f
                ? (std::max)(0.0f, (std::min)(1.0f, s_cached.health / s_cached.maxHealth))
                : 0.f;

            dl->AddRectFilled(ImVec2(bx0, bar_y0), ImVec2(bx1, bar_y1), IM_COL32(30, 30, 34, 200), bar_h * 0.5f);

            const float fill_w = (bx1 - bx0) * frac;
            if (fill_w > 0.f)
            {
                const float fill_x0 = Options::Aimbot::TargetHudBarDirection == 0 ? bx0 : bx1 - fill_w;
                const float fill_x1 = Options::Aimbot::TargetHudBarDirection == 0 ? bx0 + fill_w : bx1;

                const ImU32 bar_col = Options::Aimbot::TargetHudBarStyle == 1
                    ? HealthGradientCol(frac)
                    : UI::U(UI::P.accent);

                dl->AddRectFilled(ImVec2(fill_x0, bar_y0), ImVec2(fill_x1, bar_y1), bar_col, bar_h * 0.5f);

                // Soft glow: progressively wider, more transparent layers.
                const ImVec4 bc = ImGui::ColorConvertU32ToFloat4(bar_col);
                const int r = (int)(bc.x * 255.f);
                const int g = (int)(bc.y * 255.f);
                const int b = (int)(bc.z * 255.f);
                dl->AddRectFilled(ImVec2(fill_x0, bar_y0 - 3.f), ImVec2(fill_x1, bar_y1 + 3.f),
                    IM_COL32(r, g, b, 46), bar_h * 0.5f + 3.f);
                dl->AddRectFilled(ImVec2(fill_x0, bar_y0 - 6.f), ImVec2(fill_x1, bar_y1 + 6.f),
                    IM_COL32(r, g, b, 23), bar_h * 0.5f + 6.f);
            }

            // Health text centered inside the bar.
            char hp[32];
            snprintf(hp, sizeof(hp), "%.0f / %.0f", s_cached.health, s_cached.maxHealth);
            const float small_fs = 11.f;
            const ImVec2 hp_sz = font->CalcTextSizeA(small_fs, FLT_MAX, -1.f, hp);
            const float hp_x = bx0 + (bx1 - bx0 - hp_sz.x) * 0.5f;
            const float hp_y = bar_y0 + (bar_h - hp_sz.y) * 0.5f;
            const ImVec2 hp_pos(hp_x, hp_y);

            const ImU32 out = IM_COL32(0, 0, 0, 255);
            dl->AddText(font, small_fs, ImVec2(hp_pos.x - 1.f, hp_pos.y - 1.f), out, hp);
            dl->AddText(font, small_fs, ImVec2(hp_pos.x + 1.f, hp_pos.y + 1.f), out, hp);
            dl->AddText(font, small_fs, ImVec2(hp_pos.x + 1.f, hp_pos.y - 1.f), out, hp);
            dl->AddText(font, small_fs, ImVec2(hp_pos.x - 1.f, hp_pos.y + 1.f), out, hp);
            dl->AddText(font, small_fs, hp_pos, IM_COL32(255, 255, 255, 255), hp);
        }
    }
}
