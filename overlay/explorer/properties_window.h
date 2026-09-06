#pragma once

#include "../imgui/imgui.h"
#include <cstdint>
#include <string>

namespace gui
{
    struct properties_target
    {
        std::string name;
        std::string cls;
        std::string path;
        std::uint64_t address = 0;
        int children = 0;
    };

    void render_properties_window(bool* open, ImVec2 anchor_pos, ImVec2 anchor_size, const properties_target& target);
}
