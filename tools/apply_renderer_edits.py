"""
One-shot patcher for Seraph/overlay/renderer.cpp.

Inserts:
  1. MenuWeather::Update() call in the main per-frame update loop
     immediately after Globals::Viewport::Update().
  2. MenuWeather::Render() call in the main render sequence
     immediately after CombatFeedback::Render(...).
  3. A "Menu Font" combo + a "Background Weather" panel inside the
     Misc tab UI block, appended right after the Stream Proof
     checkbox (and before the ImGui::EndChild() of the "Main Group"
     child window).

This script is idempotent and refuses to write if any anchor is
missing or already patched.
"""
import sys

PATH = 'Seraph/overlay/renderer.cpp'

with open(PATH, 'r', encoding='utf-8') as f:
    src = f.read()

# ---- Anchor 1: updater ----
OLD1 = '        Globals::Viewport::Update();\n'
NEW1 = '        Globals::Viewport::Update();\n        MenuWeather::Update();\n'

if src.count(OLD1) != 1:
    sys.exit(f'[abort] anchor1 (Globals::Viewport::Update) found {src.count(OLD1)} times (expected 1)')
src = src.replace(OLD1, NEW1, 1)

# ---- Anchor 2: renderer ----
OLD2 = '        CombatFeedback::Render(ImGui::GetBackgroundDrawList());\n'
NEW2 = (
    '        CombatFeedback::Render(ImGui::GetBackgroundDrawList());\n'
    '        if (MenuWeather::Enabled)\n'
    '            MenuWeather::Render(ImGui::GetBackgroundDrawList());\n'
)

if src.count(OLD2) != 1:
    sys.exit(f'[abort] anchor2 (CombatFeedback::Render) found {src.count(OLD2)} times (expected 1)')
src = src.replace(OLD2, NEW2, 1)

# ---- Anchor 3: Misc tab Stream Proof area ----
# 24 spaces of indent (inside MenuChild); 20 spaces on the closing brace.
INDENT24 = ' ' * 24
INDENT20 = ' ' * 20

OLD3 = (
    f'{INDENT24}ImGui::Checkbox("Stream Proof", &Options::Misc::StreamProof);\n'
    f'{INDENT24}ImGui::PopStyleColor(1);\n'
    f'{INDENT20}}}\n'
    f'{INDENT20}ImGui::EndChild();\n'
)

NEW3 = (
    f'{INDENT24}ImGui::Checkbox("Stream Proof", &Options::Misc::StreamProof);\n'
    f'{INDENT24}ImGui::PopStyleColor(1);\n'
    '\n'
    f'{INDENT24}ImGui::Dummy(ImVec2(0, 8));\n'
    f'{INDENT24}ImGui::Separator();\n'
    f'{INDENT24}ImGui::Dummy(ImVec2(0, 8));\n'
    '\n'
    f'{INDENT24}ImGui::TextColored(main_color, "Menu Font");\n'
    f'{INDENT24}static const char* menuFontNames[] = {{\n'
    '                            "Verdana", "Segoe UI", "Tahoma", "Arial",\n'
    '                            "Georgia", "Calibri", "Consolas"\n'
    '                        };\n'
    f'{INDENT24}ImGui::Combo("Font", &Options::Misc::MenuFont, menuFontNames, IM_ARRAYSIZE(menuFontNames));\n'
    f'{INDENT24}if (ImGui::IsItemHovered())\n'
    f'{INDENT24}    ImGui::SetTooltip("Live-switches the menu font at runtime. Takes effect on next menu open.");\n'
    '\n'
    f'{INDENT24}ImGui::Dummy(ImVec2(0, 8));\n'
    f'{INDENT24}ImGui::Separator();\n'
    f'{INDENT24}ImGui::Dummy(ImVec2(0, 8));\n'
    '\n'
    f'{INDENT24}ImGui::TextColored(main_color, "Background Weather");\n'
    f'{INDENT24}ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);\n'
    f'{INDENT24}ImGui::Checkbox("Enable Weather Effect", &MenuWeather::Enabled);\n'
    f'{INDENT24}ImGui::PopStyleColor(1);\n'
    '\n'
    f'{INDENT24}const char* weatherKinds[] = {{ "Snow", "Rain" }};\n'
    f'{INDENT24}ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);\n'
    f'{INDENT24}ImGui::Combo("Weather Type", &MenuWeather::Type, weatherKinds, 2);\n'
    f'{INDENT24}ImGui::SliderInt("Intensity", &MenuWeather::Intensity, 64, 2000, "%d particles");\n'
    f'{INDENT24}ImGui::SliderFloat("Fall Speed", &MenuWeather::Speed, 0.2f, 6.0f, "%.2fx");\n'
    f'{INDENT24}ImGui::SliderFloat("Wind", &MenuWeather::Wind, -3.0f, 3.0f, "%.2fx");\n'
    f'{INDENT24}ImGui::SliderFloat("Snow Size", &MenuWeather::SnowSize, 0.5f, 4.0f, "%.1f px");\n'
    f'{INDENT24}ImGui::SliderFloat("Rain Thickness", &MenuWeather::RainThickness, 0.5f, 3.0f, "%.1f px");\n'
    f'{INDENT24}ImGui::ColorEdit3("Particle Color", MenuWeather::Color, ImGuiColorEditFlags_NoInputs);\n'
    f'{INDENT24}ImGui::PopStyleColor(1);\n'
    f'{INDENT24}if (ImGui::IsItemHovered())\n'
    f'{INDENT24}    ImGui::SetTooltip("Falling snowflakes or rain streaks across the menu background. Settings are saved with your config.");\n'
    f'{INDENT20}}}\n'
    f'{INDENT20}ImGui::EndChild();\n'
)

if src.count(OLD3) != 1:
    sys.exit(f'[abort] anchor3 (Stream Proof block) found {src.count(OLD3)} times (expected 1)')
src = src.replace(OLD3, NEW3, 1)

with open(PATH, 'w', encoding='utf-8') as f:
    f.write(src)

print(f'[ok] patched {PATH}, {len(src)} bytes')
