#pragma once

// ── In-game HUD editor ───────────────────────────────────────────────────────
// When Options::Misc::HUDEditMode is on, a small draggable handle is drawn on
// every screen-space HUD element (keybind list, target HUD, hit notifications,
// ESP preview box, …). Dragging a handle live-updates the element's saved
// position float(s), so the layout sticks without fiddling with sliders.
// All queries are ImGui IO-level (no window context required), so this works
// from the background/foreground draw lists.

#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"

#include <string>
#include <unordered_map>

namespace HudEditor
{
    inline bool Enabled()
    {
        return Options::Misc::HUDEditMode;
    }

    struct DragState
    {
        bool active = false;
        ImVec2 originMouse;
    };

    inline void Handle(const char* id, float& x, float& y, ImDrawList* dl, bool forceEnabled = false)
    {
        if ((!forceEnabled && !Enabled()) || !dl)
            return;

        ImGuiIO& io = ImGui::GetIO();
        const float g = 28.0f;            // handle size
        const float halfG = g * 0.5f;
        const bool hover = ImGui::IsMouseHoveringRect(
            ImVec2(x, y), ImVec2(x + g, y + g), false);

        const ImU32 bg   = hover ? IM_COL32(255, 255, 170, 255) : IM_COL32(230, 230, 120, 210);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + g, y + g), IM_COL32(18, 18, 24, 235), 5.0f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + g, y + g), bg, 5.0f, 0, 1.5f);

        // three grip lines (like a "drag" affordance)
        const ImU32 lineCol = bg;
        for (int i = 0; i < 3; ++i)
        {
            float ly = y + halfG - 6.5f + i * 4.0f;
            dl->AddLine(ImVec2(x + 6, ly), ImVec2(x + g - 6, ly), lineCol, 1.6f);
        }

        static std::unordered_map<std::string, DragState> s_states;
        auto& st = s_states[std::string(id)];

        if (hover && ImGui::IsMouseDown(0) && !st.active)
        {
            st.active = true;
            st.originMouse = io.MousePos;
        }
        if (st.active)
        {
            if (ImGui::IsMouseDown(0))
            {
                ImVec2 d(io.MousePos.x - st.originMouse.x, io.MousePos.y - st.originMouse.y);
                x += d.x;
                y += d.y;
                st.originMouse = io.MousePos;
                st.active = true;
            }
            else
            {
                st.active = false;
            }
        }

        // nudge the element so the handle stays roughly reachable
        (void)halfG;
    }

    inline void Banner(ImDrawList* dl)
    {
        if (!Enabled() || !dl)
            return;

        const char* txt = "HUD EDIT MODE — drag the yellow handles to reposition elements";
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 ts = ImGui::CalcTextSize(txt);
        ImVec2 p(io.DisplaySize.x * 0.5f - ts.x * 0.5f, 10.0f);

        dl->AddRectFilled(ImVec2(p.x - 10, p.y - 4), ImVec2(p.x + ts.x + 10, p.y + ts.y + 6),
            IM_COL32(18, 18, 24, 235), 4.0f);
        dl->AddRect(ImVec2(p.x - 10, p.y - 4), ImVec2(p.x + ts.x + 10, p.y + ts.y + 6),
            IM_COL32(255, 255, 170, 200), 4.0f, 0, 1.5f);
        dl->AddText(p, IM_COL32(255, 255, 210, 255), txt);
    }
}
