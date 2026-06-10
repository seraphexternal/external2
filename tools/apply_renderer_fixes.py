"""
Second-pass renderer.cpp patcher using regex (not exact) anchors.

Fixes from code-review:
  A. Main loop Update signature: needs (float, float).
  B. Main loop Render signature: needs (drawList, origin, size).
  C. Misc tab UI panel: rebind to Options::Weather, fix SliderGrab
     scope (only around sliders), move tooltip to anchor on Enable
     checkbox, make weatherKinds static.
  D. Add MenuWeather::SyncFromOptions() helper.
"""
import re
import sys

PATH = 'Seraph/overlay/renderer.cpp'

with open(PATH, 'r', encoding='utf-8') as f:
    src = f.read()

# ---- Patch A: Update call ----
OLD_A = '        MenuWeather::Update();\n'
NEW_A = (
    '        MenuWeather::SyncFromOptions();\n'
    '        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);\n'
)
if src.count(OLD_A) != 1:
    sys.exit(f'[abort] A found {src.count(OLD_A)} times')
src = src.replace(OLD_A, NEW_A, 1)

# ---- Patch B: Render call ----
OLD_B = (
    '        if (MenuWeather::Enabled)\n'
    '            MenuWeather::Render(ImGui::GetBackgroundDrawList());\n'
)
NEW_B = (
    '        if (MenuWeather::Enabled)\n'
    '        {\n'
    '            const ImVec2 displaySize = ImGui::GetIO().DisplaySize;\n'
    '            MenuWeather::Render(ImGui::GetBackgroundDrawList(), ImVec2(0.0f, 0.0f), displaySize);\n'
    '        }\n'
)
if src.count(OLD_B) != 1:
    sys.exit(f'[abort] B found {src.count(OLD_B)} times')
src = src.replace(OLD_B, NEW_B, 1)

# ---- Patch C: Misc tab UI panel via regex ----
# Anchor: from the "Menu Font" header until the next "EndChild();" at
# indent 20 (the one that closes the "Main Group" MenuChild).
old_pattern = re.compile(
    r'(\n)(                            ImGui::TextColored\(main_color, "Menu Font"\);.*?\n)(                    ImGui::EndChild\(\);\n)',
    re.DOTALL,
)

m = old_pattern.search(src)
if not m:
    sys.exit('[abort] C regex matched 0 times')

NEW_C = (
    '                            ImGui::Dummy(ImVec2(0, 8));\n'
    '                            ImGui::Separator();\n'
    '                            ImGui::Dummy(ImVec2(0, 8));\n'
    '\n'
    '                            ImGui::TextColored(main_color, "Menu Font");\n'
    '                            static const char* menuFontNames[] = {\n'
    '                                "Verdana", "Segoe UI", "Tahoma", "Arial",\n'
    '                                "Georgia", "Calibri", "Consolas"\n'
    '                            };\n'
    '                            ImGui::Combo("Font", &Options::Misc::MenuFont, menuFontNames, IM_ARRAYSIZE(menuFontNames));\n'
    '                            if (ImGui::IsItemHovered())\n'
    '                                ImGui::SetTooltip("Live-switches the menu font at runtime. Takes effect on next menu open.");\n'
    '\n'
    '                            ImGui::Dummy(ImVec2(0, 8));\n'
    '                            ImGui::Separator();\n'
    '                            ImGui::Dummy(ImVec2(0, 8));\n'
    '\n'
    '                            ImGui::TextColored(main_color, "Background Weather");\n'
    '                            ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);\n'
    '                            ImGui::Checkbox("Enable Weather Effect", &Options::Weather::Enabled);\n'
    '                            ImGui::PopStyleColor(1);\n'
    '                            if (ImGui::IsItemHovered())\n'
    '                                ImGui::SetTooltip("Snow / rain particles that fall across the menu background. Settings save with your config.");\n'
    '\n'
    '                            static const char* weatherKinds[] = { "Snow", "Rain" };\n'
    '                            ImGui::Combo("Weather Type", &Options::Weather::Type, weatherKinds, 2);\n'
    '\n'
    '                            ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);\n'
    '                            ImGui::SliderInt("Intensity", &Options::Weather::Intensity, 64, 2000, "%d particles");\n'
    '                            ImGui::SliderFloat("Fall Speed", &Options::Weather::Speed, 0.2f, 6.0f, "%.2fx");\n'
    '                            ImGui::SliderFloat("Wind", &Options::Weather::Wind, -3.0f, 3.0f, "%.2fx");\n'
    '                            ImGui::SliderFloat("Snow Size", &Options::Weather::SnowSize, 0.5f, 4.0f, "%.1f px");\n'
    '                            ImGui::SliderFloat("Rain Thickness", &Options::Weather::RainThickness, 0.5f, 3.0f, "%.1f px");\n'
    '                            ImGui::PopStyleColor(1);\n'
    '\n'
    '                            ImGui::ColorEdit3("Particle Color", Options::Weather::Color, ImGuiColorEditFlags_NoInputs);\n'
)

src = src[:m.start(2)] + NEW_C + src[m.end(2):]

# ---- Patch D: SyncFromOptions helper inside MenuWeather namespace ----
OLD_D = '    inline void Update(float maxX, float maxY)'
NEW_D = (
    '    // Pull user-tweakable weather settings from the persistent\n'
    '    // Options namespace into engine-local state. Defensive clamps\n'
    '    // keep stale/corrupt configs from breaking the Combo display\n'
    '    // or the intensity slider range. Clears leftover particles\n'
    '    // on disable so snowflakes do not freeze in place.\n'
    '    inline void SyncFromOptions()\n'
    '    {\n'
    '        const bool wasEnabled = Enabled;\n'
    '        Enabled       = Options::Weather::Enabled;\n'
    '        Type          = (Options::Weather::Type == 0 || Options::Weather::Type == 1) ? Options::Weather::Type : 0;\n'
    '        Intensity     = Options::Weather::Intensity < 64 ? 64 : (Options::Weather::Intensity > 2000 ? 2000 : Options::Weather::Intensity);\n'
    '        Speed         = Options::Weather::Speed;\n'
    '        Wind          = Options::Weather::Wind;\n'
    '        SnowSize      = Options::Weather::SnowSize;\n'
    '        RainThickness = Options::Weather::RainThickness;\n'
    '        for (int i = 0; i < 3; ++i)\n'
    '            Color[i] = Options::Weather::Color[i];\n'
    '        if (wasEnabled && !Enabled)\n'
    '            particles.clear();\n'
    '    }\n'
    '\n'
    '    inline void Update(float maxX, float maxY)'
)
if src.count(OLD_D) != 1:
    sys.exit(f'[abort] D found {src.count(OLD_D)} times')
src = src.replace(OLD_D, NEW_D, 1)

with open(PATH, 'w', encoding='utf-8') as f:
    f.write(src)

print(f'[ok] patched {PATH}, {len(src)} bytes')
