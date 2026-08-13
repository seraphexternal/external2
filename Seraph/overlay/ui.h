#pragma once
#include "imgui/imgui.h"
#include "imgui/KeyBind.h"
#include "../rbx/globals/options.h"

namespace UI
{
    struct Palette
    {
        ImVec4 accent;
        ImVec4 accent2;
        ImVec4 accentHover;
        ImVec4 accentDim;
        ImVec4 accentSoft;
        ImVec4 accentGlow;
        ImVec4 divider;
        ImVec4 card;
        ImVec4 cardHov;
        ImVec4 surface;
        ImVec4 surfaceAlt;
        ImVec4 surfaceHi;
        ImVec4 borderDim;
        ImVec4 track;
        ImVec4 glowPurple;
        ImVec4 line;
        ImVec4 shadow;
        ImVec4 textStrong;
        ImVec4 text;
        ImVec4 textMid;
        ImVec4 textDim;
        ImVec4 disabled;
        ImVec4 good;
        ImVec4 bad;
        ImVec4 glow;
    };

    inline Palette P;

    inline float sc = 1.0f;
    inline float SidebarX = 16.0f;
    inline float SidebarW = 150.0f;
    inline float ContentX = 170.0f;
    inline float ContentW = 680.0f;
    inline float CardW = 325.0f;
    inline float CardH = 430.0f;
    static const float RADIUS    = 10.0f;
    static const float RADIUS_SM = 6.0f;
    static const float PAD       = 16.0f;
    static const float PAD_SM    = 10.0f;
    static const float ROW       = 12.0f;
    static const float GROUP     = 16.0f;
    static const float SECTION   = 24.0f;
    static const float ANIM      = 0.16f;
    static const float BORDER    = 1.0f;

    inline float EaseOutCubic(float t) { return 1.0f - powf(1.0f - t, 3.0f); }

    inline float Anim(ImGuiID id, bool active, float speed = 9.0f)
    {
        ImGuiStorage* store = ImGui::GetStateStorage();
        float val = store->GetFloat(id, 0.0f);
        float target = active ? 1.0f : 0.0f;
        float dt = ImGui::GetIO().DeltaTime;
        float rate = 1.0f - expf(-dt * speed);
        val += (target - val) * rate;
        if (fabsf(val - target) < 0.001f) val = target;
        store->SetFloat(id, val);
        return val;
    }

    inline float AnimEased(ImGuiID id, bool active, float speed = 9.0f)
    {
        return 1.0f - powf(1.0f - Anim(id, active, speed), 3.0f);
    }

    inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }
    inline ImU32 U(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
    inline ImU32 LerpU32(ImU32 a, ImU32 b, float t)
    {
        ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
        ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            ca.x + (cb.x - ca.x) * t,
            ca.y + (cb.y - ca.y) * t,
            ca.z + (cb.z - ca.z) * t,
            ca.w + (cb.w - ca.w) * t));
    }
    inline ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
    inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    inline float MaxF(float a, float b) { return a > b ? a : b; }

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

    inline const char* VKName(int vk)
    {
        static char buf[64];
        if (vk == 0) return "NONE";
        if (vk == VK_XBUTTON1) return "Mouse 4";
        if (vk == VK_XBUTTON2) return "Mouse 5";
        if (vk == VK_LBUTTON) return "LMB";
        if (vk == VK_RBUTTON) return "RMB";
        if (vk == VK_MBUTTON) return "MMB";
        UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextA((LONG)(sc << 16), buf, sizeof(buf)) > 0)
            return buf;
        return "?";
    }

    inline void ApplyStyle(const ImVec4& accent, const ImVec4& bg, const ImVec4& panel)
    {
        // ── Fixed professional palette ──
        // Background:  #090C10
        // Surface:     #10151B
        // Surface Alt: #151B22
        // Hover:       #1A222B
        // Border:      #222B35
        // Accent:      #3B9DFF

        P.accent      = ImVec4(0.231f, 0.616f, 1.000f, 1.0f);  // #3B9DFF
        P.accentHover = ImVec4(0.353f, 0.686f, 1.000f, 1.0f);  // #5AAFFF
        P.accentDim   = ImVec4(0.116f, 0.308f, 0.500f, 1.0f);  // dimmed accent
        P.accentSoft  = ImVec4(0.231f, 0.616f, 1.000f, 0.10f); // soft accent fill
        P.accentGlow  = ImVec4(0.231f, 0.616f, 1.000f, 0.06f); // subtle glow
        P.glow        = ImVec4(0.231f, 0.616f, 1.000f, 0.15f);
        P.glowPurple  = ImVec4(0.231f, 0.616f, 1.000f, 0.08f);
        P.accent2     = accent; // keep user's accent for gradient line

        // Surfaces
        P.surface     = ImVec4(0.063f, 0.082f, 0.106f, 1.0f);  // #10151B
        P.surfaceAlt  = ImVec4(0.082f, 0.106f, 0.133f, 1.0f);  // #151B22
        P.surfaceHi   = ImVec4(0.102f, 0.133f, 0.169f, 1.0f);  // #1A222B

        // Cards and panels
        P.card        = ImVec4(0.063f, 0.082f, 0.106f, 1.0f);  // #10151B
        P.cardHov     = ImVec4(0.082f, 0.106f, 0.133f, 1.0f);  // #151B22

        // Borders — neutral gray, NOT accent-colored
        P.borderDim   = ImVec4(0.133f, 0.169f, 0.208f, 1.0f);  // #222B35
        P.line        = ImVec4(0.133f, 0.169f, 0.208f, 1.0f);  // #222B35
        P.divider     = ImVec4(0.102f, 0.133f, 0.169f, 1.0f);  // #1A222B

        // Track (slider background)
        P.track       = ImVec4(0.102f, 0.133f, 0.169f, 1.0f);  // #1A222B

        // Text hierarchy
        P.textStrong  = ImVec4(0.945f, 0.961f, 0.976f, 1.0f);  // #F1F5F9
        P.text        = ImVec4(0.545f, 0.588f, 0.647f, 1.0f);  // #8B96A5
        P.textMid     = ImVec4(0.545f, 0.588f, 0.647f, 1.0f);  // #8B96A5
        P.textDim     = ImVec4(0.373f, 0.420f, 0.471f, 1.0f);  // #5F6B78
        P.disabled    = ImVec4(0.373f, 0.420f, 0.471f, 1.0f);  // #5F6B78

        P.shadow      = ImVec4(0, 0, 0, 1);
        P.good        = ImVec4(0.224f, 0.851f, 0.541f, 1.0f);  // #39D98A
        P.bad         = ImVec4(1.000f, 0.314f, 0.408f, 1.0f);

        // ImGui style — clean and minimal
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 10.0f;
        s.FrameRounding     = 6.0f;
        s.GrabRounding      = 6.0f;
        s.ChildRounding     = 8.0f;
        s.PopupRounding     = 8.0f;
        s.ScrollbarRounding = 6.0f;
        s.WindowBorderSize  = 0.0f;
        s.FrameBorderSize   = 0.0f;
        s.WindowPadding     = ImVec2(0.0f, 0.0f);
        s.FramePadding      = ImVec2(10.0f, 6.0f);
        s.ItemSpacing       = ImVec2(10.0f, 8.0f);
        s.CellPadding       = ImVec2(10.0f, 8.0f);
        s.ItemInnerSpacing  = ImVec2(8, 5);
        s.ScrollbarSize     = 4.0f;
        s.PopupBorderSize   = 1.0f;

        // ImGui colors — flat, no accent tinting on backgrounds
        s.Colors[ImGuiCol_WindowBg]             = P.surface;
        s.Colors[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_Border]               = P.borderDim;
        s.Colors[ImGuiCol_Text]                 = P.textStrong;
        s.Colors[ImGuiCol_TextDisabled]         = P.textDim;
        s.Colors[ImGuiCol_Button]               = P.surfaceAlt;
        s.Colors[ImGuiCol_ButtonHovered]        = P.surfaceHi;
        s.Colors[ImGuiCol_ButtonActive]         = ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.30f);
        s.Colors[ImGuiCol_FrameBg]              = P.surfaceAlt;
        s.Colors[ImGuiCol_FrameBgHovered]       = P.surfaceHi;
        s.Colors[ImGuiCol_FrameBgActive]        = ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.15f);
        s.Colors[ImGuiCol_SliderGrab]           = P.accent;
        s.Colors[ImGuiCol_SliderGrabActive]     = P.accentHover;
        s.Colors[ImGuiCol_CheckMark]            = P.accent;
        s.Colors[ImGuiCol_Header]               = ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.12f);
        s.Colors[ImGuiCol_HeaderHovered]        = ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.20f);
        s.Colors[ImGuiCol_HeaderActive]         = ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.30f);
        s.Colors[ImGuiCol_Separator]            = P.divider;
        s.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.231f, 0.616f, 1.000f, 0.20f);
        s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.231f, 0.616f, 1.000f, 0.35f);
        s.Colors[ImGuiCol_PopupBg]              = P.surface;
    }



    inline bool BeginSection(const char* title, const ImVec2& size = ImVec2(0, 0))
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P.surface);
        ImGui::PushStyleColor(ImGuiCol_Border, P.line);
        bool open = ImGui::BeginChild(ImGui::GetID(title), size, true,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        for (int i = 4; i >= 1; i--)
        {
            float o = (float)i * 1.6f;
            dl->AddRectFilled(ImVec2(wp.x - o, wp.y - o + 2.0f),
                              ImVec2(wp.x + ws.x + o, wp.y + ws.y + o + 2.0f),
                              IM_COL32(0, 0, 0, (int)(0.22f * 10.0f)), 9.0f + o);
        }
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ImGui::ColorConvertFloat4ToU32(P.surface), 9.0f);
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ImGui::ColorConvertFloat4ToU32(P.line), 9.0f, 0, 1.0f);
        dl->AddLine(ImVec2(wp.x + 2, wp.y + 1.5f), ImVec2(wp.x + ws.x - 2, wp.y + 1.5f),
                    IM_COL32(255, 255, 255, 10), 1.0f);
        if (title && *title)
        {
            ImGui::SetCursorPos(ImVec2(16.0f, 12.0f));
            ImGui::TextColored(P.textStrong, "%s", title);
            ImGui::Dummy(ImVec2(0, 8));
        }
        return open;
    }
    inline void EndSection() { ImGui::EndChild(); ImGui::PopStyleColor(2); }

    inline void Header(const char* title)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x - 6, p.y + 1), ImVec2(p.x - 2, p.y + 15), ImGui::ColorConvertFloat4ToU32(P.accent), 1.5f, 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2);
        ImGui::TextColored(P.textStrong, "%s", title);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        ImVec2 sp = ImGui::GetCursorScreenPos();
        float sw = ImGui::GetContentRegionAvail().x;
        dl->AddLine(ImVec2(sp.x, sp.y - 4), ImVec2(sp.x + sw, sp.y - 4), ImGui::ColorConvertFloat4ToU32(P.line), 1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    }

    inline bool Toggle(const char* label, bool* v)
    {
        ImGui::PushID(label);
        const float w = 36.0f, h = 19.0f, knobR = 6.5f;
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::Dummy(ImVec2(1, h));
        if (label && *label)
        {
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(cursor.y + 1);
            ImGui::TextColored(*v ? P.textStrong : P.textMid, "%s", label);
        }
        
        // Align toggle switch absolutely to the right margin of the card
        float toggleX = ImGui::GetWindowContentRegionMax().x - w - 4.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(toggleX);
        
        ImVec2 screen = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tgl", ImVec2(w, h));
        bool changed = false;
        if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
        bool hover = ImGui::IsItemHovered();

        ImGuiID id = ImGui::GetID("##tgl");
        ImGuiStorage* store = ImGui::GetStateStorage();
        float t = store->GetFloat(id, 0.0f);
        float target = *v ? 1.0f : 0.0f;
        float dt = ImGui::GetIO().DeltaTime;
        float rate = 1.0f - expf(-dt * 11.0f);
        t += (target - t) * rate;
        if (fabsf(t - target) < 0.001f) t = target;
        store->SetFloat(id, t);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = screen, b = ImVec2(screen.x + w, screen.y + h);

        ImVec4 trackCol = Mix(P.track, P.accentDim, t);
        if (hover && !*v) trackCol = Mix(trackCol, P.surfaceHi, 0.4f);
        dl->AddRectFilled(a, b, U(trackCol), h * 0.5f);
        
        // Track border & glow
        if (t > 0.01f)
        {
            ImVec4 borderCol = Mix(P.borderDim, P.accent, t);
            dl->AddRect(a, b, U(borderCol), h * 0.5f, 0, 1.2f);
            
            // Soft outer glow when active
            ImVec4 glowCol = ImVec4(P.accent.x, P.accent.y, P.accent.z, t * 0.22f);
            dl->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), U(glowCol), (h + 2.0f) * 0.5f, 0, 1.0f);
        }
        else
        {
            dl->AddRect(a, b, U(P.borderDim), h * 0.5f, 0, 1.0f);
        }

        float pad = 2.5f;
        float currentKnobR = knobR * (0.85f + 0.15f * t);
        float knobX = a.x + knobR + pad + t * (w - 2.0f * knobR - 2.0f * pad);
        float knobY = (a.y + b.y) * 0.5f;

        // Knob shadow
        dl->AddCircleFilled(ImVec2(knobX, knobY + 1.0f), currentKnobR, IM_COL32(0, 0, 0, 80), 24);

        ImVec4 knobCol = hover ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : Mix(ImVec4(0.70f, 0.73f, 0.78f, 1.0f), P.textStrong, t);
        dl->AddCircleFilled(ImVec2(knobX, knobY), currentKnobR, U(knobCol), 24);

        ImGui::PopID();
        return changed;
    }

    inline bool CheckboxBind(const char* label, bool* v, int* key, int* mode = nullptr)
    {
        ImGui::PushID(label);
        const float tglW = 36.0f, h = 19.0f, knobR = 6.5f;
        ImVec2 cursor = ImGui::GetCursorPos();
        
        ImGui::Dummy(ImVec2(1, h));
        
        if (label && *label)
        {
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(cursor.y + 1);
            ImGui::TextColored(*v ? P.textStrong : P.textMid, "%s", label);
        }
        
        const char* kn = VKName(*key);
        const float kw = MaxF(ImGui::CalcTextSize(kn).x + 12.0f, 42.0f);
        
        // Position keybind absolutely to the left of the toggle
        float cardRight = ImGui::GetWindowContentRegionMax().x - 4.0f;
        float keybindX = cardRight - tglW - kw - 6.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(keybindX);
        ImVec2 kp = ImGui::GetCursorScreenPos();
        
        ImGuiID self = ImGui::GetID("##kb");
        static bool s_listen = false;
        static bool s_waitRelease = false;
        static ImGuiID s_ownerId = 0;
        bool listening = s_listen && (s_ownerId == self);
        if (listening) kn = "...";
        
        ImGui::InvisibleButton("##k", ImVec2(kw, h));
        if (ImGui::IsItemClicked()) { s_listen = true; s_waitRelease = true; s_ownerId = self; }
        if (listening) {
            if (s_waitRelease) {
                bool anyDown = false;
                for (int k = VK_LBUTTON; k <= 0xFE; k++) {
                    if (GetAsyncKeyState(k) & 0x8000) { anyDown = true; break; }
                }
                if (!anyDown) s_waitRelease = false;
            }
            else {
                for (int k = VK_LBUTTON; k <= 0xFE; k++) {
                    if (GetAsyncKeyState(k) & 0x8000) {
                        *key = k;
                        s_listen = false;
                        break;
                    }
                }
            }
        }
        
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 kpmax(kp.x + kw, kp.y + h);
        ImVec4 fillCol = P.surfaceAlt;
        ImVec4 borderCol = P.borderDim;
        if (listening) {
            float pulse = (sinf((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
            fillCol = Mix(P.accentDim, P.accentSoft, pulse);
            borderCol = P.accent;
        }
        dl->AddRectFilled(kp, kpmax, U(fillCol), 4.0f);
        dl->AddRect(kp, kpmax, U(borderCol), 4.0f, 0, 1.0f);
        dl->AddText(ImVec2(kp.x + (kw - ImGui::CalcTextSize(kn).x) * 0.5f,
                     kp.y + (h - ImGui::GetFontSize()) * 0.5f),
            listening ? U(P.textStrong) : U(P.text), kn);
            
        // Position toggle absolutely on the far right
        ImGui::SameLine();
        ImGui::SetCursorPosX(cardRight - tglW);
        ImVec2 screen = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tgl", ImVec2(tglW, h));
        bool toggle_hover = ImGui::IsItemHovered();
        bool changed = false;
        if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
        
        ImGuiID id = ImGui::GetID("##tgl");
        ImGuiStorage* store = ImGui::GetStateStorage();
        float t = store->GetFloat(id, 0.0f);
        float target = *v ? 1.0f : 0.0f;
        float dt = ImGui::GetIO().DeltaTime;
        t += (target - t) * (1.0f - expf(-dt * 11.0f));
        if (fabsf(t - target) < 0.001f) t = target;
        store->SetFloat(id, t);
        
        ImVec2 a = screen, b = ImVec2(screen.x + tglW, screen.y + h);
        ImVec4 trackCol = Mix(P.track, P.accentDim, t);
        if (toggle_hover && !*v) trackCol = Mix(trackCol, P.surfaceHi, 0.4f);
        dl->AddRectFilled(a, b, U(trackCol), h * 0.5f);
        if (t > 0.01f) {
            dl->AddRect(a, b, U(Mix(P.borderDim, P.accent, t)), h * 0.5f, 0, 1.2f);
            dl->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                U(ImVec4(P.accent.x, P.accent.y, P.accent.z, t * 0.22f)), (h + 2.f) * 0.5f, 0, 1.0f);
        } else {
            dl->AddRect(a, b, U(P.borderDim), h * 0.5f, 0, 1.0f);
        }
        float pad = 2.5f;
        float currentKnobR = knobR * (0.85f + 0.15f * t);
        float knobX = a.x + knobR + pad + t * (tglW - 2.0f * knobR - 2.0f * pad);
        float knobY = (a.y + b.y) * 0.5f;
        dl->AddCircleFilled(ImVec2(knobX, knobY + 1.f), currentKnobR, IM_COL32(0, 0, 0, 80), 24);
        dl->AddCircleFilled(ImVec2(knobX, knobY), currentKnobR, U((toggle_hover) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : Mix(ImVec4(0.70f, 0.73f, 0.78f, 1.0f), P.textStrong, t)), 24);
        
        ImGui::PopID();
        return changed;
    }

    inline bool Subtab(const char* label, bool active, float width = -1)
    {
        ImVec2 ts = ImGui::CalcTextSize(label);
        float w = (width > 0) ? width : 130.0f;
        ImVec2 sz(w, 32.0f);
        ImGui::InvisibleButton(label, sz);
        bool clicked = ImGui::IsItemClicked();
        bool hover = ImGui::IsItemHovered();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiID id = ImGui::GetID("##subtab");
        ImGuiStorage* store = ImGui::GetStateStorage();
        float hov = store->GetFloat(id, 0.0f);
        float target = hover ? 1.0f : 0.0f;
        float dt = ImGui::GetIO().DeltaTime;
        float rate = 1.0f - expf(-dt / 0.15f);
        hov += (target - hov) * rate;
        store->SetFloat(id, hov);

        if (active)
        {
            dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(P.accentSoft), 6.0f, 0);
            dl->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(P.accent), 6.0f, 0, 1.5f);
            dl->AddRect(ImVec2(a.x - 1, a.y + 3), ImVec2(a.x + 1, b.y - 3), ImGui::ColorConvertFloat4ToU32(P.accent), 1.5f, 0, 2.0f);
        }
        else
        {
            ImVec4 hcol = Mix(P.surfaceAlt, P.accentGlow, hov * 0.6f);
            dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(hcol), 6.0f, 0);
        }
        dl->AddText(ImVec2(a.x + 12, (a.y + b.y) / 2.0f - ts.y / 2.0f),
                    ImGui::ColorConvertFloat4ToU32(active ? P.textStrong : (hover ? P.text : P.textDim)), label);
        return clicked;
    }

    inline void Divider()
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        dl->AddLine(p, ImVec2(p.x + w, p.y), ImGui::ColorConvertFloat4ToU32(P.line), 1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    }

    inline void Status(const char* label, bool active)
    {
        ImVec4 c = active ? P.good : P.bad;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tw = ImGui::CalcTextSize(label).x + 18;
        dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + 22), ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 0.14f)), 6.0f, 0);
        dl->AddCircleFilled(ImVec2(p.x + 11, p.y + 11), 4, ImGui::ColorConvertFloat4ToU32(c));
        ImGui::SetCursorScreenPos(ImVec2(p.x + 22, p.y + 2));
        ImGui::TextColored(c, "%s", label);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 26));
    }

    inline void Row(const char* label, const char* value)
    {
        ImGui::TextColored(P.textDim, "%s", label);
        ImVec2 sz = ImGui::CalcTextSize(value);
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - sz.x);
        ImGui::TextColored(P.text, "%s", value);
    }

    struct card {
        static bool begin(const char* id, ImVec2 size, const char* title = nullptr)
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (size.x > 0.f && size.y > 0.f)
            {
                dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), U(P.card), 8.0f);
                dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), U(P.borderDim), 8.0f, 0, 1.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, P.card);
            ImGui::PushStyleColor(ImGuiCol_Border, P.line);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
            const bool open = ImGui::BeginChild(id, size, true, ImGuiWindowFlags_None);
            if (open && title)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
                ImDrawList* cd = ImGui::GetWindowDrawList();
                ImVec2 tp = ImGui::GetCursorScreenPos();
                cd->AddRectFilled(tp, ImVec2(tp.x + 2.0f, tp.y + ImGui::GetFontSize() + 2.0f),
                    U(P.accent), 1.0f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
                ImGui::TextColored(P.textDim, "%s", title);
                ImGui::Dummy(ImVec2(0, 8.0f));
            }
            return open;
        }
        static void end()
        {
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);
        }
    };

    inline bool SidebarTab(int iconId, const char* label, bool active, float startX, float startY, float width)
    {
        const float tabH = 38.0f;
        const float tabPad = 12.0f;
        const float tabW = width - tabPad * 2.0f;
        const float tabX = startX + tabPad;
        const float tabY = startY;
        
        ImVec2 tMin = ImVec2(tabX, tabY);
        ImVec2 tMax = ImVec2(tabX + tabW, tabY + tabH);
        ImVec2 center = ImVec2(tMin.x + 16.0f, tMin.y + tabH * 0.5f);

        ImGui::SetCursorScreenPos(tMin);
        ImGui::PushID(label);
        ImGui::InvisibleButton("##tab", ImVec2(tabW, tabH));
        bool clicked = ImGui::IsItemClicked();
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();

        ImGuiID id = ImGui::GetID(label);
        float t = EaseOutCubic(Anim(id, active || hov, 9.0f));
        float at = EaseOutCubic(Anim(id ^ 0x8A13C4u, active, 10.0f));
        
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImU32 hoverFill = LerpU32(IM_COL32(0,0,0,0), U(P.surfaceHi), t);
        ImU32 activeFill = U(ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.08f));
        dl->AddRectFilled(tMin, tMax, active ? activeFill : hoverFill, 6.0f);

        if (at > 0.01f)
        {
            // Vertical accent indicator bar on the left edge
            dl->AddRectFilled(ImVec2(tMin.x, tMin.y + 8.0f), ImVec2(tMin.x + 3.0f, tMax.y - 8.0f), U(P.accent), 1.5f);
        }

        float s = 16.0f + at * 1.0f;
        ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(
            Mix(hov ? P.textStrong : P.textDim, P.accent, at));
        switch (iconId)
        {
        case 1: // crosshair
        {
            const float r = s * .38f, gap = s * .13f, ll = s * .26f, lw = 1.5f;
            dl->AddCircle(center, r, iconCol, 32, lw);
            dl->AddCircleFilled(center, 2.0f, iconCol);
            dl->AddLine(ImVec2(center.x, center.y - r - gap), ImVec2(center.x, center.y - r - gap - ll), iconCol, lw);
            dl->AddLine(ImVec2(center.x, center.y + r + gap), ImVec2(center.x, center.y + r + gap + ll), iconCol, lw);
            dl->AddLine(ImVec2(center.x - r - gap, center.y), ImVec2(center.x - r - gap - ll, center.y), iconCol, lw);
            dl->AddLine(ImVec2(center.x + r + gap, center.y), ImVec2(center.x + r + gap + ll, center.y), iconCol, lw);
            break;
        }
        case 2: // eye
        {
            const float rx = s * .44f, ry = s * .22f, lw = 1.5f;
            const ImVec2 L = ImVec2(center.x - rx, center.y);
            const ImVec2 R = ImVec2(center.x + rx, center.y);
            const ImVec2 T = ImVec2(center.x, center.y - ry);
            const ImVec2 B = ImVec2(center.x, center.y + ry);
            dl->AddBezierCubic(L, ImVec2(L.x + rx * .65f, T.y - ry * .3f), ImVec2(R.x - rx * .65f, T.y - ry * .3f), R, iconCol, lw);
            dl->AddBezierCubic(L, ImVec2(L.x + rx * .65f, B.y + ry * .3f), ImVec2(R.x - rx * .65f, B.y + ry * .3f), R, iconCol, lw);
            dl->AddCircleFilled(center, s * .13f, iconCol);
            break;
        }
        case 3: // globe
        {
            const float r = s * .40f, cx = s * .22f, lw = 1.5f;
            dl->AddCircle(center, r, iconCol, 32, lw);
            dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), iconCol, lw);
            dl->AddBezierCubic(ImVec2(center.x, center.y - r), ImVec2(center.x + cx, center.y - r * .5f), ImVec2(center.x + cx, center.y + r * .5f), ImVec2(center.x, center.y + r), iconCol, lw);
            dl->AddBezierCubic(ImVec2(center.x, center.y - r), ImVec2(center.x - cx, center.y - r * .5f), ImVec2(center.x - cx, center.y + r * .5f), ImVec2(center.x, center.y + r), iconCol, lw);
            break;
        }
        case 4: // layer (3 stacked bars)
        {
            const float hw = s * .35f, hh = s * .07f, gap = s * .17f;
            for (int i = -1; i <= 1; i++)
                dl->AddRectFilled(ImVec2(center.x - hw, center.y + i * gap - hh), ImVec2(center.x + hw, center.y + i * gap + hh), iconCol, 1.0f);
            break;
        }
        case 5: // diamond
        {
            const float r = s * .42f;
            dl->AddQuad(ImVec2(center.x, center.y - r), ImVec2(center.x + r, center.y), ImVec2(center.x, center.y + r), ImVec2(center.x - r, center.y), iconCol, 1.6f);
            dl->AddLine(ImVec2(center.x - r * .45f, center.y), ImVec2(center.x + r * .45f, center.y), iconCol, 1.3f);
            dl->AddCircleFilled(center, s * .08f, iconCol, 12);
            break;
        }
        case 6: // person
        {
            const float r = s * .28f;
            dl->AddCircleFilled(ImVec2(center.x, center.y - r * .3f), r * .45f, iconCol, 16);
            dl->AddCircleFilled(ImVec2(center.x, center.y + r * .8f), r, iconCol, 24);
            break;
        }
        case 7: // gear
        {
            const float ri = s * .28f, ro = s * .42f;
            dl->AddCircle(center, ri, iconCol, 24, 1.5f);
            for (int i = 0; i < 6; i++)
            {
                const float a = (float)i / 6.f * 6.2832f;
                const float a1 = a - .24f, a2 = a + .24f;
                dl->AddQuadFilled(
                    ImVec2(center.x + cosf(a1) * ri, center.y + sinf(a1) * ri),
                    ImVec2(center.x + cosf(a2) * ri, center.y + sinf(a2) * ri),
                    ImVec2(center.x + cosf(a2) * ro, center.y + sinf(a2) * ro),
                    ImVec2(center.x + cosf(a1) * ro, center.y + sinf(a1) * ro), iconCol);
            }
            break;
        }
        }

        if (label && *label)
        {
            dl->AddText(ImVec2(tMin.x + 38.0f, tMin.y + (tabH - ImGui::GetFontSize()) * 0.5f), iconCol, label);
        }

        return clicked;
    }

    inline bool ContentSubtab(const char* label, bool active, float& animStore)
    {
        ImVec2 ts = ImGui::CalcTextSize(label);
        float w = ts.x + 24.0f;
        float h = 28.0f;
        ImVec2 sz(w, h);
        ImGui::InvisibleButton(label, sz);
        bool clicked = ImGui::IsItemClicked();
        bool hover = ImGui::IsItemHovered();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGuiID id = ImGui::GetID(label);
        float t = AnimEased(id, active || hover, 10.f);
        float at = AnimEased(id ^ 0x5C3A1Bu, active, 12.f);

        if (active)
        {
            ImVec4 activeBg = ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.10f);
            dl->AddRectFilled(a, b, U(activeBg), 6.0f, 0);
        }
        else if (hover)
        {
            dl->AddRectFilled(a, b, U(P.surfaceHi), 6.0f, 0);
        }

        ImU32 labelCol = U(Mix(hover ? P.textStrong : P.textMid, P.textStrong, at));
        dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y * 0.5f),
            labelCol, label);
        animStore = t;
        return clicked;
    }

    inline void ContentHeader(const char* title, const char* subtitle = nullptr)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        dl->AddRectFilled(p, ImVec2(p.x + 2.5f, p.y + ImGui::GetFontSize() + 2.0f),
            U(P.accent), 1.0f);
        
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::TextColored(P.textStrong, "%s", title);
        
        ImGui::PushStyleColor(ImGuiCol_Separator, P.divider);
        ImGui::Separator();
        ImGui::PopStyleColor();
        
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        
        if (subtitle)
        {
            ImGui::TextColored(P.textDim, "%s", subtitle);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
    }

    inline bool Checkbox(const char* label, bool* v)
    {
        return Toggle(label, v);
    }

    inline bool sliderfloat(const char* label, float* v, float mn, float mx, const char* fmt = "%.3f")
    {
        ImGui::PushID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float avail = ImGui::GetContentRegionAvail().x;

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::TextColored(P.textDim, "%s", label);
        char buf[32]; snprintf(buf, sizeof(buf), fmt ? fmt : "%.2f", *v);
        
        // Value badge (drawn manually so we don't mess up cursor)
        ImVec2 valSize = ImGui::CalcTextSize(buf);
        float badgeW = valSize.x + 10.0f;
        float badgeH = valSize.y + 2.0f;
        ImVec2 bMin = ImVec2(p0.x + avail - badgeW, p0.y);
        
        dl->AddRectFilled(bMin, ImVec2(bMin.x + badgeW, bMin.y + badgeH), U(P.surfaceAlt), 4.0f);
        dl->AddText(ImVec2(bMin.x + 5.0f, bMin.y + 1.0f), U(P.accent), buf);

        // Advance cursor properly instead of absolute set
        ImGui::Dummy(ImVec2(0, 2.0f));
        ImVec2 p = ImGui::GetCursorScreenPos();
        
        const float w = avail;
        const float cy = p.y + 10.0f;
        constexpr float kTrkH = 3.0f;

        ImGui::InvisibleButton("##sl", ImVec2(w, 20.0f));
        bool active = ImGui::IsItemActive();
        bool hov = ImGui::IsItemHovered();
        if (active)
        {
            float rel = (ImGui::GetIO().MousePos.x - p.x) / w;
            *v = mn + ClampF(rel, 0.0f, 1.0f) * (mx - mn);
        }
        const float t = mx == mn ? 0.0f : ClampF((*v - mn) / (mx - mn), 0.0f, 1.0f);

        // Track background
        dl->AddRectFilled(ImVec2(p.x, cy - kTrkH * 0.5f), ImVec2(p.x + w, cy + kTrkH * 0.5f), U(P.track), kTrkH);
        
        // Active track fill
        if (t > 0.001f)
        {
            dl->AddRectFilled(ImVec2(p.x, cy - kTrkH * 0.5f), ImVec2(p.x + w * t, cy + kTrkH * 0.5f),
                active ? U(P.accentHover) : U(P.accent), kTrkH);
        }

        const float hh = active ? 6.0f : 5.0f;
        const float hx = p.x + w * t;
        
        // Thumb shadow & circle
        dl->AddCircleFilled(ImVec2(hx, cy + 1.0f), hh, IM_COL32(0, 0, 0, 70), 22);
        dl->AddCircleFilled(ImVec2(hx, cy), hh, active ? U(P.accentHover) : U(P.accent), 22);
        dl->AddCircle(ImVec2(hx, cy), hh, U(P.textStrong), 22, 1.0f);

        ImGui::PopID();
        return ImGui::IsItemDeactivatedAfterEdit();
    }

    inline bool SliderFloat(const char* label, float* v, float mn, float mx, const char* fmt = "%.3f")
    {
        return sliderfloat(label, v, mn, mx, fmt);
    }

    inline bool SliderInt(const char* label, int* v, int mn, int mx, const char* fmt = "%d")
    {
        float fv = (float)*v;
        bool r = sliderfloat(label, &fv, (float)mn, (float)mx, fmt);
        *v = (int)roundf(fv);
        return r;
    }

    inline bool combo(const char* label, int* idx, const std::vector<const char*>& items)
    {
        ImGui::PushID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float avail = ImGui::GetContentRegionAvail().x;

        ImGui::TextColored(P.textDim, "%s", label);

        ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = avail, h = 32.0f;

        ImGui::InvisibleButton("##cb", ImVec2(w, h));
        bool hov = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImVec2 pmax(p.x + w, p.y + h);
        dl->AddRectFilled(p, pmax, hov ? U(P.surfaceHi) : U(P.surfaceAlt), 6.0f);
        dl->AddRect(p, pmax, hov ? U(P.accent) : U(P.borderDim), 6.0f, 0, 1.0f);

        const char* cur = (*idx >= 0 && *idx < (int)items.size()) ? items[*idx] : "---";
        dl->AddText(ImVec2(p.x + 10.0f, p.y + (h - ImGui::GetFontSize()) * 0.5f), U(P.textStrong), cur);

        const float cx = p.x + w - 16.0f, cy = p.y + h * 0.5f;
        dl->AddTriangleFilled(ImVec2(cx - 4.0f, cy - 2.0f), ImVec2(cx + 4.0f, cy - 2.0f), ImVec2(cx, cy + 3.0f), U(P.textDim));

        bool changed = false;
        if (clicked) ImGui::OpenPopup("##cop");
        ImGui::SetNextWindowPos(ImVec2(p.x, p.y + h + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, 200.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, P.surface);
        ImGui::PushStyleColor(ImGuiCol_Border, P.borderDim);
        if (ImGui::BeginPopup("##cop"))
        {
            for (int i = 0; i < (int)items.size(); i++)
            {
                bool sel = (i == *idx);
                ImGui::PushStyleColor(ImGuiCol_Header,
                    sel ? ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.12f) : ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                    ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.12f));
                ImGui::PushStyleColor(ImGuiCol_Text,
                    sel ? P.accent : P.text);
                if (ImGui::Selectable(items[i], sel))
                {
                    *idx = i;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(3);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        return changed;
    }

    inline bool Combo(const char* label, int* idx, const char* const* items, int items_count)
    {
        std::vector<const char*> v(items, items + items_count);
        return combo(label, idx, v);
    }

    inline bool Button(const char* label, ImVec2 size = ImVec2(-1, 0))
    {
        return ImGui::Button(label, size);
    }

    inline bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0)
    {
        return ImGui::ColorEdit3(label, col, flags | ImGuiColorEditFlags_NoInputs);
    }

    inline bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0)
    {
        return ImGui::ColorEdit4(label, col, flags | ImGuiColorEditFlags_NoInputs);
    }

    inline void labelsection(const char* text)
    {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + 2.0f, p.y + ImGui::GetFontSize() + 1.0f), U(P.accent), 1.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::TextColored(P.textDim, "%s", text);
        ImGui::PushStyleColor(ImGuiCol_Separator, P.divider);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    inline void gap(float px = 4.0f) { ImGui::Dummy(ImVec2(0.0f, px)); }

    inline bool Bind(const char* id, int* key, int* mode = nullptr)
    {
        static bool s_listen = false;
        static bool s_waitRelease = false;
        static ImGuiID s_ownerId = 0;
        ImGui::PushID(id);
        ImGuiID self = ImGui::GetID("##kb");

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float h = 20.0f;
        bool listening = s_listen && (s_ownerId == self);
        const char* kn = listening ? "..." : VKName(*key);
        const char* mn = (mode && *mode == 1) ? "TOGG" : "HOLD";
        const float kw = MaxF(ImGui::CalcTextSize(kn).x + 16.0f, 42.0f);
        const float mw = mode ? (ImGui::CalcTextSize(mn).x + 12.0f) : 0.0f;

        ImVec2 kp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##k", ImVec2(kw, h));
        if (ImGui::IsItemClicked()) { s_listen = true; s_waitRelease = true; s_ownerId = self; }
        if (listening) {
            if (s_waitRelease) {
                bool anyDown = false;
                for (int k = VK_LBUTTON; k <= 0xFE; k++) {
                    if (GetAsyncKeyState(k) & 0x8000) { anyDown = true; break; }
                }
                if (!anyDown) s_waitRelease = false;
            }
            else {
                for (int k = VK_LBUTTON; k <= 0xFE; k++) {
                    if (GetAsyncKeyState(k) & 0x8000) {
                        *key = k;
                        s_listen = false;
                        break;
                    }
                }
            }
        }
        ImVec2 kpmax(kp.x + kw, kp.y + h);
        
        ImVec4 fillCol = P.surfaceAlt;
        ImVec4 borderCol = P.borderDim;
        if (listening)
        {
            float pulse = (sinf((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
            fillCol = Mix(P.accentDim, P.accentSoft, pulse);
            borderCol = P.accent;
        }
        
        dl->AddRectFilled(kp, kpmax, U(fillCol), 5.0f);
        dl->AddRect(kp, kpmax, U(borderCol), 5.0f, 0, 1.0f);
        dl->AddText(ImVec2(kp.x + (kw - ImGui::CalcTextSize(kn).x) * 0.5f,
                     kp.y + (h - ImGui::GetFontSize()) * 0.5f),
            listening ? U(P.textStrong) : U(P.text), kn);

        if (mode) {
            ImGui::SameLine(0.0f, 4.0f);
            ImVec2 mp = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##m", ImVec2(mw, h));
            bool mhov = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) *mode = (*mode == 0) ? 1 : 0;
            ImVec2 mpmax(mp.x + mw, mp.y + h);
            dl->AddRectFilled(mp, mpmax, mhov ? U(P.cardHov) : U(P.surface), 5.0f);
            dl->AddRect(mp, mpmax, mhov ? U(P.accentDim) : U(P.borderDim), 5.0f, 0, 1.0f);
            dl->AddText(ImVec2(mp.x + (mw - ImGui::CalcTextSize(mn).x) * 0.5f,
                         mp.y + (h - ImGui::GetFontSize()) * 0.5f), U(P.textMid), mn);
        }

        ImGui::PopID();
        return false;
    }

    inline void Tooltip(const char* text)
    {
        if (ImGui::IsItemHovered())
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, P.card);
            ImGui::PushStyleColor(ImGuiCol_Border, P.line);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(280.0f);
            ImGui::TextColored(P.text, "%s", text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    inline bool CheckboxWithColorPicker(const char* label, bool* v, float col[3])
    {
        ImGui::PushID(label);
        bool changed = Toggle(label, v);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 18.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImU32 sw = IM_COL32((int)(col[0] * 255), (int)(col[1] * 255), (int)(col[2] * 255), 255);
        dl->AddRectFilled(p, ImVec2(p.x + 15.0f, p.y + 14.0f), sw, 2.0f);
        dl->AddRect(p, ImVec2(p.x + 15.0f, p.y + 14.0f), *v ? U(P.accent) : U(P.line), 2.0f);
        ImGui::InvisibleButton("##cp", ImVec2(15.0f, 14.0f));
        if (ImGui::IsItemClicked()) ImGui::OpenPopup("##colpick");
        ImGui::SetNextWindowPos(ImVec2(p.x + 19.0f, p.y - 4.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, P.card);
        ImGui::PushStyleColor(ImGuiCol_Border, P.line);
        if (ImGui::BeginPopup("##colpick")) {
            ImGui::ColorPicker3("##pk", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar);
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        return changed;
    }
}
