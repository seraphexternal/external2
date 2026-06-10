"""
Renderer.cpp UI polish patcher (round 3 - CRLF-safe).

Repeats the prior work but normalizes CRLF in the source so anchors use
LF (Python string matches). Writes back with CRLF preserved.
"""

import sys

FILE = "Seraph/overlay/renderer.cpp"

# Read as bytes, normalize CRLF -> LF
with open(FILE, "rb") as f:
    raw = f.read()
src = raw.replace(b"\r\n", b"\n").decode("utf-8")

original_size = len(src)
print(f"[patch] loaded {FILE} (raw {len(raw)} bytes; LF-normalized {original_size})")

# ---------------------------------------------------------------------------
# PATCH 1: add MenuFonts namespace at file scope
# (inserts after the last global device pointer, before the MenuWeather
# comment block).
# ---------------------------------------------------------------------------
P1_OLD = """ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// -----------------------------------------------------------------------------
// Menu weather (snow / rain) effect. This is a self-contained visual feature"""

P1_NEW = """ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// -----------------------------------------------------------------------------
// MenuFonts: file-scope mirror of the menu font choices pre-loaded into the
// ImGui font atlas at startup. The Misc tab's "Menu Font" combo just picks
// an index here, and the per-frame menu drawing in ShowImgui PushFont/PopFont
// the chosen entry so the change shows up live (no atlas rebuild).
// -----------------------------------------------------------------------------
namespace MenuFonts
{
    inline ImFont* Fonts[7] = {};
    inline const char* Names[7] = {
        "Verdana", "Segoe UI", "Tahoma", "Arial",
        "Georgia", "Calibri", "Consolas"
    };
    inline int Count = 0;
}

// -----------------------------------------------------------------------------
// Menu weather (snow / rain) effect. This is a self-contained visual feature"""

