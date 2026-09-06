#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "properties_window.h"
#include "lua_editor.h"
#include "widgets.h"

#include "../imgui/imgui.h"

#include "../../features/explorer/ValueBase.h"
#include "../../features/scriptbytecode/scriptbytecode.h"
#include "../../rbx/SDK/sdk.h"

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace gui
{
    static const ImVec4 border_outer = ImVec4(0.13f, 0.13f, 0.13f, 1.f);
    static const ImVec4 border_inner = ImVec4(0.18f, 0.18f, 0.18f, 1.f);

    static bool is_script_class(const std::string& cls)
    {
        return ScriptBytecode::IsScriptClass(cls);
    }

    static std::string sanitize_filename(const std::string& in)
    {
        std::string out;
        for (char c : in)
            out += std::isalnum((unsigned char)c) ? c : '_';
        return out.empty() ? "instance" : out;
    }

    static bool action_button(const char* label, ImVec2 min, ImVec2 max)
    {
        ImGui::PushID(label);
        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton("##act", ImVec2(max.x - min.x, max.y - min.y));
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();
        bool clicked = ImGui::IsItemClicked();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.08f, 1.f)));
        if (held)
            draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.10f)));
        else if (hovered)
            draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.06f)));
        draw->AddRect(min, max, ImGui::GetColorU32(border_inner));

        ImVec2 ts = ImGui::CalcTextSize(label);
        widgets::text_outlined(draw, ImVec2(min.x + (max.x - min.x - ts.x) * 0.5f, min.y + (max.y - min.y - ts.y) * 0.5f),
            ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.f)), label);

        ImGui::PopID();
        return clicked;
    }

    static void info_row(ImDrawList* dl, ImVec2 pos, float w, float h, const char* label, const std::string& value, bool sep)
    {
        widgets::text_outlined(dl, ImVec2(pos.x + 10.f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(140, 140, 140, 255), label);
        widgets::text_outlined(dl, ImVec2(pos.x + 80.f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(225, 225, 225, 255), value.c_str());
        if (sep)
            dl->AddLine(ImVec2(pos.x, pos.y + h), ImVec2(pos.x + w, pos.y + h), ImGui::GetColorU32(border_inner));
    }

    static void render_decompiled_window(bool* open, ImVec2 prop_pos, ImVec2 prop_size, editor::lua_editor& ed)
    {
        if (!open || !*open)
            return;

        constexpr float title_h = 26.f;
        constexpr float margin = 3.f;
        constexpr float gap = 6.f;

        static float win_w = 560.f;
        static float win_h = 340.f;

        ImGui::SetNextWindowPos(ImVec2(prop_pos.x, prop_pos.y + prop_size.y + gap), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(360.f, 180.f), ImVec2(FLT_MAX, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Border, border_outer);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
        bool visible = ImGui::Begin("##decompiled_window", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            0 | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        if (!visible)
        {
            ImGui::End();
            return;
        }

        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        win_w = ws.x;
        win_h = ws.y;
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + title_h), IM_COL32(20, 20, 20, 255));
        ImVec2 title_ts = ImGui::CalcTextSize("decompiled");
        widgets::text_outlined(draw, ImVec2(wp.x + (ws.x - title_ts.x) * 0.5f, wp.y + (title_h - title_ts.y) * 0.5f),
            IM_COL32(230, 230, 230, 255), "decompiled");

        ImVec2 xsz = ImGui::CalcTextSize("X");
        ImVec2 xmin(wp.x + ws.x - xsz.x - 14.f, wp.y);
        ImGui::SetCursorScreenPos(xmin);
        ImGui::InvisibleButton("##decompiled_close", ImVec2(xsz.x + 14.f, title_h));
        bool xhov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            *open = false;
        widgets::text_outlined(draw, ImVec2(xmin.x + 7.f, wp.y + (title_h - xsz.y) * 0.5f),
            xhov ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255), "X");

        float body_top = title_h + margin;
        float body_h = ws.y - body_top - margin;
        float body_w = ws.x - margin * 2.f;

        ImGui::SetCursorPos(ImVec2(margin, body_top));
        ImGui::PushStyleColor(ImGuiCol_Border, border_inner);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.06f, 1.f));
        ed.readonly = true;
        ed.show_scrollbar = false;
        ed.render("##decompiled_ed", ImVec2(body_w, body_h));
        ImGui::PopStyleColor(2);

        ImGui::End();
    }

    void render_properties_window(bool* open, ImVec2 anchor_pos, ImVec2 anchor_size, const properties_target& target)
    {
        if (!open || !*open)
            return;

        static editor::lua_editor decompiled_ed;
        static std::uint64_t last_addr = 0;
        static bool show_decompiled = false;
        static std::vector<std::uint8_t> bc_bytes;
        static std::uint64_t bc_for = 0;
        static std::string bc_status;

        bool is_script = is_script_class(target.cls);
        bool is_hum = (target.cls == "Humanoid");
        bool is_value = ValueBase::IsClass(target.cls);

        static char val_buf[256] = "";
        static char val_x[32] = "";
        static char val_y[32] = "";
        static char val_z[32] = "";
        static char obj_buf[32] = "";
        static bool val_bool = false;
        static char hum_hp[32] = "";
        static char hum_ws[32] = "";
        static char hum_jp[32] = "";

        auto sync_value_bufs = [&]() {
            val_buf[0] = 0;
            val_x[0] = val_y[0] = val_z[0] = 0;
            obj_buf[0] = 0;
            val_bool = false;
            hum_hp[0] = hum_ws[0] = hum_jp[0] = 0;
            if (!target.address)
                return;
            if (is_value)
            {
                if (target.cls == "BoolValue")
                    val_bool = ValueBase::GetBool(target.address);
                else if (target.cls == "IntValue")
                    std::snprintf(val_buf, sizeof(val_buf), "%d", ValueBase::GetInt(target.address));
                else if (target.cls == "NumberValue")
                    std::snprintf(val_buf, sizeof(val_buf), "%.6g", ValueBase::GetNumber(target.address));
                else if (target.cls == "StringValue")
                {
                    std::string s = ValueBase::GetString(target.address);
                    if (s.size() >= sizeof(val_buf))
                        s.resize(sizeof(val_buf) - 1);
                    std::memcpy(val_buf, s.c_str(), s.size() + 1);
                }
                else if (target.cls == "ObjectValue")
                {
                    std::snprintf(obj_buf, sizeof(obj_buf), "0x%llX",
                        (unsigned long long)ValueBase::GetObject(target.address));
                }
                else if (target.cls == "Vector3Value")
                {
                        Vectors::Vector3 v = ValueBase::GetVector3(target.address);
                    std::snprintf(val_x, sizeof(val_x), "%.6g", v.x);
                    std::snprintf(val_y, sizeof(val_y), "%.6g", v.y);
                    std::snprintf(val_z, sizeof(val_z), "%.6g", v.z);
                }
            }
            if (is_hum)
            {
                RobloxInstance hu(target.address);
                std::snprintf(hum_hp, sizeof(hum_hp), "%.1f", hu.Health());
                std::snprintf(hum_ws, sizeof(hum_ws), "%.1f", hu.GetWalkspeed());
                std::snprintf(hum_jp, sizeof(hum_jp), "%.1f", hu.GetJumpPower());
            }
        };

        if (target.address != last_addr)
        {
            show_decompiled = false;
            decompiled_ed.set_text("");
            bc_bytes.clear();
            bc_for = 0;
            bc_status.clear();
            last_addr = target.address;
            sync_value_bufs();
        }

        constexpr float title_h = 26.f;
        constexpr float margin = 3.f;
        constexpr float gap = 6.f;
        constexpr float header_h = 22.f;
        constexpr float info_row_h = 24.f;
        constexpr float btn_h = 28.f;
        constexpr float gap_y = 6.f;
        constexpr float prop_w = 400.f;
        constexpr float edit_row_h = 28.f;

        int info_rows = 4;
        float info_h = header_h + info_row_h * (float)info_rows;
        float actions_h = is_script ? (header_h + btn_h + 10.f) : 0.f;
        int value_rows = 0;
        if (is_value)
        {
            if (target.cls == "Vector3Value")
                value_rows = 3;
            else if (target.cls == "ObjectValue")
                value_rows = 2;
            else
                value_rows = 1;
        }
        else if (is_hum)
            value_rows = 3;
        float value_h = value_rows > 0 ? (header_h + edit_row_h * (float)value_rows + 8.f) : 0.f;
        float fit_h = title_h + margin + info_h + margin;
        if (value_h > 0.f)
            fit_h += gap_y + value_h;
        if (is_script)
            fit_h += gap_y + actions_h;

        ImGui::SetNextWindowPos(ImVec2(anchor_pos.x + anchor_size.x + gap, anchor_pos.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(prop_w, fit_h), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Border, border_outer);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
        bool visible = ImGui::Begin("##properties_window", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            0 | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        if (!visible)
        {
            ImGui::End();
            return;
        }

        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + title_h), IM_COL32(20, 20, 20, 255));
        ImVec2 title_ts = ImGui::CalcTextSize("properties");
        widgets::text_outlined(draw, ImVec2(wp.x + (ws.x - title_ts.x) * 0.5f, wp.y + (title_h - title_ts.y) * 0.5f),
            IM_COL32(230, 230, 230, 255), "properties");

        ImVec2 xsz = ImGui::CalcTextSize("X");
        ImVec2 xmin(wp.x + ws.x - xsz.x - 14.f, wp.y);
        ImGui::SetCursorScreenPos(xmin);
        ImGui::InvisibleButton("##properties_close", ImVec2(xsz.x + 14.f, title_h));
        bool xhov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            *open = false;
            show_decompiled = false;
        }
        widgets::text_outlined(draw, ImVec2(xmin.x + 7.f, wp.y + (title_h - xsz.y) * 0.5f),
            xhov ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255), "X");

        float body_top = title_h + margin;
        float body_h = ws.y - body_top - margin;
        float body_w = ws.x - margin * 2.f;

        ImGui::SetCursorPos(ImVec2(margin, body_top));
        ImGui::PushStyleColor(ImGuiCol_Border, border_inner);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.06f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::BeginChild("##properties_body", ImVec2(body_w, body_h), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        char addr_buf[40];
        std::snprintf(addr_buf, sizeof(addr_buf), "0x%llX", (unsigned long long)target.address);

        int kids = target.children;
        if (target.address)
        {
            RobloxInstance inst(target.address);
            kids = (int)inst.GetChildren().size();
        }
        char children_buf[16];
        std::snprintf(children_buf, sizeof(children_buf), "%d", kids);

        ImGui::SetCursorPos(ImVec2(0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Border, border_inner);
        ImGui::BeginChild("##properties_info", ImVec2(body_w, info_h), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImVec2 ip = ImGui::GetWindowPos();
            ImVec2 isz = ImGui::GetWindowSize();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 hts = ImGui::CalcTextSize("info");
            widgets::text_outlined(dl, ImVec2(ip.x + (isz.x - hts.x) * 0.5f, ip.y + (header_h - hts.y) * 0.5f), IM_COL32(210, 210, 210, 255), "info");
            dl->AddLine(ImVec2(ip.x, ip.y + header_h), ImVec2(ip.x + isz.x, ip.y + header_h), IM_COL32(255, 255, 255, 35));

            float y = ip.y + header_h;
            info_row(dl, ImVec2(ip.x, y), isz.x, info_row_h, "class", target.cls, true);
            y += info_row_h;
            info_row(dl, ImVec2(ip.x, y), isz.x, info_row_h, "path", target.path, true);
            y += info_row_h;
            info_row(dl, ImVec2(ip.x, y), isz.x, info_row_h, "address", addr_buf, true);
            y += info_row_h;
            info_row(dl, ImVec2(ip.x, y), isz.x, info_row_h, "children", children_buf, false);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        float next_y = info_h;
        if (value_h > 0.f && target.address)
        {
            next_y += gap_y;
            ImGui::SetCursorPos(ImVec2(0.f, next_y));
            ImGui::PushStyleColor(ImGuiCol_Border, border_inner);
            ImGui::BeginChild("##properties_value", ImVec2(body_w, value_h), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 vp = ImGui::GetWindowPos();
                ImVec2 vsz = ImGui::GetWindowSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const char* hdr = is_hum ? "humanoid" : "value";
                ImVec2 hts = ImGui::CalcTextSize(hdr);
                widgets::text_outlined(dl, ImVec2(vp.x + (vsz.x - hts.x) * 0.5f, vp.y + (header_h - hts.y) * 0.5f), IM_COL32(210, 210, 210, 255), hdr);
                dl->AddLine(ImVec2(vp.x, vp.y + header_h), ImVec2(vp.x + vsz.x, vp.y + header_h), IM_COL32(255, 255, 255, 35));

                constexpr float pad = 8.f;
                float field_w = vsz.x - pad * 2.f;
                float y = header_h + 4.f;

                auto edit_row = [&](const char* label, char* buf, int buf_sz, auto apply) {
                    ImGui::SetCursorScreenPos(ImVec2(vp.x + pad, vp.y + y));
                    ImGui::PushItemWidth(field_w);
                    widgets::input_text(label, buf, buf_sz);
                    ImGui::PopItemWidth();
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        apply();
                    y += edit_row_h;
                };

                if (is_value && target.cls == "BoolValue")
                {
                    ImGui::SetCursorScreenPos(ImVec2(vp.x + pad, vp.y + y + 2.f));
                    if (!ImGui::IsAnyItemActive())
                        val_bool = ValueBase::GetBool(target.address);
                    if (widgets::checkbox("Value", &val_bool))
                        ValueBase::SetBool(target.address, val_bool);
                }
                else if (is_value && target.cls == "IntValue")
                {
                    if (!ImGui::IsAnyItemActive())
                        std::snprintf(val_buf, sizeof(val_buf), "%d", ValueBase::GetInt(target.address));
                    edit_row("Value", val_buf, sizeof(val_buf), [&]() {
                        ValueBase::SetInt(target.address, (std::int32_t)std::strtol(val_buf, nullptr, 10));
                    });
                }
                else if (is_value && target.cls == "NumberValue")
                {
                    if (!ImGui::IsAnyItemActive())
                        std::snprintf(val_buf, sizeof(val_buf), "%.6g", ValueBase::GetNumber(target.address));
                    edit_row("Value", val_buf, sizeof(val_buf), [&]() {
                        ValueBase::SetNumber(target.address, std::strtod(val_buf, nullptr));
                    });
                }
                else if (is_value && target.cls == "StringValue")
                {
                    if (!ImGui::IsAnyItemActive())
                    {
                        std::string s = ValueBase::GetString(target.address);
                        if (s.size() >= sizeof(val_buf))
                            s.resize(sizeof(val_buf) - 1);
                        std::memcpy(val_buf, s.c_str(), s.size() + 1);
                    }
                    edit_row("Value", val_buf, sizeof(val_buf), [&]() {
                        ValueBase::SetString(target.address, val_buf);
                    });
                }
                else if (is_value && target.cls == "ObjectValue")
                {
                    std::string cur = ValueBase::Format(target.address, target.cls);
                    info_row(dl, ImVec2(vp.x, vp.y + y), vsz.x, info_row_h, "current", cur, true);
                    y += info_row_h;
                    if (!ImGui::IsAnyItemActive())
                    {
                        std::snprintf(obj_buf, sizeof(obj_buf), "0x%llX",
                            (unsigned long long)ValueBase::GetObject(target.address));
                    }
                    edit_row("addr", obj_buf, sizeof(obj_buf), [&]() {
                        const char* p = obj_buf;
                        while (*p == ' ') ++p;
                        if (std::strcmp(p, "nil") == 0 || std::strcmp(p, "0") == 0 || std::strcmp(p, "0x0") == 0)
                            ValueBase::SetObject(target.address, 0);
                        else
                            ValueBase::SetObject(target.address, std::strtoull(p, nullptr, 0));
                    });
                }
                else if (is_value && target.cls == "Vector3Value")
                {
                    if (!ImGui::IsAnyItemActive())
                    {
                    Vectors::Vector3 v = ValueBase::GetVector3(target.address);
                        std::snprintf(val_x, sizeof(val_x), "%.6g", v.x);
                        std::snprintf(val_y, sizeof(val_y), "%.6g", v.y);
                        std::snprintf(val_z, sizeof(val_z), "%.6g", v.z);
                    }
                    auto apply_v3 = [&]() {
                        Vectors::Vector3 v{};
                        v.x = (float)std::strtod(val_x, nullptr);
                        v.y = (float)std::strtod(val_y, nullptr);
                        v.z = (float)std::strtod(val_z, nullptr);
                        ValueBase::SetVector3(target.address, v);
                    };
                    edit_row("X", val_x, sizeof(val_x), apply_v3);
                    edit_row("Y", val_y, sizeof(val_y), apply_v3);
                    edit_row("Z", val_z, sizeof(val_z), apply_v3);
                }
                else if (is_hum)
                {
                    RobloxInstance hu(target.address);
                    if (!ImGui::IsAnyItemActive())
                    {
                        std::snprintf(hum_hp, sizeof(hum_hp), "%.1f", hu.Health());
                        std::snprintf(hum_ws, sizeof(hum_ws), "%.1f", hu.GetWalkspeed());
                        std::snprintf(hum_jp, sizeof(hum_jp), "%.1f", hu.GetJumpPower());
                    }
                    edit_row("health", hum_hp, sizeof(hum_hp), [&]() {
                        Memory->write<float>(target.address + Offsets::Humanoid::Health, (float)std::strtod(hum_hp, nullptr));
                    });
                    edit_row("walk", hum_ws, sizeof(hum_ws), [&]() {
                        RobloxInstance(target.address).SetWalkspeed((float)std::strtod(hum_ws, nullptr));
                    });
                    edit_row("jump", hum_jp, sizeof(hum_jp), [&]() {
                        RobloxInstance(target.address).SetJumpPower((float)std::strtod(hum_jp, nullptr));
                    });
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            next_y += value_h;
        }

        if (is_script)
        {
            ImGui::SetCursorPos(ImVec2(0.f, next_y + gap_y));
            ImGui::PushStyleColor(ImGuiCol_Border, border_inner);
            ImGui::BeginChild("##properties_actions", ImVec2(body_w, actions_h), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 ap = ImGui::GetWindowPos();
                ImVec2 asz = ImGui::GetWindowSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 hts = ImGui::CalcTextSize("actions");
                widgets::text_outlined(dl, ImVec2(ap.x + (asz.x - hts.x) * 0.5f, ap.y + (header_h - hts.y) * 0.5f), IM_COL32(210, 210, 210, 255), "actions");
                dl->AddLine(ImVec2(ap.x, ap.y + header_h), ImVec2(ap.x + asz.x, ap.y + header_h), IM_COL32(255, 255, 255, 35));

                const char* labels[] = { "dump", "save", "copy hex", "decompile" };
                constexpr int count = 4;
                constexpr float pad = 8.f;
                constexpr float btn_gap = 6.f;
                float total_w = asz.x - pad * 2.f;
                float bw = (total_w - btn_gap * (count - 1)) / (float)count;
                float by = ap.y + header_h + (actions_h - header_h - btn_h) * 0.5f;

                auto ensure_dump = [&]() {
                    if (bc_for == target.address && !bc_bytes.empty())
                        return;
                    bc_bytes.clear();
                    bc_for = target.address;
                    bc_status.clear();
                    if (!ScriptBytecode::ReadRaw(target.address, target.cls, bc_bytes))
                    {
                        bc_status = "no bytecode object";
                        return;
                    }
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%zu bytes (compressed Luau bytecode)", bc_bytes.size());
                    bc_status = buf;
                };

                for (int i = 0; i < count; ++i)
                {
                    ImVec2 min(ap.x + pad + i * (bw + btn_gap), by);
                    ImVec2 max(min.x + bw, min.y + btn_h);
                    if (action_button(labels[i], min, max))
                    {
                        if (i == 0)
                        {
                            ensure_dump();
                            show_decompiled = false;
                        }
                        else if (i == 1)
                        {
                            ensure_dump();
                            if (!bc_bytes.empty())
                            {
                                std::string fname = sanitize_filename(target.name) + ".luauc";
                                std::ofstream f(fname, std::ios::binary);
                                if (f)
                                {
                                    f.write((const char*)bc_bytes.data(), (std::streamsize)bc_bytes.size());
                                    bc_status = "saved: " + fname;
                                }
                                else
                                    bc_status = "failed to write file";
                            }
                        }
                        else if (i == 2)
                        {
                            ensure_dump();
                            std::string hex;
                            hex.reserve(bc_bytes.size() * 2);
                            static const char* d = "0123456789ABCDEF";
                            for (unsigned char b : bc_bytes)
                            {
                                hex += d[b >> 4];
                                hex += d[b & 0xF];
                            }
                            ImGui::SetClipboardText(hex.c_str());
                        }
                        else if (i == 3)
                        {
                            ensure_dump();
                            std::vector<std::uint8_t> raw(bc_bytes.begin(), bc_bytes.end());
                            bc_status = "decompile...";
                            std::string src = ScriptBytecode::Decompile(raw, target.name.c_str());
                            decompiled_ed.set_text(src);
                            show_decompiled = true;
                            if (src.find("via Fission") != std::string::npos)
                                bc_status = "fission ok";
                            else if (src.find("built-in lifter") != std::string::npos)
                                bc_status = "lifter (no fission)";
                            else if (src.find("not Luau") != std::string::npos)
                                bc_status = "no luau bytecode";
                            else
                                bc_status = "decompiled";
                        }
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        ImVec2 prop_pos = wp;
        ImVec2 prop_size = ws;
        ImGui::End();

        if (is_script && show_decompiled)
            render_decompiled_window(&show_decompiled, prop_pos, prop_size, decompiled_ed);
    }
}
