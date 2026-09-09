#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/KeyBind.h"
#include "../rbx/globals/options.h"
#include <map>
#include <string>
#include <random>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UI
{
    // ── EXTERIUM style values ──────────────────────────────────────
    inline const ImColor main_color = ImColor(230, 134, 224, 255);
    inline const ImVec4 text_color[3] = {
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
        ImVec4(1.0f, 1.0f, 1.0f, 200.0f / 255.0f),
        ImVec4(1.0f, 1.0f, 1.0f, 150.0f / 255.0f) };
    inline const ImVec4 background_color(13.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 200.0f / 255.0f);
    inline const ImVec4 second_color(1.0f, 1.0f, 1.0f, 20.0f / 255.0f);
    inline const ImVec4 stroke_color(35.0f / 255.0f, 35.0f / 255.0f, 35.0f / 255.0f, 1.0f);
    inline const ImVec4 child_color(19.0f / 255.0f, 19.0f / 255.0f, 19.0f / 255.0f, 1.0f);
    inline const ImVec4 scroll_bg_col(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f);
    inline const ImVec4 winbg_color(15.0f / 255.0f, 16.0f / 255.0f, 18.0f / 255.0f, 200.0f / 255.0f);
    inline ImVec2 frame_size = ImVec2(305.0f, 46.0f);
    inline const float round_4 = 4.0f;
    inline const float round_5 = 5.0f;
    inline const float round_360 = 360.0f;

    // ── shared control metrics (derived from the EXTERIUM design) ──
    // Toggle: track is bottom-right anchored in the row, exactly as
    // EXTERIUM sizes its checkbox (track.Min = row.Max - (45,32),
    // track.Max = row.Max - (14,14)), thumb 5.5r with 13px travel.
    inline const float TOG_W        = 31.0f;  // track width  (45 - 14)
    inline const float TOG_H        = 18.0f;  // track height (32 - 14)
    inline const float TOG_INSET_R  = 14.0f;  // right/bottom inset from row edge
    inline const float TOG_INSET_T  = 32.0f;  // track top inset (from row bottom edge)
    inline const float TOG_KNOB_R   = 5.5f;   // thumb radius
    inline const float TOG_KNOB_X   = 9.0f;   // thumb inset from track left
    inline const float TOG_KNOB_Y   = 8.5f;   // thumb inset from track top
    inline const float TOG_TRAVEL   = 13.0f;  // thumb travel when ON
    // Slider: dark track, accent fill, small crisp handle (EXTERIUM).
    inline const float SLI_TRACK_H  = 5.0f;   // track height inside the row
    inline const float SLI_HANDLE_R = 5.5f;   // handle radius
    inline const float SLI_HANDLE_INNER_R = 3.5f;

    inline ImFont* icon_font = nullptr;
    inline ImFont* icon_big_font = nullptr;
    inline ImFont* small_font = nullptr;
    inline ImFont* medium_font = nullptr;
    inline ImFont* logo_font = nullptr;
    inline ImFont* small_icon_font = nullptr;
    inline ImFont* arrow_icons = nullptr;

    // ── Seraph layout compat ──────────────────────────────────────
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
    inline float SidebarX = 10.0f;
    inline float SidebarW = 187.0f;
    inline float ContentX = 197.0f;
    inline float ContentW = 630.0f;
    inline float CardW = 305.0f;
    inline float CardH = 532.0f;
    inline float ColGap = 10.0f;
    static const float RADIUS    = 10.0f;
    static const float RADIUS_SM = 6.0f;
    static const float PAD       = 16.0f;
    static const float PAD_SM    = 10.0f;
    static const float ROW       = 12.0f;
    static const float GROUP     = 16.0f;
    static const float SECTION   = 24.0f;
    static const float ANIM      = 0.16f;
    static const float BORDER    = 1.0f;
    inline const float SECTION_HEADER_HEIGHT = 46.0f;
    inline const float CONTROL_HEIGHT = 46.0f;
    inline const float CONTROL_GAP = 4.0f;
    inline const float LABEL_HPAD = 18.0f;
    inline const float SECTION_GAP = 14.0f;

    // ── math / color helpers ──────────────────────────────────────
    inline float EaseOutCubic(float t) { return 1.0f - powf(1.0f - t, 3.0f); }
    inline float SqrtF(float v) { return sqrtf(v < 0.0f ? 0.0f : v); }

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
    inline ImU32 AnimColorU32(const ImVec4& from, const ImVec4& to, float t) { return U(Mix(from, to, t)); }
    inline ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
    inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    inline float MaxF(float a, float b) { return a > b ? a : b; }
    inline ImVec2 CalcText(ImFont* font, float sz, const char* text) { return font ? font->CalcTextSizeA(sz, FLT_MAX, 0.0f, text) : ImGui::CalcTextSize(text); }

    inline ImU32 GetDarkColor(const ImColor& color)
    {
        return IM_COL32((int)(color.Value.x * 0.40f * 255.0f), (int)(color.Value.y * 0.40f * 255.0f),
                        (int)(color.Value.z * 0.40f * 255.0f), 255);
    }

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
        UINT scn = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextA((LONG)(scn << 16), buf, sizeof(buf)) > 0)
            return buf;
        return "?";
    }

    // ── glow (soft colored bloom) ─────────────────────────────────
    // Layered bloom that FOLLOWS the actual control geometry: each
    // pass grows slightly and fades with a smooth falloff, so the
    // result reads as "the control emits a little colored light" —
    // never as a hard disc/halo placed behind the control. Always
    // draw the bloom BEFORE the crisp control so the control stays
    // the sharpest element. Extent stays ~1 control width at most.
    inline float BloomStep(float t) { return expf(-t * t * 3.4f); }

    inline void SoftCircleBloom(ImDrawList* dl, const ImVec2& center, float radius, const ImVec4& color, float extent)
    {
        int steps = (int)(extent * 2.0f);
        if (steps < 4) steps = 4;
        const int segs = 48;
        for (int i = steps; i >= 1; i--)
        {
            float t = (float)i / (float)steps;
            int a = (int)(color.w * 255.0f * BloomStep(t) + 0.5f);
            if (a <= 1) continue;
            dl->AddCircleFilled(center, radius + extent * t,
                IM_COL32((int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f), a), segs);
        }
    }

    inline void SoftRectBloom(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec4& color, float extent, float rounding)
    {
        int steps = (int)(extent * 2.0f);
        if (steps < 4) steps = 4;
        for (int i = steps; i >= 1; i--)
        {
            float t = (float)i / (float)steps;
            int al = (int)(color.w * 255.0f * BloomStep(t) + 0.5f);
            if (al <= 1) continue;
            float e = extent * t;
            dl->AddRectFilled(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e),
                IM_COL32((int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f), al), rounding + e);
        }
    }

    inline void centerText(const ImVec2& min, const ImVec2& max, const char* text, ImU32 color, float fontsize = 0.0f, ImFont* font = nullptr)
    {
        ImFont* f = font ? font : ImGui::GetFont();
        float fs = fontsize > 0.0f ? fontsize : f->FontSize;
        ImVec2 size = CalcText(f, fs, text);
        ImVec2 pos((min.x + max.x - size.x) * 0.5f, (min.y + max.y - size.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(f, fs, pos, color, text);
    }

    // ── rows ──────────────────────────────────────────────────────
    inline void BeginRow(const ImVec2& pos, bool bg = true)
    {
        if (bg)
            ImGui::GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(frame_size.x * sc, frame_size.y * sc), U(background_color), round_5 * sc);
    }

    inline void Row() { ImGui::Dummy(ImVec2(0.0f, 4.0f)); }
    inline void gap(float px = 4.0f) { ImGui::Dummy(ImVec2(0.0f, px)); }

    inline void Divider()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(p, ImVec2(p.x + frame_size.x * sc, p.y), U(ImVec4(1, 1, 1, 12.0f / 255.0f)), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    struct CheckboxState { float anim = 0.0f; };
    static std::map<ImGuiID, CheckboxState> checkbox_states;

    static inline bool CheckboxEx(const char* label, bool* v)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const float Sc = sc;
        const float dt = io.DeltaTime;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect total_bb(pos, pos + ImVec2(frame_size.x * Sc, frame_size.y * Sc));
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        bool changed = false;

        float& anim = checkbox_states[id].anim;
        float target = *v ? 1.0f : 0.0f;
        float rate = 1.0f - expf(-dt * 12.0f);
        anim += (target - anim) * rate;
        if (fabsf(anim - target) < 0.0005f) anim = target;

        if (hovered && ImGui::IsMouseClicked(0))
        {
            *v = !*v;
            anim = *v ? 1.0f : 0.0f;
            changed = true;
        }

        const ImRect check_bb(ImVec2(total_bb.Max.x - (TOG_INSET_R + TOG_W) * Sc, total_bb.Max.y - TOG_INSET_T * Sc),
                              ImVec2(total_bb.Max.x - TOG_INSET_R * Sc, total_bb.Max.y - TOG_INSET_R * Sc));

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        const ImVec4 main_soft = ImVec4(main.x, main.y, main.z, 0.30f);

        float eased = EaseOutCubic(anim);

        // row background
        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);

        // toggle track (dark OFF, subtle accent ON)
        ImU32 pill = U(Mix(second_color, main_soft, eased));
        dl->AddRectFilled(check_bb.Min, check_bb.Max, pill, round_360);

        const ImVec2 knob(check_bb.Min.x + (TOG_KNOB_X + TOG_TRAVEL * eased) * Sc, check_bb.Min.y + TOG_KNOB_Y * Sc);

        // soft bloom following the toggle track (barely noticeable)
        if (eased > 0.02f)
            SoftRectBloom(dl, check_bb.Min, check_bb.Max, ImVec4(main.x, main.y, main.z, 0.10f * eased), 7.0f * Sc, round_360);

        // tiny dark drop shadow under thumb (offset down-right, like EXTERIUM)
        SoftCircleBloom(dl, knob + ImVec2(1.0f * Sc, 1.0f * Sc), TOG_KNOB_R * Sc, ImVec4(0, 0, 0, 0.22f), 3.0f * Sc);

        // thumb (crisp, white→accent as it turns on)
        const ImU32 knobCol = LerpU32(U(ImVec4(1, 1, 1, 0.92f)), U(main), eased);
        dl->AddCircleFilled(knob, TOG_KNOB_R * Sc, knobCol, 24);

        // label
        dl->AddText(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + (frame_size.y * Sc - ImGui::GetFontSize()) * 0.5f),
            hovered ? U(text_color[0]) : U(text_color[1]), label);

        return changed;
    }

    static inline bool Checkbox(const char* label, bool* v) { return CheckboxEx(label, v); }
    static inline bool Toggle(const char* label, bool* v) { return CheckboxEx(label, v); }

    static inline bool CheckboxBind(const char* label, bool* v, int* key, int* mode = nullptr)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const float Sc = sc;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect total_bb(pos, pos + ImVec2(frame_size.x * Sc, frame_size.y * Sc));
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        bool changed = false;

        float& anim = checkbox_states[id].anim;
        float target = *v ? 1.0f : 0.0f;
        if (hovered && ImGui::IsMouseClicked(0)) { *v = !*v; anim = *v ? 1.0f : 0.0f; changed = true; }
        float rate = 1.0f - expf(-io.DeltaTime * 12.0f);
        anim += (target - anim) * rate;
        float eased = EaseOutCubic(anim);

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        const ImVec4 main_soft = ImVec4(main.x, main.y, main.z, 0.30f);

        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);

        // key bind mini-box (left of pill)
        const float kbbW = (mode ? 84.0f : 62.0f) * Sc;
        const ImRect key_bb(ImVec2(total_bb.Max.x - 126.0f * Sc, total_bb.Min.y + 20.0f * Sc),
                            ImVec2(total_bb.Max.x - 126.0f * Sc + kbbW, total_bb.Min.y + frame_size.y * Sc - 20.0f * Sc));
        const char* kn = VKName(*key);
        dl->AddRectFilled(key_bb.Min, key_bb.Max, U(second_color), round_5 * Sc);
        ImGui::PushFont(small_font);
        centerText(key_bb.Min, key_bb.Max, kn, U(text_color[1]), 17.0f * Sc, small_font);
        ImGui::PopFont();

        if (mode)
        {
            const char* mn = (*mode == 1) ? "TOGG" : "HOLD";
            const ImRect mode_bb(ImVec2(total_bb.Max.x - 58.0f * Sc, total_bb.Min.y + 20.0f * Sc),
                                 ImVec2(total_bb.Max.x - 14.0f * Sc, total_bb.Min.y + frame_size.y * Sc - 20.0f * Sc));
            dl->AddRectFilled(mode_bb.Min, mode_bb.Max, U(second_color), round_5 * Sc);
            bool mhov = ImGui::IsMouseHoveringRect(mode_bb.Min, mode_bb.Max);
            if (mhov && ImGui::IsMouseClicked(0)) *mode = (*mode == 0) ? 1 : 0;
            ImGui::PushFont(small_font);
            centerText(mode_bb.Min, mode_bb.Max, mn, mhov ? U(text_color[0]) : U(text_color[1]), 15.0f * Sc, small_font);
            ImGui::PopFont();
        }

        // toggle track (dark OFF, subtle accent ON)
        const ImRect check_bb(ImVec2(total_bb.Max.x - (TOG_INSET_R + TOG_W) * Sc, total_bb.Max.y - TOG_INSET_T * Sc),
                              ImVec2(total_bb.Max.x - TOG_INSET_R * Sc, total_bb.Max.y - TOG_INSET_R * Sc));
        ImU32 pill = U(Mix(second_color, main_soft, eased));
        dl->AddRectFilled(check_bb.Min, check_bb.Max, pill, round_360);
        const ImVec2 knob(check_bb.Min.x + (TOG_KNOB_X + TOG_TRAVEL * eased) * Sc, check_bb.Min.y + TOG_KNOB_Y * Sc);
        if (eased > 0.02f)
            SoftRectBloom(dl, check_bb.Min, check_bb.Max, ImVec4(main.x, main.y, main.z, 0.10f * eased), 7.0f * Sc, round_360);
        SoftCircleBloom(dl, knob + ImVec2(1.0f * Sc, 1.0f * Sc), TOG_KNOB_R * Sc, ImVec4(0, 0, 0, 0.22f), 3.0f * Sc);
        dl->AddCircleFilled(knob, TOG_KNOB_R * Sc, LerpU32(U(ImVec4(1, 1, 1, 0.92f)), U(main), eased), 24);

        dl->AddText(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + (frame_size.y * Sc - ImGui::GetFontSize()) * 0.5f),
            hovered ? U(text_color[0]) : U(text_color[1]), label);

        return changed;
    }

    static inline bool CheckboxWithColorPicker(const char* label, bool* v, float col[3])
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;
        bool changed = CheckboxEx(label, v);
        ImVec2 cur = ImGui::GetCursorScreenPos();
        ImVec2 pos(cur.x, cur.y - frame_size.y * Sc - ImGui::GetStyle().ItemSpacing.y);
        const ImVec2 box_min(pos.x + (frame_size.x - 96.0f) * Sc, pos.y + (frame_size.y - 30.0f) * Sc * 0.5f);
        const ImVec2 box_max(box_min.x + 24.0f * Sc, box_min.y + 24.0f * Sc);
        dl->AddRectFilled(box_min, box_max, IM_COL32((int)(col[0] * 255.0f), (int)(col[1] * 255.0f), (int)(col[2] * 255.0f), 255), 3.0f * Sc);
        dl->AddRect(box_min, box_max, U(stroke_color), 3.0f * Sc, 0, 1.0f);
        bool mhov = ImGui::IsMouseHoveringRect(box_min, box_max);
        if (mhov && ImGui::IsMouseClicked(0)) ImGui::OpenPopup((std::string(label) + "##cwpick").c_str());
        if (ImGui::BeginPopup((std::string(label) + "##cwpick").c_str()))
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, U(background_color));
            ImGui::PushStyleColor(ImGuiCol_Border, U(stroke_color));
            ImGui::ColorPicker3("##pk", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar);
            ImGui::PopStyleColor(2);
            ImGui::EndPopup();
        }
        return changed;
    }

    // ── slider ────────────────────────────────────────────────────
    static inline bool SliderScalarEx(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const float Sc = sc;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 item_size = ImVec2(frame_size.x * Sc, (frame_size.y + 15.0f) * Sc);
        const ImRect total_bb(pos, pos + item_size);
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        float v_min, v_max, v;
        if (data_type == ImGuiDataType_Float)
        {
            v = *(float*)p_data; v_min = *(float*)p_min; v_max = *(float*)p_max;
        }
        else
        {
            v = (float)*(int*)p_data; v_min = (float)*(int*)p_min; v_max = (float)*(int*)p_max;
        }

        float norm = (v_max - v_min != 0.0f) ? ClampF((v - v_min) / (v_max - v_min), 0.0f, 1.0f) : 0.0f;

        bool hovered = ImGui::IsItemHovered();
        bool changed = false;

        if (hovered || ImGui::IsItemActive())
        {
            if (ImGui::IsMouseDown(0))
            {
                float vpos = (io.MousePos.x - total_bb.Min.x) / total_bb.GetWidth();
                vpos = ClampF(vpos, 0.0f, 1.0f);
                float nv = v_min + vpos * (v_max - v_min);
                if (data_type == ImGuiDataType_Float)
                {
                    if (*(float*)p_data != nv) { *(float*)p_data = nv; changed = true; }
                }
                else
                {
                    int iv = (int)(nv + 0.5f) > (int)v_max ? (int)v_max : (int)(nv + 0.5f);
                    if (iv < (int)v_min) iv = (int)v_min;
                    if (*(int*)p_data != iv) { *(int*)p_data = iv; changed = true; }
                }
                v = nv;
                norm = ClampF((v - v_min) / (v_max - v_min), 0.0f, 1.0f);
            }
            if (io.MouseWheel != 0.0f)
            {
                float step = (v_max - v_min) * 0.02f;
                float nv = v + io.MouseWheel * step * 4.0f;
                nv = ClampF(nv, v_min, v_max);
                if (data_type == ImGuiDataType_Float) { *(float*)p_data = nv; }
                else { *(int*)p_data = (int)(nv + 0.5f); }
                changed = true;
                v = nv;
                norm = ClampF((v - v_min) / (v_max - v_min), 0.0f, 1.0f);
            }
        }

        // value box (top right)
        char value_buf[128];
        if (data_type == ImGuiDataType_Float)
            ImFormatString(value_buf, sizeof(value_buf), format, *(float*)p_data);
        else
            ImFormatString(value_buf, sizeof(value_buf), format, *(int*)p_data);
        const ImVec2 value_sz = CalcText(small_font, 17.0f * Sc, value_buf);
        const ImRect input_bb(ImVec2(total_bb.Max.x - 22.0f * Sc - value_sz.x, total_bb.Min.y + 7.0f * Sc),
                              ImVec2(total_bb.Max.x - 14.0f * Sc, total_bb.Min.y + 28.0f * Sc));

        const ImRect slider_bb(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + (frame_size.y - SLI_TRACK_H) * Sc),
                               ImVec2(total_bb.Max.x - 14.0f * Sc, total_bb.Min.y + frame_size.y * Sc));

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);

        // row background
        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);

        // label
        ImU32 labelCol = hovered ? U(text_color[0]) : U(text_color[1]);
        dl->AddText(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + 10.0f * Sc), labelCol, label);

        // value box
        dl->AddRectFilled(input_bb.Min, input_bb.Max, U(second_color), round_4 * Sc);
        ImGui::PushFont(small_font);
        dl->AddText(small_font, 17.0f * Sc, ImVec2(input_bb.Max.x - value_sz.x, input_bb.Min.y + (input_bb.GetHeight() - 17.0f * Sc) * 0.5f),
            hovered ? U(text_color[0]) : U(text_color[1]), value_buf);
        ImGui::PopFont();

        // track
        dl->AddRectFilled(slider_bb.Min, slider_bb.Max, U(second_color), round_360);

        const float fillW = slider_bb.GetWidth() * norm;
        const ImRect slider_fill(slider_bb.Min, ImVec2(slider_bb.Min.x + fillW, slider_bb.Max.y));

        // soft bloom over the filled part (follows the track geometry)
        if (fillW > 0.5f * Sc)
            SoftRectBloom(dl, slider_fill.Min, slider_fill.Max, ImVec4(main.x, main.y, main.z, 0.12f), 6.0f * Sc, round_360);

        // active track (subtle accent, not a laser)
        dl->AddRectFilled(slider_fill.Min, slider_fill.Max, U(ImVec4(main.x, main.y, main.z, 0.55f)), round_360);

        // grab (small crisp handle)
        const ImVec2 grab(slider_bb.Min.x + fillW, slider_bb.Min.y + slider_bb.GetHeight() * 0.5f);
        SoftCircleBloom(dl, grab + ImVec2(1.0f * Sc, 1.0f * Sc), SLI_HANDLE_R * Sc, ImVec4(0, 0, 0, 0.20f), 3.0f * Sc);
        dl->AddCircleFilled(grab, SLI_HANDLE_R * Sc, U(ImVec4(1, 1, 1, 0.95f)), 24);
        dl->AddCircleFilled(grab, SLI_HANDLE_INNER_R * Sc, U(text_color[2]), 16);

        return changed;
    }

    static inline bool sliderfloat(const char* label, float* v, float v_min, float v_max, const char* format)
    {
        return SliderScalarEx(label, ImGuiDataType_Float, v, &v_min, &v_max, format ? format : "%.3f");
    }

    static inline bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f")
    {
        return SliderScalarEx(label, ImGuiDataType_Float, v, &v_min, &v_max, format);
    }

    static inline bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d")
    {
        return SliderScalarEx(label, ImGuiDataType_S32, v, &v_min, &v_max, format);
    }

    // ── combo ─────────────────────────────────────────────────────
    struct ComboState { bool opened = false; float anim = 0.0f; };
    static std::map<ImGuiID, ComboState> combo_states;

    static inline bool Combo(const char* label, int* current_item, const char* const items[], int items_count)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const float Sc = sc;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect total_bb(pos, pos + ImVec2(frame_size.x * Sc, frame_size.y * Sc));
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        ComboState& state = combo_states[id];

        const char* preview_value = (current_item && *current_item >= 0 && *current_item < items_count) ? items[*current_item] : NULL;
        const ImVec2 preview_sz = preview_value ? CalcText(small_font, 17.0f * Sc, preview_value) : ImVec2(0, 0);

        const ImRect rect_bb(ImVec2(total_bb.Max.x - (22.0f + preview_sz.x) * Sc, total_bb.Min.y + 11.0f * Sc),
                             ImVec2(total_bb.Max.x - 14.0f * Sc, total_bb.Max.y - 11.0f * Sc));
        const ImRect arrow_bb(ImVec2(total_bb.Max.x - 25.0f * Sc, total_bb.Min.y + 20.0f * Sc),
                              ImVec2(total_bb.Max.x - 4.0f * Sc, total_bb.Max.y - 4.0f * Sc));
        const ImVec2 label_pos(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + 12.0f * Sc);

        if (hovered && ImGui::IsMouseClicked(0))
        {
            ImGui::OpenPopup(label);
            state.opened = true;
        }

        // row background
        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);

        // label
        ImU32 labelCol = hovered ? U(text_color[0]) : U(text_color[1]);
        dl->AddText(label_pos, labelCol, label);

        // value box
        float value_alpha = hovered ? 1.0f : 0.8f;
        dl->AddRectFilled(rect_bb.Min, rect_bb.Max, U(second_color), round_4 * Sc);
        if (preview_value)
        {
            ImGui::PushFont(small_font);
            dl->AddText(small_font, 17.0f * Sc,
                ImVec2(rect_bb.Min.x + (rect_bb.GetWidth() - preview_sz.x) * 0.5f,
                       rect_bb.Min.y + (rect_bb.GetHeight() - 17.0f * Sc) * 0.5f),
                U(ImVec4(1, 1, 1, value_alpha)), preview_value);
            ImGui::PopFont();
        }

        // arrow box
        dl->AddRectFilled(arrow_bb.Min, arrow_bb.Max, U(background_color), round_4 * Sc);
        if (icon_font)
        {
            ImGui::PushFont(icon_font);
            ImVec2 sz = CalcText(icon_font, 18.0f * Sc, "7");
            dl->AddText(icon_font, 18.0f * Sc,
                ImVec2(arrow_bb.Min.x + (arrow_bb.GetWidth() - sz.x) * 0.5f,
                       arrow_bb.Min.y + (arrow_bb.GetHeight() - sz.y) * 0.5f),
                hovered ? U(text_color[1]) : U(text_color[2]), "7");
            ImGui::PopFont();
        }

        bool value_changed = false;

        const bool popup_open = ImGui::IsPopupOpen(label);
        if (popup_open)
        {
            const float target_size = (float)items_count * 30.0f * Sc;
            const float rate = 1.0f - expf(-io.DeltaTime * 12.0f);
            state.anim += (1.0f - state.anim) * rate;
            if (state.anim > 0.999f) state.anim = 1.0f;
            ImGui::SetNextWindowSize(ImVec2(rect_bb.GetWidth(), target_size * EaseOutCubic(state.anim)));
            ImGui::SetNextWindowPos(ImVec2(rect_bb.Min.x, rect_bb.Max.y + 5.0f * Sc), ImGuiCond_Always);
        }

        if (popup_open && ImGui::BeginPopup(label))
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, U(background_color));
            ImGui::PushStyleColor(ImGuiCol_Header, U(second_color));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, U(ImVec4(1, 1, 1, 0.10f)));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, U(ImVec4(1, 1, 1, 0.14f)));
            ImGui::PushStyleColor(ImGuiCol_Border, U(stroke_color));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            for (int i = 0; i < items_count; i++)
            {
                if (i > 0) ImGui::Dummy(ImVec2(0.0f, 2.0f * Sc));
                ImGui::PushID(i);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 0.0f);
                if (ImGui::Selectable(items[i], *current_item == i, 0, ImVec2(rect_bb.GetWidth(), 28.0f * Sc)))
                {
                    *current_item = i;
                    value_changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(5);
            state.anim = value_changed ? 0.0f : state.anim;
            ImGui::EndPopup();
        }

        return value_changed;
    }

    // ── keybind ───────────────────────────────────────────────────
    struct KeybindState { bool listening = false; bool wait_release = false; };
    static std::map<ImGuiID, KeybindState> keybind_states;

    static inline bool Bind(const char* label, int* key, int* mode = nullptr)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect total_bb(pos, pos + ImVec2(frame_size.x * Sc, frame_size.y * Sc));
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        KeybindState& st = keybind_states[id];

        const ImRect button_bb(ImVec2(total_bb.Min.x + 10.0f * Sc, total_bb.Min.y + 20.0f * Sc),
                               ImVec2(total_bb.Max.x - 10.0f * Sc, total_bb.Max.y - 20.0f * Sc));

        if (hovered && ImGui::IsMouseClicked(0))
        {
            st.listening = true;
            st.wait_release = true;
        }

        if (st.listening)
        {
            if (st.wait_release)
            {
                bool stillDown = false;
                for (int k = VK_LBUTTON; k <= 0xFE; k++)
                {
                    if (GetAsyncKeyState(k) & 0x8000) { stillDown = true; break; }
                }
                if (!stillDown) st.wait_release = false;
            }
            else
            {
                for (int k = VK_LBUTTON; k <= 0xFE; k++)
                {
                    if (GetAsyncKeyState(k) & 0x8000)
                    {
                        if (k == VK_ESCAPE) *key = 0;
                        else *key = k;
                        st.listening = false;
                        break;
                    }
                }
            }
        }

        // row background
        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);

        // button (whole row area)
        dl->AddRectFilled(button_bb.Min, button_bb.Max, U(second_color), round_5 * Sc);
        if (st.listening)
            dl->AddRect(button_bb.Min, button_bb.Max, U(main_color), round_5 * Sc, 0, 1.0f);

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        const char* kn = st.listening ? "..." : VKName(*key);
        const ImVec2 key_sz = CalcText(small_font, 17.0f * Sc, kn);
        const bool hidden = (label[0] == '#');

        const ImRect mode_bb(ImVec2(total_bb.Max.x - 66.0f * Sc, total_bb.Min.y + 20.0f * Sc),
                             ImVec2(total_bb.Max.x - 14.0f * Sc, total_bb.Max.y - 20.0f * Sc));

        if (hidden)
        {
            centerText(button_bb.Min, button_bb.Max, kn, st.listening ? U(main) : U(text_color[1]), 17.0f * Sc, small_font);
        }
        else
        {
dl->AddText(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + (frame_size.y * Sc - ImGui::GetFontSize()) * 0.5f),
                hovered ? U(text_color[0]) : U(text_color[1]), label);
            const float kbRight = (mode ? mode_bb.Min.x - 8.0f * Sc : total_bb.Max.x - 14.0f * Sc);
            const ImRect key_bb(ImVec2(kbRight - key_sz.x - 16.0f * Sc, total_bb.Min.y + 20.0f * Sc),
                                ImVec2(kbRight, total_bb.Max.y - 20.0f * Sc));
            dl->AddRectFilled(key_bb.Min, key_bb.Max, U(second_color), round_5 * Sc);
            centerText(key_bb.Min, key_bb.Max, kn, st.listening ? U(main) : U(text_color[1]), 17.0f * Sc, small_font);
        }

        if (mode)
        {
            const char* mn = (*mode == 1) ? "TOGG" : "HOLD";
            bool mhov = ImGui::IsMouseHoveringRect(mode_bb.Min, mode_bb.Max);
            if (mhov && ImGui::IsMouseClicked(0)) *mode = (*mode == 0) ? 1 : 0;
            dl->AddRectFilled(mode_bb.Min, mode_bb.Max, U(second_color), round_5 * Sc);
            ImGui::PushFont(small_font);
            centerText(mode_bb.Min, mode_bb.Max, mn, mhov ? U(text_color[0]) : U(text_color[1]), 15.0f * Sc, small_font);
            ImGui::PopFont();
        }

        return false;
    }

    static inline void Tooltip(const char* text)
    {
        if (ImGui::IsItemHovered())
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, U(background_color));
            ImGui::PushStyleColor(ImGuiCol_Border, U(stroke_color));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f * sc);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(280.0f * sc);
            ImGui::TextColored(ImVec4(1, 1, 1, 0.8f), "%s", text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    static inline bool Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0))
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size = size_arg;
        if (size.x <= 0.0f) size.x = frame_size.x * Sc;
        if (size.y <= 0.0f) size.y = 30.0f * Sc;
        const ImRect bb(pos, pos + size);
        ImGui::ItemSize(bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        bool pressed = false;
        if (hovered && ImGui::IsMouseClicked(0)) pressed = true;

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        ImU32 frame_col = LerpU32(U(ImVec4(main.x, main.y, main.z, 0.30f)), U(main), Anim(id, hovered, 14.0f));
        ImU32 text_col = LerpU32(U(text_color[0]), GetDarkColor(main_color), Anim(id, hovered, 14.0f));

        dl->AddRectFilled(bb.Min, bb.Max, frame_col, round_5 * Sc);
        dl->AddRect(bb.Min, bb.Max, U(stroke_color), round_5 * Sc, 0, 1.0f);
        centerText(bb.Min, bb.Max, label, text_col);

        return pressed;
    }

    // ── color ─────────────────────────────────────────────────────
    static inline void RenderPickerBox(const char* label, float* col, int channels, ImGuiColorEditFlags flags)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return;
        const ImGuiID id = window->GetID(label);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect total_bb(pos, pos + ImVec2(frame_size.x * Sc, frame_size.y * Sc));
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, id)) return;

        bool hovered = ImGui::IsItemHovered();

        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);
        dl->AddText(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + (frame_size.y * Sc - ImGui::GetFontSize()) * 0.5f),
            hovered ? U(text_color[0]) : U(text_color[1]), label);

        const ImRect box(ImVec2(total_bb.Max.x - 62.0f * Sc, total_bb.Min.y + 16.0f * Sc),
                         ImVec2(total_bb.Max.x - 14.0f * Sc, total_bb.Min.y + 30.0f * Sc));
        ImU32 fill = IM_COL32((int)(col[0] * 255.0f), (int)(col[1] * 255.0f), (int)(col[2] * 255.0f), 255);
        if (channels == 4) fill = IM_COL32((int)(col[0] * 255.0f), (int)(col[1] * 255.0f), (int)(col[2] * 255.0f), (int)(col[3] * 255.0f));
        dl->AddRectFilled(box.Min, box.Max, fill, 3.0f * Sc);
        dl->AddRect(box.Min, box.Max, U(stroke_color), 3.0f * Sc, 0, 1.0f);

        const std::string popup_id = std::string(label) + "##cppk";
        bool box_hov = ImGui::IsMouseHoveringRect(box.Min, box.Max);
        if (box_hov && ImGui::IsMouseClicked(0)) ImGui::OpenPopup(popup_id.c_str());
        if (ImGui::BeginPopup(popup_id.c_str()))
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, U(background_color));
            ImGui::PushStyleColor(ImGuiCol_Border, U(stroke_color));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f * Sc);
            if (channels == 3)
                ImGui::ColorPicker3("##p", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar | flags);
            else
                ImGui::ColorPicker4("##p", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar | flags);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::EndPopup();
        }
    }

    static inline void ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0)
    {
        RenderPickerBox(label, col, 3, flags);
    }

    static inline void ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0)
    {
        RenderPickerBox(label, col, 4, flags);
    }

    // ── status ────────────────────────────────────────────────────
    static inline void Status(const char* label, bool active)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect total_bb(pos, pos + ImVec2(frame_size.x * Sc, frame_size.y * Sc));
        ImGui::ItemSize(total_bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(total_bb, ImGui::GetID(label))) return;

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        dl->AddRectFilled(total_bb.Min, total_bb.Max, U(background_color), round_5 * Sc);
        dl->AddText(ImVec2(total_bb.Min.x + 12.0f * Sc, total_bb.Min.y + (frame_size.y * Sc - ImGui::GetFontSize()) * 0.5f),
            active ? U(main) : U(ImVec4(1, 1, 1, 0.6f)), label);

        ImVec2 dotc = ImVec2(total_bb.Max.x - 32.0f * Sc, total_bb.Min.y + (frame_size.y * Sc) * 0.5f);
        ImU32 dot = active ? U(ImVec4(0.28f, 0.82f, 0.50f, 1.0f)) : U(ImVec4(1.0f, 0.31f, 0.41f, 1.0f));
        if (active) SoftCircleBloom(dl, dotc, 3.5f * Sc, ImVec4(0.28f, 0.82f, 0.50f, 0.55f), 5.0f * Sc);
        dl->AddCircleFilled(dotc, 3.5f * Sc, dot, 24);
    }

    // ── labels / headers ──────────────────────────────────────────
    static inline void labelsection(const char* text)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;
        ImGui::Dummy(ImVec2(0.0f, 6.0f * Sc));
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (small_font)
        {
            ImGui::PushFont(small_font);
            dl->AddText(small_font, 17.0f * Sc, ImVec2(p.x + 12.0f * Sc, p.y), U(ImVec4(1, 1, 1, 77.0f / 255.0f)), text);
            ImGui::PopFont();
        }
        else
        {
            dl->AddText(ImVec2(p.x + 12.0f * Sc, p.y), U(ImVec4(1, 1, 1, 77.0f / 255.0f)), text);
        }
        ImGui::Dummy(ImVec2(0.0f, 8.0f * Sc));
    }

    static inline void Header(const char* label)
    {
        labelsection(label);
    }

    static inline void ContentHeader(const char* /*label*/)
    {
        // Faint in-content section labels overlapped the subtab bar and first
        // card, so they're removed (the sidebar already labels the section).
    }

    static inline void BeginSection() {}
    static inline void EndSection() {}
    static inline void card(bool /*rounded*/ = true) {}

    // ── subtabs ───────────────────────────────────────────────────
    static inline bool ContentSubtab(const char* label, bool active, float& anim)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;
        const ImGuiID id = window->GetID(label);

        const float w = (24.0f + ImGui::CalcTextSize(label).x) * Sc;
        const float h = 30.0f * Sc;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect bb(pos, pos + ImVec2(w, h));
        ImGui::ItemSize(bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        anim = AnimEased(id, active || hovered, 12.0f);

        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        bool pressed = false;
        if (hovered && ImGui::IsMouseClicked(0)) pressed = true;

        if (active || hovered)
        {
            ImU32 bg = LerpU32(U(second_color), U(ImVec4(1, 1, 1, 45.0f / 255.0f)), anim);
            dl->AddRectFilled(pos, pos + ImVec2(w, h), bg, round_5 * Sc);
        }

        dl->AddText(ImVec2(pos.x + (w - ImGui::CalcTextSize(label).x) * 0.5f, pos.y + (h - ImGui::GetFontSize()) * 0.5f),
            active ? U(text_color[0]) : LerpU32(U(text_color[2]), U(text_color[1]), anim), label);

        return pressed;
    }

    static inline bool Subtab(const char* label, bool active, float width = 130.0f)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float Sc = sc;
        const ImGuiID id = window->GetID(label);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect bb(pos, pos + ImVec2(width * Sc, 32.0f * Sc));
        ImGui::ItemSize(bb, ImGui::GetStyle().ItemSpacing.y * 0.5f);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered = ImGui::IsItemHovered();
        bool pressed = false;
        if (hovered && ImGui::IsMouseClicked(0)) pressed = true;

        if (active || hovered)
            dl->AddRectFilled(bb.Min, bb.Max, U(second_color), round_5 * Sc);
        centerText(bb.Min, bb.Max, label, active ? U(text_color[0]) : U(text_color[1]));
        return pressed;
    }

    // ── sidebar tab (EXTERIUM-style) ──────────────────────────────
    static inline bool Tab(const char* label, const char* icon_glyph, bool active, float startX, float startY)
    {
        const float Sc = sc;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float tabW = 167.0f * Sc;
        const float tabH = 40.0f * Sc;
        const ImVec2 tab_min(startX, startY);
        const ImVec2 tab_max(startX + tabW, startY + tabH);
        const ImRect tab_bb(tab_min, tab_max);

        bool hovered = ImGui::IsMouseHoveringRect(tab_min, tab_max);
        bool pressed = false;
        if (hovered && ImGui::IsMouseClicked(0)) pressed = true;

        float anim = AnimEased(ImGui::GetID(label), active, 12.0f);
        const ImVec4 main = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);

        if (anim > 0.02f || hovered)
        {
            dl->AddRectFilled(tab_min, tab_max, U(second_color), round_5 * Sc);
        }

        // icon
        if (icon_big_font && icon_glyph && icon_glyph[0])
        {
            ImGui::PushFont(icon_big_font);
            ImVec2 isz = CalcText(icon_big_font, 23.0f * Sc, icon_glyph);
            dl->AddText(icon_big_font, 23.0f * Sc,
                ImVec2(tab_min.x + 10.0f * Sc, tab_min.y + (tabH - isz.y) * 0.5f),
                active ? U(main) : U(text_color[2]),
                icon_glyph);
            ImGui::PopFont();
        }

        // label
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(tab_min.x + 45.0f * Sc, tab_min.y + (tabH - ImGui::GetFontSize()) * 0.5f),
            active ? U(text_color[0]) : U(text_color[2]), label);

        return pressed;
    }

    static inline bool SidebarTab(int icon_id, const char* label, bool active, float startX, float startY, float /*width*/)
    {
        static const char* glyphs[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        const char* g = (icon_id >= 0 && icon_id < 9) ? glyphs[icon_id] : "1";
        return Tab(label, g, active, startX, startY);
    }

    // ── cards / collapsible sections ──────────────────────────────
    static inline bool CollapsibleSection(const char* label, float width, bool defaultOpen = true)
    {
        ImGuiID id = ImGui::GetID(label) ^ 0x5151u;
        ImGuiStorage* store = ImGui::GetStateStorage();
        bool open = store->GetBool(id, defaultOpen);
        const float Sc = sc;
        const float hdrH = SECTION_HEADER_HEIGHT * Sc;
        bool begin_child = open;

        if (begin_child)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, U(child_color));
            ImGui::PushStyleColor(ImGuiCol_Border, U(stroke_color));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, round_5 * Sc);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild(label, ImVec2(width, 0), true, ImGuiWindowFlags_NoScrollWithMouse);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##csh", ImVec2(width, hdrH));
        if (ImGui::IsItemClicked())
        {
            open = !open;
            store->SetBool(id, open);
        }
        bool hov = ImGui::IsItemHovered();

        float openAnim = EaseOutCubic(Anim(id ^ 0x99AB4u, open, 10.0f));

        // card header
        if (small_font)
        {
            ImGui::PushFont(small_font);
            dl->AddText(small_font, 17.0f * Sc, ImVec2(p.x + 12.0f * Sc, p.y + (hdrH - 17.0f * Sc) * 0.5f),
                open ? U(text_color[1]) : U(ImVec4(1, 1, 1, 0.55f)), label);
            ImGui::PopFont();
        }
        else
        {
            dl->AddText(ImVec2(p.x + 12.0f * Sc, p.y + (hdrH - ImGui::GetFontSize()) * 0.5f),
                open ? U(text_color[0]) : U(ImVec4(1, 1, 1, 0.55f)), label);
        }

        // chevron indicator
        float cx = p.x + width - 20.0f * Sc;
        float cy = p.y + hdrH * 0.5f;
        float cs = 5.0f * Sc;
        float rot = openAnim * (float)M_PI;
        float cosR = cosf(rot), sinR = sinf(rot);
        auto rotp = [&](float x, float y) { return ImVec2(cx + x * cosR - y * sinR, cy + x * sinR + y * cosR); };
        ImU32 chevCol = LerpU32(U(text_color[2]), U(main_color), openAnim * (hov ? 0.8f : 0.4f));
        dl->AddTriangleFilled(rotp(-cs, -cs), rotp(cs, 0), rotp(-cs, cs), chevCol);

        // row background behind header when collapsed
        if (!open)
        {
            dl->AddRectFilled(p, p + ImVec2(width, hdrH), U(background_color), round_5 * Sc);
        }

        if (open)
        {
            ImGui::Dummy(ImVec2(0.0f, CONTROL_GAP * Sc));
            return true;
        }
        return false;
    }

    static inline void CollapsibleEnd()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    // ── animated ambient background (EXTERIUM-style, subtle) ───────
    // Slowly-rising accent-tinted squares behind the content. Kept
    // very low opacity / low density so the menu itself stays the
    // dominant layer.
    struct BGState { ImVec2 pos; float velY; float sz; float rot; float tw; };
    inline std::vector<BGState> bg_particles;

    inline void ExteriumBG_Update(float w, float h)
    {
        static std::mt19937 rng{ std::random_device{}() };
        if (bg_particles.empty())
        {
            for (int i = 0; i < 26; i++)
            {
                BGState s;
                s.pos = ImVec2((float)(rng() % 10000) / 10000.0f * w, (float)(rng() % 10000) / 10000.0f * h);
                s.velY = -(35.0f + (float)(rng() % 5500) / 100.0f);
                s.sz = 6.0f + (float)(rng() % 1700) / 100.0f;
                s.rot = (float)(rng() % 628) / 100.0f;
                s.tw = (float)(rng() % 1000) / 1000.0f;
                bg_particles.push_back(s);
            }
        }
        const float dt = ImGui::GetIO().DeltaTime;
        const float t = (float)ImGui::GetTime();
        for (auto& s : bg_particles)
        {
            s.pos.x += sinf(t * 0.5f + s.tw) * 4.0f * dt;
            s.pos.y += s.velY * dt;
            s.rot += 0.35f * dt;
            if (s.pos.y < -s.sz - 24.0f)
            {
                s.pos.y = h + s.sz + 24.0f;
                s.pos.x = (float)(rng() % 10000) / 10000.0f * w;
            }
        }
    }

    inline void ExteriumBG_Render(ImDrawList* dl, const ImVec2& origin, const ImVec2& size, float menuAlpha)
    {
        if (bg_particles.empty()) return;
        const ImVec4 mc = ImVec4(main_color.Value.x, main_color.Value.y, main_color.Value.z, 1.0f);
        const ImU32 accent = U(mc);
        const float t = (float)ImGui::GetTime();
        dl->PushClipRect(origin, origin + size, true);
        for (const auto& s : bg_particles)
        {
            float pulse = 0.75f + 0.25f * sinf(t * 0.9f + s.tw);
            int a = (int)(0.09f * menuAlpha * 255.0f * pulse);
            if (a <= 1) continue;
            ImVec2 c = origin + s.pos;
            float cr = cosf(s.rot), sr = sinf(s.rot);
            ImVec2 cpts[4];
            cpts[0] = ImVec2(c.x - s.sz * cr + s.sz * sr, c.y - s.sz * sr - s.sz * cr);
            cpts[1] = ImVec2(c.x + s.sz * cr + s.sz * sr, c.y - s.sz * sr + s.sz * cr);
            cpts[2] = ImVec2(c.x + s.sz * cr - s.sz * sr, c.y + s.sz * sr + s.sz * cr);
            cpts[3] = ImVec2(c.x - s.sz * cr - s.sz * sr, c.y + s.sz * sr - s.sz * cr);
            dl->AddConvexPolyFilled(cpts, 4, (accent & 0x00FFFFFFu) | ((ImU32)a << 24));
        }
        dl->PopClipRect();
    }

    // ── global style ──────────────────────────────────────────────
    static inline void ApplyStyle(ImVec4 /*accent*/, ImVec4 /*bg*/, ImVec4 /*panel*/)
    {
        const float cx = 230.0f / 255.0f, cy = 134.0f / 255.0f, cz = 224.0f / 255.0f;

        P.accent    = ImVec4(cx, cy, cz, 1.0f);
        P.accent2   = ImVec4(0.73f, 0.33f, 0.83f, 1.0f);
        P.accentHover = ImVec4(0.95f, 0.63f, 0.93f, 1.0f);
        P.accentDim = ImVec4(cx, cy, cz, 0.35f);
        P.accentSoft = ImVec4(cx, cy, cz, 0.12f);
        P.accentGlow = ImVec4(cx, cy, cz, 0.25f);
        P.glow      = P.accentGlow;
        P.glowPurple = ImVec4(0.55f, 0.25f, 0.95f, 0.22f);
        P.divider   = ImVec4(1, 1, 1, 0.06f);
        P.line      = ImVec4(1, 1, 1, 0.06f);
        P.card      = child_color;
        P.cardHov   = ImVec4(26.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 1.0f);
        P.surface   = ImVec4(13.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
        P.surfaceAlt = ImVec4(17.0f / 255.0f, 17.0f / 255.0f, 17.0f / 255.0f, 1.0f);
        P.surfaceHi = ImVec4(23.0f / 255.0f, 23.0f / 255.0f, 23.0f / 255.0f, 1.0f);
        P.borderDim = stroke_color;
        P.track     = second_color;
        P.shadow    = ImVec4(0, 0, 0, 0.3f);
        P.textStrong = ImVec4(1, 1, 1, 1.0f);
        P.text      = ImVec4(1, 1, 1, 0.78f);
        P.textMid   = ImVec4(1, 1, 1, 0.55f);
        P.textDim   = ImVec4(1, 1, 1, 0.25f);
        P.disabled  = ImVec4(1, 1, 1, 0.12f);
        P.good      = ImVec4(0.28f, 0.82f, 0.50f, 1.0f);
        P.bad       = ImVec4(1.0f, 0.31f, 0.41f, 1.0f);

        // global style for the few stock widgets that still render
        ImGuiStyle& g = ImGui::GetStyle();
        ImVec4* c = g.Colors;
        c[ImGuiCol_Text]                  = ImVec4(1, 1, 1, 0.85f);
        c[ImGuiCol_TextDisabled]          = ImVec4(1, 1, 1, 0.30f);
        c[ImGuiCol_WindowBg]              = winbg_color;
        c[ImGuiCol_ChildBg]               = child_color;
        c[ImGuiCol_PopupBg]               = ImVec4(13.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 0.94f);
        c[ImGuiCol_Border]                = stroke_color;
        c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg]               = second_color;
        c[ImGuiCol_FrameBgHovered]        = ImVec4(1, 1, 1, 0.10f);
        c[ImGuiCol_FrameBgActive]         = ImVec4(1, 1, 1, 0.14f);
        c[ImGuiCol_TitleBg]               = child_color;
        c[ImGuiCol_TitleBgActive]         = child_color;
        c[ImGuiCol_CheckMark]             = ImVec4(cx, cy, cz, 1.0f);
        c[ImGuiCol_SliderGrab]            = ImVec4(1, 1, 1, 1.0f);
        c[ImGuiCol_SliderGrabActive]      = ImVec4(cx, cy, cz, 1.0f);
        c[ImGuiCol_Button]                = ImVec4(cx, cy, cz, 0.30f);
        c[ImGuiCol_ButtonHovered]         = ImVec4(cx, cy, cz, 0.55f);
        c[ImGuiCol_ButtonActive]          = ImVec4(cx, cy, cz, 0.70f);
        c[ImGuiCol_Header]                = ImVec4(1, 1, 1, 0.08f);
        c[ImGuiCol_HeaderHovered]         = ImVec4(cx, cy, cz, 0.30f);
        c[ImGuiCol_HeaderActive]          = ImVec4(cx, cy, cz, 0.45f);
        c[ImGuiCol_Separator]             = ImVec4(1, 1, 1, 0.08f);
        c[ImGuiCol_SeparatorHovered]      = ImVec4(cx, cy, cz, 0.40f);
        c[ImGuiCol_SeparatorActive]       = ImVec4(cx, cy, cz, 0.55f);
        c[ImGuiCol_ScrollbarBg]           = scroll_bg_col;
        c[ImGuiCol_ScrollbarGrab]         = ImVec4(1, 1, 1, 0.18f);
        c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1, 1, 1, 0.30f);
        c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(cx, cy, cz, 0.55f);
        c[ImGuiCol_Tab]                   = ImVec4(1, 1, 1, 0.03f);
        c[ImGuiCol_TabHovered]            = ImVec4(1, 1, 1, 0.08f);
        c[ImGuiCol_TabActive]             = second_color;
        c[ImGuiCol_TabUnfocused]          = ImVec4(1, 1, 1, 0.03f);
        c[ImGuiCol_TabUnfocusedActive]    = ImVec4(1, 1, 1, 0.05f);
        c[ImGuiCol_TextSelectedBg]        = ImVec4(cx, cy, cz, 0.30f);
        c[ImGuiCol_NavHighlight]          = ImVec4(cx, cy, cz, 0.5f);
        c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.5f);

        g.WindowPadding   = ImVec2(10, 10);
        g.FramePadding    = ImVec2(6, 9);
        g.ItemSpacing     = ImVec2(10, 5);
        g.ItemInnerSpacing = ImVec2(8, 8);
        g.WindowBorderSize = 1.0f;
        g.WindowRounding  = 0.0f;
        g.ChildRounding   = round_5;
        g.FrameRounding   = round_5;
        g.PopupRounding   = round_5;
        g.ScrollbarRounding = round_4;
        g.GrabRounding    = round_5;
        g.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    }

    static inline float GetAnimationSpeed(ImGuiIO& io) { return io.DeltaTime * 12.0f; }
}