if P1_OLD not in src:
    print("[patch] P1 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(P1_OLD, P1_NEW, 1)
print("[patch] P1 inserted MenuFonts namespace")

# ---------------------------------------------------------------------------
# PATCH 2: mirror loaded fonts into MenuFonts after init pre-load
# ---------------------------------------------------------------------------
P2_OLD = """    // Apply current font selection; clamp to the loaded count so an out-of-
    // range value falls back gracefully to the first entry (Verdana).
    if (Options::Misc::MenuFont >= menuFontCount || Options::Misc::MenuFont < 0)
        Options::Misc::MenuFont = 0;
    ImFont* font = (menuFontCount > 0 && menuFonts[Options::Misc::MenuFont].font)
        ? menuFonts[Options::Misc::MenuFont].font
        : baseFont;
    io.FontDefault = font;"""

P2_NEW = """    // Mirror the loaded fonts into the file-scope MenuFonts namespace so
    // ShowImgui can switch between them at runtime via PushFont/PopFont
    // (instead of rebuilding the font atlas, which would stall the renderer).
    for (int i = 0; i < menuFontCount && i < (int)(sizeof(MenuFonts::Fonts)/sizeof(MenuFonts::Fonts[0])); ++i)
        MenuFonts::Fonts[i] = menuFonts[i].font;
    MenuFonts::Count = menuFontCount;

    // Apply current font selection; clamp to the loaded count so an out-of-
    // range value falls back gracefully to the first entry (Verdana).
    if (Options::Misc::MenuFont >= menuFontCount || Options::Misc::MenuFont < 0)
        Options::Misc::MenuFont = 0;
    ImFont* font = (menuFontCount > 0 && menuFonts[Options::Misc::MenuFont].font)
        ? menuFonts[Options::Misc::MenuFont].font
        : baseFont;
    io.FontDefault = font;"""

if P2_OLD not in src:
    print("[patch] P2 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(P2_OLD, P2_NEW, 1)
print("[patch] P2 mirrored loaded fonts into MenuFonts")

# ---------------------------------------------------------------------------
# PATCH 3: add SyncToOptions helper + persistent back-sync in Update()
# ---------------------------------------------------------------------------
P3_OLD = """    // Copy persistent Options::Weather values into engine state
    // once per frame. Defensive clamps keep stale configs sane
    // (Type in {0,1}, Intensity in [64, 2000]). Also mirrors
    // UI-driven MenuWeather changes BACK into Options::Weather
    // so JSON save/load picks them up.
    inline void SyncFromOptions()
    {
        Enabled       = Options::Weather::Enabled;
        Type          = (Options::Weather::Type == 0 || Options::Weather::Type == 1) ? Options::Weather::Type : 0;
        Intensity     = Options::Weather::Intensity < 64 ? 64 : (Options::Weather::Intensity > 2000 ? 2000 : Options::Weather::Intensity);
        Speed         = Options::Weather::Speed;
        Wind          = Options::Weather::Wind;
        SnowSize      = Options::Weather::SnowSize;
        RainThickness = Options::Weather::RainThickness;
        for (int i = 0; i < 3; ++i)
            Color[i] = Options::Weather::Color[i];
    }

    inline void Update(float maxX, float maxY)
    {
        if (!Enabled)
        {
            if (!particles.empty())
            {
                particles.clear();
                initialised = false;
            }
            return;
        }

        if (!initialised || lastRenderedIntensity != Intensity
            || lastRenderedSpeed != Speed || lastRenderedWind != Wind)
        {
            RebuildParticleBuffer(maxX, maxY);
        }

        for (auto& p : particles)
        {
            p.x += p.vx;
            p.y += p.vy;

            if (p.y > maxY + 12.f)
                SeedParticle(p, false, maxX, maxY);
            if (p.x < -8.f)            p.x = maxX + 4.f;
            else if (p.x > maxX + 8.f) p.x = -4.f;
        }
    }"""

P3_NEW = """    // Copy persistent Options::Weather values into engine state.
    // Called exactly ONCE on the first frame (one-time bootstrap); after
    // that, MenuWeather::* is the source of truth and SyncToOptions()
    // runs every frame inside Update() to back the engine state into
    // Options for JSON persistence.
    //
    // Defensive clamps keep stale configs sane (Type in {0,1}, Intensity
    // in [64, 2000]).
    inline void SyncFromOptions()
    {
        Enabled       = Options::Weather::Enabled;
        Type          = (Options::Weather::Type == 0 || Options::Weather::Type == 1) ? Options::Weather::Type : 0;
        Intensity     = Options::Weather::Intensity < 64 ? 64 : (Options::Weather::Intensity > 2000 ? 2000 : Options::Weather::Intensity);
        Speed         = Options::Weather::Speed;
        Wind          = Options::Weather::Wind;
        SnowSize      = Options::Weather::SnowSize;
        RainThickness = Options::Weather::RainThickness;
        for (int i = 0; i < 3; ++i)
            Color[i] = Options::Weather::Color[i];
    }

    // Engine -> Options mirror. Runs each Update so widget-driven changes
    // (toggles, slider drags, color edits) flow into the persistent
    // Options snapshot without the UI ever having to write to Options.
    inline void SyncToOptions()
    {
        Options::Weather::Enabled       = Enabled;
        Options::Weather::Type          = Type;
        Options::Weather::Intensity     = Intensity;
        Options::Weather::Speed         = Speed;
        Options::Weather::Wind          = Wind;
        Options::Weather::SnowSize      = SnowSize;
        Options::Weather::RainThickness = RainThickness;
        for (int i = 0; i < 3; ++i)
            Options::Weather::Color[i] = Color[i];
    }

    inline void Update(float maxX, float maxY)
    {
        SyncToOptions();
        if (!Enabled)
        {
            if (!particles.empty())
            {
                particles.clear();
                initialised = false;
            }
            return;
        }

        if (!initialised || lastRenderedIntensity != Intensity
            || lastRenderedSpeed != Speed || lastRenderedWind != Wind)
        {
            RebuildParticleBuffer(maxX, maxY);
            return;
        }

        for (auto& p : particles)
        {
            p.x += p.vx;
            p.y += p.vy;

            if (p.y > maxY + 12.f)
                SeedParticle(p, false, maxX, maxY);
            if (p.x < -8.f)            p.x = maxX + 4.f;
            else if (p.x > maxX + 8.f) p.x = -4.f;
        }
    }"""

if P3_OLD not in src:
    print("[patch] P3 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(P3_OLD, P3_NEW, 1)
print("[patch] P3 added SyncToOptions + persistent back-sync in Update")

# ---------------------------------------------------------------------------
# PATCH 4: main loop - replace per-frame SyncFromOptions with one-time bootstrap
# ---------------------------------------------------------------------------
P4_OLD = """        Globals::Viewport::Update();
        MenuWeather::SyncFromOptions();
        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);"""

P4_NEW = """        Globals::Viewport::Update();

        // One-shot bootstrap from persistent Options on the FIRST frame only.
        // After this runs, MenuWeather::* is the live engine state and Update()
        // back-syncs to Options every frame; the old per-frame SyncFromOptions
        // was destroying the user's enable-checkbox writes.
        static bool menuWeatherBootstrapped = false;
        if (!menuWeatherBootstrapped)
        {
            MenuWeather::SyncFromOptions();
            menuWeatherBootstrapped = true;
        }
        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);"""

if P4_OLD not in src:
    print("[patch] P4 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(P4_OLD, P4_NEW, 1)
print("[patch] P4 replaced per-frame SyncFromOptions with one-time bootstrap")

# ---------------------------------------------------------------------------
# PATCH 5: switch the menu PushFont to a per-frame MenuFonts lookup
# ---------------------------------------------------------------------------
P5_OLD = """                ImGui::PushFont(font);
                draw->AddText(ImVec2(p.x + 9.5, p.y + 7), ImColor(main_color), "Seraph");"""

P5_NEW = """                // Use the chosen menu font. Fall back to ImGui's default font
                // if MenuFonts hasn't populated yet (only on the very first
                // frame, before pre-load completes).
                ImFont* menuFont = (MenuFonts::Count > 0
                    && Options::Misc::MenuFont >= 0
                    && Options::Misc::MenuFont < MenuFonts::Count
                    && MenuFonts::Fonts[Options::Misc::MenuFont])
                    ? MenuFonts::Fonts[Options::Misc::MenuFont]
                    : io.FontDefault;
                ImGui::PushFont(menuFont);
                draw->AddText(ImVec2(p.x + 9.5, p.y + 7), ImColor(main_color), "Seraph");"""

if P5_OLD not in src:
    print("[patch] P5 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(P5_OLD, P5_NEW, 1)
print("[patch] P5 switched menu PushFont to dynamic MenuFonts lookup")

# ---------------------------------------------------------------------------
# PATCH 6: Misc tab Main Group cleanup + rename Background Weather -> Menu Effect
# ---------------------------------------------------------------------------
P6_OLD = """                else if (tab == 2)
                {
                    // Misc tab - Local settings only
                    ImGui::SetCursorPosY(38);
                    ImGui::SetCursorPosX(122);
                    ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                    {
                        ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                        ImGui::Checkbox("Headless", &Options::ESP::Headless);
                        ImGui::Checkbox("Show FOV", &Options::Aimbot::ShowFOV);
                        ImGui::Checkbox("Show FOV Fill", &Options::Aimbot::ShowFOVFill);
                        ImGui::Checkbox("Crosshair", &Options::Crosshair::Enabled);
                        ImGui::Checkbox("Camera FOV", &Options::Misc::FOVEnabled);
                        ImGui::Checkbox("Cache NPCs", &Options::Misc::CacheNPCs);
                        ImGui::Checkbox("Keybind List", &Options::Misc::KeybindList);
                        ImGui::Checkbox("Stream Proof", &Options::Misc::StreamProof);
                        ImGui::PopStyleColor(1);

                        ImGui::Dummy(ImVec2(0, 8));
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0, 8));

                        ImGui::TextColored(main_color, "Menu Font");
                        static const char* menuFontNames[] = {
                            "Verdana", "Segoe UI", "Tahoma", "Arial",
                            "Georgia", "Calibri", "Consolas"
                        };
                        ImGui::Combo("Font", &Options::Misc::MenuFont, menuFontNames, IM_ARRAYSIZE(menuFontNames));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Live-switches the menu font at runtime. Takes effect on next menu open.");

                        ImGui::Dummy(ImVec2(0, 8));
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0, 8));

                        ImGui::TextColored(main_color, "Background Weather");
                        ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                        ImGui::Checkbox("Enable Weather Effect", &MenuWeather::Enabled);
                        ImGui::PopStyleColor(1);

                        const char* weatherKinds[] = { "Snow", "Rain" };
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                        ImGui::Combo("Weather Type", &MenuWeather::Type, weatherKinds, 2);
                        ImGui::SliderInt("Intensity", &MenuWeather::Intensity, 64, 2000, "%d particles");
                        ImGui::SliderFloat("Fall Speed", &MenuWeather::Speed, 0.2f, 6.0f, "%.2fx");
                        ImGui::SliderFloat("Wind", &MenuWeather::Wind, -3.0f, 3.0f, "%.2fx");
                        ImGui::SliderFloat("Snow Size", &MenuWeather::SnowSize, 0.5f, 4.0f, "%.1f px");
                        ImGui::SliderFloat("Rain Thickness", &MenuWeather::RainThickness, 0.5f, 3.0f, "%.1f px");
                        ImGui::ColorEdit3("Particle Color", MenuWeather::Color, ImGuiColorEditFlags_NoInputs);
                        ImGui::PopStyleColor(1);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Falling snowflakes or rain streaks across the menu background. Settings are saved with your config.");
                    }
                    ImGui::EndChild();"""

P6_NEW = """                else if (tab == 2)
                {
                    // Misc tab - Local settings only
                    ImGui::SetCursorPosY(38);
                    ImGui::SetCursorPosX(122);
                    ImGui::MenuChild("Main Group", ImVec2(226, 337), false);
                    {
                        // ---- Local behaviour toggles ----
                        ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                        ImGui::TextColored(main_color, "Local");
                        ImGui::Dummy(ImVec2(0, 2));
                        ImGui::Checkbox("Headless",       &Options::ESP::Headless);
                        ImGui::Checkbox("Show FOV",       &Options::Aimbot::ShowFOV);
                        ImGui::Checkbox("Show FOV Fill",  &Options::Aimbot::ShowFOVFill);
                        ImGui::Checkbox("Crosshair",      &Options::Crosshair::Enabled);
                        ImGui::Checkbox("Camera FOV",     &Options::Misc::FOVEnabled);
                        ImGui::Checkbox("Cache NPCs",     &Options::Misc::CacheNPCs);
                        ImGui::Checkbox("Keybind List",   &Options::Misc::KeybindList);
                        ImGui::Checkbox("Stream Proof",   &Options::Misc::StreamProof);
                        ImGui::PopStyleColor(1);

                        ImGui::Dummy(ImVec2(0, 6));
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0, 6));

                        // ---- Menu Font ----
                        ImGui::TextColored(main_color, "Menu Font");
                        ImGui::Dummy(ImVec2(0, 2));
                        ImGui::Combo("Font", &Options::Misc::MenuFont,
                            MenuFonts::Names, IM_ARRAYSIZE(MenuFonts::Names));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Live-switches the menu typography. Applies immediately.");

                        ImGui::Dummy(ImVec2(0, 6));
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0, 6));

                        // ---- Menu Effect (snow / rain particles) ----
                        ImGui::PushStyleColor(ImGuiCol_CheckMark, main_color);
                        ImGui::TextColored(main_color, "Menu Effect");
                        ImGui::Dummy(ImVec2(0, 2));
                        ImGui::Checkbox("Enable", &MenuWeather::Enabled);
                        ImGui::PopStyleColor(1);

                        static const char* weatherKinds[] = { "Snow", "Rain" };
                        ImGui::Combo("Type", &MenuWeather::Type, weatherKinds, 2);

                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, main_color);
                        ImGui::SliderInt ("Intensity",       &MenuWeather::Intensity,     64,   2000, "%d particles");
                        ImGui::SliderFloat("Fall Speed",     &MenuWeather::Speed,         0.2f, 6.0f,  "%.2fx");
                        ImGui::SliderFloat("Wind",           &MenuWeather::Wind,         -3.f,  3.f,   "%.2fx");
                        ImGui::SliderFloat("Snow Size",      &MenuWeather::SnowSize,      0.5f, 4.0f,  "%.1f px");
                        ImGui::SliderFloat("Rain Thickness", &MenuWeather::RainThickness, 0.5f, 3.0f,  "%.1f px");
                        ImGui::ColorEdit3 ("Particle Color", MenuWeather::Color, ImGuiColorEditFlags_NoInputs);
                        ImGui::PopStyleColor(1);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Falling snowflakes or rain streaks across the menu background. Settings are saved with your config.");
                    }
                    ImGui::EndChild();"""

if P6_OLD not in src:
    print("[patch] P6 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(P6_OLD, P6_NEW, 1)
print("[patch] P6 cleaned Misc tab + renamed Background Weather -> Menu Effect")

# ---------------------------------------------------------------------------
# Write back as bytes, restoring CRLF to match the rest of the codebase.
# ---------------------------------------------------------------------------
out_bytes = src.encode("utf-8").replace(b"\n", b"\r\n")
with open(FILE, "wb") as f:
    f.write(out_bytes)

print(f"[patch] wrote {FILE} ({len(out_bytes)} bytes, delta {len(out_bytes) - len(raw):+d})")
