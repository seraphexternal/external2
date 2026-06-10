"""
Minimal renderer.cpp signature fixer (third pass).

The first-pass patch inserted
    MenuWeather::Update();           (no args - won't compile)
    MenuWeather::Render(drawList);   (missing origin + size)
but the actual namespace signatures are
    inline void Update(float maxX, float maxY);
    inline void Render(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size);

This script fixes ONLY the two broken call sites and injects a small
MenuWeather::SyncFromOptions() helper that copies the persistent
Options::Weather values into the engine state each frame. We leave
the Misc tab UI bindings (still pointing at MenuWeather::) alone --
they keep the slider feedback responsive, with one extra one-line
back-sync inside SyncFromOptions that mirrors user-driven MenuWeather
changes back into Options::Weather so JSON save/load works.
"""
import sys

PATH = 'Seraph/overlay/renderer.cpp'

with open(PATH, 'r', encoding='utf-8') as f:
    src = f.read()

# ---- Fix Update call ----
OLD_A = '        MenuWeather::Update();\n'
NEW_A = (
    '        MenuWeather::SyncFromOptions();\n'
    '        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);\n'
)
if src.count(OLD_A) != 1:
    sys.exit(f'[abort] A found {src.count(OLD_A)} times')
src = src.replace(OLD_A, NEW_A, 1)

# ---- Fix Render call ----
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

# ---- Inject SyncFromOptions helper just above Update definition ----
OLD_D = '    inline void Update(float maxX, float maxY)'
NEW_D = (
    '    // Copy persistent Options::Weather values into engine state\n'
    '    // once per frame. Defensive clamps keep stale configs sane\n'
    '    // (Type in {0,1}, Intensity in [64, 2000]). Also mirrors\n'
    '    // UI-driven MenuWeather changes BACK into Options::Weather\n'
    '    // so JSON save/load picks them up.\n'
    '    inline void SyncFromOptions()\n'
    '    {\n'
    '        Enabled       = Options::Weather::Enabled;\n'
    '        Type          = (Options::Weather::Type == 0 || Options::Weather::Type == 1) ? Options::Weather::Type : 0;\n'
    '        Intensity     = Options::Weather::Intensity < 64 ? 64 : (Options::Weather::Intensity > 2000 ? 2000 : Options::Weather::Intensity);\n'
    '        Speed         = Options::Weather::Speed;\n'
    '        Wind          = Options::Weather::Wind;\n'
    '        SnowSize      = Options::Weather::SnowSize;\n'
    '        RainThickness = Options::Weather::RainThickness;\n'
    '        for (int i = 0; i < 3; ++i)\n'
    '            Color[i] = Options::Weather::Color[i];\n'
    '    }\n'
    '\n'
    '    inline void Update(float maxX, float maxY)'
)
if src.count(OLD_D) != 1:
    sys.exit(f'[abort] D found {src.count(OLD_D)} times')
src = src.replace(OLD_D, NEW_D, 1)

# ---- Add back-sync at end of Update so the JSON snapshot stays accurate ----
# The Update function ends with a closing brace at indent 4 inside the
# namespace. Easier: anchor on a unique trailing brace. We pick a
# distinctive statement that's near the bottom. Look for
# `lastRenderedSpeed = Speed;` (the last cached-state record).
BACK_SYNC_OLD = '        lastRenderedSpeed = Speed;'
BACK_SYNC_NEW = (
    '        lastRenderedSpeed = Speed;\n'
    '        // Mirror user-tweakable engine-side state into the\n'
    '        // persistent Options snapshot so JSON save/load reflects\n'
    '        // the latest UI changes.\n'
    '        Options::Weather::Enabled       = Enabled;\n'
    '        Options::Weather::Type          = Type;\n'
    '        Options::Weather::Intensity     = Intensity;\n'
    '        Options::Weather::Speed         = Speed;\n'
    '        Options::Weather::Wind          = Wind;\n'
    '        Options::Weather::SnowSize      = SnowSize;\n'
    '        Options::Weather::RainThickness = RainThickness;\n'
    '        for (int i = 0; i < 3; ++i)\n'
    '            Options::Weather::Color[i] = Color[i];'
)
n = src.count(BACK_SYNC_OLD)
if n != 1:
    sys.exit(f'[abort] back-sync anchor found {n} times')
src = src.replace(BACK_SYNC_OLD, BACK_SYNC_NEW, 1)

with open(PATH, 'w', encoding='utf-8') as f:
    f.write(src)

print(f'[ok] patched {PATH}, {len(src)} bytes')
