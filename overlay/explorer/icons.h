#pragma once

#include "../imgui/imgui.h"
#include <string>

struct ID3D11Device;

namespace gui
{
    void icons_init(ID3D11Device* device);
    ImTextureID icon_for_class(const std::string& cls);
    void draw_icon(ImDrawList* dl, const std::string& cls, ImVec2 pos, float size);
}
