#pragma once
#include "imgui/imgui.h"
#include "imgui/KeyBind.h"
#include "../rbx/globals/options.h"

// =============================================================================
// Fleasion UI — polished design system (v2)
// Fully custom-drawn controls via ImDrawList. Neutral dark surfaces, single
// restrained accent (active tab, enabled controls, slider fill, focus, status).
// Animated, soft-depth, no stock ImGui widget look. Every Options:: binding the
// caller passes through is preserved untouched.
// =============================================================================

namespace UI
{
    // ---- Palette ---------------------------------------------------------------
    struct Palette
    {
        ImVec4 accent;       // primary accent (used sparingly)
        ImVec4 accentHover;  // brighter accent on hover (#C72A3C)
        ImVec4 accentSoft;   // translucent accent fill (hover/active)
        ImVec4 accentGlow;   // faint accent (hover wash)
        ImVec4 surface;      // panel background (slightly lighter than window)
        ImVec4 surfaceAlt;   // control / nested surface
        ImVec4 surfaceHi;    // hovered surface
        ImVec4 line;         // hairline border (low opacity)
        ImVec4 shadow;       // soft shadow tint
        ImVec4 textStrong;  // titles / active (near white)
        ImVec4 text;        // primary text
        ImVec4 textDim;     // secondary / labels
        ImVec4 disabled;    // disabled text
        ImVec4 good;        // on / connected
        ImVec4 bad;         // off / error
    };

    inline Palette P;

    // ---- Sidebar (subtab rail) layout constants --------------------------
    // Set by the renderer each frame from the current scale so every subtab
    // pill is the same width and aligns cleanly inside the rail.
    inline float sc = 1.0f;          // current window font scale (set each frame)
    inline float SidebarX = 16.0f;   // left padding inside the rail
    inline float SidebarW = 96.0f;   // uniform subtab pill width

    // ---- Geometry / motion constants ------------------------------------------
    static const float RADIUS    = 9.0f;   // card / button radius
    static const float RADIUS_SM = 6.0f;  // small control radius
    static const float PAD       = 16.0f;  // card inner padding
    static const float PAD_SM    = 10.0f;  // control inner padding
    static const float ROW       = 11.0f;  // gap between controls
    static const float GROUP     = 14.0f;  // gap between groups
    static const float SECTION   = 30.0f;  // gap between sections
    static const float ANIM      = 0.16f;  // tween time (s)
    static const float BORDER    = 1.0f;

    // ---- Animation easing ------------------------------------------------------
    inline float Anim(float& store, float target, float speed = ANIM)
    {
        float dt = ImGui::GetIO().DeltaTime;
        float rate = 1.0f - expf(-dt / (speed <= 0 ? ANIM : speed));
        store += (target - store) * rate;
        if (fabsf(store - target) < 0.001f) store = target;
        return store;
    }

    inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }
    inline ImU32 U(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
    inline ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
    inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // Soft drop shadow (faked with a few stacked rounded rects).
    inline void SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float strength = 0.30f)
    {
        for (int i = 4; i >= 1; i--)
        {
            float o = (float)i * 1.6f;
            dl->AddRectFilled(ImVec2(a.x - o, a.y - o + 2.0f),
                              ImVec2(b.x + o, b.y + o + 2.0f),
                              IM_COL32(0, 0, 0, (int)(strength * 10.0f)), r + o);
        }
    }

    // ---- Global style ----------------------------------------------------------
    inline void ApplyStyle(const ImVec4& accent, const ImVec4& bg, const ImVec4& panel)
    {
        // ── Spec palette (dark red theme) ──────────────────────────────
        // Background #101010, Secondary panels #161616, Borders #252525,
        // Accent #9D1D2B, Hover #C72A3C, text #FFFFFF/#BEBEBE/#707070.
        P.accent     = accent;                                     // theme accent (default red)
        P.accentHover= ImVec4(0.780f, 0.165f, 0.235f, 1.0f);       // #C72A3C
        P.accentSoft = ImVec4(accent.x, accent.y, accent.z, 0.20f);
        P.accentGlow = ImVec4(accent.x, accent.y, accent.z, 0.10f);
        P.surface    = ImVec4(0.086f, 0.086f, 0.086f, 1.0f);       // #161616
        P.surfaceAlt = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);       // control surface
        P.surfaceHi  = ImVec4(0.140f, 0.140f, 0.140f, 1.0f);       // hovered surface
        P.line       = ImVec4(0.145f, 0.145f, 0.145f, 1.0f);       // #252525
        P.shadow     = ImVec4(0, 0, 0, 1);
        P.textStrong = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);          // #FFFFFF
        P.text       = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);          // #FFFFFF (primary)
        P.textDim    = ImVec4(0.745f, 0.745f, 0.745f, 1.0f);       // #BEBEBE (secondary)
        P.disabled   = ImVec4(0.439f, 0.439f, 0.439f, 1.0f);       // #707070
        P.good       = ImVec4(0.30f, 0.85f, 0.55f, 1.0f);
        P.bad        = ImVec4(0.95f, 0.38f, 0.42f, 1.0f);

        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = RADIUS;
        s.FrameRounding     = RADIUS_SM;
        s.GrabRounding      = RADIUS_SM;
        s.ChildRounding     = RADIUS;
        s.PopupRounding     = RADIUS;
        s.ScrollbarRounding = RADIUS_SM;
        s.WindowBorderSize  = 0.0f;
        s.FrameBorderSize   = 0.0f;
        s.WindowPadding     = ImVec2(PAD, PAD);
        s.FramePadding      = ImVec2(PAD_SM, PAD_SM);
        s.ItemSpacing       = ImVec2(ROW, ROW);
        s.CellPadding       = ImVec2(ROW, ROW);
        s.ItemInnerSpacing  = ImVec2(8, 8);
        s.Colors[ImGuiCol_WindowBg]     = bg;
        s.Colors[ImGuiCol_ChildBg]      = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_Border]       = P.line;
        s.Colors[ImGuiCol_Text]         = P.text;
        s.Colors[ImGuiCol_TextDisabled] = P.textDim;
        s.Colors[ImGuiCol_Button]       = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ButtonHovered]= ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ButtonActive] = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_FrameBg]      = P.surfaceAlt;
        s.Colors[ImGuiCol_FrameBgHovered]= P.surfaceHi;
        s.Colors[ImGuiCol_FrameBgActive] = P.accentSoft;
        s.Colors[ImGuiCol_SliderGrab]   = P.accent;
        s.Colors[ImGuiCol_SliderGrabActive] = Mix(P.accent, ImVec4(1,1,1,0), 0.2f);
        s.Colors[ImGuiCol_CheckMark]    = P.accent;
        s.Colors[ImGuiCol_Header]       = P.accentSoft;
        s.Colors[ImGuiCol_HeaderHovered]= Mix(P.accent, ImVec4(1,1,1,0), 0.12f);
        s.Colors[ImGuiCol_HeaderActive] = P.accentSoft;
        s.Colors[ImGuiCol_Separator]    = P.line;
        s.Colors[ImGuiCol_Tab]          = ImVec4(0,0,0,0);
        s.Colors[ImGuiCol_TabHovered]   = ImVec4(0,0,0,0);
        s.Colors[ImGuiCol_TabActive]    = ImVec4(0,0,0,0);
        s.Colors[ImGuiCol_PopupBg]      = P.surface;
        s.Colors[ImGuiCol_ScrollbarBg]  = ImVec4(0,0,0,0);
        s.Colors[ImGuiCol_ScrollbarGrab]= P.line;
        s.Colors[ImGuiCol_ScrollbarGrabHovered] = P.accentSoft;
    }

    // ---- Section (card with title + soft elevation) ---------------------------
    inline bool BeginSection(const char* title, const ImVec2& size = ImVec2(0, 0))
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P.surface);
        ImGui::PushStyleColor(ImGuiCol_Border, P.line);
        bool open = ImGui::BeginChild(ImGui::GetID(title), size, true,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        // soft shadow + filled surface
        SoftShadow(dl, wp, ImVec2(wp.x + ws.x, wp.y + ws.y), RADIUS, 0.22f);
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), U(P.surface), RADIUS, 0);
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), U(P.line), RADIUS, 0, 1.0f);
        // subtle top inner highlight
        dl->AddLine(ImVec2(wp.x + 2, wp.y + 1.5f), ImVec2(wp.x + ws.x - 2, wp.y + 1.5f),
                    IM_COL32(255, 255, 255, 10), 1.0f);
        if (title && *title)
        {
            ImGui::SetCursorPos(ImVec2(PAD, PAD - 4));
            ImGui::TextColored(P.textStrong, "%s", title);
            ImGui::Dummy(ImVec2(0, 8));
        }
        return open;
    }
    inline void EndSection() { ImGui::EndChild(); ImGui::PopStyleColor(2); }

    // ---- Header (bold category title, visually separated) --------------------
    inline void Header(const char* title)
    {
        // Manual cursor advance (no layout items => no injected ItemSpacing)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x - 6, p.y + 1), ImVec2(p.x - 2, p.y + 15), U(P.accent), 1.5f, 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2);
        // bold, larger section header
        ImGui::TextColored(P.textStrong, "%s", title);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        // subtle separator under the header
        ImVec2 sp = ImGui::GetCursorScreenPos();
        float sw = ImGui::GetContentRegionAvail().x;
        dl->AddLine(ImVec2(sp.x, sp.y - 4), ImVec2(sp.x + sw, sp.y - 4), U(P.line), 1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    }

    // ---- Toggle (custom animated switch) --------------------------------------
    inline bool Toggle(const char* label, bool* v)
    {
        ImGui::PushID(label);
        const float h = 22.0f, w = 40.0f;
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::Dummy(ImVec2(1, h));
        if (label && *label)
        {
            ImGui::SameLine(0, 10);
            ImGui::SetCursorPosY(cursor.y + 3);
            ImGui::TextColored(P.text, "%s", label);
        }
        ImGui::SameLine();
        float tw = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tw - w);
        ImVec2 screen = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tgl", ImVec2(w, h));
        bool changed = false;
        if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
        bool hover = ImGui::IsItemHovered();
        // scroll-aware screen pos matches the InvisibleButton hitbox
        ImVec2 a = screen;
        ImVec2 b = ImVec2(a.x + w, a.y + h);

        ImGuiID id = ImGui::GetID("##tgl");
        ImGuiStorage* store = ImGui::GetStateStorage();
        float st = store->GetFloat(id, 0.0f);
        float on = Anim(st, *v ? 1.0f : 0.0f);
        store->SetFloat(id, st);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec4 track = *v ? P.accent : (hover ? P.surfaceHi : P.surfaceAlt);
        if (*v && hover) track = Mix(P.accent, ImVec4(1,1,1,0), 0.12f);
        dl->AddRectFilled(a, b, U(track), h / 2.0f, 0);
        if (*v) // subtle glow
            dl->AddRect(a, b, U(Mix(P.accent, ImVec4(1,1,1,0), 0.2f)), h / 2.0f, 0, 1.5f);
        else
            dl->AddRect(a, b, U(P.line), h / 2.0f, 0, 1.0f);
        float kx = a.x + 2 + on * (w - h + 2);
        ImVec2 ka(kx, a.y + 2), kb(kx + h - 4, b.y - 2);
        dl->AddRectFilled(ka, kb, IM_COL32(255,255,255,255), (h - 4) / 2.0f, 0);

        ImGui::PopID();
        return changed;
    }

    // ---- Custom Slider (ImDrawList track + handle, invisible drag) ------------
    // Layout: label row (label left), then a thick track with the current value
    // drawn directly to the RIGHT of the handle so the number travels with it.
    // Rendering uses scroll-aware screen pos so track + hitbox never drift.
    inline bool SliderFloat(const char* label, float* v, float mn, float mx, const char* fmt = "%.2f", float width = 0)
    {
        bool changed = false;
        float w = width ? width : ImGui::GetContentRegionAvail().x;
        const float rowH = 18.0f * sc, hitH = 26.0f * sc, trackH = 10.0f * sc;

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // --- label row (drawn via drawlist; cursor advanced manually) ---
        ImVec2 labelPos = ImGui::GetCursorScreenPos();
        dl->AddText(labelPos, U(P.text), label);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowH + 4 * sc);

        // --- invisible hit area (unique ID per control) ---
        ImGui::PushID(label);
        ImGuiID id = ImGui::GetID("##slider");
        ImVec2 screen = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##slider", ImVec2(w, hitH));
        bool hover = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        ImGui::PopID();

        ImVec2 a = screen;
        ImVec2 b = ImVec2(a.x + w, a.y + hitH);
        ImVec2 t0(a.x, a.y + (hitH - trackH) / 2.0f);
        ImVec2 t1(a.x + w, t0.y + trackH);

        ImGuiStorage* store = ImGui::GetStateStorage();
        float hov = store->GetFloat(id, 0.0f);
        Anim(hov, (hover || active) ? 1.0f : 0.0f, 0.12f);
        store->SetFloat(id, hov);

        float range = (mx - mn) ? (mx - mn) : 1.0f;
        float t = UI::ClampF((*v - mn) / range, 0.0f, 1.0f);
        if (active)
        {
            float nt = UI::ClampF((ImGui::GetIO().MousePos.x - t0.x) / (t1.x - t0.x), 0.0f, 1.0f);
            float nv = mn + nt * range;
            if (nv != *v) { *v = nv; changed = true; }
        }

        float hx = t0.x + t * (t1.x - t0.x);
        // track
        dl->AddRectFilled(t0, t1, U(Mix(P.surfaceAlt, P.surfaceHi, hov * 0.6f)), trackH / 2.0f, 0);
        // fill
        ImVec4 fillA = Mix(P.accent, ImVec4(1,1,1,0), 0.25f);
        ImVec4 fillB = Mix(P.accent, ImVec4(0,0,0,0), -0.05f);
        dl->AddRectFilledMultiColor(
            ImVec2(t0.x, t0.y), ImVec2(hx, t1.y),
            U(fillA), U(fillA), U(fillB), U(fillB));
        // handle (accent knob with white ring)
        float cy = (t0.y + t1.y) / 2.0f;
        float hr = 7.0f + hov * 1.5f;
        dl->AddCircleFilled(ImVec2(hx, cy), hr + 2.5f, IM_COL32(0,0,0,55));
        dl->AddCircleFilled(ImVec2(hx, cy), hr, U(P.accent));
        dl->AddCircle(ImVec2(hx, cy), hr, IM_COL32(255,255,255,230), 0, 12);
        // value chip travels with the handle, just to its right
        char buf[64]; sprintf_s(buf, sizeof(buf), fmt, *v);
        ImVec2 vsz = ImGui::CalcTextSize(buf);
        float valX = hx + hr + 8.0f;
        bool flip = (valX + vsz.x > t1.x);
        if (flip) valX = hx - hr - 8.0f - vsz.x;
        float vy = cy - vsz.y / 2.0f;
        dl->AddText(ImVec2(valX, vy), U(P.textStrong), buf);

        return changed;
    }
    inline bool SliderInt(const char* label, int* v, int mn, int mx, float width = 0)
    {
        float f = (float)*v;
        bool c = SliderFloat(label, &f, (float)mn, (float)mx, "%.0f", width);
        if (c) *v = (int)(f + 0.5f);
        return c;
    }

    // ---- Custom Button (ImDrawList) -------------------------------------------
    inline bool Button(const char* label, const ImVec2& size = ImVec2(0, 32), bool accent = false)
    {
        ImVec2 sz = (size.x == 0) ? ImVec2(ImGui::GetContentRegionAvail().x, size.y) : size;
        ImGui::InvisibleButton(label, sz);
        bool hover = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImVec4 base = accent ? P.accentSoft : P.surfaceAlt;
        ImVec4 fill = base;
        if (accent)      fill = active ? Mix(P.accent, ImVec4(0,0,0,0), -0.1f) : (hover ? Mix(P.accent, ImVec4(1,1,1,0), 0.12f) : P.accentSoft);
        else             fill = active ? P.accentSoft : (hover ? P.surfaceHi : P.surfaceAlt);
        dl->AddRectFilled(a, b, U(fill), RADIUS_SM, 0);
        dl->AddRect(a, b, U(accent ? Mix(P.accent, ImVec4(1,1,1,0), 0.1f) : P.line), RADIUS_SM, 0, 1.0f);
        ImVec4 txcol = accent ? P.textStrong : P.text;
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2((a.x + b.x) / 2.0f - ts.x / 2.0f, (a.y + b.y) / 2.0f - ts.y / 2.0f),
                    U(txcol), label);
        return ImGui::IsItemClicked();
    }

    // ---- Custom Combo (drawn field + styled popup) ----------------------------
    inline bool Combo(const char* label, int* current, const char* const items[], int count)
    {
        ImGui::TextColored(P.textDim, "%s", label);
        float w = ImGui::GetContentRegionAvail().x;
        const float hh = 30.0f;
        ImGui::PushID(label);
        ImVec2 screen = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##combo", ImVec2(w, hh));
        bool open = ImGui::IsItemClicked();
        bool hover = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        ImVec2 a = screen, b = ImVec2(a.x + w, a.y + hh);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec4 fill = active ? P.accentSoft : (hover ? P.surfaceHi : P.surfaceAlt);
        dl->AddRectFilled(a, b, U(fill), RADIUS_SM, 0);
        dl->AddRect(a, b, U(active ? Mix(P.accent, ImVec4(1,1,1,0), 0.1f) : P.line), RADIUS_SM, 0, 1.0f);
        const char* preview = (*current >= 0 && *current < count) ? items[*current] : "—";
        ImVec2 ts = ImGui::CalcTextSize(preview);
        dl->AddText(ImVec2(a.x + 12, (a.y + b.y) / 2.0f - ts.y / 2.0f), U(P.text), preview);
        // custom chevron (downward V)
        ImVec2 ctr = ImVec2(b.x - 16, (a.y + b.y) / 2.0f);
        dl->AddLine(ImVec2(ctr.x - 4, ctr.y - 2.0f), ImVec2(ctr.x, ctr.y + 2.0f), U(P.textDim), 1.8f);
        dl->AddLine(ImVec2(ctr.x, ctr.y + 2.0f), ImVec2(ctr.x + 4, ctr.y - 2.0f), U(P.textDim), 1.8f);

        bool changed = false;
        if (open)
        {
            ImGui::OpenPopup("##pick");
        }
        if (ImGui::BeginPopup("##pick", ImGuiWindowFlags_NoMove))
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, P.surface);
            ImGui::PushStyleColor(ImGuiCol_Header, P.accentSoft);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Mix(P.accent, ImVec4(1,1,1,0), 0.12f));
            ImGui::PushStyleColor(ImGuiCol_Border, P.line);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RADIUS_SM);
            for (int i = 0; i < count; i++)
            {
                bool sel = (i == *current);
                ImGui::PushStyleColor(ImGuiCol_Text, sel ? P.textStrong : P.text);
                if (ImGui::Selectable(items[i], sel, ImGuiSelectableFlags_DontClosePopups))
                {
                    *current = i; changed = true;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && sel) ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    // ---- ColorEdit (drawn swatch + stock picker popup) ------------------------
    inline bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Border, P.line);
        bool r = ImGui::ColorEdit3(label, col, flags | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoInputs);
        ImGui::PopStyleColor();
        return r;
    }
    inline bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Border, P.line);
        bool r = ImGui::ColorEdit4(label, col, flags | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoInputs);
        ImGui::PopStyleColor();
        return r;
    }

    // ---- Keybind (delegates to the existing custom KeybindSelector widget) ----
    // KeybindSelector is already a bespoke widget (text + capture popup + mode
    // selector), so we forward to it to preserve all of its behaviour.
    inline bool Keybind(const char* label, int* key)
    {
        KeybindSelector(label, key);
        return false;
    }

    // ---- Custom Checkbox (animated round check) -------------------------------
    inline bool Checkbox(const char* label, bool* v)
    {
        ImGui::PushID(label);
        const float box = 18.0f * sc;
        const float r = 5.0f;
        ImVec2 screen = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cb", ImVec2(box, box));
        bool changed = false;
        if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
        bool hover = ImGui::IsItemHovered();
        // box rect derived from scroll-aware screen pos matches the InvisibleButton hitbox
        ImVec2 a = screen;
        ImVec2 b = ImVec2(a.x + box, a.y + box);

        ImGuiID id = ImGui::GetID("##cb");
        ImGuiStorage* store = ImGui::GetStateStorage();
        float chk = store->GetFloat(id, 0.0f);
        Anim(chk, *v ? 1.0f : 0.0f, 0.14f);
        store->SetFloat(id, chk);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec4 boxCol = *v ? P.accent : (hover ? P.surfaceHi : Mix(P.surface, ImVec4(1,1,1,0), 0.05f));
        dl->AddRectFilled(a, b, U(boxCol), r, 0);
        // visible border: bright accent when checked, clear light hairline when unchecked
        dl->AddRect(a, b, U(*v ? Mix(P.accent, ImVec4(1,1,1,0), 0.22f) : Mix(P.line, ImVec4(1,1,1,0), 0.55f)), r, 0, 1.5f);
        // animated check mark
        if (chk > 0.01f)
        {
            float s = chk;
            ImVec2 p1 = ImVec2(a.x + 4.0f, a.y + 8.5f);
            ImVec2 p2 = ImVec2(a.x + 6.5f, a.y + 11.0f);
            ImVec2 p3 = ImVec2(a.x + 12.0f, a.y + 5.0f);
            ImVec2 m1 = ImVec2(p1.x + (p2.x - p1.x) * s, p1.y + (p2.y - p1.y) * s);
            ImVec2 m2 = ImVec2(p2.x + (p3.x - p2.x) * s, p2.y + (p3.y - p2.y) * s);
            dl->AddLine(p1, m1, IM_COL32(255,255,255,255), 2.0f * s);
            if (s > 0.5f) dl->AddLine(p2, m2, IM_COL32(255,255,255,255), 2.0f);
        }
        if (label && *label)
        {
            ImGui::SameLine(0, 10 * sc);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (box - ImGui::GetTextLineHeight()) / 2.0f);
            ImGui::TextColored(P.text, "%s", label);
        }
        ImGui::PopID();
        return changed;
    }

    // ---- Top tab (drawn icon + label; thin accent underline, brighter active text) ----
    // `iconId` selects a small ImDrawList icon drawn left of the centered label.
    //   0=none  1=crosshair  2=eye  3=lightning  4=diamond
    //   5=arrow  6=gear  7=star
    inline bool TabButton(int iconId, const char* label, bool active, float width)
    {
        ImGui::InvisibleButton(label, ImVec2(width, 38 * sc));
        bool clicked = ImGui::IsItemClicked();
        bool hover = ImGui::IsItemHovered();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImVec2 ctr((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hover && !active)
            dl->AddRectFilled(a, b, U(P.accentGlow), RADIUS, 0);
        if (active)
        {
            dl->AddRectFilled(a, b, U(P.accentGlow), RADIUS, 0);
            dl->AddRectFilled(ImVec2(a.x + 2, b.y - 2.5f), ImVec2(b.x - 2, b.y), U(P.accent), 1.5f, 0);
            dl->AddRect(a, b, U(P.accent), RADIUS, 0, 1.0f);
        }
        ImVec2 ts = ImGui::CalcTextSize(label);
        ImU32 acc = U(P.accent);
        ImU32 col = U(active ? P.textStrong : P.textDim);
        const float iconBox = 20.0f * sc;
        const float gap = 6.0f * sc;
        float totalW = ts.x + (iconId > 0 ? iconBox + gap : 0.0f);
        float leftX = ctr.x - totalW * 0.5f;
        if (iconId > 0)
        {
            float icx = leftX + iconBox * 0.5f;
            float icy = ctr.y;
            float hs = iconBox * 0.5f;
            float s = hs * 0.75f;
            switch (iconId)
            {
            case 1: // crosshair (circle + 4 arms)
                dl->AddCircle(ImVec2(icx, icy), s * 0.4f, acc, 0, 2.0f);
                dl->AddLine(ImVec2(icx, icy - s), ImVec2(icx, icy + s), acc, 2.0f);
                dl->AddLine(ImVec2(icx - s, icy), ImVec2(icx + s, icy), acc, 2.0f);
                dl->AddCircleFilled(ImVec2(icx, icy), 2.0f * sc, acc);
                break;
            case 2: // eye (almond + pupil + highlight)
                {
                float rx = s * 0.5f, ry = s * 0.3f;
                dl->PathClear();
                for (int i = 0; i <= 16; i++)
                {
                    float a = (6.2831855f / 16) * i;
                    dl->PathLineTo(ImVec2(icx + rx * cosf(a), icy + ry * sinf(a)));
                }
                dl->PathStroke(acc, false, 2.0f);
                dl->AddCircleFilled(ImVec2(icx + s * 0.15f, icy), s * 0.12f, acc);
                dl->AddCircleFilled(ImVec2(icx - s * 0.03f, icy - s * 0.1f), s * 0.04f, IM_COL32(255,255,255,180));
                }
                break;
            case 3: // lightning bolt
                {
                float h = s * 0.8f, w = s * 0.5f;
                ImVec2 pts[5] = {
                    ImVec2(icx + w * 0.3f, icy - h),
                    ImVec2(icx - w * 0.2f, icy - h * 0.1f),
                    ImVec2(icx + w * 0.05f, icy - h * 0.1f),
                    ImVec2(icx - w * 0.3f, icy + h),
                    ImVec2(icx + w * 0.25f, icy + h * 0.2f)
                };
                dl->AddConvexPolyFilled(pts, 5, acc);
                dl->AddPolyline(pts, 5, IM_COL32(255,255,255,80), true, 1.0f);
                }
                break;
            case 4: // diamond
                dl->AddQuadFilled(
                    ImVec2(icx, icy - s),
                    ImVec2(icx + s * 0.7f, icy),
                    ImVec2(icx, icy + s),
                    ImVec2(icx - s * 0.7f, icy), acc);
                dl->AddQuad(
                    ImVec2(icx, icy - s * 0.85f),
                    ImVec2(icx + s * 0.6f, icy),
                    ImVec2(icx, icy + s * 0.85f),
                    ImVec2(icx - s * 0.6f, icy), IM_COL32(255,255,255,80), 1.0f);
                break;
            case 5: // arrow (filled triangle + stem)
                dl->AddTriangleFilled(
                    ImVec2(icx - s * 0.5f, icy - s * 0.5f),
                    ImVec2(icx - s * 0.5f, icy + s * 0.5f),
                    ImVec2(icx + s * 0.5f, icy), acc);
                dl->AddLine(ImVec2(icx - s * 0.7f, icy), ImVec2(icx + s * 0.7f, icy), IM_COL32(255,255,255,100), 2.0f);
                break;
            case 6: // gear (circle + cross + diagonal teeth)
                {
                float gr = s * 0.4f;
                dl->AddCircle(ImVec2(icx, icy), gr, acc, 0, 2.0f);
                dl->AddCircleFilled(ImVec2(icx, icy), 2.0f * sc, acc);
                float tl = s * 0.3f;
                dl->AddLine(ImVec2(icx - gr - tl, icy), ImVec2(icx + gr + tl, icy), acc, 2.5f);
                dl->AddLine(ImVec2(icx, icy - gr - tl), ImVec2(icx, icy + gr + tl), acc, 2.5f);
                dl->AddLine(ImVec2(icx - gr * 0.7f - tl, icy - gr * 0.7f - tl), ImVec2(icx + gr * 0.7f + tl, icy + gr * 0.7f + tl), acc, 2.0f);
                dl->AddLine(ImVec2(icx + gr * 0.7f + tl, icy - gr * 0.7f - tl), ImVec2(icx - gr * 0.7f - tl, icy + gr * 0.7f + tl), acc, 2.0f);
                }
                break;
            case 7: // 5-pointed star
                {
                float outerR = s * 0.75f, innerR = s * 0.3f;
                ImVec2 sp[10];
                for (int i = 0; i < 10; i++)
                {
                    float a = -1.5707963f + (6.2831855f / 10) * i;
                    float r = (i & 1) ? innerR : outerR;
                    sp[i] = ImVec2(icx + r * cosf(a), icy + r * sinf(a));
                }
                dl->AddConvexPolyFilled(sp, 10, acc);
                dl->AddPolyline(sp, 10, IM_COL32(255,255,255,80), true, 1.0f);
                }
                break;
            }
            leftX += iconBox + gap;
        }
        dl->AddText(ImVec2(leftX, ctr.y - ts.y * 0.5f), col, label);
        return clicked;
    }

    // ---- Sidebar category header (e.g. AIM / RAGE / MOVEMENT) -----------------
    inline void CategoryHeader(const char* label)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10 * sc);
        ImGui::SetCursorPosX(SidebarX);
        ImGui::TextColored(P.textDim, "%s", label);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4 * sc);
    }

    // ---- Subtab pill (grouped sidebar button) ---------------------------------
    inline bool Subtab(const char* label, bool active, float width = -1)
    {
        ImVec2 ts = ImGui::CalcTextSize(label);
        float w = (width > 0) ? width : SidebarW;
        ImVec2 sz(w, 32 * sc);
        ImGui::InvisibleButton(label, sz);
        bool clicked = ImGui::IsItemClicked();
        bool hover = ImGui::IsItemHovered();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiID id = ImGui::GetID("##subtab");
        ImGuiStorage* store = ImGui::GetStateStorage();
        float hov = store->GetFloat(id, 0.0f);
        Anim(hov, hover ? 1.0f : 0.0f, 0.15f);
        store->SetFloat(id, hov);

        if (active)
        {
            // glowing accent border + soft fill
            dl->AddRectFilled(a, b, U(P.accentSoft), RADIUS_SM, 0);
            dl->AddRect(a, b, U(P.accent), RADIUS_SM, 0, 1.5f);
            dl->AddRect(ImVec2(a.x - 1, a.y + 3), ImVec2(a.x + 1, b.y - 3), U(P.accent), 1.5f, 0, 2.0f);
        }
        else
        {
            ImVec4 hcol = Mix(P.surfaceAlt, P.accentGlow, hov * 0.6f);
            dl->AddRectFilled(a, b, U(hcol), RADIUS_SM, 0);
        }
        dl->AddText(ImVec2(a.x + 12, (a.y + b.y) / 2.0f - ts.y / 2.0f),
                    U(active ? P.textStrong : (hover ? P.text : P.textDim)), label);
        return clicked;
    }

    // ---- Divider ---------------------------------------------------------------
    inline void Divider()
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        dl->AddLine(p, ImVec2(p.x + w, p.y), U(P.line), 1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    }

    // ---- Status pill ----------------------------------------------------------
    inline void Status(const char* label, bool active)
    {
        ImVec4 c = active ? P.good : P.bad;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tw = ImGui::CalcTextSize(label).x + 18;
        dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + 22), U(ImVec4(c.x, c.y, c.z, 0.14f)), RADIUS_SM, 0);
        dl->AddCircleFilled(ImVec2(p.x + 11, p.y + 11), 4, U(c));
        ImGui::SetCursorScreenPos(ImVec2(p.x + 22, p.y + 2));
        ImGui::TextColored(c, "%s", label);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 26));
    }

    // ---- Muted text row (label : value) ---------------------------------------
    inline void Row(const char* label, const char* value)
    {
        ImGui::TextColored(P.textDim, "%s", label);
        ImVec2 sz = ImGui::CalcTextSize(value);
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - sz.x);
        ImGui::TextColored(P.text, "%s", value);
    }
}
