"""
Round 4 patcher for renderer.cpp.

Reviewer flagged a regression: the one-time SyncFromOptions bootstrap
breaks runtime config load. After LoadConfig() updates Options::Weather::*,
the static bootstrap flag stays true-but-flipped, so SyncToOptions on the
next frame clobbers the freshly-loaded Options back to stale Engine state.

Fix:
  1. Make the bootstrap guard file-scope (inline bool g_MenuWeatherNeedsBootstrap)
     so LoadConfig call sites can flip it back to true.
  2. Add MenuWeather::Rebootstrap() that clears engine state + re-syncs
     from Options.
  3. After every LoadConfig(...) == true in renderer.cpp, the Configs tab
     also forces the rebootstrap.
"""

import sys

FILE = "Seraph/overlay/renderer.cpp"

with open(FILE, "rb") as f:
    raw = f.read()
src = raw.replace(b"\r\n", b"\n").decode("utf-8")

original_size = len(src)
print(f"[patch] loaded {FILE} (raw {len(raw)} bytes; LF-normalized {original_size})")

# ---------------------------------------------------------------------------
# PATCH R1: add file-scope bootstrap flag alongside MenuFonts namespace,
# plus add MenuWeather::Rebootstrap() helper right after SyncToOptions.
# ---------------------------------------------------------------------------
R1_OLD = """namespace MenuFonts
{
    inline ImFont* Fonts[7] = {};
    inline const char* Names[7] = {
        "Verdana", "Segoe UI", "Tahoma", "Arial",
        "Georgia", "Calibri", "Consolas"
    };
    inline int Count = 0;
}"""

R1_NEW = """namespace MenuFonts
{
    inline ImFont* Fonts[7] = {};
    inline const char* Names[7] = {
        "Verdana", "Segoe UI", "Tahoma", "Arial",
        "Georgia", "Calibri", "Consolas"
    };
    inline int Count = 0;
}

// Bootstrap guard for MenuWeather engine state. Flipped to true during
// one-time startup and re-flipped to true after a successful LoadConfig so
// the engine re-reads the freshly-loaded Options::Weather::*. Without this,
// SyncToOptions() in Update() would clobber the just-loaded Options with
// stale Engine values on subsequent frames.
inline bool g_MenuWeatherNeedsBootstrap = true;"""

if R1_OLD not in src:
    print("[patch] R1 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(R1_OLD, R1_NEW, 1)
print("[patch] R1 added file-scope g_MenuWeatherNeedsBootstrap")

R2_OLD = """    // Engine -> Options mirror. Runs each Update so widget-driven changes
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
    }"""

R2_NEW = """    // Engine -> Options mirror. Runs each Update so widget-driven changes
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

    // Public re-bootstrap entry point. Clears engine runtime state and
    // re-reads the persistent Options::Weather::* values. Called after
    // LoadConfig so a freshly-loaded config takes effect instead of
    // being clobbered by SyncToOptions from a stale Engine snapshot.
    inline void Rebootstrap()
    {
        particles.clear();
        initialised = false;
        lastRenderedIntensity = 0;
        lastRenderedSpeed = -1.f;
        lastRenderedWind = -1.f;
        SyncFromOptions();
    }"""

if R2_OLD not in src:
    print("[patch] R2 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(R2_OLD, R2_NEW, 1)
print("[patch] R2 added MenuWeather::Rebootstrap()")

# ---------------------------------------------------------------------------
# PATCH R3: replace the local-static bootstrap guard in the main loop with
# the file-scope g_MenuWeatherNeedsBootstrap flag.
# ---------------------------------------------------------------------------
R3_OLD = """        // One-shot bootstrap from persistent Options on the FIRST frame only.
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

R3_NEW = """        // Bootstrap from persistent Options on the first frame, and again
        // after a runtime LoadConfig (Configs tab) completes. The latter
        // is signalled by the Configs tab setting g_MenuWeatherNeedsBootstrap
        // back to true so the engine picks up freshly-loaded values instead
        // of SyncToOptions clobbering them with stale Engine state.
        if (g_MenuWeatherNeedsBootstrap)
        {
            MenuWeather::Rebootstrap();
            g_MenuWeatherNeedsBootstrap = false;
        }
        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);"""

if R3_OLD not in src:
    print("[patch] R3 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(R3_OLD, R3_NEW, 1)
print("[patch] R3 replaced local-static with file-scope g_MenuWeatherNeedsBootstrap")

# ---------------------------------------------------------------------------
# PATCH R4: after every successful LoadConfig(name) in the Configs tab body,
# re-flag the bootstrap so the engine picks up the new Options values.
# There are exactly two such LoadConfig calls (tappable list row + Load
# button). We transform them in-place.
# ---------------------------------------------------------------------------
R4_OLD = """                if (LoadConfig(name))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Loaded " + name;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to load this config");"""

R4_NEW = """                if (LoadConfig(name))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Loaded " + name;
                    g_MenuWeatherNeedsBootstrap = true;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to load this config");"""

if R4_OLD not in src:
    print("[patch] R4 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(R4_OLD, R4_NEW, 1)
print("[patch] R4 flagged rebootstrap after LoadConfig (tappable list row)")

R5_OLD = """                if (LoadConfig(name))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Loaded " + name;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 3));

        if (ImGui::Button("Save", ImVec2(-1, 24)))"""

R5_NEW = """                if (LoadConfig(name))
                {
                    configStatusSuccess = true;
                    configStatusMessage = "Loaded " + name;
                    g_MenuWeatherNeedsBootstrap = true;
                }
                else
                {
                    configStatusSuccess = false;
                    configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 3));

        if (ImGui::Button("Save", ImVec2(-1, 24)))"""

if R5_OLD not in src:
    print("[patch] R5 anchor MISS", file=sys.stderr)
    sys.exit(1)
src = src.replace(R5_OLD, R5_NEW, 1)
print("[patch] R5 flagged rebootstrap after LoadConfig (Load button)")

# ---------------------------------------------------------------------------
# Write back with CRLF preserved.
# ---------------------------------------------------------------------------
out_bytes = src.encode("utf-8").replace(b"\n", b"\r\n")
with open(FILE, "wb") as f:
    f.write(out_bytes)
print(f"[patch] wrote {FILE} ({len(out_bytes)} bytes, delta {len(out_bytes) - len(raw):+d})")
