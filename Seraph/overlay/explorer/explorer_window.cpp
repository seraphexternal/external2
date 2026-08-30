#include "explorer_window.h"
#include "properties_window.h"
#include "icons.h"
#include "widgets.h"

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include "../../features/explorer/ValueBase.h"
#include "../../rbx/SDK/sdk.h"
#include "../../rbx/globals/globals.h"

#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
#include "../../features/explorer/ExplorerTree.h"
#include "../../features/explorer/ExplorerSearch.h"

    static const ImVec4 border_outer = ImVec4(0.13f, 0.13f, 0.13f, 1.f);
    static const ImVec4 border_inner = ImVec4(0.18f, 0.18f, 0.18f, 1.f);

    struct ctx_target
    {
        std::uint64_t address = 0;
        std::string name;
        std::string cls;
        std::string path;
        int children = 0;
        bool valid = false;
    };

    void fill_prop(gui::properties_target& out, std::uint64_t addr, const std::string& name, const std::string& cls, const std::string& path, int children)
    {
        out.name = name;
        out.cls = cls;
        out.path = path;
        out.address = addr;
        out.children = children;
        if (addr && children <= 0)
        {
            RobloxInstance inst(addr);
            out.children = (int)inst.GetChildren().size();
        }
    }

    void fill_prop(gui::properties_target& out, const ctx_target& t)
    {
        fill_prop(out, t.address, t.name, t.cls, t.path, t.children);
    }

    void open_ctx(ctx_target& ctx, std::uint64_t addr, const std::string& name, const std::string& cls, const std::string& path, ImVec2& ctx_pos)
    {
        ctx.address = addr;
        ctx.name = name;
        ctx.cls = cls;
        ctx.path = path;
        ctx.children = 0;
        if (addr)
        {
            RobloxInstance inst(addr);
            ctx.children = (int)inst.GetChildren().size();
        }
        ctx.valid = true;
        ctx_pos = ImGui::GetMousePos();
        ImGui::OpenPopup("##ex_ctx_menu");
    }

    void select_node(std::uint64_t& selected_addr, gui::properties_target& prop, bool& properties_open,
        std::uint64_t addr, const std::string& name, const std::string& cls, const std::string& path, int children)
    {
        selected_addr = addr;
        fill_prop(prop, addr, name, cls, path, children);
        properties_open = true;
    }

    void render_node(Node& node, int depth, std::uint64_t& selected_addr, ctx_target& ctx, ImVec2& ctx_pos,
        gui::properties_target& prop, bool& properties_open)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr float row_h = 22.f;
        constexpr float indent = 15.f;
        constexpr float icon_size = 14.f;

        bool has_children = node.loaded ? !node.children.empty() : node.has_kids;
        bool selected = (selected_addr == node.address);

        ImVec2 row_pos = ImGui::GetCursorScreenPos();
        float avail_w = ImGui::GetContentRegionAvail().x;
        ImVec2 row_max(row_pos.x + avail_w, row_pos.y + row_h);
        float arrow_x = row_pos.x + depth * indent + 2.f;
        float arrow_max_x = arrow_x + indent + 4.f;

        ImGui::PushID((void*)(uintptr_t)node.address);
        ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(avail_w, row_h));
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        bool hovered = ImGui::IsItemHovered();
        bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered;
        ImGui::PopID();

        if (clicked)
        {
            float mx = ImGui::GetIO().MousePos.x;
            bool on_arrow = has_children && mx >= arrow_x && mx < arrow_max_x;
            if (on_arrow)
            {
                node.open = !node.open;
                if (node.open)
                    LoadChildren(node);
                selected_addr = node.address;
            }
            else
            {
                select_node(selected_addr, prop, properties_open, node.address, node.name, node.cls,
                    BuildPath(node.address), node.loaded ? (int)node.children.size() : -1);
                if (dbl && has_children)
                {
                    node.open = !node.open;
                    if (node.open)
                        LoadChildren(node);
                }
            }
        }
        if (right_clicked)
        {
            selected_addr = node.address;
            open_ctx(ctx, node.address, node.name, node.cls, BuildPath(node.address), ctx_pos);
        }

        if (hovered && !selected)
            dl->AddRectFilled(row_pos, row_max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.06f)));
        if (selected)
            dl->AddRect(ImVec2(row_pos.x + 2.f, row_pos.y + 2.f), ImVec2(row_max.x - 2.f, row_max.y - 2.f), ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.35f)));

        if (has_children)
            ImGui::RenderArrow(dl, ImVec2(arrow_x, row_pos.y + row_h * 0.5f - 4.f), ImGui::GetColorU32(ImVec4(0.65f, 0.65f, 0.65f, 1.f)),
                node.open ? ImGuiDir_Down : ImGuiDir_Right, 0.8f);

        float icon_x = row_pos.x + depth * indent + indent + 4.f;
        gui::draw_icon(dl, node.cls, ImVec2(icon_x, row_pos.y + (row_h - icon_size) * 0.5f), icon_size);

        float text_x = icon_x + icon_size + 5.f;
        float text_y = row_pos.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
        ImU32 text_col = ImGui::GetColorU32(selected ? ImVec4(1.f, 1.f, 1.f, 1.f) : ImVec4(0.82f, 0.82f, 0.82f, 1.f));
        const char* label = node.name.empty() ? "?" : node.name.c_str();
        widgets::text_outlined(dl, ImVec2(text_x, text_y), text_col, label);

        if (ValueBase::IsClass(node.cls))
        {
            std::string val = ValueBase::Format(node.address, node.cls);
            if (!val.empty())
            {
                float lx = text_x + ImGui::CalcTextSize(label).x + 6.f;
                std::string suffix = "= " + val;
                widgets::text_outlined(dl, ImVec2(lx, text_y), IM_COL32(120, 160, 120, 220), suffix.c_str());
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(row_pos.x, row_pos.y + row_h));

        if (node.open && node.loaded)
        {
            int shown = (int)node.children.size();
            if (shown > 800) shown = 800;
            for (int i = 0; i < shown; ++i)
                render_node(node.children[i], depth + 1, selected_addr, ctx, ctx_pos, prop, properties_open);
            if ((int)node.children.size() > shown)
            {
                ImGui::SetCursorPosX(depth * indent + 28.f);
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "... %d more (use search)", (int)node.children.size() - shown);
            }
        }
    }

    void render_search_row(const SearchResult& r, int idx, std::uint64_t& selected_addr, ctx_target& ctx, ImVec2& ctx_pos,
        gui::properties_target& prop, bool& properties_open)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr float row_h = 22.f;
        constexpr float icon_size = 14.f;

        bool selected = (selected_addr == r.address);
        ImVec2 row_pos = ImGui::GetCursorScreenPos();
        float avail_w = ImGui::GetContentRegionAvail().x;
        ImVec2 row_max(row_pos.x + avail_w, row_pos.y + row_h);

        ImGui::PushID(idx);
        bool clicked = ImGui::Selectable("##srow", selected, 0, ImVec2(avail_w, row_h));
        bool right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (clicked)
            select_node(selected_addr, prop, properties_open, r.address, r.name, r.cls, r.path, -1);
        if (right_clicked)
        {
            selected_addr = r.address;
            open_ctx(ctx, r.address, r.name, r.cls, r.path, ctx_pos);
        }

        if (hovered && !selected)
            dl->AddRectFilled(row_pos, row_max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.06f)));
        if (selected)
            dl->AddRect(ImVec2(row_pos.x + 2.f, row_pos.y + 2.f), ImVec2(row_max.x - 2.f, row_max.y - 2.f), ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.35f)));

        float icon_x = row_pos.x + 8.f;
        gui::draw_icon(dl, r.cls, ImVec2(icon_x, row_pos.y + (row_h - icon_size) * 0.5f), icon_size);

        float text_x = icon_x + icon_size + 5.f;
        float text_y = row_pos.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
        ImU32 text_col = ImGui::GetColorU32(selected ? ImVec4(1.f, 1.f, 1.f, 1.f) : ImVec4(0.82f, 0.82f, 0.82f, 1.f));
        const char* label = r.name.empty() ? "?" : r.name.c_str();
        widgets::text_outlined(dl, ImVec2(text_x, text_y), text_col, label);

        if (ValueBase::IsClass(r.cls))
        {
            std::string val = ValueBase::Format(r.address, r.cls);
            if (!val.empty())
            {
                float lx = text_x + ImGui::CalcTextSize(label).x + 6.f;
                std::string suffix = "= " + val;
                widgets::text_outlined(dl, ImVec2(lx, text_y), IM_COL32(120, 160, 120, 220), suffix.c_str());
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(row_pos.x, row_pos.y + row_h));
    }

    void draw_explorer(bool* open)
    {
        if (!open || !*open)
            return;

        static std::uint64_t selected_addr = 0;
        static ctx_target ctx;
        static ImVec2 ctx_pos(0.f, 0.f);
        static bool properties_open = false;
        static gui::properties_target prop_target;
        static char search_buf[128] = "";

        EnsureRoot();

        constexpr float title_h = 26.f;
        constexpr float margin = 3.f;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(ImVec2(center.x + 80.f, center.y - 40.f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(300.f, 460.f), ImGuiCond_Once);
        ImGui::SetNextWindowSizeConstraints(ImVec2(220.f, 260.f), ImVec2(FLT_MAX, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Border, border_outer);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
        bool visible = ImGui::Begin("##explorer_window", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | 0);
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
        ImVec2 title_ts = ImGui::CalcTextSize("explorer");
        widgets::text_outlined(draw, ImVec2(wp.x + (ws.x - title_ts.x) * 0.5f, wp.y + (title_h - title_ts.y) * 0.5f),
            IM_COL32(230, 230, 230, 255), "explorer");

        ImVec2 xsz = ImGui::CalcTextSize("X");
        float x_w = xsz.x + 14.f;
        float drag_w = ws.x - x_w - 6.f;
        ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
        ImGui::InvisibleButton("##explorer_drag", ImVec2(drag_w, title_h));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 cur = ImGui::GetWindowPos();
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(cur.x + delta.x, cur.y + delta.y));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        ImVec2 xmin(wp.x + ws.x - x_w, wp.y);
        ImGui::SetCursorScreenPos(xmin);
        ImGui::InvisibleButton("##explorer_close", ImVec2(x_w, title_h));
        bool xhov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            *open = false;
            properties_open = false;
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
        ImGui::BeginChild("##explorer_body", ImVec2(body_w, body_h), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        constexpr float search_pad = 6.f;
        constexpr float header_h = 22.f;

        ImGui::SetCursorPos(ImVec2(search_pad, search_pad));
        ImGui::PushItemWidth(body_w - search_pad * 2.f);
        bool changed = widgets::input_text("##explorer_search", search_buf, sizeof(search_buf));
        ImGui::PopItemWidth();

        if (ImGui::IsItemDeactivatedAfterEdit() && search_buf[0])
            StartSearch(search_buf);
        if (changed && search_buf[0] == '\0' && g_search_on)
            StopSearch();
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) && search_buf[0])
            StartSearch(search_buf);

        float search_bottom = search_pad + ImGui::GetItemRectSize().y;
        float panel_top = search_bottom + search_pad;
        float panel_h = body_h - panel_top - search_pad;
        float panel_w = body_w - search_pad * 2.f;

        ImGui::SetCursorPos(ImVec2(search_pad, panel_top));
        ImGui::PushStyleColor(ImGuiCol_Border, border_inner);
        ImGui::BeginChild("##explorer_panel", ImVec2(panel_w, panel_h), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImVec2 pp = ImGui::GetWindowPos();
            ImVec2 psz = ImGui::GetWindowSize();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 hts = ImGui::CalcTextSize("datamodel");
            widgets::text_outlined(dl, ImVec2(pp.x + (psz.x - hts.x) * 0.5f, pp.y + (header_h - hts.y) * 0.5f), IM_COL32(210, 210, 210, 255), "datamodel");
            dl->AddLine(ImVec2(pp.x + 1.f, pp.y + header_h), ImVec2(pp.x + psz.x - 1.f, pp.y + header_h), IM_COL32(255, 255, 255, 40));

            ImGui::SetCursorPos(ImVec2(0.f, header_h));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::BeginChild("##explorer_tree", ImVec2(psz.x, psz.y - header_h), false);

            if (g_search_on)
            {
                std::lock_guard<std::mutex> lk(g_search_mtx);
                if (g_searching.load())
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "searching...");
                if (g_search_results.empty() && !g_searching.load())
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "no matches");

                ImGuiListClipper clipper;
                clipper.Begin((int)g_search_results.size(), 22.f);
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                        render_search_row(g_search_results[i], i, selected_addr, ctx, ctx_pos, prop_target, properties_open);
                }
            }
            else if (g_root.address)
            {
                if (!g_root.loaded)
                    LoadChildren(g_root);
                for (auto& c : g_root.children)
                    render_node(c, 0, selected_addr, ctx, ctx_pos, prop_target, properties_open);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.f), "not attached / no datamodel");
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, border_outer);
            ImGui::SetNextWindowPos(ctx_pos, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("##ex_ctx_menu"))
            {
                if (ctx.valid)
                {
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    constexpr float row_h = 24.f;
                    constexpr float pad_x = 10.f;
                    constexpr float w = 170.f;
                    ImVec2 pwp = ImGui::GetWindowPos();

                    float header_top = 8.f;
                    float name_h = ImGui::GetTextLineHeight();
                    const char* nm = ctx.name.empty() ? "?" : ctx.name.c_str();
                    widgets::text_outlined(pdl, ImVec2(pwp.x + pad_x, pwp.y + header_top), IM_COL32(235, 235, 235, 255), nm);
                    widgets::text_outlined(pdl, ImVec2(pwp.x + pad_x, pwp.y + header_top + name_h + 2.f), IM_COL32(130, 130, 130, 255), ctx.cls.c_str());
                    float ctx_header_h = header_top + name_h * 2.f + 2.f + 8.f;
                    pdl->AddLine(ImVec2(pwp.x, pwp.y + ctx_header_h), ImVec2(pwp.x + w, pwp.y + ctx_header_h), ImGui::GetColorU32(border_inner));

                    ImGui::SetCursorPos(ImVec2(0.f, ctx_header_h));

                    auto ctx_row = [&](const char* label) -> bool {
                        ImGui::PushID(label);
                        ImVec2 rp = ImGui::GetCursorScreenPos();
                        ImVec2 rmax(rp.x + w, rp.y + row_h);
                        bool row_clicked = ImGui::Selectable("##r", false, 0, ImVec2(w, row_h));
                        bool hov = ImGui::IsItemHovered();
                        ImGui::PopID();
                        if (hov)
                            pdl->AddRectFilled(rp, rmax, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.08f)));
                        widgets::text_outlined(pdl, ImVec2(rp.x + pad_x, rp.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(220, 220, 220, 255), label);
                        return row_clicked;
                    };

                    if (ctx_row("properties"))
                    {
                        fill_prop(prop_target, ctx);
                        properties_open = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ctx_row("copy name"))
                    {
                        ImGui::SetClipboardText(ctx.name.c_str());
                        ImGui::CloseCurrentPopup();
                    }
                    if (ctx_row("copy class"))
                    {
                        ImGui::SetClipboardText(ctx.cls.c_str());
                        ImGui::CloseCurrentPopup();
                    }
                    if (ctx_row("copy path"))
                    {
                        ImGui::SetClipboardText(ctx.path.c_str());
                        ImGui::CloseCurrentPopup();
                    }
                    if (ctx_row("copy address"))
                    {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)ctx.address);
                        ImGui::SetClipboardText(buf);
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();

            ImGui::EndChild();
            ImGui::PopStyleColor(3);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::End();

        if (properties_open)
            gui::render_properties_window(&properties_open, wp, ws, prop_target);
    }

    void shutdown_impl()
    {
        StopSearch();
        g_root = Node{};
        g_root_addr = 0;
    }
}

namespace gui
{
    void explorer_init()
    {
    }

    void explorer_shutdown()
    {
        shutdown_impl();
    }

    void render_explorer_window(bool* open)
    {
        draw_explorer(open);
    }
}
