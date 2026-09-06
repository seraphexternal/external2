#include "lua_editor.h"
#include "../imgui/imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace editor
{
    // imgui 1.83: IsKeyPressed takes a key index, ImGuiKey_* are enum codes.
    static bool key_pressed(int key_enum)
    {
        return ImGui::IsKeyPressed(ImGui::GetKeyIndex(key_enum));
    }

    enum token_kind { tok_plain, tok_kw, tok_str, tok_num, tok_comment, tok_ident };

    static const char* k_keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while", nullptr
    };

    static const char* k_completions[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while",
        "print", "pairs", "ipairs", "next", "type", "tostring", "tonumber",
        "require", "pcall", "xpcall", "error", "assert", "select", "unpack",
        "math.abs", "math.acos", "math.asin", "math.atan", "math.ceil", "math.clamp",
        "math.cos", "math.deg", "math.floor", "math.fmod", "math.huge", "math.log",
        "math.max", "math.min", "math.pi", "math.rad", "math.random", "math.sin",
        "math.sqrt", "math.tan",
        "string.byte", "string.char", "string.find", "string.format", "string.gmatch",
        "string.gsub", "string.len", "string.lower", "string.match", "string.rep",
        "string.reverse", "string.sub", "string.upper",
        "table.concat", "table.insert", "table.remove", "table.sort", "table.unpack",
        "Vector2", "Vector2.new", "Vector3", "Vector3.new",
        "draw", "draw.text", "draw.text_size", "draw.line", "draw.rect", "draw.circle",
        "workspace", "game", "Players", "LocalPlayer", "GetService",
        nullptr
    };

    static bool is_ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
    static bool is_ident(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

    static int utf8_len(const char* s, size_t n)
    {
        if (!n) return 0;
        const unsigned char c = (unsigned char)s[0];
        int need = 1;
        if (c >= 0xF0) need = 4;
        else if (c >= 0xE0) need = 3;
        else if (c >= 0xC0) need = 2;
        if ((size_t)need > n) return 1;
        for (int i = 1; i < need; ++i)
            if (((unsigned char)s[i] & 0xC0) != 0x80) return 1;
        return need;
    }

    static bool is_kw(const char* s, size_t n)
    {
        for (int i = 0; k_keywords[i]; ++i)
            if (std::strncmp(k_keywords[i], s, n) == 0 && k_keywords[i][n] == 0)
                return true;
        return false;
    }

    static ImU32 col_for(token_kind k)
    {
        switch (k)
        {
        case tok_kw:      return IM_COL32(120, 180, 220, 255);
        case tok_str:     return IM_COL32(200, 140, 180, 255);
        case tok_num:     return IM_COL32(140, 200, 120, 255);
        case tok_comment: return IM_COL32(110, 110, 110, 255);
        case tok_ident:   return IM_COL32(220, 220, 220, 255);
        default:          return IM_COL32(200, 200, 200, 255);
        }
    }

    lua_editor::lua_editor()
    {
        lines.push_back("");
    }

    void lua_editor::set_text(const std::string& text)
    {
        lines.clear();
        std::string cur;
        for (char c : text)
        {
            if (c == '\n')
            {
                lines.push_back(cur);
                cur.clear();
            }
            else if (c != '\r')
                cur.push_back(c);
        }
        lines.push_back(cur);
        if (lines.empty())
            lines.push_back("");
        cursor_row = sel_row = 0;
        cursor_col = sel_col = 0;
        scroll_y = scroll_x = 0.f;
        prefer_ac = false;
        ac_items.clear();
    }

    std::string lua_editor::get_text() const
    {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i) out.push_back('\n');
            out += lines[i];
        }
        return out;
    }

    static void clamp_pos(const std::vector<std::string>& lines, int& row, int& col)
    {
        if (lines.empty()) { row = 0; col = 0; return; }
        row = std::clamp(row, 0, (int)lines.size() - 1);
        col = std::clamp(col, 0, (int)lines[row].size());
    }

    static bool has_sel(const lua_editor& e)
    {
        return e.cursor_row != e.sel_row || e.cursor_col != e.sel_col;
    }

    static void norm_sel(const lua_editor& e, int& r0, int& c0, int& r1, int& c1)
    {
        r0 = e.sel_row; c0 = e.sel_col; r1 = e.cursor_row; c1 = e.cursor_col;
        if (r0 > r1 || (r0 == r1 && c0 > c1))
        {
            std::swap(r0, r1);
            std::swap(c0, c1);
        }
    }

    static std::string get_sel(const lua_editor& e)
    {
        if (!has_sel(e)) return {};
        int r0, c0, r1, c1;
        norm_sel(e, r0, c0, r1, c1);
        if (r0 == r1)
            return e.lines[r0].substr(c0, c1 - c0);
        std::string out = e.lines[r0].substr(c0);
        out.push_back('\n');
        for (int r = r0 + 1; r < r1; ++r)
        {
            out += e.lines[r];
            out.push_back('\n');
        }
        out += e.lines[r1].substr(0, c1);
        return out;
    }

    static void delete_sel(lua_editor& e)
    {
        if (!has_sel(e)) return;
        int r0, c0, r1, c1;
        norm_sel(e, r0, c0, r1, c1);
        if (r0 == r1)
            e.lines[r0].erase(c0, c1 - c0);
        else
        {
            std::string merged = e.lines[r0].substr(0, c0) + e.lines[r1].substr(c1);
            e.lines.erase(e.lines.begin() + r0, e.lines.begin() + r1 + 1);
            e.lines.insert(e.lines.begin() + r0, merged);
        }
        e.cursor_row = e.sel_row = r0;
        e.cursor_col = e.sel_col = c0;
    }

    static void insert_text(lua_editor& e, const std::string& text)
    {
        delete_sel(e);
        for (char c : text)
        {
            if (c == '\n')
            {
                std::string rest = e.lines[e.cursor_row].substr(e.cursor_col);
                e.lines[e.cursor_row].erase(e.cursor_col);
                e.lines.insert(e.lines.begin() + e.cursor_row + 1, rest);
                e.cursor_row++;
                e.cursor_col = 0;
            }
            else if (c != '\r')
            {
                e.lines[e.cursor_row].insert(e.lines[e.cursor_row].begin() + e.cursor_col, c);
                e.cursor_col++;
            }
        }
        e.sel_row = e.cursor_row;
        e.sel_col = e.cursor_col;
    }

    static void word_prefix(const lua_editor& e, int& start_col, std::string& prefix)
    {
        const std::string& line = e.lines[e.cursor_row];
        start_col = e.cursor_col;
        while (start_col > 0)
        {
            char c = line[start_col - 1];
            if (is_ident(c) || c == '.')
                start_col--;
            else
                break;
        }
        prefix = line.substr(start_col, e.cursor_col - start_col);
    }

    static void refresh_ac(lua_editor& e)
    {
        e.ac_items.clear();
        word_prefix(e, e.ac_start_col, e.ac_prefix);
        if (e.ac_prefix.empty())
        {
            e.prefer_ac = false;
            return;
        }
        std::string pref = e.ac_prefix;
        for (char& c : pref) c = (char)std::tolower((unsigned char)c);

        for (int i = 0; k_completions[i]; ++i)
        {
            std::string item = k_completions[i];
            std::string low = item;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if (low.size() >= pref.size() && low.compare(0, pref.size(), pref) == 0 && low != pref)
                e.ac_items.push_back(item);
        }
        if (e.ac_items.empty())
            e.prefer_ac = false;
        else
        {
            e.prefer_ac = true;
            e.ac_index = std::clamp(e.ac_index, 0, (int)e.ac_items.size() - 1);
        }
    }

    static void apply_ac(lua_editor& e)
    {
        if (e.ac_items.empty()) return;
        const std::string& pick = e.ac_items[e.ac_index];
        e.sel_row = e.cursor_row;
        e.sel_col = e.ac_start_col;
        e.cursor_col = e.cursor_col;
        delete_sel(e);
        insert_text(e, pick);
        e.prefer_ac = false;
        e.ac_items.clear();
    }

    static void draw_line_tokens(ImDrawList* dl, ImVec2 pos, const std::string& line)
    {
        const char* s = line.c_str();
        size_t i = 0, n = line.size();
        float x = pos.x;
        float y = pos.y;
        while (i < n)
        {
            if (s[i] == '-' && i + 1 < n && s[i + 1] == '-')
            {
                dl->AddText(ImVec2(x, y), col_for(tok_comment), s + i);
                break;
            }
            if (s[i] == '"' || s[i] == '\'')
            {
                char q = s[i];
                size_t j = i + 1;
                while (j < n)
                {
                    if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
                    if (s[j] == q) { j++; break; }
                    j++;
                }
                std::string tok(s + i, j - i);
                dl->AddText(ImVec2(x, y), col_for(tok_str), tok.c_str());
                x += ImGui::CalcTextSize(tok.c_str()).x;
                i = j;
                continue;
            }
            if (std::isdigit((unsigned char)s[i]) || (s[i] == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1])))
            {
                size_t j = i;
                while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == 'x' || s[j] == 'X' ||
                    (s[j] >= 'a' && s[j] <= 'f') || (s[j] >= 'A' && s[j] <= 'F')))
                    j++;
                std::string tok(s + i, j - i);
                dl->AddText(ImVec2(x, y), col_for(tok_num), tok.c_str());
                x += ImGui::CalcTextSize(tok.c_str()).x;
                i = j;
                continue;
            }
            if (is_ident_start(s[i]))
            {
                size_t j = i + 1;
                while (j < n && is_ident(s[j])) j++;
                token_kind k = is_kw(s + i, j - i) ? tok_kw : tok_ident;
                std::string tok(s + i, j - i);
                dl->AddText(ImVec2(x, y), col_for(k), tok.c_str());
                x += ImGui::CalcTextSize(tok.c_str()).x;
                i = j;
                continue;
            }
            const int cl = utf8_len(s + i, n - i);
            char tmp[8]{};
            memcpy(tmp, s + i, (size_t)cl);
            dl->AddText(ImVec2(x, y), col_for(tok_plain), tmp);
            x += ImGui::CalcTextSize(tmp).x;
            i += (size_t)cl;
        }
    }

    void lua_editor::render(const char* id, ImVec2 size)
    {
        ImGui::PushID(id);
        ImGui::BeginChild("##ed", size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        const float sb_w = show_scrollbar ? 6.f : 0.f;
        ImVec2 canvas_avail(std::max(1.f, avail.x - sb_w), avail.y);

        ImGui::InvisibleButton("##canvas", canvas_avail);
        bool hovered = ImGui::IsItemHovered();
        bool canvas_active = ImGui::IsItemActive();
        bool win_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemClicked() || canvas_active)
            focused = true;
        if (!hovered && !canvas_active && ImGui::IsMouseClicked(0) && !prefer_ac && !win_hovered)
            focused = false;

        if (focused)
        {
            io.WantCaptureKeyboard = true;
            io.WantTextInput = true;
        }

        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        const float gutter_w = ImGui::CalcTextSize("0000").x + 12.f;
        const float pad = 4.f;
        float content_h = (float)lines.size() * line_h;
        float max_scroll = std::max(0.f, content_h - canvas_avail.y);
        scroll_y = std::clamp(scroll_y, 0.f, max_scroll);

        int vis_lines = std::max(1, (int)(canvas_avail.y / line_h) + 1);
        int first = (int)(scroll_y / line_h);
        first = std::clamp(first, 0, std::max(0, (int)lines.size() - 1));

        if ((hovered || win_hovered) && io.MouseWheel != 0.f && max_scroll > 0.f)
        {
            scroll_y -= io.MouseWheel * line_h * 3.f;
            scroll_y = std::clamp(scroll_y, 0.f, max_scroll);
            io.MouseWheel = 0.f;
        }

        dl->AddRectFilled(origin, ImVec2(origin.x + gutter_w, origin.y + canvas_avail.y), IM_COL32(18, 18, 18, 255));
        dl->AddLine(ImVec2(origin.x + gutter_w, origin.y), ImVec2(origin.x + gutter_w, origin.y + canvas_avail.y), IM_COL32(45, 45, 45, 255));

        auto char_x = [&](int row, int col) -> float {
            std::string sub = lines[row].substr(0, std::min(col, (int)lines[row].size()));
            return origin.x + gutter_w + pad - scroll_x + ImGui::CalcTextSize(sub.c_str()).x;
        };

        if (has_sel(*this))
        {
            int r0, c0, r1, c1;
            norm_sel(*this, r0, c0, r1, c1);
            for (int r = r0; r <= r1; ++r)
            {
                float y = origin.y + (float)r * line_h - scroll_y;
                if (y + line_h < origin.y || y > origin.y + canvas_avail.y) continue;
                int a = (r == r0) ? c0 : 0;
                int b = (r == r1) ? c1 : (int)lines[r].size();
                float x0 = char_x(r, a);
                float x1 = char_x(r, b);
                if (a == b) x1 = x0 + 4.f;
                dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + line_h), IM_COL32(60, 80, 110, 160));
            }
        }

        for (int i = 0; i < vis_lines + 2; ++i)
        {
            int row = first + i;
            if (row < 0 || row >= (int)lines.size()) continue;
            float y = origin.y + (float)row * line_h - scroll_y;
            if (y + line_h < origin.y || y > origin.y + canvas_avail.y) continue;

            char num[16];
            snprintf(num, sizeof(num), "%d", row + 1);
            ImVec2 ns = ImGui::CalcTextSize(num);
            dl->AddText(ImVec2(origin.x + gutter_w - ns.x - 6.f, y), IM_COL32(90, 140, 150, 255), num);
            draw_line_tokens(dl, ImVec2(origin.x + gutter_w + pad - scroll_x, y), lines[row]);
        }

        if (focused)
        {
            float cx = char_x(cursor_row, cursor_col);
            float cy = origin.y + (float)cursor_row * line_h - scroll_y;
            if (fmodf((float)ImGui::GetTime(), 1.f) < 0.5f)
                dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + 1.f, cy + line_h - 2.f), IM_COL32(220, 220, 220, 255));
        }

        auto pos_at = [&](float mx, float my, int& row, int& col) {
            row = (int)((my - origin.y + scroll_y) / line_h);
            row = std::clamp(row, 0, (int)lines.size() - 1);
            col = 0;
            float x = origin.x + gutter_w + pad - scroll_x;
            const std::string& line = lines[row];
            while (col < (int)line.size())
            {
                const int cl = utf8_len(line.c_str() + col, line.size() - (size_t)col);
                char tmp[8]{};
                memcpy(tmp, line.c_str() + col, (size_t)cl);
                float w = ImGui::CalcTextSize(tmp).x;
                if (mx < x + w * 0.5f) break;
                x += w;
                col += cl;
            }
        };

        if (focused && hovered && ImGui::IsMouseClicked(0))
        {
            int row, col;
            pos_at(io.MousePos.x, io.MousePos.y, row, col);
            cursor_row = sel_row = row;
            cursor_col = sel_col = col;
            prefer_ac = false;
            ac_items.clear();
        }

        if (focused && ImGui::IsMouseDragging(0) && (hovered || canvas_active))
        {
            int row, col;
            pos_at(io.MousePos.x, io.MousePos.y, row, col);
            cursor_row = row;
            cursor_col = col;
        }

        if (focused)
        {
            bool shift = io.KeyShift;
            bool ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
            auto move = [&](int nr, int nc) {
                clamp_pos(lines, nr, nc);
                cursor_row = nr;
                cursor_col = nc;
                if (!shift) { sel_row = cursor_row; sel_col = cursor_col; }
            };

            bool ac_used = false;
            if (!readonly && prefer_ac && !ac_items.empty())
            {
                if (key_pressed(ImGuiKey_UpArrow))
                    ac_index = (ac_index + (int)ac_items.size() - 1) % (int)ac_items.size();
                else if (key_pressed(ImGuiKey_DownArrow))
                    ac_index = (ac_index + 1) % (int)ac_items.size();
                else if (key_pressed(ImGuiKey_Tab) || key_pressed(ImGuiKey_Enter))
                {
                    apply_ac(*this);
                    ac_used = true;
                }
                else if (key_pressed(ImGuiKey_Escape))
                {
                    prefer_ac = false;
                    ac_items.clear();
                }
            }
            else
            {
                if (key_pressed(ImGuiKey_LeftArrow))
                {
                    if (cursor_col > 0) move(cursor_row, cursor_col - 1);
                    else if (cursor_row > 0) move(cursor_row - 1, (int)lines[cursor_row - 1].size());
                }
                if (key_pressed(ImGuiKey_RightArrow))
                {
                    if (cursor_col < (int)lines[cursor_row].size()) move(cursor_row, cursor_col + 1);
                    else if (cursor_row + 1 < (int)lines.size()) move(cursor_row + 1, 0);
                }
                if (key_pressed(ImGuiKey_UpArrow) && cursor_row > 0)
                    move(cursor_row - 1, cursor_col);
                if (key_pressed(ImGuiKey_DownArrow) && cursor_row + 1 < (int)lines.size())
                    move(cursor_row + 1, cursor_col);
                if (key_pressed(ImGuiKey_Home))
                    move(cursor_row, 0);
                if (key_pressed(ImGuiKey_End))
                    move(cursor_row, (int)lines[cursor_row].size());
            }

            if (!readonly)
            {
                if (!ac_used && key_pressed(ImGuiKey_Enter))
                {
                    insert_text(*this, "\n");
                    prefer_ac = false;
                    ac_items.clear();
                }
                if (key_pressed(ImGuiKey_Backspace))
                {
                    if (has_sel(*this))
                        delete_sel(*this);
                    else if (cursor_col > 0)
                    {
                        lines[cursor_row].erase(cursor_col - 1, 1);
                        cursor_col--;
                        sel_row = cursor_row;
                        sel_col = cursor_col;
                    }
                    else if (cursor_row > 0)
                    {
                        int prev = (int)lines[cursor_row - 1].size();
                        lines[cursor_row - 1] += lines[cursor_row];
                        lines.erase(lines.begin() + cursor_row);
                        cursor_row--;
                        cursor_col = prev;
                        sel_row = cursor_row;
                        sel_col = cursor_col;
                    }
                    refresh_ac(*this);
                }
                if (key_pressed(ImGuiKey_Delete))
                {
                    if (has_sel(*this))
                        delete_sel(*this);
                    else if (cursor_col < (int)lines[cursor_row].size())
                        lines[cursor_row].erase(cursor_col, 1);
                    else if (cursor_row + 1 < (int)lines.size())
                    {
                        lines[cursor_row] += lines[cursor_row + 1];
                        lines.erase(lines.begin() + cursor_row + 1);
                    }
                }
            }

            if (ctrl && key_pressed(ImGuiKey_A))
            {
                sel_row = 0; sel_col = 0;
                cursor_row = (int)lines.size() - 1;
                cursor_col = (int)lines[cursor_row].size();
            }
            if (ctrl && key_pressed(ImGuiKey_C))
            {
                std::string s = has_sel(*this) ? get_sel(*this) : lines[cursor_row];
                ImGui::SetClipboardText(s.c_str());
            }
            if (!readonly && ctrl && key_pressed(ImGuiKey_X))
            {
                std::string s = has_sel(*this) ? get_sel(*this) : lines[cursor_row];
                ImGui::SetClipboardText(s.c_str());
                if (has_sel(*this))
                    delete_sel(*this);
                else
                {
                    lines[cursor_row].clear();
                    cursor_col = sel_col = 0;
                }
            }
            if (!readonly && ctrl && key_pressed(ImGuiKey_V))
            {
                const char* clip = ImGui::GetClipboardText();
                if (clip) insert_text(*this, clip);
                refresh_ac(*this);
            }

            if (!readonly)
            {
                for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
                {
                    ImWchar c = io.InputQueueCharacters[n];
                    if (c == '\t') continue;
                    if (c >= 32 && c < 127)
                    {
                        char ch = (char)c;
                        insert_text(*this, std::string(1, ch));
                        refresh_ac(*this);
                    }
                }
                io.InputQueueCharacters.resize(0);
            }
        }

        if (readonly)
        {
            prefer_ac = false;
            ac_items.clear();
        }

        bool nav_key = key_pressed(ImGuiKey_UpArrow) || key_pressed(ImGuiKey_DownArrow) ||
            key_pressed(ImGuiKey_LeftArrow) || key_pressed(ImGuiKey_RightArrow) ||
            key_pressed(ImGuiKey_Home) || key_pressed(ImGuiKey_End) ||
            key_pressed(ImGuiKey_PageUp) || key_pressed(ImGuiKey_PageDown);
        if (focused && (!readonly || nav_key))
        {
            float cy = origin.y + (float)cursor_row * line_h - scroll_y;
            if (cy < origin.y)
                scroll_y = (float)cursor_row * line_h;
            if (cy + line_h > origin.y + canvas_avail.y)
                scroll_y = (float)cursor_row * line_h - canvas_avail.y + line_h;
            scroll_y = std::clamp(scroll_y, 0.f, max_scroll);
        }

        if (prefer_ac && !ac_items.empty() && !readonly)
        {
            float px = char_x(cursor_row, ac_start_col);
            float py = origin.y + (float)cursor_row * line_h - scroll_y + line_h;
            float max_w = 0.f;
            for (auto& it : ac_items)
                max_w = std::max(max_w, ImGui::CalcTextSize(it.c_str()).x);
            int show = std::min(8, (int)ac_items.size());
            float ph = show * line_h + 4.f;
            float pw = max_w + 16.f;
            ImVec2 pmin(px, py);
            ImVec2 pmax(px + pw, py + ph);
            dl->AddRectFilled(pmin, pmax, IM_COL32(28, 28, 28, 245));
            dl->AddRect(pmin, pmax, IM_COL32(70, 70, 70, 255));
            int start = std::max(0, ac_index - show + 1);
            for (int i = 0; i < show; ++i)
            {
                int idx = start + i;
                if (idx >= (int)ac_items.size()) break;
                ImVec2 tp(pmin.x + 6.f, pmin.y + 2.f + i * line_h);
                if (idx == ac_index)
                    dl->AddRectFilled(ImVec2(pmin.x + 1.f, tp.y), ImVec2(pmax.x - 1.f, tp.y + line_h), IM_COL32(55, 55, 55, 255));
                dl->AddText(tp, IM_COL32(230, 230, 230, 255), ac_items[idx].c_str());
            }
        }

        if (show_scrollbar && max_scroll > 0.f)
        {
            ImVec2 sb_min(origin.x + canvas_avail.x + 1.f, origin.y);
            ImVec2 sb_max(origin.x + avail.x - 1.f, origin.y + canvas_avail.y);
            dl->AddRectFilled(sb_min, sb_max, IM_COL32(20, 20, 20, 255));

            float track_h = sb_max.y - sb_min.y;
            float thumb_h = std::max(16.f, track_h * (canvas_avail.y / content_h));
            float thumb_y = sb_min.y + (track_h - thumb_h) * (scroll_y / max_scroll);
            ImVec2 th_min(sb_min.x, thumb_y);
            ImVec2 th_max(sb_max.x, thumb_y + thumb_h);

            ImGui::SetCursorScreenPos(sb_min);
            ImGui::InvisibleButton("##sb", ImVec2(sb_max.x - sb_min.x, track_h));
            bool sb_hov = ImGui::IsItemHovered();
            bool sb_held = ImGui::IsItemActive();
            if (sb_held && track_h > thumb_h)
            {
                float rel = (io.MousePos.y - sb_min.y - thumb_h * 0.5f) / (track_h - thumb_h);
                scroll_y = std::clamp(rel, 0.f, 1.f) * max_scroll;
                thumb_y = sb_min.y + (track_h - thumb_h) * (scroll_y / max_scroll);
                th_min.y = thumb_y;
                th_max.y = thumb_y + thumb_h;
            }

            ImU32 th_col = IM_COL32(sb_held ? 120 : (sb_hov ? 100 : 70), sb_held ? 120 : (sb_hov ? 100 : 70), sb_held ? 120 : (sb_hov ? 100 : 70), 255);
            dl->AddRectFilled(th_min, th_max, th_col);
        }

        ImGui::EndChild();
        ImGui::PopID();
    }
}
