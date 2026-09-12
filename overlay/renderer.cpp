// Windows headers define the `min` / `max` preprocessor macros
// which clash with `std::max` / `std::min` used by the
// MenuWeather particle engine. Defining NOMINMAX FIRST ensures
// every windows-family header (transitively pulled in by the
// project headers on lines below) respects the opt-out.
#define NOMINMAX
#include "renderer.h"
#include "ragesubtabs.h"
#include "ui.h"
#include "fonts_ex.h"
#include "../features/chams.h"
#include "../rbx/configs/configs.h"
#include "../features/desync.h"
#include "../features/ragebot.h"
#include "../features/aimbot.h"
#include "../features/playerfilter.h"
#include "../features/orbit.h"
#include "../features/PlayerAvatars.h"
#include "animation.h"
#include "explorer/explorer_window.h"
#include "explorer/lua_editor.h"
#include "executor/executor.h"
#include "injector.h"
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#include "../seraph_log.h"

#include <random>
#include <algorithm>
#include <cctype>

#ifdef _MSC_VER
#pragma warning (disable: 26812)    // [Static Analyzer] The enum type 'xxx' is unscoped. Prefer 'enum class' over 'enum' (Enum.3). ImGui uses unscoped enum flag bitmasks heavily.
#endif

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Persistent script text box for the Executor tab (survives tab switches).
static editor::lua_editor g_executorEditor;

// -----------------------------------------------------------------------------
// MenuFonts: file-scope mirror of the menu font choices pre-loaded into the
// ImGui font atlas at startup. The Misc tab's "Menu Font" combo just picks
// an index here, and the per-frame menu drawing in ShowImgui PushFont/PopFont
// the chosen entry so the change shows up live (no atlas rebuild).
// -----------------------------------------------------------------------------
namespace MenuFonts
{
inline ImFont* Fonts[8] = {};
inline const char* Names[8] = {
"Nunito", "Verdana", "Segoe UI", "Tahoma", "Arial",
"Georgia", "Calibri", "Consolas"
};
inline int Count = 0;
}

// -----------------------------------------------------------------------------
// Menu themes: built-in presets for the menu background / panel / accent colors.
// Index 0 is "Custom" (uses Options::Misc::MenuBgColor / MenuPanelColor / accents).
// -----------------------------------------------------------------------------
namespace MenuThemes
{
struct Theme
{
const char* name;
float bg[3];
float panel[3];
float accent[3];
float accent2[3];
bool gradient;
};

inline const Theme Presets[] = {
// Custom — pure blue accent
{ "Custom",        {0.015f,0.025f,0.045f}, {0.040f,0.060f,0.100f}, {0.300f,0.550f,1.000f}, {0.200f,0.400f,0.850f}, false },
// Midnight — deep navy, electric blue + purple gradient
{ "Midnight",      {0.015f,0.020f,0.038f}, {0.038f,0.050f,0.085f}, {0.340f,0.560f,1.000f}, {0.640f,0.400f,1.000f}, true  },
// Carbon — near-black, cool white accent, monochrome
{ "Carbon",        {0.014f,0.014f,0.017f}, {0.052f,0.052f,0.060f}, {0.870f,0.880f,0.920f}, {0.520f,0.550f,0.620f}, false },
// Sunset — dark burgundy, coral + gold gradient
{ "Sunset",        {0.045f,0.022f,0.035f}, {0.090f,0.042f,0.068f}, {1.000f,0.480f,0.340f}, {1.000f,0.780f,0.300f}, true  },
// Matrix — dark forest, neon green accent
{ "Matrix",        {0.010f,0.028f,0.016f}, {0.028f,0.062f,0.038f}, {0.240f,1.000f,0.380f}, {0.580f,1.000f,0.240f}, false },
// Ice — cool blue, ice blue + pale cyan gradient
{ "Ice",           {0.018f,0.035f,0.052f}, {0.060f,0.098f,0.130f}, {0.480f,0.870f,1.000f}, {0.760f,0.940f,1.000f}, true  },
// Crimson — deep red, bright red + orange gradient
{ "Crimson",       {0.048f,0.010f,0.016f}, {0.115f,0.028f,0.048f}, {1.000f,0.200f,0.280f}, {1.000f,0.530f,0.260f}, true  },
// Lilac — dark violet, bright purple + pink gradient
{ "Lilac",         {0.032f,0.020f,0.048f}, {0.080f,0.060f,0.125f}, {0.700f,0.480f,1.000f}, {1.000f,0.560f,0.900f}, true  },
};

inline int Count = (int)(sizeof(Presets) / sizeof(Presets[0]));

// Resolve the currently-active colors (preset or custom) into out params.
inline void Resolve(const float*& bg, const float*& panel, const float*& accent, const float*& accent2, bool& gradient)
{
int idx = Options::Misc::MenuTheme;
if (idx > 0 && idx < Count)
{
bg = Presets[idx].bg;
panel = Presets[idx].panel;
accent = Presets[idx].accent;
accent2 = Presets[idx].accent2;
gradient = Presets[idx].gradient;
}
else
{
bg = Options::Misc::MenuBgColor;
panel = Options::Misc::MenuPanelColor;
accent = Options::Misc::MenuAccentColor;
accent2 = Options::Misc::MenuAccentColor2;
gradient = Options::Misc::MenuGradient;
}
}

// Rainbox/whatever: apply the theme accent colors to the in-game feature
// colors so switching themes re-styles the whole overlay. Each feature keeps
// its own picker afterwards, so a theme change is the only thing that rewrites
// these — per-feature customization is still fully available.
inline void ApplyFeatureColors(const float* accent, const float* accent2)
{
const float r = accent[0], g = accent[1], b = accent[2];
const float r2 = accent2[0], g2 = accent2[1], b2 = accent2[2];

// ESP primary visuals follow the theme accent.
float* esp = &Options::ESP::Color[0];
esp[0] = r; esp[1] = g; esp[2] = b;
float* esp2 = &Options::ESP::BoxColor[0];
esp2[0] = r; esp2[1] = g; esp2[2] = b;
float* esp3 = &Options::ESP::CornerColor[0];
esp3[0] = r; esp3[1] = g; esp3[2] = b;
float* esp4 = &Options::ESP::SkeletonColor[0];
esp4[0] = r; esp4[1] = g; esp4[2] = b;
float* esp5 = &Options::ESP::ESP3DColor[0];
esp5[0] = r; esp5[1] = g; esp5[2] = b;
float* esp6 = &Options::ESP::HeadCircleColor[0];
esp6[0] = r; esp6[1] = g; esp6[2] = b;

// Accent2 goes to secondary / distance / tracer elements.
float* esc = &Options::ESP::DistanceColor[0];
esc[0] = r2; esc[1] = g2; esc[2] = b2;
float* esc2 = &Options::ESP::TracerColor[0];
esc2[0] = r2; esc2[1] = g2; esc2[2] = b2;
float* esc3 = &Options::ESP::HeadDotColor[0];
esc3[0] = r2; esc3[1] = g2; esc3[2] = b2;

// Chams + aimbot also pick up the theme.
float* cc = &Options::Chams::FillColor[0];
cc[0] = r; cc[1] = g; cc[2] = b; cc[3] = 0.5f;
float* cc2 = &Options::Chams::OutlineColor[0];
cc2[0] = r2; cc2[1] = g2; cc2[2] = b2; cc2[3] = 1.0f;
float* ac = &Options::Aimbot::FOVColor[0];
ac[0] = r; ac[1] = g; ac[2] = b;
float* ac2 = &Options::Aimbot::FOVFillColor[0];
ac2[0] = r; ac2[1] = g; ac2[2] = b; ac2[3] = 0.1f;
float* ac3 = &Options::Aimbot::TargetLineColor[0];
ac3[0] = r2; ac3[1] = g2; ac3[2] = b2;

// Crosshair + Desync visuals.
float* cr = &Options::Crosshair::Color[0];
cr[0] = r; cr[1] = g; cr[2] = b;
float* dv = &Options::Desync::VisualColor[0];
dv[0] = r; dv[1] = g; dv[2] = b;
float* dv2 = &Options::Desync::LineColor[0];
dv2[0] = r2; dv2[1] = g2; dv2[2] = b2;
}
}

// Bootstrap guard for MenuWeather engine state. Flipped to true during
// one-time startup and re-flipped to true after a successful LoadConfig so
// the engine re-reads the freshly-loaded Options::Weather::*. Without this,
// SyncToOptions() in Update() would clobber the just-loaded Options with
// stale Engine values on subsequent frames.
inline bool g_MenuWeatherNeedsBootstrap = true;

// -----------------------------------------------------------------------------
// Menu weather (snow / rain) effect. This is a self-contained visual feature
// drawn on top of the menu via ImGui::GetWindowDrawList(). State is stored
// here with sensible defaults so it survives without needing entries in
// options.h. All options are exposed in the Misc -> Menu -> Weather panel.
//
// Performance: 200-400 particles is comfortable at 60fps. The default 150
// keeps GPU draw-call cost near-zero on top of the menu's existing draw list.
// -----------------------------------------------------------------------------
namespace MenuWeather
{
struct Particle
{
float x, y;
float vx, vy;
};

inline bool Enabled = false;
inline int  Type = 0;                 // 0 = snow, 1 = rain
inline int  Intensity = 150;          // particle count (capped 64..2000)
inline float Speed = 1.0f;            // vertical velocity multiplier
inline float Wind = 0.0f;             // horizontal drift (units per frame)
inline float Color[3] = { 1.0f, 1.0f, 1.0f };
inline float SnowSize = 1.8f;         // pixel radius of each snowflake
inline float RainThickness = 1.4f;    // pixel width of each rain streak

inline std::vector<Particle> particles;
inline std::mt19937 rng{ std::random_device{}() };
inline bool initialised = false;
inline int lastRenderedIntensity = 0;
inline float lastRenderedSpeed = -1.f;
inline float lastRenderedWind = -1.f;

inline void SeedParticle(Particle& p, bool anywhere, float maxX, float maxY)
{
p.x = static_cast<float>(rng() % std::max(1, static_cast<int>(maxX)));
p.y = anywhere
? static_cast<float>(rng() % std::max(1, static_cast<int>(maxY)))
: -10.f;
const float angleJitter = (static_cast<float>(rng() % 100) / 100.f - 0.5f) * 0.4f;
p.vx = Wind * 0.025f + angleJitter;
p.vy = Speed * (0.6f + static_cast<float>(rng() % 60) / 100.f) * 1.2f;
}

inline void RebuildParticleBuffer(float maxX, float maxY)
{
particles.clear();
const int count = std::clamp(Intensity, 0, 2000);
particles.reserve(count);
for (int i = 0; i < count; ++i)
{
Particle p;
SeedParticle(p, true, maxX, maxY);
particles.push_back(p);
}
initialised = true;
lastRenderedIntensity = Intensity;
lastRenderedSpeed = Speed;
// Mirror user-tweakable engine-side state into the
// persistent Options snapshot so JSON save/load reflects
// the latest UI changes.
Options::Weather::Enabled       = Enabled;
Options::Weather::Type          = Type;
Options::Weather::Intensity     = Intensity;
Options::Weather::Speed         = Speed;
Options::Weather::Wind          = Wind;
Options::Weather::SnowSize      = SnowSize;
Options::Weather::RainThickness = RainThickness;
for (int i = 0; i < 3; ++i)
Options::Weather::Color[i] = Color[i];
lastRenderedWind = Wind;
}

// Copy persistent Options::Weather values into engine state.
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
}

inline void Render(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size)
{
if (!Enabled || particles.empty()) return;

const ImU32 col = IM_COL32(
static_cast<int>(std::clamp(Color[0], 0.f, 1.f) * 255.f),
static_cast<int>(std::clamp(Color[1], 0.f, 1.f) * 255.f),
static_cast<int>(std::clamp(Color[2], 0.f, 1.f) * 255.f),
200);

// Clip to the menu interior so particles cannot leak outside the
// window or overlap game HUD.
drawList->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

if (Type == 0) // Snow: small filled circles
{
for (const auto& p : particles)
drawList->AddCircleFilled(
ImVec2(origin.x + p.x, origin.y + p.y),
SnowSize, col, 8);
}
else // Rain: short slanted streaks
{
const float slant = Wind * 0.5f;
for (const auto& p : particles)
{
const float px = origin.x + p.x;
const float py = origin.y + p.y;
drawList->AddLine(
ImVec2(px, py),
ImVec2(px + slant, py + 8.f),
col, RainThickness);
}
}

drawList->PopClipRect();
}
}



HWND FindRobloxWindow() {
DWORD pid = 0;
if (Memory)
pid = (DWORD)Memory->getProcessId();
if (pid == 0) return nullptr;

HWND result = nullptr;
EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
DWORD wpid = 0;
GetWindowThreadProcessId(hwnd, &wpid);
if (wpid == (DWORD)lParam && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
HWND* out = reinterpret_cast<HWND*>(lParam);
*out = hwnd;
return FALSE;
}
return TRUE;
}, reinterpret_cast<LPARAM>(&result));

return result;
}

bool IsGameOnTop(const std::string& expectedTitle) {
HWND hwnd = GetForegroundWindow();
if (!hwnd) return false;

DWORD fgPid = 0;
GetWindowThreadProcessId(hwnd, &fgPid);
DWORD rbPid = Memory ? (DWORD)Memory->getProcessId() : 0;
if (rbPid != 0)
return fgPid == rbPid;

char windowTitle[256];
int length = GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
if (length == 0) return false;

return expectedTitle == std::string(windowTitle);
}

void HideFromTaskbar(HWND hwnd);

static volatile LONG g_OverlayWheelAccum = 0;
static WNDPROC g_GameWndProc = nullptr;
static HWND g_GameWnd = nullptr;

static LRESULT CALLBACK GameWheelHookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_MOUSEWHEEL)
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta != 0)
            InterlockedExchangeAdd(&g_OverlayWheelAccum, (LONG)((double)delta * 4096.0 / 120.0));
    }
    return CallWindowProcW(g_GameWndProc, hWnd, msg, wParam, lParam);
}

struct WheelForwarderCtx { HWND overlay; HWND best; int bestArea; };

static BOOL CALLBACK EnumWheelForwarderWindows(HWND h, LPARAM lp)
{
    WheelForwarderCtx* c = (WheelForwarderCtx*)lp;
    if (h == c->overlay || !::IsWindowVisible(h))
        return TRUE;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(h, &pid);
    if (pid != ::GetCurrentProcessId())
        return TRUE;
    LONG_PTR ex = ::GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return TRUE;
    RECT rc;
    ::GetWindowRect(h, &rc);
    int area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > c->bestArea)
    {
        c->best = h;
        c->bestArea = area;
    }
    return TRUE;
}

static void InstallWheelForwarder(HWND overlay)
{
    WheelForwarderCtx c = { overlay, nullptr, 0 };
    ::EnumWindows(EnumWheelForwarderWindows, (LPARAM)&c);
    if (!c.best)
        return;
    g_GameWnd = c.best;
    g_GameWndProc = (WNDPROC)::SetWindowLongPtrW(c.best, GWLP_WNDPROC, (LONG_PTR)GameWheelHookWndProc);
}

static void UninstallWheelForwarder()
{
    if (g_GameWnd && g_GameWndProc)
        ::SetWindowLongPtrW(g_GameWnd, GWLP_WNDPROC, (LONG_PTR)g_GameWndProc);
    g_GameWnd = nullptr;
    g_GameWndProc = nullptr;
}

static void ApplyOverlayWindowStyle(HWND hwnd, bool clickThrough)
{
LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

// Topmost + layered at all times so the ESP/menu layer is a visible overlay.
exStyle |= WS_EX_LAYERED | WS_EX_TOPMOST;

// Click-through (transparent to input) when in-game and the menu is closed;
// clickable when the menu is open.
if (clickThrough)
exStyle |= WS_EX_TRANSPARENT;
else
exStyle &= ~WS_EX_TRANSPARENT;

if (Options::Misc::HideFromTabs)
{
exStyle |= WS_EX_TOOLWINDOW;
exStyle &= ~WS_EX_APPWINDOW;
}
else
{
exStyle &= ~WS_EX_TOOLWINDOW;
}

SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);

if (Options::Misc::HideFromTabs)
HideFromTaskbar(hwnd);
}

void SetTransparency(HWND hwnd, bool clickThrough)
{
ApplyOverlayWindowStyle(hwnd, clickThrough);
}

void HideFromTaskbar(HWND hwnd)
{
ITaskbarList* taskbarList = nullptr;
if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList, reinterpret_cast<void**>(&taskbarList))))
{
taskbarList->HrInit();
taskbarList->DeleteTab(hwnd);
taskbarList->Release();
}
}

void DrawNode(RobloxInstance& node)
{
const auto& children = node.GetChildren();
if (children.empty())
{
ImGui::BulletText(node.Name().c_str());
}
else
{
if (ImGui::TreeNode(node.Name().c_str()))
{
for (auto child : children)
{
DrawNode(child);
}
ImGui::TreePop();
}
}
}

static void RenderConfigTab()
{
const float sc = std::clamp(Options::Misc::MenuScale, 0.6f, 2.5f);
UI::ContentHeader("CONFIGS");
static char configNameBuffer[128] = "";
static std::vector<std::string> configsList;
static int selectedConfigIndex = -1;
static std::string configStatusMessage;
static bool configStatusSuccess = true;
static bool listInitialized = false;
static AutoloadSettings autoloadSettings;
static bool autoloadEnabled = false;

if (!listInitialized)
{
configsList = ListConfigFiles();
autoloadSettings = LoadAutoloadSettings();
autoloadEnabled = autoloadSettings.enabled;
listInitialized = true;
}

const float panelY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosX(16.0f * sc);
    if (UI::CollapsibleSection("MANAGE CONFIGS", UI::CardW))
{
UI::labelsection("AUTOLOAD");
if (ImGui::Checkbox("Autoload on startup", &autoloadEnabled))
{
autoloadSettings.enabled = autoloadEnabled;
if (SaveAutoloadSettings(autoloadSettings))
{
configStatusSuccess = true;
configStatusMessage = autoloadEnabled ? "Autoload enabled" : "Autoload disabled";
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Failed to save autoload setting" : Config::lastError;
}
}

if (autoloadSettings.configName.empty())
ImGui::TextDisabled("No autoload config set");
else
ImGui::TextColored(UI::P.accent, "Autoload: %s", autoloadSettings.configName.c_str());

if (ImGui::Button("Refresh List", ImVec2(-1, 24)))
configsList = ListConfigFiles();

char _clsBuf[64]; snprintf(_clsBuf, sizeof(_clsBuf), "CONFIG LIST (%d)", (int)configsList.size()); UI::labelsection(_clsBuf);

const float listHeight = ImGui::GetContentRegionAvail().y;
ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
ImGui::BeginChild("##config_list_scroll", ImVec2(-1, listHeight > 24.0f ? listHeight : 24.0f), false);
for (int i = 0; i < (int)configsList.size(); i++)
{
ImGui::PushID(i);
const bool isAutoload = !autoloadSettings.configName.empty()
&& NormalizeConfigFilename(configsList[i]) == autoloadSettings.configName;

const std::string rowLabel = isAutoload
? (configsList[i] + "  *")
: configsList[i];

ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.06f, 0.06f, 0.06f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.30f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.55f));

if (ImGui::Button(rowLabel.c_str(), ImVec2(-1, 22)))
{
selectedConfigIndex = i;
strncpy_s(configNameBuffer, configsList[i].c_str(), _TRUNCATE);

const std::string name = NormalizeConfigFilename(configsList[i]);
if (LoadConfig(name))
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
ImGui::SetTooltip("Click to load this config");

ImGui::PopStyleColor(3);
ImGui::PopID();
}
ImGui::EndChild();
ImGui::PopStyleVar();
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
    ImGui::SetCursorPosX(16.0f * sc + UI::CardW + 6.0f * sc);
    if (UI::CollapsibleSection("ACTIONS", UI::CardW))
    {
        // Nudge button labels slightly above the vertical center.
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.42f));
        if (!configStatusMessage.empty())
{
const ImVec4 statusColor = configStatusSuccess
? ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 1.0f)
: ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
ImGui::TextWrapped("%s", configStatusMessage.c_str());
ImGui::PopStyleColor();
ImGui::Dummy(ImVec2(0, 4));
}

UI::labelsection("CONFIG NAME");
ImGui::InputText("##config_name_edit", configNameBuffer, IM_ARRAYSIZE(configNameBuffer));
ImGui::Dummy(ImVec2(0, 6));

if (ImGui::Button("Load", ImVec2(-1, 24)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Enter a config name first";
}
else
{
strncpy_s(configNameBuffer, name.c_str(), _TRUNCATE);
if (LoadConfig(name))
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

if (ImGui::Button("Save", ImVec2(-1, 24)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Enter a config name first";
}
else
{
strncpy_s(configNameBuffer, name.c_str(), _TRUNCATE);
if (SaveConfig(name))
{
configsList = ListConfigFiles();
selectedConfigIndex = -1;
for (int i = 0; i < (int)configsList.size(); i++)
{
if (configsList[i] == name)
{
selectedConfigIndex = i;
break;
}
}
configStatusSuccess = true;
configStatusMessage = "Saved " + name;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? ("Failed to save " + name) : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0, 3));

if (ImGui::Button("Set Autoload", ImVec2(-1, 24)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Select or enter a config first";
}
else if (!std::filesystem::exists(GetConfigFilePath(name)))
{
configStatusSuccess = false;
configStatusMessage = "Config not found";
}
else
{
autoloadSettings.configName = name;
autoloadSettings.enabled = true;
autoloadEnabled = true;
if (SaveAutoloadSettings(autoloadSettings))
{
configStatusSuccess = true;
configStatusMessage = "Autoload set to " + name;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Failed to save autoload" : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0, 3));

if (ImGui::Button("Delete", ImVec2(-1, 24)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Enter a config name first";
}
else
{
const std::filesystem::path fullPath = GetConfigFilePath(name);
std::error_code ec;
if (std::filesystem::exists(fullPath, ec) && !ec)
{
std::filesystem::remove(fullPath, ec);
if (ec)
{
configStatusSuccess = false;
configStatusMessage = "Delete failed: " + ec.message();
}
else
{
ClearAutoloadIfMatches(name);
autoloadSettings = LoadAutoloadSettings();
autoloadEnabled = autoloadSettings.enabled;
configNameBuffer[0] = '\0';
selectedConfigIndex = -1;
configsList = ListConfigFiles();
configStatusSuccess = true;
configStatusMessage = "Deleted " + name;
}
}
else
{
configStatusSuccess = false;
configStatusMessage = "Config not found";
}
}
}

UI::labelsection("FILE ACTIONS");

if (ImGui::Button("Import Config...", ImVec2(-1, 24)))
{
std::string pickedPath;
if (OpenWindowsFileDialog(true, pickedPath, "*.json\0*.json\0All Files\0*.*\0", SX("Import Seraph config").c_str()))
{
const bool ok = ImportConfigFromFile(std::filesystem::path(pickedPath));
if (ok)
{
configsList = ListConfigFiles();
selectedConfigIndex = -1;
configStatusSuccess = true;
configStatusMessage = "Imported " + std::filesystem::path(pickedPath).stem().string() + ".json";
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Import failed" : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0, 3));

const std::string exportSource = configsList.empty()
? NormalizeConfigFilename(configNameBuffer)
: (selectedConfigIndex >= 0 && selectedConfigIndex < (int)configsList.size()
? NormalizeConfigFilename(configsList[selectedConfigIndex])
: NormalizeConfigFilename(configNameBuffer));

if (ImGui::Button("Export Config...", ImVec2(-1, 24)))
{
if (exportSource.empty())
{
configStatusSuccess = false;
configStatusMessage = "Select a config to export first";
}
else
{
std::string pickedPath;
const std::string suggestedName = exportSource;
if (OpenWindowsFileDialog(false, pickedPath, "*.json\0*.json\0All Files\0*.*\0", SX("Export Seraph config").c_str(), suggestedName))
{
const bool ok = ExportConfigToFile(exportSource, std::filesystem::path(pickedPath));
if (ok)
{
configStatusSuccess = true;
configStatusMessage = "Exported " + exportSource;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Export failed" : Config::lastError;
}
}
}
}
}
ImGui::PopStyleVar();
UI::CollapsibleEnd();
}

// (OpenWindowsFileDialog + UTF8ToWide / WideToUTF8 live in
// Seraph/rbx/configs/configs.h)

void RenderKeybindList(ImDrawList* drawList)
{
if (!Options::Misc::KeybindList)
return;

ImGuiIO& io = ImGui::GetIO();
std::vector<std::pair<std::string, std::string>> activeBinds;

// Check Aimbot
if (Options::Aimbot::Aimbot && Options::Aimbot::AimbotKey != 0)
{
bool isActive = false;
if (Options::Aimbot::ToggleType == 1) // Toggle
isActive = Options::Aimbot::Toggled;
else // Hold
isActive = (GetAsyncKeyState(Options::Aimbot::AimbotKey) & 0x8000) != 0;

if (isActive)
activeBinds.push_back({"Aimbot", Options::Aimbot::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
}

// Check Triggerbot
if (Options::Triggerbot::Enabled && Options::Triggerbot::TriggerbotKey != 0)
{
bool isActive = false;
if (Options::Triggerbot::ToggleType == 1) // Toggle
isActive = Options::Triggerbot::Toggled;
else // Hold
isActive = (GetAsyncKeyState(Options::Triggerbot::TriggerbotKey) & 0x8000) != 0;

if (isActive)
activeBinds.push_back({"Triggerbot", Options::Triggerbot::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
}

// Check Fly
if (Options::Fly::Enabled && Options::Fly::FlyKey != 0)
{
bool isActive = false;
if (Options::Fly::ToggleType == 1) // Toggle
isActive = Options::Fly::Toggled;
else // Hold
isActive = (GetAsyncKeyState(Options::Fly::FlyKey) & 0x8000) != 0;

if (isActive)
activeBinds.push_back({"Fly", Options::Fly::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
}

// Check WalkSpeed
if (Options::WalkSpeed::Enabled && Options::WalkSpeed::WalkSpeedKey != 0)
{
bool isActive = false;
if (Options::WalkSpeed::ToggleType == 1) // Toggle
isActive = Options::WalkSpeed::Toggled;
else // Hold
isActive = (GetAsyncKeyState(Options::WalkSpeed::WalkSpeedKey) & 0x8000) != 0;

if (isActive)
activeBinds.push_back({"WalkSpeed", Options::WalkSpeed::ToggleType == 1 ? "[Toggled]" : "[Hold]"});
}

if (activeBinds.empty())
return;

// Calculate dimensions - much smaller and compact
float padding = 8.0f;
float lineHeight = 14.0f;
float titleHeight = 20.0f;
float minWidth = 150.0f; // Reduced minimum width
float maxWidth = minWidth;

for (const auto& bind : activeBinds)
{
std::string fullText = bind.first + " " + bind.second;
float textWidth = ImGui::CalcTextSize(fullText.c_str()).x;
if (textWidth > maxWidth)
maxWidth = textWidth;
}

float boxWidth = maxWidth + padding * 2;
float boxHeight = titleHeight + (activeBinds.size() * lineHeight) + padding;

// Use custom position from sliders
ImVec2 pos = ImVec2(Options::Misc::KeybindListX, Options::Misc::KeybindListY);

// Draw background - fully opaque (255 alpha instead of 200)
drawList->AddRectFilled(pos, ImVec2(pos.x + boxWidth, pos.y + boxHeight), IM_COL32(8, 8, 8, 255), 4.0f);
drawList->AddRect(pos, ImVec2(pos.x + boxWidth, pos.y + boxHeight), IM_COL32(27, 27, 27, 255), 4.0f);

// Draw title - centered
const char* title = "Keybinds";
float titleWidth = ImGui::CalcTextSize(title).x;
float titleX = pos.x + (boxWidth - titleWidth) / 2.0f;
drawList->AddText(ImVec2(titleX, pos.y + 4), IM_COL32(255, 255, 255, 255), title);
drawList->AddLine(ImVec2(pos.x, pos.y + titleHeight), ImVec2(pos.x + boxWidth, pos.y + titleHeight), IM_COL32(27, 27, 27, 255));

// Draw active binds - centered
float yOffset = pos.y + titleHeight + 3;
for (const auto& bind : activeBinds)
{
std::string fullText = bind.first + " " + bind.second;
float textWidth = ImGui::CalcTextSize(fullText.c_str()).x;
float textX = pos.x + (boxWidth - textWidth) / 2.0f;

// Draw the full text centered
drawList->AddText(ImVec2(textX, yOffset), IM_COL32(255, 255, 255, 255), bind.first.c_str());

// Draw status in accent color right after the name
float nameWidth = ImGui::CalcTextSize(bind.first.c_str()).x;
drawList->AddText(ImVec2(textX + nameWidth + 5, yOffset), IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255), bind.second.c_str());

yOffset += lineHeight;
}
}

// ── Katana Alert ─────────────────────────────────────────────────────────
// Red animated "PARRYING" warning drawn to the center of the screen while any
// enemy has a katana out (Rivals). Heart-beat pulse + two staggered expanding
// rings that evoke the incoming deflection.
static void RenderKatanaAlert(ImDrawList* dl)
{
    const ImVec2 dims = ImGui::GetIO().DisplaySize;
    const float t = (float)ImGui::GetTime();
    const float cycle = fmodf(t * 1.15f, 1.0f);

    ImFont* font = UI::logo_font ? UI::logo_font : ImGui::GetFont();
    const float size = (font == UI::logo_font) ? font->FontSize : 25.0f;
    const char* txt = "> PARRYING <";
    ImVec2 tsz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, txt);
    const ImVec2 c = ImVec2((dims.x - tsz.x) * 0.5f, dims.y * 0.30f - tsz.y * 0.5f);
    const ImVec2 ctr = ImVec2(dims.x * 0.5f, c.y + tsz.y * 0.5f);

    // two staggered expanding rings, a fresh one every ~0.43s
    for (int i = 0; i < 2; i++)
    {
        float ph = fmodf(cycle + i * 0.5f, 1.0f);
        float rr = 26.0f + ph * 58.0f;
        int a = (int)(130.0f * (1.0f - ph));
        dl->AddCircle(ctr, rr, IM_COL32(255, 46, 60, a), 48, 2.0f);
    }

    const float pulse = (sinf(t * 6.0f) + 1.0f) * 0.5f;
    const ImU32 col = IM_COL32(255, 46, 60, (int)(230 + pulse * 25));
    dl->AddText(font, size, ImVec2(c.x + 2.5f, c.y + 2.8f), IM_COL32(12, 10, 12, 215), txt);
    dl->AddText(font, size, ImVec2(c.x - 0.8f, c.y - 0.8f), IM_COL32(80, 8, 12, 160), txt);
    dl->AddText(font, size, c, col, txt);
}

// ── Player List window (Misc -> "Player List") ────────────────────────────
// Mirrors the Explorer window look. Lists live players from the player-object
// cache; clicking a player selects them and lets you toggle Exclude / Focus /
// clear, or add/remove them as a friend. Drives the existing PlayerFilter.
static void RenderPlayerListWindow(bool* open)
{
    if (!open || !*open)
        return;

    const ImVec4 border_outer = ImVec4(0.13f, 0.13f, 0.13f, 1.f);
    constexpr float title_h = 26.f;
    constexpr float margin = 3.f;

    static ImVec2 playerListPos = ImVec2(-1, -1);
    static ImVec2 playerListSize = ImVec2(340.f, 430.f);
    static bool playerListPosInitialized = false;

    if (!playerListPosInitialized)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        playerListPos = ImVec2(center.x - 180.f, center.y - 60.f);
        playerListPosInitialized = true;
    }

    ImGui::SetNextWindowPos(playerListPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(playerListSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280.f, 300.f), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_Border, border_outer);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    bool visible = ImGui::Begin("##playerlist_window", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    if (!visible)
    {
        ImGui::End();
        return;
    }

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    playerListPos = wp;
    playerListSize = ws;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + title_h), IM_COL32(20, 20, 20, 255));
    const char* title = "player list";
    ImVec2 title_ts = ImGui::CalcTextSize(title);
    draw->AddText(ImVec2(wp.x + (ws.x - title_ts.x) * 0.5f, wp.y + (title_h - title_ts.y) * 0.5f), IM_COL32(230, 230, 230, 255), title);

    // Close button
    ImVec2 xsz = ImGui::CalcTextSize("X");
    float x_w = xsz.x + 14.f;
    float drag_w = ws.x - x_w - 6.f;
    ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
    ImGui::InvisibleButton("##playerlist_drag", ImVec2(drag_w, title_h));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 cur = ImGui::GetWindowPos();
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(cur.x + delta.x, cur.y + delta.y));
    }
    ImVec2 xmin(wp.x + ws.x - x_w, wp.y);
    ImGui::SetCursorScreenPos(xmin);
    ImGui::InvisibleButton("##playerlist_close", ImVec2(x_w, title_h));
    bool xhov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
        *open = false;
    draw->AddText(ImVec2(xmin.x + 7.f, wp.y + (title_h - xsz.y) * 0.5f),
        xhov ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255), "X");

    float body_top = title_h + margin;
    float body_h = ws.y - body_top - margin;

    ImGui::SetCursorPos(ImVec2(margin, body_top));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.06f, 1.f));
    ImGui::BeginChild("##playerlist_body", ImVec2(ws.x - margin * 2.f, body_h), true);

    // Snapshot of the live player cache (never read the shared vector unlocked).
    std::vector<RobloxPlayer> players;
    {
        std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
        players = Globals::Caches::CachedPlayerObjects;
    }
    std::string localName = Globals::Roblox::LocalPlayer.address ? Globals::Roblox::LocalPlayer.Name() : std::string();

    static std::string selected;
    float avail = ImGui::GetContentRegionAvail().x;

    for (size_t i = 0; i < players.size(); i++)
    {
        const RobloxPlayer& p = players[i];
        if (p.Name.empty())
            continue;

        bool isLocal = (p.Name == localName);
        int mark = PlayerFilter::GetMark(p.Name);
        bool isFriendly = PlayerFilter::IsFriend(p.Name);

        const char* tag = "  ";
        ImU32 tagCol = IM_COL32(150, 150, 150, 255);
        if (mark == Options::PlayerFilter::Focus) { tag = "F "; tagCol = IM_COL32(80, 200, 120, 255); }
        else if (mark == Options::PlayerFilter::Exclude) { tag = "X "; tagCol = IM_COL32(220, 90, 90, 255); }
        if (isFriendly) { tag = "+ "; tagCol = IM_COL32(110, 170, 230, 255); }

        std::string row = std::string(tag) + (isLocal ? std::string("[YOU] ") : std::string());
        row += p.Name;
        if (!p.TeamName.empty()) row += "  (" + p.TeamName + ")";

        char id[48];
        std::snprintf(id, sizeof(id), "##plrow_%zu", i);
        bool itemSel = (selected == p.Name);
        if (ImGui::Selectable((row.c_str() + std::string(id)).c_str(), itemSel))
            selected = p.Name;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nhealth %.0f/%.0f", p.Name.c_str(), p.Health, p.MaxHealth);
    }

    if (players.empty())
    {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "no players in cache");
    }

    ImGui::Separator();

    // Edit panel for the selected player.
    ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.f), "Selected: %s", selected.empty() ? "(none)" : selected.c_str());
    if (!selected.empty())
    {
        int mark = PlayerFilter::GetMark(selected);
        bool isFriendly = PlayerFilter::IsFriend(selected);
        const char* markLabel =
            mark == Options::PlayerFilter::Focus ? "Focus" :
            mark == Options::PlayerFilter::Exclude ? "Exclude" : "Default";

        ImGui::Text("Mark: %s", markLabel);

        if (mark != Options::PlayerFilter::Exclude)
            if (ImGui::SmallButton("Exclude"))
                PlayerFilter::SetMark(selected, Options::PlayerFilter::Exclude);
        ImGui::SameLine();
        if (mark != Options::PlayerFilter::Focus)
            if (ImGui::SmallButton("Focus"))
                PlayerFilter::SetMark(selected, Options::PlayerFilter::Focus);
        ImGui::SameLine();
        if (mark != Options::PlayerFilter::None)
            if (ImGui::SmallButton("Clear"))
                PlayerFilter::SetMark(selected, Options::PlayerFilter::None);

        ImGui::SameLine();
        if (isFriendly)
        {
            if (ImGui::SmallButton("Unfriend"))
                PlayerFilter::RemoveFriend(selected);
        }
        else
        {
            if (ImGui::SmallButton("Friend"))
                PlayerFilter::AddFriend(selected);
        }

        ImGui::Checkbox("Exclude Friends", &Options::PlayerFilter::ExcludeFriends);
        ImGui::Checkbox("Focus Only", &Options::PlayerFilter::FocusOnly);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::End();
}

void ShowImgui()
{
    OutputDebugStringA("[S] ShowImgui: START\n");
    SeraphLog("[S] ShowImgui: START, resetting overlay flags");
    Globals::overlayShouldShutdown = false;
    Globals::overlayDone = false;
    InitializeConfigPaths();
    OutputDebugStringA("[S] ShowImgui: Calling Executor::Initialize...\n");
    Executor::Initialize();
    OutputDebugStringA("[S] ShowImgui: Executor::Initialize returned\n");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Size the overlay to the Roblox client window instead of the whole virtual
    // screen. A fullscreen "screen+1px" layered popup is a textbook overlay
    // tell; binding to the game client rect is far less conspicuous.
    // Globals::Viewport::ScreenPos / Dimensions are populated once the game is
    // attached; fall back to the primary screen until then.
    LONG winX = 0, winY = 0, winW = 0, winH = 0;
    {
        size_t width = (size_t)GetSystemMetrics(SM_CXSCREEN);
        size_t height = (size_t)GetSystemMetrics(SM_CYSCREEN);
        if (Globals::Viewport::Valid && Globals::Viewport::Dimensions.x > 0 && Globals::Viewport::Dimensions.y > 0)
        {
            winX = Globals::Viewport::ScreenPos.x;
            winY = Globals::Viewport::ScreenPos.y;
            winW = (LONG)(Globals::Viewport::Dimensions.x);
            winH = (LONG)(Globals::Viewport::Dimensions.y);
        }
        else
        {
            winX = 0; winY = 0;
            winW = (LONG)width;
            winH = (LONG)height;
        }
    }

    // Benign, non-descript window class + title so the overlay doesn't advertise
    // itself. The old "ImGui Example" class name is a widely-flagged signature.
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Windows.UI.Core.CoreWindow", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        winX, winY, winW, winH,
        nullptr, nullptr, wc.hInstance, nullptr);

    // Opaque swapchain + color-key transparency (this system cannot create
    // DXGI_ALPHA_MODE_PREMULTIPLIED flip-model swapchains -- every permutation
    // of SwapEffect/BufferCount/WS_EX_NOREDIRECTIONBITMAP returns
    // DXGI_ERROR_INVALID_CALL 0x887A0001). Instead we use LWA_COLORKEY:
    // pure-black (RGB 0,0,0) pixels become see-through while all drawn UI/ESP
    // (non-black) renders opaquely, so the game shows through around the GUI.
    // Avoid pure-black foreground colors in the menu/ESP.

    OutputDebugStringA("[S] ShowImgui: Window created\n");
    SeraphLog("[S] ShowImgui: Window created, hwnd=0x" + std::to_string((uintptr_t)hwnd));

    ::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Publish the overlay HWND so the file-dialog helpers (configs.h)
    // can present Import/Export as modal-to-owner dialogs. This is what
    // makes the OS dialog actually clickable when the overlay is open:
    // ownership forces Windows to route mouse + focus through the
    // dialog above our WS_EX_LAYERED + WS_EX_TOPMOST overlay.
    g_OverlayHWND = hwnd;

    InstallWheelForwarder(hwnd);

    HideFromTaskbar(hwnd);

    // Apply streamproof if enabled (WDA_EXCLUDEFROMCAPTURE = 0x00000011)
    if (Options::Misc::StreamProof)
    {
        SetWindowDisplayAffinity(hwnd, 0x00000011);
    }

    if (!CreateDeviceD3D(hwnd))
    {
        OutputDebugStringA("[S] ShowImgui: CreateDeviceD3D FAILED\n");
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CoUninitialize();
        return;
    }

    OutputDebugStringA("[S] ShowImgui: D3D device created\n");
    SeraphLog("[S] ShowImgui: D3D device created");

    ::ShowWindow(hwnd, SW_HIDE);
    ::UpdateWindow(hwnd);
    HideFromTaskbar(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    OutputDebugStringA("[S] ShowImgui: ImGui context created\n");

    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

ImFontConfig config;
config.MergeMode = false;
config.PixelSnapH = true;

ImFont* baseFont = io.Fonts->AddFontDefault(&config);

// Menu font list: embedded Nunito Medium (Exterium default, 21px) is index
// 0, then a curated set of Windows system fonts for the runtime "Menu Font"
// combo (Options::Misc::MenuFont) so switching never rebuilds the atlas.
struct MenuFontEntry { ImFont* font; const char* path; const char* name; };
static MenuFontEntry menuFonts[8];
int menuFontCount = 0;

auto LoadSystemFont = [&](const char* path, float size) -> ImFont*
{
const std::wstring widePath = UTF8ToWide(path);
if (widePath.empty()) return nullptr;
if (GetFileAttributesW(widePath.c_str()) == INVALID_FILE_ATTRIBUTES)
return nullptr;
return io.Fonts->AddFontFromFileTTF(path, size, &config, io.Fonts->GetGlyphRangesJapanese());
};

if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 21.0f, &config, io.Fonts->GetGlyphRangesCyrillic()), "", "Nunito" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\verdana.ttf", 14.0f), "C:\\Windows\\Fonts\\verdana.ttf", "Verdana" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\segoeui.ttf", 14.0f), "C:\\Windows\\Fonts\\segoeui.ttf", "Segoe UI" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\tahoma.ttf", 14.0f),  "C:\\Windows\\Fonts\\tahoma.ttf",  "Tahoma" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\arial.ttf",   14.0f), "C:\\Windows\\Fonts\\arial.ttf",   "Arial" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\georgia.ttf",  14.0f), "C:\\Windows\\Fonts\\georgia.ttf",  "Georgia" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\calibri.ttf",  14.0f), "C:\\Windows\\Fonts\\calibri.ttf",  "Calibri" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\consola.ttf",  14.0f), "C:\\Windows\\Fonts\\consola.ttf",  "Consolas" };

// Mirror the loaded fonts into the file-scope MenuFonts namespace so
// ShowImgui can switch between them at runtime via PushFont/PopFont
// (instead of rebuilding the font atlas, which would stall the renderer).
for (int i = 0; i < menuFontCount && i < (int)(sizeof(MenuFonts::Fonts)/sizeof(MenuFonts::Fonts[0])); ++i)
MenuFonts::Fonts[i] = menuFonts[i].font;
MenuFonts::Count = menuFontCount;

// Apply current font selection; clamp to the loaded count so an out-of-
// range value falls back gracefully to the first entry (Nunito).
if (Options::Misc::MenuFont >= menuFontCount || Options::Misc::MenuFont < 0)
Options::Misc::MenuFont = 0;
ImFont* font = (menuFontCount > 0 && menuFonts[Options::Misc::MenuFont].font)
? menuFonts[Options::Misc::MenuFont].font
: baseFont;
io.FontDefault = font;

// Exterium UI faces (icons + secondary text) — loaded into the same atlas
// so there is a single texture upload and no per-frame font switching stalls.
{
ImFontConfig uicfg;
uicfg.MergeMode = false;
uicfg.PixelSnapH = true;
uicfg.OversampleH = 6;
uicfg.OversampleV = 6;
const ImWchar* cyr = io.Fonts->GetGlyphRangesCyrillic();
UI::small_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 17.0f, &uicfg, cyr);
UI::medium_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 18.0f, &uicfg, cyr);
UI::small_icon_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 15.0f, &uicfg, cyr);
UI::logo_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 25.0f, &uicfg, cyr);
uicfg.OversampleH = 8;
uicfg.OversampleV = 8;
UI::icon_font = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 18.0f, &uicfg, io.Fonts->GetGlyphRangesDefault());
UI::icon_big_font = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 23.0f, &uicfg, io.Fonts->GetGlyphRangesDefault());
UI::arrow_icons = io.Fonts->AddFontFromMemoryTTF((void*)arrowicon, (int)sizeof(arrowicon), 18.0f, &uicfg, io.Fonts->GetGlyphRangesDefault());
}

config.MergeMode = true;
ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    // Opaque bitblt swapchain + color-key transparency: the target has no
    // per-pixel alpha, so use straight-alpha blend so ImGui's colors composite
    // correctly (premultiplied blend would look washed out on an opaque target).
    ImGui_ImplDX11_SetPremultipliedBlend(false);
    ImGui_ImplDX11_CreateDeviceObjects();

    OutputDebugStringA("[S] ShowImgui: ImGui backends initialized\n");

    ImVec4 clear_color = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    bool done = false;
    bool menu_open = false;
    static bool gMenuWasEverOpen = false;
    int tab = 0;
    int tab2 = 0;
    int lastTab = -1;
    static ImVec2 menuPos = ImVec2(-1, -1); // persisted menu window position; -1 = center on first show
    static bool menuDragging = false;
    static ImVec2 menuDragOffset = ImVec2(0, 0);

    // Overlay is a topmost, click-through ESP layer: always visible in-game.
    // Click-through so it never blocks game input; menu toggles it clickable.
    ::ShowWindow(hwnd, SW_SHOW);
    ApplyOverlayWindowStyle(hwnd, true);
OutputDebugStringA("[S] ShowImgui: Entering render loop\n");
    SeraphLog("[S] ShowImgui: Entering render loop");
    int frameCount = 0;
    while (!done && Globals::running && !Globals::overlayShouldShutdown)
    {
        if (frameCount == 0) {
            OutputDebugStringA("[S] ShowImgui: First frame\n");
        }
        frameCount++;
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

if (g_SwapChainOccluded && g_pSwapChain->Present(0, 0) == DXGI_STATUS_OCCLUDED)
{
::Sleep(10);
continue;
}
g_SwapChainOccluded = false;

if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
{
CleanupRenderTarget();
g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
g_ResizeWidth = g_ResizeHeight = 0;
CreateRenderTarget();
}

ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
    if (menu_open)
    {
        const LONG wheel = InterlockedExchange(&g_OverlayWheelAccum, 0);
        if (wheel != 0)
            ImGui::GetIO().MouseWheel += (float)wheel / 4096.0f;
    }
    else
        InterlockedExchange(&g_OverlayWheelAccum, 0);
ImGui::NewFrame();

// Pump the external executor: resumes due coroutines, fires RunService events.
Executor::Tick();

// Update player avatars
Cheat::Features::PlayerAvatars::Tick();

Globals::Viewport::Update();

// Bootstrap from persistent Options on the first frame, and again
// after a runtime LoadConfig (Configs tab) completes. The latter
// is signalled by the Configs tab setting g_MenuWeatherNeedsBootstrap
// back to true so the engine picks up freshly-loaded values instead
// of SyncToOptions clobbering them with stale Engine state.
if (g_MenuWeatherNeedsBootstrap)
{
MenuWeather::Rebootstrap();
g_MenuWeatherNeedsBootstrap = false;
}
    // Keep Roblox global instance pointers updated/valid to prevent stale-pointer crashes
if (Globals::Roblox::DataModel.address)
{
static DWORD lastUpdateTick = 0;
DWORD currentTick = GetTickCount();
if (currentTick - lastUpdateTick > 500)
{
lastUpdateTick = currentTick;
Globals::Roblox::Workspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
Globals::Roblox::Players = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
Globals::Roblox::Camera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");
Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
}
}

    if (Options::Misc::MenuKey != 0 && (GetAsyncKeyState(Options::Misc::MenuKey) & 1))
    {
        menu_open = !menu_open;
        gMenuWasEverOpen = true;
        // Menu open -> clickable; menu closed -> click-through ESP layer. Window stays shown.
        ApplyOverlayWindowStyle(hwnd, !menu_open);
    }

    // Fade animation (ease-cubic-out time-based, ported from jew-dick-hack
    // ease utilities) -- smooth, non-linear menu open/close transition.
    static float fadeProgress = 0.0f;   // 0..1 (open) / 1..0 (close)
    static bool  fadeWasOpen = false;
    const float dt = ImGui::GetIO().DeltaTime;
    const float easeDur = 0.16f;        // seconds for the eased transition
    if (menu_open)
    {
        fadeWasOpen = true;
        fadeProgress = std::min(1.0f, fadeProgress + (dt / easeDur));
    }
    else
    {
        if (fadeWasOpen)
        {
            // On first frame of close, restart the timing from the current eased value.
            fadeProgress = (fadeProgress <= 0.f) ? 0.f : fadeProgress;
        }
        fadeWasOpen = false;
        fadeProgress = std::max(0.0f, fadeProgress - (dt / easeDur));
    }

    float menuAlpha = anim::ease_cubic_out(fadeProgress);

    // Skip weather physics when the menu has never been opened
    if (gMenuWasEverOpen && (menu_open || menuAlpha > 0.0f))
        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);

// Dynamic streamproof toggle
static bool lastStreamProofState = Options::Misc::StreamProof;
if (lastStreamProofState != Options::Misc::StreamProof)
{
if (Options::Misc::StreamProof)
{
SetWindowDisplayAffinity(hwnd, 0x00000011); // WDA_EXCLUDEFROMCAPTURE
}
else
{
SetWindowDisplayAffinity(hwnd, 0x00000000); // WDA_NONE
}
lastStreamProofState = Options::Misc::StreamProof;
}

// Update accent colors from options (with rainbow + gradient support)
if (Options::Misc::RainbowAccent)
{
float t = fmodf(static_cast<float>(ImGui::GetTime()) * Options::Misc::RainbowSpeed, 6.0f);
int segment = static_cast<int>(t);
float frac = t - segment;
float r, g, b;
switch (segment)
{
case 0: r = 1; g = frac; b = 0; break;
case 1: r = 1 - frac; g = 1; b = 0; break;
case 2: r = 0; g = 1; b = frac; break;
case 3: r = 0; g = 1 - frac; b = 1; break;
case 4: r = frac; g = 0; b = 1; break;
default: r = 1; g = 0; b = 1 - frac; break;
}
main_color = ImVec4(r, g, b, 1.0f);
// Second accent is the rainbow hue shifted 180 degrees (opposite side).
float t2 = fmodf(t + 3.0f, 6.0f);
int seg2 = static_cast<int>(t2);
float frac2 = t2 - seg2;
float r2, g2, b2;
switch (seg2)
{
case 0: r2 = 1; g2 = frac2; b2 = 0; break;
case 1: r2 = 1 - frac2; g2 = 1; b2 = 0; break;
case 2: r2 = 0; g2 = 1; b2 = frac2; break;
case 3: r2 = 0; g2 = 1 - frac2; b2 = 1; break;
case 4: r2 = frac2; g2 = 0; b2 = 1; break;
default: r2 = 1; g2 = 0; b2 = 1 - frac2; break;
}
main_color2 = ImVec4(r2, g2, b2, 1.0f);
}
else
{
main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);
main_color2 = ImVec4(Options::Misc::MenuAccentColor2[0], Options::Misc::MenuAccentColor2[1], Options::Misc::MenuAccentColor2[2], 1.0f);
}

// Resolve the active theme (preset or custom) and apply it.
const float* themeBg = nullptr;
const float* themePanel = nullptr;
const float* themeAccent = nullptr;
const float* themeAccent2 = nullptr;
bool themeGradient = false;
MenuThemes::Resolve(themeBg, themePanel, themeAccent, themeAccent2, themeGradient);
// Whenever the user switches themes, restyle in-game feature colors so the
// overlay matches. Per-feature pickers stay available afterwards.
static int lastAppliedTheme = -1;
if (Options::Misc::MenuTheme != lastAppliedTheme)
{
MenuThemes::ApplyFeatureColors(themeAccent, themeAccent2);
lastAppliedTheme = Options::Misc::MenuTheme;
}
// Always use preset accent colors (even for Custom) to prevent old saved configs from overriding
main_color = ImVec4(themeAccent[0], themeAccent[1], themeAccent[2], 1.0f);
main_color2 = ImVec4(themeAccent2[0], themeAccent2[1], themeAccent2[2], 1.0f);
// Gradient flag follows the preset (or the custom toggle).
const bool useGradient = themeGradient;
        (void)useGradient; // Exterium palette is fixed; gradient accent line removed

if (menu_open || menuAlpha > 0.0f)
{
// No full-screen dark dim backdrop drawn here: with color-key transparency the
// opaque pitch-black fill would render as a solid dark band around the menu
// (keyed out only when exactly RGB(0,0,0)), so it was removed. This also drops
// a full-window fill per frame, fixing the jank when spamming the menu key.
// Visuals tab uses a wider layout so the ESP preview has a dedicated side panel.
        // MenuScale acts as a uniform zoom factor for the whole UI.
        const float sc = std::clamp(Options::Misc::MenuScale, 0.6f, 2.5f);
        // keep every subtab pill a uniform width, aligned inside the left rail
        // Wider layout: 187px sidebar + 800px content, two 390px cards.
        UI::sc = sc;
        UI::SidebarX = 10.0f * sc;
        UI::SidebarW = 187.0f * sc;
        UI::ContentX = 197.0f * sc;
        UI::ContentW = 800.0f * sc;
        UI::CardW = 340.0f * sc;
        const float menuWidth = 997.0f * sc;
        const float menuHeight = 680.0f * sc;
// Window frame stays a fixed size so dragging the scale slider doesn't
// resize the window under the cursor (which caused a big/small feedback loop).
// Zoom is applied to content via SetWindowFontScale + scaled positions.
auto s = ImVec2{}, p = ImVec2{}, gs = ImVec2{ menuWidth, menuHeight };

// Center on first show (offset up slightly so Roblox chat at bottom stays visible),
// otherwise keep last dragged position.
if (menuPos.x < 0.0f)
menuPos = ImVec2((io.DisplaySize.x - gs.x) * 0.5f, (io.DisplaySize.y - gs.y) * 0.5f - 30.0f * sc);
ImGui::SetNextWindowPos(menuPos);

ImGui::SetNextWindowSize(gs);
ImGui::SetNextWindowBgAlpha(menuAlpha);
ImGui::PushStyleVar(ImGuiStyleVar_Alpha, menuAlpha);
ImGui::Begin("##GUI", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
{
s = ImVec2(ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x * 2, ImGui::GetWindowSize().y - ImGui::GetStyle().WindowPadding.y * 2);
p = ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y);
auto draw = ImGui::GetWindowDrawList();

// â”€â”€ Title-bar drag (top 25*sc px is the grab region) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            const ImVec2 titleMin = ImVec2(p.x, p.y);
            const ImVec2 titleMax = ImVec2(p.x + s.x, p.y + 25.0f * sc);
            if (ImGui::IsMouseHoveringRect(titleMin, titleMax) && !ImGui::IsAnyItemHovered())
            {
                if (ImGui::IsMouseClicked(0))
                {
                    menuDragging = true;
                    menuDragOffset = ImVec2(io.MousePos.x - menuPos.x, io.MousePos.y - menuPos.y);
                }
            }
            if (menuDragging)
            {
                if (ImGui::IsMouseDown(0))
                    menuPos = ImVec2(io.MousePos.x - menuDragOffset.x, io.MousePos.y - menuDragOffset.y);
                else
                    menuDragging = false;
            }
        }

        // Scale widgets/text inside the menu to match the zoom factor.
        ImGui::SetWindowFontScale(sc);

        // Apply the cohesive Seraph design system: unified palette +
        // global style so every widget (built-in or UI::) matches.
        UI::ApplyStyle(main_color, main_color2,
        ImVec4(themeBg[0], themeBg[1], themeBg[2], 1.0f),
        ImVec4(themePanel[0], themePanel[1], themePanel[2], 1.0f));

        // ── Exterium chrome ─────────────────────────────────────────────────
        // Near-black window frame (13,14,16,200), 55px header with a main-
        // color accent strip at its bottom, 187px sidebar panel, centered logo,
        // grouped sidebar tabs, and a small muted status line in the sidebar
        // footer. Layout target: 827x604.
        const float time = (float)ImGui::GetTime();
        const float glow = (sinf(time * 2.3f) + 1.0f) * 0.5f;
        const ImVec4 mainA = UI::P.accent;
        const ImU32 colWin = UI::U(UI::winbg_color);
        const ImU32 colPanel = UI::U(UI::background_color);
        const ImU32 colStroke = UI::U(UI::stroke_color);
        const ImU32 colMain = ImGui::ColorConvertFloat4ToU32(mainA);
        const ImU32 colMainStr = ImGui::ColorConvertFloat4ToU32(ImVec4(mainA.x, mainA.y, mainA.z, 1.0f));

        const float headerH = 55.0f * sc;
        const float sbW = UI::SidebarW;

        // Drop shadow (soft, subtle) — drawn on window drawlist
        for (int i = 4; i >= 0; i--)
        {
            float spread = 16.0f + i * 10.0f;
            int shAlpha = (int)((11.0f - i * 1.8f) * menuAlpha);
            draw->AddRectFilled(
                ImVec2(p.x - spread, p.y - spread * 0.5f),
                ImVec2(p.x + s.x + spread, p.y + s.y + spread),
                IM_COL32(4, 4, 6, shAlpha), (13.0f + spread) * sc);
        }

        // Window frame fill (rounded, near-black translucent)
        draw->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + s.y),
            colWin, 16.0f * sc);

        // Header fill (rounded top only) — matches theme
        const ImVec4 headerBg = ImVec4(UI::winbg_color.x + 0.004f, UI::winbg_color.y + 0.004f, UI::winbg_color.z + 0.006f, 1.0f);
        draw->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + headerH),
            ImGui::ColorConvertFloat4ToU32(headerBg), 16.0f * sc, ImDrawFlags_RoundCornersTop);

        // Accent strip at bottom of header (0,52)-(827,55)
        draw->AddRectFilled(
            ImVec2(p.x, p.y + 52.0f * sc),
            ImVec2(p.x + s.x, p.y + 55.0f * sc),
            colMainStr);

        // Logo: "SERAPH" centered in header
        {
            const std::string logoText = SX("SERAPH");
            ImFont* lf = UI::logo_font ? UI::logo_font : io.FontDefault;
            const float logoSize = UI::logo_font ? lf->FontSize : 25.0f * sc;
            ImVec2 tsz = lf->CalcTextSizeA(logoSize, FLT_MAX, 0.f, logoText.c_str());
            ImVec2 c = ImVec2(p.x + (s.x - tsz.x) * 0.5f, p.y + (52.0f * sc - logoSize) * 0.5f);
            ImU32 lc = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.94f, 1.0f));
            draw->AddText(lf, logoSize, ImVec2(c.x + 1.2f, c.y + 1.2f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(mainA.x, mainA.y, mainA.z, 0.20f)), logoText.c_str());
            draw->AddText(lf, logoSize, c, lc, logoText.c_str());
        }

        // Sidebar panel (0,55)-(187,604), near-black, bottom-left rounded
        const ImVec2 sideMin = ImVec2(p.x, p.y + headerH);
        const ImVec2 sideMax = ImVec2(p.x + sbW, p.y + s.y);
        draw->AddRectFilled(sideMin, sideMax, colPanel, 14.0f * sc,
            ImDrawFlags_RoundCornersBottom + ImDrawFlags_RoundCornersLeft);
        // right hairline of sidebar
        draw->AddLine(
            ImVec2(sideMax.x, sideMin.y),
            ImVec2(sideMax.x, sideMax.y),
            colStroke, 1.0f * sc);

        // Content cards area background (right of sidebar, below header)
        draw->AddRectFilled(
            ImVec2(p.x + sbW, p.y + headerH),
            ImVec2(p.x + s.x, p.y + s.y),
            colWin, 16.0f * sc, ImDrawFlags_RoundCornersBottomRight);

// Use the chosen menu font. Fall back to ImGui's default font
// if MenuFonts hasn't populated yet (only on the very first
// frame, before pre-load completes).
ImFont* menuFont = (MenuFonts::Count > 0
&& Options::Misc::MenuFont >= 0
&& Options::Misc::MenuFont < MenuFonts::Count
&& MenuFonts::Fonts[Options::Misc::MenuFont])
? MenuFonts::Fonts[Options::Misc::MenuFont]
: io.FontDefault;
        ImGui::PushFont(menuFont);

// â”€â”€ Animated Exterium ambient background (behind content only) â”€â”€â”€â”€
{
    const float bgW = s.x - sbW;
    const ImVec2 bgOrigin = ImVec2(p.x + sbW, p.y + headerH);
    UI::ExteriumBG_Update(bgW, s.y - headerH);
    UI::ExteriumBG_Render(draw, bgOrigin, ImVec2(bgW, s.y - headerH), menuAlpha);
    if (Options::Misc::ExteriumSword)
        UI::ExteriumSword_Render(draw, bgOrigin, ImVec2(bgW, s.y - headerH), menuAlpha);
}

// ═══ Sidebar footer status line (small, muted) ══════════════════════
{
    char fpsBuf[64];    sprintf_s(fpsBuf, "%d FPS", (int)ImGui::GetIO().Framerate);
    bool connected = (Globals::Roblox::LocalPlayer.address != 0);
    std::string uname = connected ? Globals::Roblox::LocalPlayer.Name() : "";
    const char* nameStr = uname.empty() ? "Not connected" : uname.c_str();
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.30f));
    ImFont* sf = UI::small_font ? UI::small_font : ImGui::GetFont();
    const float fs = UI::small_font ? sf->FontSize : 17.0f * sc;
    float yy = sideMax.y - 130.0f * sc;

// Avatar (circular profile icon)
        {
            const float avSize = 72.0f * sc;
            const float avX = sideMin.x + 12.0f * sc;
            const float avY = yy - 2.0f * sc;
            const ImVec2 avCenter(avX + avSize * 0.5f, avY + avSize * 0.5f);
            const float avR = avSize * 0.5f;
            const ImU32 avBorder = ImGui::ColorConvertFloat4ToU32(UI::P.borderDim);
            draw->AddCircleFilled(avCenter, avR, ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.30f)));
            draw->AddCircle(avCenter, avR, avBorder, 0, 1.0f * sc);

            if (connected) {
                std::int64_t uid = Cheat::Features::PlayerAvatars::LookupUserId(uname);
                ID3D11ShaderResourceView* av = uid != 0 ? Cheat::Features::PlayerAvatars::Get(uid) : nullptr;
                if (av) {
                    const ImVec2 avMin(avX, avY);
                    const ImVec2 avMax(avX + avSize, avY + avSize);
                    draw->AddImageRounded((ImTextureID)av, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, avR);
                } else if (!uname.empty()) {
                    char ini[2] = { (char)std::toupper((unsigned char)uname[0]), 0 };
                    ImVec2 tsz = sf->CalcTextSizeA(fs, FLT_MAX, 0.f, ini);
                    ImVec2 tpos(avX + (avSize - tsz.x) * 0.5f, avY + (avSize - tsz.y) * 0.5f);
                    draw->AddText(sf, fs, tpos, muted, ini);
                }
            }
            yy += avSize + 4.0f * sc;
        }

    ImVec2 ns = sf->CalcTextSizeA(fs, FLT_MAX, 0.f, nameStr);
    draw->AddText(sf, fs, ImVec2(sideMin.x + 18.0f * sc, yy), muted, nameStr);
    yy += fs + 3.0f * sc;
    draw->AddText(sf, fs, ImVec2(sideMin.x + 18.0f * sc, yy), muted, fpsBuf);
}

        // â”€â”€ Sidebar grouped nav tabs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            // Sidebar vertical nav tabs
            {
            const float tabX = p.x + UI::SidebarX + 10.0f * sc;

            // category label helper (white @ 0.30, small font)
            auto catLabel = [&](const char* t, float y) {
                ImFont* sf = UI::small_font ? UI::small_font : ImGui::GetFont();
                const float fs = UI::small_font ? sf->FontSize : 15.0f * sc;
                draw->AddText(sf, fs, ImVec2(tabX + 2.0f * sc, y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.30f)), t);
            };

            // groups: AIMBOT, VISUALS, MISC, CONFIGS
            const char* const catN[] = { "AIMBOT", "VISUALS", "MISC", "CONFIGS" };
            // per-group (row indices -> tab id, glyph, label)
            struct Row { int id; const char* glyph; const char* name; };
            const Row g0[] = { {0,"9","Aim"}, {2,"0","Rage"} };
            const Row g1[] = { {1,"8","Visuals"} };
            const Row g2[] = { {3,"1","Misc"}, {4,"5","Movement"} };
            const Row g3[] = { {5,"6","Configs"}, {6,"3","Game"}, {7,"2","Executor"} };
            const Row* groups[4] = { g0, g1, g2, g3 };
            const int groupN[4] = { 2, 1, 2, 3 };

            float yy = sideMin.y + 15.0f * sc;
            for (int g = 0; g < 4; g++)
            {
                catLabel(catN[g], yy);
                yy += 8.0f * sc - 0.0f;
                for (int k = 0; k < groupN[g]; k++)
                {
                    const Row& r = groups[g][k];
                    if (UI::Tab(r.name, r.glyph, tab == r.id, tabX, yy)) tab = r.id;
                    yy += 40.0f * sc + 5.0f * sc;
                }
                yy += 10.0f * sc;
            }
        }

// Reset cursor to content area top (after header)
ImGui::SetCursorPos(ImVec2(UI::ContentX, 72.0f * sc));
        }


if (tab != lastTab)
{
tab2 = 0;
lastTab = tab;
}

        // ── Content area layout constants (Exterium-style) ────────
        const float ctX = 16.0f * sc; // child-relative (padding from child left edge)
        const float ctPad = 16.0f * sc;
        const float ctW = UI::ContentW;
        const float fullW = s.x - UI::ContentX - 10.0f * sc;
        const float halfW = (fullW - UI::ColGap) * 0.5f; // left card
        const float cardW = halfW; // right card mirrors left (symmetrical layout)
        const float rightCardW = cardW; // use full width for right column

        // Content area scrollable child (allows mouse wheel scrolling for all tabs)
        // Transparent ChildBg so the animated Exterium background stays visible
        // in the gaps between the buttons/cards.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##content_area", ImVec2(fullW, s.y - 72.0f * sc - 8.0f * sc), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

if (tab == 0)
{
        // ── Content header + horizontal subtab bar ────────────────
        UI::ContentHeader("AIM");
        
        {
            static float sa[5] = {};
            
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Aim", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Triggerbot", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Hitbox", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Weapon", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Autoclicker", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

        if (tab2 == 0)
        {
            // ── Aimbot: General + Smoothing (left column) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("GENERAL", halfW))
            {
                UI::labelsection("AIMBOT");
                UI::Checkbox("Enabled", &Options::Aimbot::Aimbot);
                UI::Checkbox("Team Check", &Options::Aimbot::TeamCheck);
                UI::Checkbox("Knocked Check", &Options::Aimbot::DownedCheck);
                UI::Checkbox("Anti Katana", &Options::Rivals::AntiKatana);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rivals: skip players currently holding a katana.");
                UI::Checkbox("Sticky Aim", &Options::Aimbot::StickyAim);
                UI::Checkbox("Wall Check", &Options::Aimbot::WallCheck);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only lock onto players that are not behind walls.");
                UI::Checkbox("Prediction", &Options::Aimbot::Prediction);

                UI::labelsection("RANGE & FOV");
                UI::SliderFloat("Range", &Options::Aimbot::Range, 1.f, 1000.f, "%.0f");
                UI::SliderFloat("FOV", &Options::Aimbot::FOV, 10.f, 360.f, "%.0f");

UI::labelsection("SMOOTHING & FEEL");
            static const char* aimingMethods[]{ "Camera", "Mouse", "Silent" };
            UI::Combo("Method", &Options::Aimbot::AimingType, aimingMethods, IM_ARRAYSIZE(aimingMethods), halfW);

            static const char* smoothnessCurves[]{ "Linear", "Ease In", "Ease Out", "Ease In-Out", "Custom" };
            UI::Combo("Curve", &Options::Aimbot::SmoothnessCurve, smoothnessCurves, IM_ARRAYSIZE(smoothnessCurves), halfW);

                UI::SliderFloat("Smoothness", &Options::Aimbot::Smoothness, 0.f, 1.f, "%.3f");

                // ── Smoothness Curve preview ──
                ImGui::Dummy(ImVec2(0, 6));
                UI::labelsection("SMOOTHNESS CURVE PREVIEW");
                ImGui::Dummy(ImVec2(0, 4));

                {
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImVec2 graphSize = ImVec2(avail - 6.0f * sc, 100.0f * sc);
                    ImVec2 graphPos = ImGui::GetCursorScreenPos();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    drawList->AddRectFilled(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), UI::U(UI::P.surface), 2.0f);
                    drawList->AddRect(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), UI::U(UI::P.accentSoft), 2.0f);

                    const ImU32 gridCol = UI::U(UI::P.divider);
                    for (int i = 1; i < 4; i++)
                    {
                        float y = graphPos.y + (graphSize.y / 4.0f) * i;
                        drawList->AddLine(ImVec2(graphPos.x, y), ImVec2(graphPos.x + graphSize.x, y), gridCol, 1.0f);
                    }
                    for (int i = 1; i < 4; i++)
                    {
                        float x = graphPos.x + (graphSize.x / 4.0f) * i;
                        drawList->AddLine(ImVec2(x, graphPos.y), ImVec2(x, graphPos.y + graphSize.y), gridCol, 1.0f);
                    }

                    ImVec2 prevPoint = ImVec2(graphPos.x, graphPos.y + graphSize.y);
                    for (int i = 1; i <= 100; i++)
                    {
                        float t = i / 100.0f;
                        float value;

                        switch (Options::Aimbot::SmoothnessCurve)
                        {
                        case 0: value = t; break;
                        case 1: value = t * t; break;
                        case 2: value = sqrt(t); break;
                        case 3: value = t * t * (3.0f - 2.0f * t); break;
                        case 4:
                        {
                            float p0 = 0.0f;
                            float p1 = Options::Aimbot::CustomCurveP1[1];
                            float p2 = Options::Aimbot::CustomCurveP2[1];
                            float p3 = 1.0f;
                            float u = 1.0f - t;
                            float tt = t * t;
                            float ttt = tt * t;
                            float uu = u * u;
                            float uuu = uu * u;
                            value = uuu * p0 + 3 * uu * t * p1 + 3 * u * tt * p2 + ttt * p3;
                            break;
                        }
                        default: value = t; break;
                        }

                        ImVec2 point = ImVec2(
                            graphPos.x + t * graphSize.x,
                            graphPos.y + graphSize.y - value * graphSize.y
                        );
                        drawList->AddLine(prevPoint, point, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255), 2.0f);
                        prevPoint = point;
                    }

                    if (Options::Aimbot::SmoothnessCurve == 4)
                    {
                        Options::Aimbot::CustomCurveEnabled = true;

                        ImVec2 cp1Pos = ImVec2(
                            graphPos.x + Options::Aimbot::CustomCurveP1[0] * graphSize.x,
                            graphPos.y + graphSize.y - Options::Aimbot::CustomCurveP1[1] * graphSize.y);
                        ImVec2 cp2Pos = ImVec2(
                            graphPos.x + Options::Aimbot::CustomCurveP2[0] * graphSize.x,
                            graphPos.y + graphSize.y - Options::Aimbot::CustomCurveP2[1] * graphSize.y);

                        drawList->AddLine(ImVec2(graphPos.x, graphPos.y + graphSize.y), cp1Pos, IM_COL32(100, 100, 100, 150), 1.0f);
                        drawList->AddLine(cp2Pos, ImVec2(graphPos.x + graphSize.x, graphPos.y), IM_COL32(100, 100, 100, 150), 1.0f);

                        float cpRadius = 5.0f;
                        drawList->AddCircleFilled(cp1Pos, cpRadius, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255));
                        drawList->AddCircle(cp1Pos, cpRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);
                        drawList->AddCircleFilled(cp2Pos, cpRadius, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255));
                        drawList->AddCircle(cp2Pos, cpRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);

                        ImVec2 mousePos = ImGui::GetMousePos();
                        bool mouseDown = ImGui::IsMouseDown(0);
                        static int draggedPoint = -1;

                        if (mouseDown)
                        {
                            if (draggedPoint == -1)
                            {
                                float dist1 = sqrt(pow(mousePos.x - cp1Pos.x, 2) + pow(mousePos.y - cp1Pos.y, 2));
                                if (dist1 <= cpRadius + 3.0f) draggedPoint = 0;
                                float dist2 = sqrt(pow(mousePos.x - cp2Pos.x, 2) + pow(mousePos.y - cp2Pos.y, 2));
                                if (dist2 <= cpRadius + 3.0f) draggedPoint = 1;
                            }
                            if (draggedPoint == 0)
                            {
                                Options::Aimbot::CustomCurveP1[0] = std::clamp((mousePos.x - graphPos.x) / graphSize.x, 0.0f, 1.0f);
                                Options::Aimbot::CustomCurveP1[1] = std::clamp((graphPos.y + graphSize.y - mousePos.y) / graphSize.y, 0.0f, 1.0f);
                            }
                            else if (draggedPoint == 1)
                            {
                                Options::Aimbot::CustomCurveP2[0] = std::clamp((mousePos.x - graphPos.x) / graphSize.x, 0.0f, 1.0f);
                                Options::Aimbot::CustomCurveP2[1] = std::clamp((graphPos.y + graphSize.y - mousePos.y) / graphSize.y, 0.0f, 1.0f);
                            }
                        }
                        else
                        {
                            draggedPoint = -1;
                        }
                    }
                    else
                    {
                        Options::Aimbot::CustomCurveEnabled = false;
                    }

                    drawList->AddText(ImVec2(graphPos.x + 2, graphPos.y + graphSize.y + 2), IM_COL32(150, 150, 150, 255), "0.0");
                    drawList->AddText(ImVec2(graphPos.x + graphSize.x - 20, graphPos.y + graphSize.y + 2), IM_COL32(150, 150, 150, 255), "1.0");

                    ImGui::Dummy(ImVec2(graphSize.x, graphSize.y + 15));
                }

                UI::labelsection("BEHAVIOR");
                UI::Checkbox("Shake", &Options::Aimbot::Shake);
                UI::Checkbox("Stutter", &Options::Aimbot::Stutter);
                UI::Checkbox("Ignore Jump", &Options::Aimbot::IgnoreJump);

                if (Options::Aimbot::Shake)
                    UI::SliderFloat("Shake Intensity", &Options::Aimbot::ShakeIntensity, 0.1f, 10.0f, "%.1f");
                if (Options::Aimbot::Stutter)
                    UI::SliderInt("Stutter Ticks", &Options::Aimbot::StutterTicks, 1, 20);
                if (Options::Aimbot::IgnoreJump)
                    UI::SliderFloat("Jump Threshold", &Options::Aimbot::JumpThreshold, 1.f, 100.f, "%.0f");

                UI::labelsection("KEYBIND");
                UI::Bind("##aimbot_key", &Options::Aimbot::AimbotKey, &Options::Aimbot::ToggleType);
            }
            UI::CollapsibleEnd();

            // ── Aimbot: Targeting + Silent Aim (right column) ──
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            if (UI::CollapsibleSection("TARGETING", cardW))
            {
                UI::labelsection("HITBOX");
                static const char* hitboxModes[]{ "Fixed Bone", "Closest Part" };
                UI::Combo("Hitbox Mode", &Options::Aimbot::HitboxMode, hitboxModes, IM_ARRAYSIZE(hitboxModes), cardW);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fixed Bone uses the Hit Part / Air Hit Part selectors.\nClosest Part aims at the body part nearest your cursor.");
                Options::Aimbot::ClosestPart = (Options::Aimbot::HitboxMode == 1);

                static const char* hitParts[]{ "Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Lower Torso", "Upper Torso" };
                if (!Options::Aimbot::ClosestPart)
                {
                    UI::Combo("Hit Part", &Options::Aimbot::TargetBone, hitParts, IM_ARRAYSIZE(hitParts), cardW);
                    UI::Combo("Air Hit Part", &Options::Aimbot::AirTargetBone, hitParts, IM_ARRAYSIZE(hitParts), cardW);
                }

                static const char* priorities[]{ "Closest Part", "Crosshair", "Lowest Health", "Farthest", "Highest Health" };
                UI::Combo("Target Priority", &Options::Aimbot::TargetPriority, priorities, IM_ARRAYSIZE(priorities), cardW);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which enemy wins when several are inside FOV.");

                UI::labelsection("SWITCHING");
                UI::SliderFloat("Switch Delay (ms)", &Options::Aimbot::TargetSwitchDelay, 0.f, 1000.f, "%.0f");

                UI::labelsection("SILENT AIM");
                UI::Checkbox("Silent Aim", &Options::Aimbot::SilentAim);

                if (Options::Aimbot::SilentAim || Options::Aimbot::AimingType == 2)
                {
                    static const char* silentModes[]{ "Camera Only", "Camera + Mouse Spoof" };
                    UI::Combo("Silent Mode", &Options::Aimbot::SilentAimMode, silentModes, IM_ARRAYSIZE(silentModes), cardW);
                    UI::Checkbox("Real Cursor Snap (hits on Overkill)", &Options::Aimbot::SilentAimRealCursor);
                    UI::Tooltip("Snaps your real cursor onto the target while firing so the shot lands. Visible flick on Overkill.");
                    UI::Checkbox("Teleport (no crosshair move)", &Options::Aimbot::SilentAimTeleport);
                    UI::Tooltip("Teleports your character next to the target so the shot registers without moving the crosshair.");
                }

                UI::labelsection("RAYCAST SILENT AIM");
                UI::Checkbox("Enabled", &Options::Aimbot::SilentAimEnabled);
                UI::Tooltip("Raycast-based silent aim. Uses workspace raycast for hit verification.");
                UI::Bind("##sa_key", &Options::Aimbot::SilentAimKey, &Options::Aimbot::SilentAimToggleType);

                static const char* saToggleTypes[]{ "Hold", "Toggle", "Always On" };
                UI::Combo("Mode##sa", &Options::Aimbot::SilentAimToggleType, saToggleTypes, IM_ARRAYSIZE(saToggleTypes), cardW);

                if (Options::Aimbot::SilentAimEnabled)
                {
                    UI::SliderFloat("FOV", &Options::Aimbot::SilentAimFOV, 10.f, 300.f, "%.0f");
                    UI::SliderFloat("Smoothness", &Options::Aimbot::SilentAimSmoothness, 0.f, 50.f, "%.1f");
                    UI::Tooltip("0 = instant snap. Higher = smoother camera movement.");

                    static const char* saModes[]{ "Camera Rotation", "Mouse Move", "Both" };
                    UI::Combo("Method", reinterpret_cast<int*>(&Options::Aimbot::SilentAimMethod), saModes, IM_ARRAYSIZE(saModes), cardW);
                    
                    UI::Checkbox("Team Check", &Options::Aimbot::SilentAimTeamCheck);
                    UI::Checkbox("Prediction", &Options::Aimbot::SilentAimPrediction);
                    if (Options::Aimbot::SilentAimPrediction)
                    {
                        UI::SliderFloat("Prediction X", &Options::Aimbot::SilentAimPredictionX, 0.5f, 3.0f, "%.2f");
                        UI::SliderFloat("Prediction Y", &Options::Aimbot::SilentAimPredictionY, 0.5f, 3.0f, "%.2f");
                    }

                    static const char* saBones[]{ "Head", "UpperTorso", "LowerTorso", "HumanoidRootPart" };
                    UI::Combo("Target Part", &Options::Aimbot::SilentAimTargetBone, saBones, IM_ARRAYSIZE(saBones), cardW);
                }

                UI::labelsection("SILENT LOCK");
                UI::Checkbox("Silent Lock", &Options::Aimbot::SilentLock);
                UI::Tooltip("Silently keeps aim on the closest target. Camera Rotation writes the camera matrix; Viewport Offset shifts the hit point.");
                UI::Bind("##silentlock_key", &Options::Aimbot::SilentLockKey, &Options::Aimbot::SilentLockMode);

                if (Options::Aimbot::SilentLock)
                {
                    static const char* lockModes[]{ "Camera Rotation", "Viewport Offset" };
                    UI::Combo("Lock Mode", &Options::Aimbot::SilentLockMode, lockModes, IM_ARRAYSIZE(lockModes), cardW);
                    UI::Checkbox("Target Line", &Options::Aimbot::TargetLine);
                }

                UI::labelsection("AIM INFO");
                UI::Checkbox("Enable", &Options::Aimbot::AimInfo);
                UI::Tooltip("Draws a HUD with the current target's info at the top-right corner.");
                UI::Checkbox("Name", &Options::Aimbot::AimInfoName);
                UI::Checkbox("Distance", &Options::Aimbot::AimInfoDistance);
                UI::Checkbox("Health", &Options::Aimbot::AimInfoHealth);
                UI::Checkbox("Part", &Options::Aimbot::AimInfoPart);

                ImGui::Dummy(ImVec2(0, 10));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 8));

                UI::labelsection("FLICKBOT");
                UI::Checkbox("Flickbot", &Options::Aimbot::Flickbot);
                UI::Checkbox("Team Check", &Options::Aimbot::FlickbotTeamCheck);
                UI::Bind("##flickbot_key", &Options::Aimbot::FlickbotKey);
                if (Options::Aimbot::Flickbot)
                {
                    UI::SliderFloat("Flick FOV", &Options::Aimbot::FlickbotFOV, 10.0f, 400.0f, "%.0f");
                    UI::Tooltip("Maximum angle to search for targets. Higher values can snap to enemies further from your crosshair.");
                    UI::SliderFloat("Smoothing", &Options::Aimbot::FlickbotSmoothing, 0.0f, 1.0f, "%.2f");
                    UI::Tooltip("Higher values create a smoother, more natural flick. 0 = instant snap.");
                }

                UI::labelsection("VISUALS");
                UI::Checkbox("Target Line", &Options::Aimbot::TargetLine);
                UI::Checkbox("Wall Check", &Options::Aimbot::WallCheck);
                if (Options::Aimbot::TargetLine)
                {
                    UI::SliderFloat("Line Thickness", &Options::Aimbot::TargetLineThickness, 0.5f, 5.0f, "%.1f");
                    UI::ColorEdit3("Line Color", Options::Aimbot::TargetLineColor, ImGuiColorEditFlags_NoInputs);
                }

                if (Options::Aimbot::Prediction)
                {
                    UI::SliderFloat("Prediction X", &Options::Aimbot::PredictionX, 0.01f, 10.0f, "%.2f");
                    UI::SliderFloat("Prediction Y", &Options::Aimbot::PredictionY, 0.01f, 10.0f, "%.2f");
                }
            }
            UI::CollapsibleEnd();
        }
        else if (tab2 == 1)
        {
            // ── Triggerbot: Main (left) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("TRIGGERBOT", halfW))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::Triggerbot::Enabled);
                UI::Checkbox("Team Check", &Options::Triggerbot::TeamCheck);
                UI::Checkbox("Knocked Check", &Options::Triggerbot::DownedCheck);
                UI::Checkbox("Wall Check", &Options::Triggerbot::WallCheck);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Don't trigger on enemies hidden behind walls/geometry.");
                UI::Checkbox("Prediction", &Options::Triggerbot::Prediction);
                UI::Checkbox("Advanced FOV", &Options::Triggerbot::AdvancedFOV);
                if (Options::Triggerbot::AdvancedFOV)
                    UI::Checkbox("Show FOV", &Options::Triggerbot::ShowAdvancedFOV);

                UI::labelsection("KEYBIND");
                UI::Bind("##triggerbot_key", &Options::Triggerbot::TriggerbotKey);
            }
            UI::CollapsibleEnd();

            // ── Triggerbot: Settings (right) ──
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            if (UI::CollapsibleSection("SETTINGS", cardW))
            {
                UI::labelsection("BASIC");
                if (!Options::Triggerbot::AdvancedFOV)
                    UI::SliderFloat("Radius", &Options::Triggerbot::Radius, 0.1f, 50.f, "%.1f");
                UI::SliderFloat("Range", &Options::Triggerbot::Range, 0.1f, 1000.f, "%.1f");
                UI::SliderInt("Delay (ms)", &Options::Triggerbot::Delay, 0, 500);

                UI::labelsection("DYNAMIC FOV");
                UI::Checkbox("Dynamic FOV", &Options::Triggerbot::DynamicFOV);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scales the hit zone by distance so enemies close AND far are equally easy to hit.");
                if (Options::Triggerbot::DynamicFOV)
                {
                    UI::SliderFloat("FOV Scale", &Options::Triggerbot::DynamicFOVScale, 0.1f, 5.0f, "%.2f");
                    UI::SliderFloat("Reference Dist", &Options::Triggerbot::DynamicFOVBaseDist, 5.f, 200.f, "%.0f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance (studs) at which the FOV equals the base Radius/FOV. Closer enemies keep full size; farther enemies grow.");
                }

                if (Options::Triggerbot::Prediction)
                {
                    UI::labelsection("PREDICTION");
                    UI::SliderFloat("Prediction X", &Options::Triggerbot::PredictionX, 0.0f, 10.0f, "%.2f");
                    UI::SliderFloat("Prediction Y", &Options::Triggerbot::PredictionY, 0.0f, 10.0f, "%.2f");
                }

                // Advanced FOV sliders
                if (Options::Triggerbot::AdvancedFOV)
                {
                    UI::labelsection("ADVANCED FOV (PER BONE)");
                    ImGui::Text(" HEAD");
                    UI::SliderFloat("Head FOV X", &Options::Triggerbot::HeadFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("Head FOV Y", &Options::Triggerbot::HeadFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" TORSO");
                    UI::SliderFloat("Torso FOV X", &Options::Triggerbot::TorsoFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("Torso FOV Y", &Options::Triggerbot::TorsoFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" UPPER TORSO");
                    UI::SliderFloat("U Torso FOV X", &Options::Triggerbot::UpperTorsoFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("U Torso FOV Y", &Options::Triggerbot::UpperTorsoFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" LOWER TORSO");
                    UI::SliderFloat("L Torso FOV X", &Options::Triggerbot::LowerTorsoFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Torso FOV Y", &Options::Triggerbot::LowerTorsoFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" LEFT ARM");
                    UI::SliderFloat("L U Arm FOV X", &Options::Triggerbot::LeftUpperArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L U Arm FOV Y", &Options::Triggerbot::LeftUpperArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Arm FOV X", &Options::Triggerbot::LeftLowerArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Arm FOV Y", &Options::Triggerbot::LeftLowerArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Hand FOV X", &Options::Triggerbot::LeftHandFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Hand FOV Y", &Options::Triggerbot::LeftHandFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" RIGHT ARM");
                    UI::SliderFloat("R U Arm FOV X", &Options::Triggerbot::RightUpperArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R U Arm FOV Y", &Options::Triggerbot::RightUpperArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Arm FOV X", &Options::Triggerbot::RightLowerArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Arm FOV Y", &Options::Triggerbot::RightLowerArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Hand FOV X", &Options::Triggerbot::RightHandFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Hand FOV Y", &Options::Triggerbot::RightHandFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" LEFT LEG");
                    UI::SliderFloat("L U Leg FOV X", &Options::Triggerbot::LeftUpperLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L U Leg FOV Y", &Options::Triggerbot::LeftUpperLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Leg FOV X", &Options::Triggerbot::LeftLowerLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Leg FOV Y", &Options::Triggerbot::LeftLowerLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Foot FOV X", &Options::Triggerbot::LeftFootFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Foot FOV Y", &Options::Triggerbot::LeftFootFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" RIGHT LEG");
                    UI::SliderFloat("R U Leg FOV X", &Options::Triggerbot::RightUpperLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R U Leg FOV Y", &Options::Triggerbot::RightUpperLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Leg FOV X", &Options::Triggerbot::RightLowerLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Leg FOV Y", &Options::Triggerbot::RightLowerLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Foot FOV X", &Options::Triggerbot::RightFootFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Foot FOV Y", &Options::Triggerbot::RightFootFOV_Y, 0.f, 100.f, "%.1f");
                }
            }
            UI::CollapsibleEnd();
        }
        else if (tab2 == 2)
        {
            // ── Hitbox Expander ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("HITBOX EXPANDER", halfW))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::HitboxExpander::Enabled);
                UI::Checkbox("Show Hitbox", &Options::HitboxExpander::ShowHitbox);
                UI::Checkbox("Walk Through", &Options::HitboxExpander::WalkThrough);

                UI::labelsection("SETTINGS");
                UI::SliderFloat("Horizontal Size", &Options::HitboxExpander::HorizontalSize, 1.0f, 50.0f, "%.1f");
                UI::SliderFloat("Vertical Size", &Options::HitboxExpander::VerticalSize, 1.0f, 50.0f, "%.1f");
                UI::SliderFloat("Transparency", &Options::HitboxExpander::HitboxTransparency, 0.0f, 1.0f, "%.2f");
            }
            UI::CollapsibleEnd();

            // Preview panel
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            // (preview rendered in ESP overlay)
        }
        else if (tab2 == 3)
        {
            // ── Weapon + FOV subtab ──
            // Current weapon selector on the left, full-width weapon profiles below.

            static const char* wpWeapons[] = {
                "Assault Rifle", "Warper", "Bow", "Burst Rifle", "Chainsaw",
                "Sniper", "Daggers", "Jump Pad", "Permafrost", "Uzi",
                "Exogun", "Maul", "Grenade", "Flare Gun", "Flashbang",
                "Freeze Ray", "Flamethrower", "Energy Rifle", "Gunblade", "Handgun",
                "Spear", "Katana", "Knife", "Medkit", "Minigun",
                "Molotov", "Paintball Gun", "RPG", "Revolver", "Riot Shield",
                "Satchel", "Scythe", "Shorty", "Shotgun", "Slingshot",
                "Smoke Grenade", "Crossbow", "Spray", "Battle Axe", "Trowel",
                "Grenade Launcher", "Warpstone", "Distortion", "Subspace Tripmine",
                "Energy Pistols", "Fists", "Grappler", "War Horn"
            };

            const float panelY = ImGui::GetCursorPosY();

            // Full-width: CURRENT WEAPON (FOV VISUALS moved to the VISUALS tab)
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("CURRENT WEAPON", ctW))
            {
                static int curIdx = -1;
                int match = -1;
                for (int k = 0; k < IM_ARRAYSIZE(wpWeapons); k++)
                    if (Options::WeaponProfiles::CurrentWeapon == wpWeapons[k]) { match = k; break; }
                curIdx = match;
                ImGui::SetNextItemWidth(ctW - 28.0f * sc);
                if (UI::Combo("Weapon", &curIdx, wpWeapons, IM_ARRAYSIZE(wpWeapons)))
                    Options::WeaponProfiles::CurrentWeapon =
                        (curIdx >= 0 && curIdx < (int)IM_ARRAYSIZE(wpWeapons)) ? wpWeapons[curIdx] : "";

                ImGui::Spacing();
                UI::labelsection("STATUS");
                if (Options::WeaponProfiles::ActiveProfile >= 0 &&
                    Options::WeaponProfiles::ActiveProfile < static_cast<int>(Options::WeaponProfiles::Profiles.size()))
                {
                    auto& ap = Options::WeaponProfiles::Profiles[Options::WeaponProfiles::ActiveProfile];
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Matched: %s", ap.Name[0] ? ap.Name : "?");
                }
                else
                {
                    ImGui::TextColored(UI::P.textMid, "None (add & enable a profile)");
                }
            }
            UI::CollapsibleEnd();

            // Full-width WEAPON PROFILES panel below
            ImGui::SetCursorPosY(panelY + 160 * sc + 12.0f * sc);
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("WEAPON PROFILES", ctW))
            {
                if (UI::Button("Add Profile", ImVec2(-1, 28)))
                {
                    Options::WeaponProfile p;
                    p.Enabled = true;
                    snprintf(p.Name, sizeof(p.Name), "Weapon%d", (int)Options::WeaponProfiles::Profiles.size() + 1);
                    Options::WeaponProfiles::Profiles.push_back(p);
                    Options::WeaponProfiles::SelectedProfile = (int)Options::WeaponProfiles::Profiles.size() - 1;
                }
                ImGui::Spacing();

                if (Options::WeaponProfiles::Profiles.empty())
                {
                    ImGui::TextColored(UI::P.textMid, "Click 'Add Profile' to create per-weapon aimbot settings.");
                }
                else
                {
                    std::vector<const char*> names;
                    std::vector<std::string> stable;
                    for (auto& pro : Options::WeaponProfiles::Profiles)
                    {
                        std::string lbl = std::string(pro.Name[0] ? pro.Name : "(unnamed)") +
                            (pro.Enabled ? " [ON]" : " [OFF]");
                        stable.push_back(lbl);
                        names.push_back(stable.back().c_str());
                    }
                    int sel = Options::WeaponProfiles::SelectedProfile;
                    UI::Combo("Profile", &sel, names.data(), (int)names.size());
                    Options::WeaponProfiles::SelectedProfile = std::clamp(sel, 0, (int)Options::WeaponProfiles::Profiles.size() - 1);

                    if (sel >= 0 && sel < (int)Options::WeaponProfiles::Profiles.size())
                    {
                        auto& prof = Options::WeaponProfiles::Profiles[sel];

                        UI::labelsection("PROFILE SETTINGS");
                        UI::Checkbox("Enabled", &prof.Enabled);
                        ImGui::InputText("Weapon Name", prof.Name, sizeof(prof.Name));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Matches the held weapon name (case-insensitive).");

                        static const char* wpMethods[]{ "Camera", "Mouse", "Silent" };
                        UI::Combo("Method", &prof.AimingType, wpMethods, IM_ARRAYSIZE(wpMethods));
                        UI::Checkbox("Silent Aim", &prof.SilentAim);
                        if (prof.SilentAim)
                        {
                            static const char* wpSilentModes[]{ "Camera", "Mouse Spoof" };
                            UI::Combo("Silent Mode", &prof.SilentAimMode, wpSilentModes, IM_ARRAYSIZE(wpSilentModes));
                        }

                        static const char* wpBones[]{ "Head", "Torso", "Upper Torso", "Lower Torso" };
                        UI::Combo("Target Bone", &prof.TargetBone, wpBones, IM_ARRAYSIZE(wpBones));
                        {
                            static const char* hitboxModes[]{ "Fixed Bone", "Closest Part" };
                            UI::Combo("Hitbox Mode", &prof.ClosestPart, hitboxModes, IM_ARRAYSIZE(hitboxModes));
                        }

                        UI::SliderFloat("Range", &prof.Range, 1.f, 1000.f, "%.0f");
                        UI::SliderFloat("FOV", &prof.FOV, 10.f, 360.f, "%.0f");
                        UI::SliderFloat("Smoothness", &prof.Smoothness, 0.f, 1.f, "%.3f");

                        UI::labelsection("CHECKS");
                        UI::Checkbox("Team Check", &prof.TeamCheck);
                        UI::Checkbox("Knocked Check", &prof.DownedCheck);
                        UI::Checkbox("Wall Check", &prof.WallCheck);
                        UI::Checkbox("Sticky Aim", &prof.StickyAim);
                        UI::Checkbox("Prediction", &prof.Prediction);
                        if (prof.Prediction)
                        {
                            UI::SliderFloat("Prediction X", &prof.PredictionX, 0.0f, 10.0f, "%.2f");
                            UI::SliderFloat("Prediction Y", &prof.PredictionY, 0.0f, 10.0f, "%.2f");
                        }
                        UI::Checkbox("Ignore Jump", &prof.IgnoreJump);
                        if (prof.IgnoreJump)
                            UI::SliderFloat("Jump Threshold", &prof.JumpThreshold, 1.0f, 100.0f, "%.1f");

                        ImGui::Spacing();
                        if (UI::Button("Delete Profile", ImVec2(-1, 24)))
                        {
                            Options::WeaponProfiles::Profiles.erase(Options::WeaponProfiles::Profiles.begin() + sel);
                            if (Options::WeaponProfiles::SelectedProfile >= (int)Options::WeaponProfiles::Profiles.size())
                                Options::WeaponProfiles::SelectedProfile = (int)Options::WeaponProfiles::Profiles.size() - 1;
                            if (Options::WeaponProfiles::SelectedProfile < 0)
                                Options::WeaponProfiles::SelectedProfile = 0;
                        }
                    }
                }
            }
            UI::CollapsibleEnd();
        }
        else if (tab2 == 4)
        {
            // ── Autoclicker (moved from Movement) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("AUTOCLICKER", halfW))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::Autoclicker::Enabled);
                UI::SliderFloat("CPS", &Options::Autoclicker::CPS, 1.f, 100.f, "%.0f");
                UI::Checkbox("Right Click", &Options::Autoclicker::RightClick);
                UI::Checkbox("Only On Hold (LMB)", &Options::Autoclicker::OnlyOnHold);
            }
            UI::CollapsibleEnd();
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            if (UI::CollapsibleSection("BIND", cardW))
            {
                UI::labelsection("KEYBIND");
                UI::Bind("##ac_key", &Options::Autoclicker::Key, &Options::Autoclicker::ToggleType);
            }
            UI::CollapsibleEnd();
        }
}
else if (tab == 2)
{
// ===== Rage tab =====
        // Content header + horizontal subtab bar
        UI::ContentHeader("RAGE");
        {
            static float sa[6] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Ragebot", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Orbit", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Anti-Aim", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Desync", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("VoidHide", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Bhop", tab2 == 5, sa[5])) tab2 = 5;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) { RenderRagebotSubtab(main_color); }
else if (tab2 == 1) { RenderOrbitSubtab(main_color); }
else if (tab2 == 2) { RenderAntiAimSubtab(main_color); }
else if (tab2 == 3) { RenderDesyncSubtab(main_color); }
else if (tab2 == 4) { RenderVoidHideSubtab(main_color); }
else if (tab2 == 5) { RenderBhopSubtab(main_color); }
}
else if (tab == 1)
{
        // Content header + horizontal subtab bar
        UI::ContentHeader("VISUALS");
        {
            static float sa[6] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("ESP", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Combat", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("World", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Colours", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Crosshair", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("FOV", tab2 == 5, sa[5])) tab2 = 5;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("ESP FEATURES", halfW))
{
UI::labelsection("FILTER");
UI::Checkbox("Master Enable", &Options::ESP::Enabled);
UI::Checkbox("Team Check", &Options::ESP::TeamCheck);
UI::Checkbox("Visibility Colors", &Options::ESP::VisibilityCheck);
UI::Checkbox("Visibility Bones", &Options::ESP::VisibilityChams);

UI::labelsection("PLAYER LIST");
UI::Checkbox("Player Filter", &Options::PlayerFilter::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Focus / exclude players. Excluded players are hidden from the ESP and skipped by the aimbot.");
UI::Checkbox("Focus Only", &Options::PlayerFilter::FocusOnly);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("When at least one player is marked as Focus, the aimbot only targets focused players.");
UI::Checkbox("Exclude Friends", &Options::PlayerFilter::ExcludeFriends);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hides players in your friend list from the ESP and the aimbot.");

UI::labelsection("PLAYER INFO");
	UI::Checkbox("Names", &Options::ESP::Name);
	UI::Checkbox("Distance", &Options::ESP::Distance);
	UI::Checkbox("Health Bar", &Options::ESP::Health);
	UI::Checkbox("Health Text", &Options::ESP::HealthText);
	UI::Checkbox("HP Above Head", &Options::ESP::EnemyHealthIndicator);
	UI::Checkbox("Tool", &Options::ESP::Tool);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the equipped tool under the target's name.");
	if (Options::ESP::Tool)
	{
		UI::SliderFloat("Tool Size", &Options::ESP::ToolSize, 8.0f, 24.0f, "%.1f");
		UI::ColorEdit4("Tool Color", Options::ESP::ToolColor, ImGuiColorEditFlags_NoInputs);
	}

	UI::labelsection("OVERLAYS");
	UI::Checkbox("Corner ESP", &Options::ESP::CornerESP);
	UI::Checkbox("Tracers", &Options::ESP::Tracers);
	UI::Checkbox("Skeleton", &Options::ESP::Skeleton);
	UI::Checkbox("Head Circle", &Options::ESP::HeadCircle);
	UI::Checkbox("Head Dot", &Options::ESP::HeadDot);
	UI::Checkbox("Arrows", &Options::ESP::Arrows);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows directional arrows for off-screen players.");
	UI::Checkbox("Radar", &Options::ESP::Radar);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows a circular radar overlay with nearby players.");
	if (Options::ESP::Radar)
	{
		static const char* radarThemes[]{ "Classic", "Minimal", "Neon", "Compass" };
		UI::Combo("Radar Theme", &Options::ESP::RadarTheme, radarThemes, IM_ARRAYSIZE(radarThemes));
	}
	UI::Checkbox("Rig Type", &Options::ESP::RigType);
	UI::Checkbox("Local Only", &Options::ESP::LocalOnly);
	UI::Checkbox("Avatar Icon", &Options::ESP::AvatarIcon);

	UI::labelsection("BOX FILL");
	UI::Checkbox("Box", &Options::ESP::Box);
	if (Options::ESP::Box)
	{
		static const char* boxTypes[]{ "None", "Normal Box", "3D Box" };
		UI::Combo("Type", &Options::ESP::BoxType, boxTypes, IM_ARRAYSIZE(boxTypes));
		UI::Checkbox("Gradient Fill", &Options::ESP::BoxFillGradient);
		if (Options::ESP::BoxFillGradient)
		{
			static const char* fillTypes[]{ "Vertical", "Horizontal", "Four-Corner" };
			UI::Combo("Fill Type", &Options::ESP::BoxFillType, fillTypes, IM_ARRAYSIZE(fillTypes));
			UI::Checkbox("Rotate Gradient", &Options::ESP::BoxFillGradientRotate);
			if (Options::ESP::BoxFillGradientRotate)
				UI::SliderFloat("Fill Speed", &Options::ESP::BoxFillSpeed, 0.1f, 10.0f, "%.1f");
			UI::ColorEdit4("Top Color", Options::ESP::BoxFillTopColor, ImGuiColorEditFlags_NoInputs);
			UI::ColorEdit4("Bottom Color", Options::ESP::BoxFillBottomColor, ImGuiColorEditFlags_NoInputs);
		}
		else
		{
			UI::ColorEdit4("Fill Color", Options::ESP::BoxFillColor, ImGuiColorEditFlags_NoInputs);
		}
	}

	UI::labelsection("HEALTHBAR");
	UI::Checkbox("Gradient Healthbar", &Options::ESP::GradientHealthbar);
	if (Options::ESP::GradientHealthbar)
	{
		UI::ColorEdit4("Top Color", Options::ESP::HealthbarTopColor, ImGuiColorEditFlags_NoInputs);
		UI::ColorEdit4("Middle Color", Options::ESP::HealthbarMiddleColor, ImGuiColorEditFlags_NoInputs);
		UI::ColorEdit4("Bottom Color", Options::ESP::HealthbarBottomColor, ImGuiColorEditFlags_NoInputs);
	}

	UI::labelsection("EFFECTS");
	UI::Checkbox("Glow", &Options::ESP::Glow);
	UI::Checkbox("Pulse", &Options::ESP::Pulse);
	if (Options::ESP::Pulse)
		UI::SliderFloat("Pulse Speed", &Options::ESP::PulseSpeed, 0.1f, 5.0f, "%.2f");
	UI::Checkbox("Rings", &Options::ESP::Rings);
	if (Options::ESP::Rings)
		UI::SliderFloat("Ring Radius", &Options::ESP::RingRadius, 10.0f, 150.0f, "%.0f");
	UI::Checkbox("Trails", &Options::ESP::Trails);
	if (Options::ESP::Trails)
		UI::SliderInt("Trail Length", &Options::ESP::TrailLength, 4, 60);

	UI::labelsection("NAME & IMAGE");
	static const char* nameModes[]{ "Username", "Health%" };
	UI::Combo("Name Mode", &Options::ESP::NameMode, nameModes, IM_ARRAYSIZE(nameModes));
	UI::Checkbox("Custom Image", &Options::ESP::CustomImage);
	if (Options::ESP::CustomImage)
	{
		UI::SliderFloat("Image Scale", &Options::ESP::CustomImageScale, 0.2f, 4.0f, "%.2f");
		char imgBuf[256]; strncpy_s(imgBuf, Options::ESP::CustomImagePath, sizeof(imgBuf) - 1);
		if (ImGui::InputText("Image Path", imgBuf, sizeof(imgBuf)))
			strncpy_s(Options::ESP::CustomImagePath, imgBuf, sizeof(Options::ESP::CustomImagePath) - 1);
		ImGui::SameLine();
		if (ImGui::Button("Browse", ImVec2(-1, 20)))
		{
			OPENFILENAMEA ofn = { 0 };
			char szFile[256] = { 0 };
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = GetActiveWindow();
			ofn.lpstrFile = szFile;
			ofn.nMaxFile = sizeof(szFile);
			ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
			ofn.nFilterIndex = 1;
			ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
			if (GetOpenFileNameA(&ofn))
			{
				strncpy_s(Options::ESP::CustomImagePath, szFile, _TRUNCATE);
			}
		}
	}
	}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("ESP SETTINGS", cardW))
{
	UI::labelsection("BOX");
	UI::SliderFloat("Box Thickness", &Options::ESP::BoxThickness, 1.0f, 10.0f);
UI::SliderFloat("3D Box Thickness", &Options::ESP::ESP3DThickness, 1.0f, 10.0f);

UI::labelsection("LINES");
UI::SliderFloat("Tracer Thickness", &Options::ESP::TracerThickness, 1.0f, 10.0f);
UI::SliderFloat("Skeleton Thickness", &Options::ESP::SkeletonThickness, 1.0f, 10.0f);

UI::labelsection("TEXT");
UI::SliderFloat("Name Size", &Options::ESP::NameSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Name Thickness", &Options::ESP::NameThickness, 0.0f, 5.0f, "%.1f");
UI::SliderFloat("Distance Size", &Options::ESP::DistanceSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Rig Type Size", &Options::ESP::RigTypeSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Arrow Size", &Options::ESP::ArrowSize, 8.0f, 32.0f, "%.1f");

UI::labelsection("HEAD");
UI::SliderFloat("Circle Thickness", &Options::ESP::HeadCircleThickness, 1.0f, 10.0f);
UI::SliderFloat("Circle Size", &Options::ESP::HeadCircleScale, 0.05f, 0.20f, "%.2f");

if (Options::ESP::VisibilityCheck || Options::ESP::VisibilityChams)
{
UI::labelsection("VISIBILITY");
UI::SliderFloat("Scan Range", &Options::ESP::VisibilityMaxDistance, 100.0f, 800.0f, "%.0f");
}

UI::labelsection("KEYBIND");
static const char* toggleTypes[]{ "Hold", "Toggle" };
UI::Combo("Mode##esp", &Options::ESP::ToggleType, toggleTypes, IM_ARRAYSIZE(toggleTypes));
            UI::Bind("##esp_key", &Options::ESP::ESPKey, &Options::ESP::ToggleType);

            UI::labelsection("CHAMS");
            UI::Checkbox("Chams Enabled", &Options::Chams::Enabled);
            UI::Checkbox("Team Check", &Options::Chams::TeamCheck);
            UI::Checkbox("Fade", &Options::Chams::ChamsFade);
            if (Options::Chams::ChamsFade)
                UI::SliderInt("Fade Speed", &Options::Chams::ChamsFadeSpeed, 1, 10);
            UI::Checkbox("Gradient Fill", &Options::Chams::GradientFill);
            UI::Checkbox("Wireframe", &Options::Chams::Wireframe);
            if (Options::Chams::Wireframe)
                UI::SliderFloat("Wireframe Thickness", &Options::Chams::WireframeThickness, 0.5f, 5.0f, "%.1f");
            UI::Checkbox("Include Accessories", &Options::Chams::IncludeAccessories);
            UI::Checkbox("Gradient Fill", &Options::Chams::GradientFill);
            UI::ColorEdit4("Fill Color 1", Options::Chams::FillColor, ImGuiColorEditFlags_NoInputs);
            UI::ColorEdit4("Fill Color 2", Options::Chams::FillColor2, ImGuiColorEditFlags_NoInputs);
            UI::ColorEdit4("Outline Color", Options::Chams::OutlineColor, ImGuiColorEditFlags_NoInputs);
            }
            UI::CollapsibleEnd();
}
else if (tab2 == 1) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("HIT FEEDBACK", halfW))
{
UI::labelsection("HITS");
UI::Checkbox("Hit Sounds", &Options::Combat::HitSounds);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays a sound whenever you hit someone.");
if (Options::Combat::HitSounds)
{
    auto& soundFiles = Globals::HitSounds::Files;
    if (soundFiles.empty())
    {
        ImGui::TextDisabled("No sounds found");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Place audio files in the 'hitsounds' folder next to the exe.");
    }
    else
    {
        if (Options::Combat::HitSoundType < 0 || Options::Combat::HitSoundType >= (int)soundFiles.size())
            Options::Combat::HitSoundType = 0;
        static auto hitSoundGetter = [](void*, int idx, const char** out_text) -> bool
        {
            auto& files = Globals::HitSounds::Files;
            if (idx < 0 || idx >= (int)files.size()) return false;
            *out_text = files[idx].c_str();
            return true;
        };
        ImGui::Combo("Hit Sound", &Options::Combat::HitSoundType, hitSoundGetter, nullptr, (int)soundFiles.size());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select which sound plays on hit.");
        if (ImGui::Button("Preview", ImVec2(-1, 20)))
        {
            std::string path = Globals::HitSounds::FolderPath + "\\" + soundFiles[Options::Combat::HitSoundType];
            PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        }
    }
}
UI::Checkbox("Hit Notifications", &Options::Combat::HitNotifications);
UI::Checkbox("Hit Chams", &Options::Combat::HitChams);
UI::Checkbox("Hit Effects", &Options::Combat::HitEffects);
if (Options::Combat::HitEffects)
{
    static const char* hmStyles[]{ "Cross", "Circle", "Dot" };
    UI::Combo("Hitmarker Style", &Options::Combat::HitmarkerStyle, hmStyles, IM_ARRAYSIZE(hmStyles));
    UI::Checkbox("On Crosshair", &Options::Combat::HitmarkerOnCrosshair);
    UI::SliderFloat("Hitmarker Size", &Options::Combat::HitmarkerSize, 4.0f, 20.0f, "%.1f");
    UI::SliderFloat("Hitmarker Thickness", &Options::Combat::HitmarkerThickness, 1.0f, 5.0f, "%.1f");
}

UI::labelsection("TRACERS");
UI::Checkbox("Bullet Tracers", &Options::Combat::BulletTracers);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws tracer lines from your position to the target on hit.");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("HIT SETTINGS", cardW))
{
UI::labelsection("DAMAGE");
UI::SliderFloat("Min Damage", &Options::Combat::MinDamage, 1.0f, 50.0f, "%.0f");
UI::SliderFloat("Chams Duration", &Options::Combat::HitChamsDuration, 0.1f, 2.0f, "%.2fs");
UI::SliderFloat("Effect Duration", &Options::Combat::HitEffectDuration, 0.1f, 2.0f, "%.2fs");
UI::ColorEdit3("Hit Chams Color", Options::Combat::HitChamsColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Hit Effect Color", Options::Combat::HitEffectColor, ImGuiColorEditFlags_NoInputs);

if (Options::Combat::BulletTracers)
{
UI::labelsection("BULLET TRACERS");
UI::ColorEdit3("Tracer Color", Options::Combat::BulletTracerColor, ImGuiColorEditFlags_NoInputs);
UI::SliderFloat("Tracer Duration", &Options::Combat::BulletTracerDuration, 0.1f, 3.0f, "%.1fs");
UI::SliderFloat("Tracer Width", &Options::Combat::BulletTracerThickness, 0.5f, 5.0f, "%.1f");
static const char* tracerStyles[]{ "Solid", "Glow", "Dashed", "Pulse" };
UI::Combo("Style##tracer", &Options::Combat::BulletTracerStyle, tracerStyles, IM_ARRAYSIZE(tracerStyles));
}
}
UI::CollapsibleEnd();
}
else if (tab2 == 2) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("WORLD", halfW))
{
            UI::labelsection("MAIN");
            UI::Checkbox("Enabled", &Options::World::Enabled);
            UI::Checkbox("Fullbright", &Options::World::Fullbright);
            UI::Checkbox("No Shadows", &Options::World::NoShadows);
            UI::Checkbox("Skybox Changer", &Options::World::SkyboxChanger);

            static const char* skyPresets[]{ "Default", "Blue", "Night", "Storm", "Sunset", "Grass", "Plastic", "Red", "Purple", "Pink", "Gold" };
            UI::Combo("Sky Preset", &Options::World::SkyboxPreset, skyPresets, IM_ARRAYSIZE(skyPresets));
            UI::Checkbox("Rotate Skybox", &Options::World::RotateSkybox);
            if (Options::World::RotateSkybox)
                UI::SliderFloat("Rotate Speed", &Options::World::SkyboxRotateSpeed, 0.1f, 10.0f, "%.1f");
            }
            UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("LIGHTING", cardW))
{
	UI::labelsection("TIME & BRIGHTNESS");
	UI::SliderFloat("Clock Time", &Options::World::ClockTime, 0.0f, 24.0f, "%.1f");
	UI::SliderFloat("Brightness", &Options::World::Brightness, 0.0f, 5.0f, "%.1f");
	UI::Checkbox("Brightness (Separate)", &Options::World::BrightnessEnabled);
	if (Options::World::BrightnessEnabled)
		UI::SliderFloat("Brightness Value", &Options::World::BrightnessValue, 0.0f, 5.0f, "%.1f");
	UI::Checkbox("Exposure", &Options::World::Exposure);
	if (Options::World::Exposure)
		UI::SliderFloat("Exposure Value", &Options::World::ExposureValue, -2.0f, 2.0f, "%.2f");

	UI::labelsection("FOG");
	UI::Checkbox("Fog (Separate)", &Options::World::FogEnabled);
	if (Options::World::FogEnabled)
	{
		UI::SliderFloat("Fog Distance", &Options::World::FogDistance, 0.0f, 100000.0f, "%.0f");
		UI::ColorEdit4("Fog Color", Options::World::FogColor2, ImGuiColorEditFlags_NoInputs);
	}
	else
	{
		UI::SliderFloat("Fog Start", &Options::World::FogStart, 0.0f, 1000.0f, "%.0f");
		UI::SliderFloat("Fog End", &Options::World::FogEnd, 50.0f, 100000.0f, "%.0f");
		UI::ColorEdit3("Fog Color", Options::World::FogColor, ImGuiColorEditFlags_NoInputs);
	}

	UI::labelsection("AMBIENT");
	UI::Checkbox("Ambience (Separate)", &Options::World::Ambience);
	if (Options::World::Ambience)
		UI::ColorEdit4("Ambience Color", Options::World::AmbienceColor, ImGuiColorEditFlags_NoInputs);
	else
	{
		UI::ColorEdit3("Ambient", Options::World::Ambient, ImGuiColorEditFlags_NoInputs);
		UI::ColorEdit3("Outdoor Ambient", Options::World::OutdoorAmbient, ImGuiColorEditFlags_NoInputs);
	}
}
UI::CollapsibleEnd();
}
else if (tab2 == 3) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("ESP COLOURS", halfW))
{
UI::labelsection("BOX");
UI::ColorEdit3("Box Color", Options::ESP::BoxColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("3D Box Color", Options::ESP::ESP3DColor, ImGuiColorEditFlags_NoInputs);

UI::labelsection("INFO");
UI::ColorEdit3("Name Color", Options::ESP::Color, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Distance Color", Options::ESP::DistanceColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("OVERLAYS");
	UI::ColorEdit3("Tracer Color", Options::ESP::TracerColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Skeleton Color", Options::ESP::SkeletonColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Head Circle Color", Options::ESP::HeadCircleColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Head Dot Color", Options::ESP::HeadDotColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Corner Color", Options::ESP::CornerColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("BOX FILL");
	UI::ColorEdit4("Fill Color", Options::ESP::BoxFillColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Fill Top Color", Options::ESP::BoxFillTopColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Fill Bottom Color", Options::ESP::BoxFillBottomColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("HEALTHBAR");
	UI::ColorEdit4("Health Top", Options::ESP::HealthbarTopColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Health Middle", Options::ESP::HealthbarMiddleColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Health Bottom", Options::ESP::HealthbarBottomColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("EXTRA");
	UI::ColorEdit3("Rig Type Color", Options::ESP::RigTypeColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("CHAMS");
	UI::ColorEdit4("Fill Color", Options::Chams::FillColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Outline Color", Options::Chams::OutlineColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("VISIBILITY");
UI::ColorEdit3("Visible", Options::ESP::VisibleColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Hidden", Options::ESP::HiddenColor, ImGuiColorEditFlags_NoInputs);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("FOV & MENU", cardW))
{
UI::labelsection("THEME");
{
    float availW = ImGui::GetContentRegionAvail().x;
    float cardW = (availW - 6.0f * sc) * 0.5f;
    float cardH = 52.0f * sc;
    auto* draw = ImGui::GetWindowDrawList();

    for (int t = 0; t < MenuThemes::Count; t++)
    {
        if (t % 2 == 1) ImGui::SameLine(0, 6.0f * sc);

        ImVec2 cp = ImGui::GetCursorScreenPos();
        bool sel = (Options::Misc::MenuTheme == t);

        // Card background
        ImU32 bg = sel ? ImGui::ColorConvertFloat4ToU32(ImVec4(
            UI::P.accent.x * 0.15f, UI::P.accent.y * 0.15f, UI::P.accent.z * 0.15f, 0.30f))
            : ImGui::ColorConvertFloat4ToU32(UI::P.card);
        draw->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), bg, 7.0f * sc);

        // Border
        ImU32 border = sel ? ImGui::ColorConvertFloat4ToU32(UI::P.accent)
            : ImGui::ColorConvertFloat4ToU32(UI::P.borderDim);
        float bw = sel ? 1.5f * sc : 1.0f * sc;
        draw->AddRect(cp, ImVec2(cp.x + cardW, cp.y + cardH), border, 7.0f * sc, 0, bw);

        // Theme name
        draw->AddText(ImGui::GetFont(), 10.0f * sc,
            ImVec2(cp.x + 9.0f * sc, cp.y + 7.0f * sc),
            sel ? ImGui::ColorConvertFloat4ToU32(UI::P.accent)
                : ImGui::ColorConvertFloat4ToU32(UI::P.text),
            MenuThemes::Presets[t].name);

        // Color swatches
        float swX = cp.x + 9.0f * sc, swY = cp.y + 26.0f * sc;
        for (int c = 0; c < 4; c++)
        {
            const float* ch = (c == 0) ? MenuThemes::Presets[t].bg
                : (c == 1) ? MenuThemes::Presets[t].panel
                : (c == 2) ? MenuThemes::Presets[t].accent
                : MenuThemes::Presets[t].accent2;
            ImU32 swCol = ImGui::ColorConvertFloat4ToU32(ImVec4(ch[0], ch[1], ch[2], 1.0f));
            draw->AddRectFilled(ImVec2(swX, swY), ImVec2(swX + 13.0f * sc, swY + 13.0f * sc), swCol, 3.0f * sc);
            draw->AddRect(ImVec2(swX, swY), ImVec2(swX + 13.0f * sc, swY + 13.0f * sc),
                ImGui::ColorConvertFloat4ToU32(UI::P.borderDim), 3.0f * sc, 0, 0.5f);
            swX += 16.0f * sc;
        }

        // Gradient indicator
        if (MenuThemes::Presets[t].gradient) {
            ImVec2 gp(cp.x + cardW - 32.0f * sc, cp.y + cardH - 18.0f * sc);
            draw->AddRectFilled(gp, ImVec2(gp.x + 24.0f * sc, gp.y + 5.0f * sc),
                ImGui::ColorConvertFloat4ToU32(ImVec4(
                    MenuThemes::Presets[t].accent[0], MenuThemes::Presets[t].accent[1],
                    MenuThemes::Presets[t].accent[2], 1.0f)), 2.5f * sc);
            draw->AddRectFilled(ImVec2(gp.x + 8.0f * sc, gp.y), ImVec2(gp.x + 24.0f * sc, gp.y + 5.0f * sc),
                ImGui::ColorConvertFloat4ToU32(ImVec4(
                    MenuThemes::Presets[t].accent2[0], MenuThemes::Presets[t].accent2[1],
                    MenuThemes::Presets[t].accent2[2], 1.0f)), 2.5f * sc);
        }

        // InvisibleButton for click
        ImGui::SetCursorScreenPos(cp);
        char tid[32]; sprintf_s(tid, "##th_%d", t);
        if (ImGui::InvisibleButton(tid, ImVec2(cardW, cardH)))
            Options::Misc::MenuTheme = t;

        // Hover fill
        if (ImGui::IsItemHovered() && !sel) {
            draw->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH),
                ImGui::ColorConvertFloat4ToU32(ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.08f)), 7.0f * sc);
        }

        if (t % 2 == 0) ImGui::SameLine(0, 6.0f * sc);
        else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * sc);
    }
}

if (Options::Misc::MenuTheme == 0)
{
    UI::labelsection("CUSTOM COLORS");
    UI::ColorEdit3("Menu Background", Options::Misc::MenuBgColor, ImGuiColorEditFlags_NoInputs);
    UI::ColorEdit3("Panel Background", Options::Misc::MenuPanelColor, ImGuiColorEditFlags_NoInputs);
}

UI::labelsection("ACCENT");
UI::ColorEdit3("FOV Color", Options::Aimbot::FOVColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Menu Accent", Options::Misc::MenuAccentColor, ImGuiColorEditFlags_NoInputs);
main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);
UI::Checkbox("Menu Gradient", &Options::Misc::MenuGradient);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blend the two accent colors across the menu header.");
UI::ColorEdit3("Menu Accent 2", Options::Misc::MenuAccentColor2, ImGuiColorEditFlags_NoInputs);
main_color2 = ImVec4(Options::Misc::MenuAccentColor2[0], Options::Misc::MenuAccentColor2[1], Options::Misc::MenuAccentColor2[2], 1.0f);

UI::labelsection("FOV FILL");
UI::ColorEdit4("FOV Fill Color", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs);
}
UI::CollapsibleEnd();
}

if (tab2 == 4)
{
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("CROSSHAIR", fullW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Crosshair::Enabled);
UI::Checkbox("Show Text", &Options::Crosshair::ShowText);

static const char* chStyles[]{ "Static", "Pulse", "Spin", "Dynamic" };
UI::Combo("Style", &Options::Crosshair::Style, chStyles, IM_ARRAYSIZE(chStyles));

static const char* chColorModes[]{ "Static", "Rainbow" };
UI::Combo("Color Mode", &Options::Crosshair::ColorMode, chColorModes, IM_ARRAYSIZE(chColorModes));

UI::labelsection("SIZE & SHAPE");
UI::SliderFloat("Size", &Options::Crosshair::Size, 1.0f, 40.0f, "%.1f");
UI::SliderFloat("Gap", &Options::Crosshair::Gap, 0.0f, 40.0f, "%.1f");
UI::SliderFloat("Thickness", &Options::Crosshair::Thickness, 1.0f, 10.0f, "%.1f");
UI::SliderFloat("Spin Speed", &Options::Crosshair::SpinSpeed, 0.0f, 360.0f, "%.0f deg/s");
UI::SliderFloat("Gap Speed", &Options::Crosshair::GapSpeed, 0.1f, 5.0f, "%.2f");
UI::SliderFloat("Opacity", &Options::Crosshair::Opacity, 0.1f, 1.0f, "%.2f");
UI::SliderFloat("Rainbow Speed", &Options::Crosshair::RainbowSpeed, 0.1f, 5.0f, "%.2f");

UI::labelsection("OPTIONS");
UI::Checkbox("Gap Tween", &Options::Crosshair::GapTween);
UI::Checkbox("Show Dot", &Options::Crosshair::ShowDot);
UI::Checkbox("Outline", &Options::Crosshair::Outline);
UI::Checkbox("T-Style", &Options::Crosshair::TStyle);

if (Options::Crosshair::ShowDot)
UI::SliderFloat("Dot Size", &Options::Crosshair::DotSize, 0.5f, 10.0f, "%.1f");
if (Options::Crosshair::Outline)
{
UI::SliderFloat("Outline Thickness", &Options::Crosshair::OutlineThickness, 0.5f, 5.0f, "%.1f");
UI::ColorEdit4("Outline Color", Options::Crosshair::OutlineColor, ImGuiColorEditFlags_NoInputs);
}

UI::labelsection("LENGTH");
static const char* lenModes[]{ "Equal 4 Lines", "Vertical Longer" };
UI::Combo("Length Mode", &Options::Crosshair::LengthMode, lenModes, IM_ARRAYSIZE(lenModes));
if (Options::Crosshair::LengthMode == 1)
UI::SliderFloat("Vertical Length", &Options::Crosshair::VLength, 0.0f, 40.0f, "%.1f");

UI::labelsection("COLOUR");
UI::ColorEdit4("Color", Options::Crosshair::Color, ImGuiColorEditFlags_NoInputs);
}
UI::CollapsibleEnd();
}
else if (tab2 == 5)
{
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("FOV VISUALS", halfW))
{
UI::labelsection("DISPLAY");
UI::Checkbox("Show FOV", &Options::Aimbot::ShowFOV);
UI::Checkbox("Show FOV Fill", &Options::Aimbot::ShowFOVFill);
UI::Checkbox("Show FOV Text", &Options::Aimbot::ShowFOVText);

static const char* fovPositions[]{ "Screen Center", "Follow Target" };
UI::Combo("FOV Position", &Options::Aimbot::FOVPositionMode, fovPositions, IM_ARRAYSIZE(fovPositions), halfW);

static const char* fovShapes[]{ "Circle", "Square", "Triangle", "Hexagon" };
UI::Combo("FOV Shape", &Options::Aimbot::FOVShape, fovShapes, IM_ARRAYSIZE(fovShapes), halfW);

static const char* fovColorModes[]{ "Solid", "Gradient", "Shift", "Pulse" };
UI::Combo("FOV Color Mode", &Options::Aimbot::FOVColorMode, fovColorModes, IM_ARRAYSIZE(fovColorModes), halfW);

UI::Checkbox("FOV Glow", &Options::Aimbot::FOVGlow);
UI::Checkbox("FOV Breathing", &Options::Aimbot::FOVBreathing);
UI::Checkbox("FOV Spin", &Options::Aimbot::FOVSpin);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("STYLING", halfW))
{
UI::labelsection("STYLING");
UI::SliderFloat("FOV Thickness", &Options::Aimbot::FOVThickness, 1.0f, 10.0f, "%.1f");
if (Options::Aimbot::FOVColorMode == 1 || Options::Aimbot::FOVColorMode == 2)
    UI::SliderFloat("Gradient Speed", &Options::Aimbot::FOVGradientSpeed, 0.1f, 5.0f, "%.2f");
if (Options::Aimbot::FOVSpin)
    UI::SliderFloat("Spin Speed", &Options::Aimbot::FOVSpinSpeed, 0.1f, 5.0f, "%.2f");

UI::labelsection("COLOURS");
UI::ColorEdit3("FOV Color", Options::Aimbot::FOVColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("FOV Fill", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs);
}
UI::CollapsibleEnd();
}
}
else if (tab == 3)
{
// Misc tab - Local settings only
UI::ContentHeader("MISC");
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("MAIN", halfW))
{
UI::labelsection("LOCAL");
UI::Checkbox("Headless",       &Options::ESP::Headless);
UI::Checkbox("Show FOV",       &Options::Aimbot::ShowFOV);
UI::Checkbox("Show FOV Fill",  &Options::Aimbot::ShowFOVFill);
UI::Checkbox("Crosshair",      &Options::Crosshair::Enabled);
	UI::Checkbox("Camera FOV",     &Options::Misc::FOVEnabled);
	UI::Checkbox("Cache NPCs",     &Options::Misc::CacheNPCs);
	UI::Checkbox("Keybind List",   &Options::Misc::KeybindList);
	UI::Checkbox("Explorer",       &Options::Misc::ExplorerEnabled);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the Roblox instance explorer (datamodel tree + properties / bytecode).");
	UI::Checkbox("Player List",    &Options::Misc::PlayerListEnabled);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the player list window to select, exclude, focus or mark players as friends.");
	UI::Checkbox("Third Person",   &Options::Misc::ThirdPerson);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unlocks third-person camera in games that force first-person.");

	UI::labelsection("STEALTH");
	UI::Checkbox("Hide From Tabs", &Options::Misc::HideFromTabs);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes the overlay from Alt+Tab / Win+Tab and the taskbar (WS_EX_TOOLWINDOW).");
	UI::Checkbox("Hide Process", &Options::Misc::HideProcess);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relaunches the cheat as a renamed copy in %TEMP% so Task Manager shows a benign name. Applies on next launch.");

	static const char* procPresets[]{ "MicrosoftEdgeUpdate", "OneDriveSetup", "SearchApp", "Widgets", "GameBar", "OneDriveStandaloneUpdater", "Custom..." };
	static int procSel = 0;
	if (strcmp(Options::Misc::ProcessName, "MicrosoftEdgeUpdate") == 0) procSel = 0;
	else if (strcmp(Options::Misc::ProcessName, "OneDriveSetup") == 0) procSel = 1;
	else if (strcmp(Options::Misc::ProcessName, "SearchApp") == 0) procSel = 2;
	else if (strcmp(Options::Misc::ProcessName, "Widgets") == 0) procSel = 3;
	else if (strcmp(Options::Misc::ProcessName, "GameBar") == 0) procSel = 4;
	else if (strcmp(Options::Misc::ProcessName, "OneDriveStandaloneUpdater") == 0) procSel = 5;
	else procSel = 6;

	if (UI::Combo("Process Name", &procSel, procPresets, IM_ARRAYSIZE(procPresets)))
	{
		if (procSel == 6) { /* keep custom */ }
		else strncpy_s(Options::Misc::ProcessName, procPresets[procSel], sizeof(Options::Misc::ProcessName) - 1);
	}
	if (procSel == 6)
	{
		ImGui::SameLine(); ImGui::SetNextItemWidth(180 * sc);
		char procBuf[64]; strncpy_s(procBuf, Options::Misc::ProcessName, sizeof(procBuf) - 1);
		if (ImGui::InputText("##proc_custom", procBuf, sizeof(procBuf)))
			strncpy_s(Options::Misc::ProcessName, procBuf, sizeof(Options::Misc::ProcessName) - 1);
	}

	char exclBuf[256]; strncpy_s(exclBuf, Options::Misc::ExclusionPath, sizeof(exclBuf) - 1);
	if (ImGui::InputText("Exclusion Path", exclBuf, sizeof(exclBuf)))
		strncpy_s(Options::Misc::ExclusionPath, exclBuf, sizeof(Options::Misc::ExclusionPath) - 1);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Folder the trace-wiper will never delete. Use it to store the cheat somewhere safe.");

	UI::Checkbox("Stream Proof", &Options::Misc::StreamProof);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Attempts to hide overlay from OBS/Discord stream capture (DWM exclusion).");

	UI::labelsection("MENU FONT");
UI::Combo("Font", &Options::Misc::MenuFont, MenuFonts::Names, IM_ARRAYSIZE(MenuFonts::Names));
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Live-switches the menu typography. Applies immediately.");
UI::SliderFloat("Menu Scale", &Options::Misc::MenuScale, 0.6f, 2.5f, "%.2fx");
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zooms the entire menu (text, panels and graphics). Drag the title bar to move it.");

UI::labelsection("MENU EFFECT");
UI::Checkbox("Enable##weather", &MenuWeather::Enabled);

static const char* weatherKinds[] = { "Snow", "Rain" };
UI::Combo("Type", &MenuWeather::Type, weatherKinds, 2);
ImGui::SliderInt ("Intensity",       &MenuWeather::Intensity,     64,   2000, "%d particles");
UI::SliderFloat("Fall Speed",     &MenuWeather::Speed,         0.2f, 6.0f,  "%.2fx");
UI::SliderFloat("Wind",           &MenuWeather::Wind,         -3.f,  3.f,   "%.2fx");
UI::SliderFloat("Snow Size",      &MenuWeather::SnowSize,      0.5f, 4.0f,  "%.1f px");
UI::SliderFloat("Rain Thickness", &MenuWeather::RainThickness, 0.5f, 3.0f,  "%.1f px");
ImGui::ColorEdit3 ("Particle Color", MenuWeather::Color, ImGuiColorEditFlags_NoInputs);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Falling snowflakes or rain streaks across the menu background. Settings are saved with your config.");

UI::labelsection("EXTERIUM BACKDROP");
UI::Checkbox("Sword Emblem", &Options::Misc::ExteriumSword);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws the theme-tinted Exterium sword behind the menu panels.");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", halfW))
{
if (Options::Misc::FOVEnabled)
UI::SliderFloat("Camera FOV", &Options::Misc::FOV, 70.f, 120.f, "%.0f");

UI::labelsection("KEYBIND LIST POSITION");
UI::SliderFloat("Position X", &Options::Misc::KeybindListX, 0.0f, 1920.0f, "%.0f");
UI::SliderFloat("Position Y", &Options::Misc::KeybindListY, 0.0f, 1080.0f, "%.0f");

UI::labelsection("MENU KEY");
UI::Bind("##menu_key", &Options::Misc::MenuKey);

ImGui::Dummy(ImVec2(0, 15));
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.8f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
if (ImGui::Button("Unload", ImVec2(-1, 28)))
{
exit(0);
}
ImGui::PopStyleColor(3);
}
UI::CollapsibleEnd();
}
else if (tab == 4)
{
// Movement tab
        // Content header + horizontal subtab bar
        UI::ContentHeader("MOVEMENT");
        {
            static float sa[7] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Fly", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("WalkSpeed", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("TickRate", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Noclip", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Ramp Fling", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("360 Spin", tab2 == 5, sa[5])) tab2 = 5;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Extra", tab2 == 6, sa[6])) tab2 = 6;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("FLY", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Fly::Enabled);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
        {
            UI::labelsection("PARAMETERS");
            UI::SliderFloat("Fly Speed", &Options::Fly::Speed, 10.f, 200.f, "%.0f");

            UI::labelsection("KEYBIND");
            UI::Bind("##fly_key", &Options::Fly::FlyKey, &Options::Fly::ToggleType);
        }
        UI::CollapsibleEnd();
}
    else if (tab2 == 1) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("WALKSPEED", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::WalkSpeed::Enabled);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Walk Speed", &Options::WalkSpeed::Speed, 16.f, 1000.f, "%.0f");

UI::labelsection("KEYBIND");
UI::Bind("##walkspeed_key", &Options::WalkSpeed::WalkSpeedKey, &Options::WalkSpeed::ToggleType);
}
UI::CollapsibleEnd();
}
else if (tab2 == 2) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("TICKRATE", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::TickRate::Enabled);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("RATE");
UI::SliderFloat("Tick Rate", &Options::TickRate::Rate, 10.0f, 1000.0f, "%.0f");

UI::labelsection("PRESETS");
if (ImGui::Button("Default (60)", ImVec2(-1, 24))) Options::TickRate::Rate = 60.0f;
if (ImGui::Button("High (120)", ImVec2(-1, 24))) Options::TickRate::Rate = 120.0f;
if (ImGui::Button("Ultra (240)", ImVec2(-1, 24))) Options::TickRate::Rate = 240.0f;
if (ImGui::Button("Extreme (500)", ImVec2(-1, 24))) Options::TickRate::Rate = 500.0f;
if (ImGui::Button("Max (1000)", ImVec2(-1, 24))) Options::TickRate::Rate = 1000.0f;
}
UI::CollapsibleEnd();
}
else if (tab2 == 3) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("NOCLIP", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Noclip::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets you walk through walls and solid objects.");

bool noclipActive = Options::Noclip::Enabled &&
(Options::Noclip::ToggleType == 2 ||
(Options::Noclip::NoclipKey != 0 && Options::Noclip::Toggled));
UI::Status(noclipActive ? "ACTIVE" : "INACTIVE", noclipActive);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("TOGGLE");
static const char* noclipModes[]{ "Hold", "Toggle", "Always On" };
UI::Combo("Mode##noclip", &Options::Noclip::ToggleType, noclipModes, IM_ARRAYSIZE(noclipModes));
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold = while key held, Toggle = press once, Always On = always noclip.");

if (Options::Noclip::ToggleType != 2)
UI::Bind("##noclip_key", &Options::Noclip::NoclipKey, &Options::Noclip::ToggleType);

if (Options::Noclip::ToggleType == 1 && Options::Noclip::NoclipKey != 0)
{
ImGui::PushStyleColor(ImGuiCol_Button, Options::Noclip::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::Noclip::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
if (ImGui::Button(Options::Noclip::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
Options::Noclip::Toggled = !Options::Noclip::Toggled;
ImGui::PopStyleColor(2);
}
}
UI::CollapsibleEnd();
}
else if (tab2 == 4) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("RAMP FLING", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::RampFling::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically fling when hitting ramps.");

static const char* rampModes[]{ "Hold", "Toggle", "Always On" };
UI::Combo("Mode##ramp", &Options::RampFling::ToggleType, rampModes, IM_ARRAYSIZE(rampModes));

if (Options::RampFling::ToggleType != 2)
UI::Bind("##ramp_key", &Options::RampFling::FlingKey, &Options::RampFling::ToggleType);

if (Options::RampFling::ToggleType == 1 && Options::RampFling::FlingKey != 0)
{
ImGui::PushStyleColor(ImGuiCol_Button, Options::RampFling::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.5f) : ImVec4(0.15f, 0.15f, 0.18f, 0.8f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Options::RampFling::Toggled ? ImVec4(main_color.x, main_color.y, main_color.z, 0.6f) : ImVec4(0.20f, 0.20f, 0.24f, 0.9f));
if (ImGui::Button(Options::RampFling::Toggled ? "ACTIVE" : "INACTIVE", ImVec2(-1, 24)))
Options::RampFling::Toggled = !Options::RampFling::Toggled;
ImGui::PopStyleColor(2);
}
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Fling Force", &Options::RampFling::FlingForce, 10.f, 300.f, "%.0f");
UI::SliderFloat("Min Angle", &Options::RampFling::MinAngle, 5.f, 45.f, "%.0f");
UI::SliderFloat("Max Angle", &Options::RampFling::MaxAngle, 30.f, 90.f, "%.0f");
UI::SliderFloat("Cooldown", &Options::RampFling::Cooldown, 0.1f, 2.f, "%.1fs");
UI::SliderFloat("H. Boost", &Options::RampFling::HorizontalBoost, 0.f, 2.f, "%.1f");
}
UI::CollapsibleEnd();
}
else if (tab2 == 5) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("360 SPIN", halfW))
{
UI::labelsection("MAIN");
                UI::Checkbox("Enable 360 Spin", &Options::Spin360::Enabled);
UI::Tooltip("Spins your camera in a full 360 circle while the key is held.");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("CONTROLS");
UI::SliderFloat("Spin Speed", &Options::Spin360::Speed, 1.0f, 45.0f, "%.1f deg/tick");
UI::Bind("##spin360_key", &Options::Spin360::HotKey);
UI::Tooltip("Hold this key to continuously spin your camera.");
}
UI::CollapsibleEnd();
}
else if (tab2 == 6) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("CLICK TP", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::ClickTP::Enabled);
                UI::Bind("##clicktp_key", &Options::ClickTP::Key);
                UI::Tooltip("Teleports you to the point under the cursor when the key is pressed.");
                UI::SliderFloat("Max Distance", &Options::ClickTP::MaxDistance, 50.0f, 5000.0f, "%.0f");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("HIP HEIGHT", cardW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##hipheight", &Options::HipHeight::Enabled);
UI::Bind("##hipheight_key", &Options::HipHeight::Key);
UI::SliderFloat("Height", &Options::HipHeight::Value, 0.0f, 20.0f, "%.1f");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * sc);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("FREE CAM", cardW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##freecam", &Options::FreeCam::Enabled);
UI::Bind("##freecam_key", &Options::FreeCam::Key);
UI::SliderFloat("Speed", &Options::FreeCam::Speed, 10.0f, 200.0f, "%.0f");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * sc);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("STRETCH RES", cardW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##stretchres", &Options::StretchRes::Enabled);
UI::SliderFloat("Scale X", &Options::StretchRes::ScaleX, 0.5f, 2.0f, "%.2f");
UI::SliderFloat("Scale Y", &Options::StretchRes::ScaleY, 0.5f, 2.0f, "%.2f");
}
UI::CollapsibleEnd();
}
}
else if (tab == 5)
{
RenderConfigTab();
}
else if (tab == 6)
{
UI::ContentHeader("GAME");
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("GAME DETECTION", fullW))
{
UI::labelsection("DETECTED GAME");
ImGui::TextDisabled("Name:");
ImGui::SameLine();
ImGui::Text("%s", Globals::Roblox::gameName.c_str());

char pid[32];
snprintf(pid, sizeof(pid), "%d", Globals::Roblox::lastPlaceID);
ImGui::TextDisabled("Place ID:");
ImGui::SameLine();
ImGui::Text("%s", pid);

UI::labelsection("SUPPORTED OPTIMIZATIONS");

auto gameFlag = [&](const char* label, bool on)
{
ImGui::Bullet();
ImGui::Text("%s", label);
ImGui::SameLine();
ImGui::TextColored(on ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
on ? "[active]" : "[generic]");
};

gameFlag("Phantom Forces (camera-rotation silent aim)", Globals::Roblox::isPhantomForces);
gameFlag("Rivals (smoke/flash bypass)", Globals::Roblox::isRivals);
gameFlag("Overkill / Chickynoid (cursor-snap silent aim)", Globals::Roblox::isOverkill);
                gameFlag("Generic Roblox (viewport / camera aim)", !Globals::Roblox::isPhantomForces && !Globals::Roblox::isRivals && !Globals::Roblox::isOverkill);

UI::labelsection("ARSENAL GUNMODS");
UI::Checkbox("No Recoil", &Options::ArsenalGunmods::NoRecoil);
UI::Checkbox("Fast Fire Rate", &Options::ArsenalGunmods::FastFireRate);
UI::Checkbox("All Auto", &Options::ArsenalGunmods::AllAuto);
UI::Checkbox("Infinite Ammo", &Options::ArsenalGunmods::InfiniteAmmo);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the current curse name on the HUD. Enabled automatically with Infinite Ammo.");
}
UI::CollapsibleEnd();
}
else if (tab == 7)
{
    // ---- EXECUTOR TAB ----
    // Wrapper window owns a scrollbar and lets mouse wheel fall through to it
    // (the child cards below are NoScrollWithMouse for the editor's wheel use).
    // This guarantees the whole tab is scrollable at any font scale.
    ImGui::SetCursorPosX(ctX);
    ImGui::BeginChild("##executor_tab", ImVec2(fullW, s.y - 58.0f * sc - 8.0f * sc), false);
    {
        UI::ContentHeader("EXECUTOR");
        if (UI::CollapsibleSection("SCRIPT", fullW - 16.0f * sc))
        {
            const float innerW = (fullW - 16.0f * sc) - 2.0f * 14.0f * sc; // card WindowPadding is 14
            ImGui::SetCursorPos(ImVec2(6.0f * sc, ImGui::GetCursorPosY()));
            g_executorEditor.render("##executor_script", ImVec2(innerW - 20.0f * sc, 150.0f * sc));

            ImGui::SetCursorPos(ImVec2(6.0f * sc, ImGui::GetCursorPosY()));
            if (UI::Button("EXECUTE", ImVec2(120.0f * sc, 0.0f)))
            {
                Executor::ConsolePush("[executor] EXECUTE clicked, running script...");
                Executor::ClearConsole();
                Executor::Run(g_executorEditor.get_text());
            }
            ImGui::SameLine();
            if (UI::Button("STOP", ImVec2(90.0f * sc, 0.0f)))
                Executor::Stop();
            ImGui::SameLine();
            if (UI::Button("CLEAR", ImVec2(90.0f * sc, 0.0f)))
                Executor::ClearConsole();
            ImGui::SameLine();
            static bool s_injected = false;
            if (UI::Button(s_injected ? "EJECT" : "INJECT DLL", ImVec2(110.0f * sc, 0.0f)))
            {
                if (!s_injected)
                {
                    Injector::SetLogCallback([](const std::string& msg) { Executor::ConsolePush("[injector] " + msg); });
                    
                    Executor::ConsolePush("[injector] Button clicked, starting injection...");
                    try
                    {
                        Executor::ConsolePush("[injector] Step 1: Getting module path...");
                        wchar_t exePath[MAX_PATH];
                        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                        std::wstring dllPath = std::wstring(exePath);
                        size_t pos = dllPath.find_last_of(L'\\');
                        if (pos != std::wstring::npos)
                            dllPath = dllPath.substr(0, pos + 1) + L"SeraphExecutorDLL.dll";
                        
                        Executor::ConsolePush("[injector] Step 2: Loading DLL from " + std::string(dllPath.begin(), dllPath.end()));
                        auto dllBytes = Injector::LoadDllFromDisk(dllPath.c_str());
                        if (!dllBytes.empty())
                        {
                            Executor::ConsolePush("[injector] Step 3: DLL loaded (" + std::to_string(dllBytes.size()) + " bytes), finding Roblox...");
                            int pid = Injector::FindRobloxPID();
                            if (pid == 0)
                            {
                                Executor::ConsolePush("[injector] Failed: RobloxPlayerBeta.exe not running");
                            }
                            else
                            {
                                Executor::ConsolePush("[injector] Step 4: Roblox found (pid " + std::to_string(pid) + "), injecting...");
                                auto result = Injector::InjectDLLByName(L"RobloxPlayerBeta.exe", dllBytes);
                                if (result.success)
                                {
                                    s_injected = true;
                                    Executor::ConsolePush("[injector] Success: DLL injected at 0x" + std::to_string(result.dllBase) + " (pid " + std::to_string(result.pid) + ")");
                                }
                                else
                                {
                                    Executor::ConsolePush("[injector] Failed: " + result.error);
                                }
                            }
                        }
                        else
                        {
                            Executor::ConsolePush("[injector] Failed: SeraphExecutorDLL.dll not found at " + std::string(dllPath.begin(), dllPath.end()));
                        }
                    }
                    catch (const std::exception& e)
                    {
                        Executor::ConsolePush("[injector] C++ Exception: " + std::string(e.what()));
                    }
                    catch (...)
                    {
                        Executor::ConsolePush("[injector] Unknown crash (SEH/access violation)");
                    }
                }
                else
                {
                    Executor::ConsolePush("[injector] Eject not implemented");
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(s_injected ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                s_injected ? "[INJECTED]" : "[NOT INJECTED]");
        }
        UI::CollapsibleEnd();

        if (UI::CollapsibleSection("CONSOLE", fullW - 16.0f * sc))
        {
            const float innerW = (fullW - 16.0f * sc) - 2.0f * 14.0f * sc;
            ImGui::SetCursorPos(ImVec2(6.0f * sc, ImGui::GetCursorPosY()));
            if (ImGui::BeginChild("##executor_console", ImVec2(innerW - 20.0f * sc, 170.0f * sc), true))
            {
                const auto lines = Executor::ConsoleSnapshot();
                static size_t lastConsoleCount = 0;
                const bool grew = lines.size() > lastConsoleCount;
                lastConsoleCount = lines.size();
                ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 8.0f * sc);
                for (const auto& l : lines)
                    ImGui::TextUnformatted(l.c_str());
                ImGui::PopTextWrapPos();
                if (grew)
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
        UI::CollapsibleEnd();
    }
    ImGui::EndChild();
}
    ImGui::EndChild(); // ##content_area
    ImGui::PopStyleColor();

ImGui::PopFont();
}
ImGui::PopStyleVar();
ImGui::End();
}

// ESP Preview overlay (positioned to the right of the menu, clamped to screen)
if (tab == 1 && tab2 == 0 && Options::ESP::Enabled && menuAlpha > 0.0f && menuPos.x >= 0)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float previewW = 320.0f * UI::sc;
    float previewH = 470.0f * UI::sc;
    float idealX = menuPos.x + 960.0f + 12.0f;
    float maxX = display.x - previewW - 8.0f;
    float px = (idealX > maxX) ? maxX : idealX;
    if (px < 8.0f) px = 8.0f;
    const ImVec2 previewPos = ImVec2(px, menuPos.y + 16 + 52 * UI::sc);
    const ImVec2 previewSize = ImVec2(previewW, previewH);
    RenderESPPreview(dl, previewPos, previewSize);
}

// Active binds list
if (Options::Misc::KeybindList)
{
    struct BindEntry { const char* name; const char* mode; };
    static std::vector<BindEntry> activeBinds;
    activeBinds.clear();

if (Options::Fly::Enabled)
{
bool isActive = false;
if (Options::Fly::ToggleType == 2) isActive = true;
else if (Options::Fly::ToggleType == 1) isActive = Options::Fly::Toggled;
else if (Options::Fly::FlyKey != 0) isActive = (GetAsyncKeyState(Options::Fly::FlyKey) & 0x8000) != 0;
const char* modeLabel = Options::Fly::ToggleType == 2 ? "[On]" :
Options::Fly::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"Fly", modeLabel});
}

if (Options::WalkSpeed::Enabled)
{
bool isActive = false;
if (Options::WalkSpeed::ToggleType == 2) isActive = true;
else if (Options::WalkSpeed::ToggleType == 1) isActive = Options::WalkSpeed::Toggled;
else if (Options::WalkSpeed::WalkSpeedKey != 0) isActive = (GetAsyncKeyState(Options::WalkSpeed::WalkSpeedKey) & 0x8000) != 0;
const char* modeLabel = Options::WalkSpeed::ToggleType == 2 ? "[On]" :
Options::WalkSpeed::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"WalkSpeed", modeLabel});
}

if (Options::Noclip::Enabled)
{
bool isActive = false;
if (Options::Noclip::ToggleType == 2) isActive = true;
else if (Options::Noclip::ToggleType == 1) isActive = Options::Noclip::Toggled;
else if (Options::Noclip::NoclipKey != 0) isActive = (GetAsyncKeyState(Options::Noclip::NoclipKey) & 0x8000) != 0;
const char* modeLabel = Options::Noclip::ToggleType == 2 ? "[On]" :
Options::Noclip::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"Noclip", modeLabel});
}

if (Options::Orbit::Enabled)
{
bool isActive = false;
if (Options::Orbit::ToggleType == 2) isActive = true;
else if (Options::Orbit::ToggleType == 1) isActive = Options::Orbit::Toggled;
else if (Options::Orbit::OrbitKey != 0) isActive = (GetAsyncKeyState(Options::Orbit::OrbitKey) & 0x8000) != 0;
const char* modeLabel = Options::Orbit::ToggleType == 2 ? "[On]" :
Options::Orbit::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"Orbit", modeLabel});
}

if (Options::Desync::Enabled)
{
bool isActive = false;
if (Options::Desync::ToggleType == 2) isActive = true;
else if (Options::Desync::ToggleType == 1) isActive = Options::Desync::Toggled;
else if (Options::Desync::DesyncKey != 0) isActive = (GetAsyncKeyState(Options::Desync::DesyncKey) & 0x8000) != 0;
const char* modeLabel = Options::Desync::ToggleType == 2 ? "[On]" :
Options::Desync::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"Desync", modeLabel});
}

if (Options::RampFling::Enabled)
{
bool isActive = false;
if (Options::RampFling::ToggleType == 2) isActive = true;
else if (Options::RampFling::ToggleType == 1) isActive = Options::RampFling::Toggled;
else if (Options::RampFling::FlingKey != 0) isActive = (GetAsyncKeyState(Options::RampFling::FlingKey) & 0x8000) != 0;
const char* modeLabel = Options::RampFling::ToggleType == 2 ? "[On]" :
Options::RampFling::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"Ramp Fling", modeLabel});
}

if (Options::VoidHide::Enabled)
{
bool isActive = false;
if (Options::VoidHide::ToggleType == 2) isActive = true;
else if (Options::VoidHide::ToggleType == 1) isActive = Options::VoidHide::Toggled;
else if (Options::VoidHide::VoidHideKey != 0) isActive = (GetAsyncKeyState(Options::VoidHide::VoidHideKey) & 0x8000) != 0;
const char* modeLabel = Options::VoidHide::ToggleType == 2 ? "[On]" :
Options::VoidHide::ToggleType == 1 ? "[Toggled]" : "[Hold]";
if (isActive) activeBinds.push_back({"VoidHide", modeLabel});
}

if (Options::Bhop::Enabled && Options::Bhop::BhopKey != 0 &&
(GetAsyncKeyState(Options::Bhop::BhopKey) & 0x8000) != 0)
{
activeBinds.push_back({"Bhop", "[Hold]"});
}

if (Options::ESP::Enabled)
{
bool isActive = false;
if (Options::ESP::ToggleType == 0) isActive = true;
else if (Options::ESP::ToggleType == 1) isActive = Options::ESP::Toggled;
const char* modeLabel = Options::ESP::ToggleType == 0 ? "[Always]" : Options::ESP::Toggled ? "[On]" : "[Off]";
if (isActive) activeBinds.push_back({"ESP", modeLabel});
}

if (!activeBinds.empty())
{
auto* drawList = ImGui::GetBackgroundDrawList();
float yOffset = Options::Misc::KeybindListY;
float maxWidth = 0;

for (auto& b : activeBinds)
{
std::string line = std::string(b.name) + " " + b.mode;
float w = ImGui::CalcTextSize(line.c_str()).x;
if (w > maxWidth) maxWidth = w;
}

float boxW = maxWidth + 20.0f;
float lineH = ImGui::GetTextLineHeight() + 4.0f;
float boxH = activeBinds.size() * lineH + 10.0f;

drawList->AddRectFilled(
ImVec2(Options::Misc::KeybindListX - 5, yOffset - 5),
ImVec2(Options::Misc::KeybindListX + boxW, yOffset + boxH),
IM_COL32(20, 20, 20, 255));

for (size_t i = 0; i < activeBinds.size(); i++)
{
std::string line = std::string(activeBinds[i].name) + " " + activeBinds[i].mode;
drawList->AddText(
ImVec2(Options::Misc::KeybindListX, yOffset + i * lineH),
IM_COL32(255, 255, 255, 255),
line.c_str());
}
}
}

if (IsGameOnTop("Roblox"))
{
	AntiKatanaFiringBlocked();
	CombatFeedback::Update();
	if (!menu_open)
	{
		RunAimCore(ImGui::GetBackgroundDrawList());
		RunMacro();
	}
	RunTriggerbot();
	if (Options::Aimbot::ShowFOV)
	{
		RunAimCore(ImGui::GetBackgroundDrawList());
	}

	RenderAdvancedFOV(ImGui::GetBackgroundDrawList());
	RenderCrosshair(ImGui::GetBackgroundDrawList());
	if (Options::Rivals::KatanaAlert && AnyRivalsKatanaUser())
		RenderKatanaAlert(ImGui::GetBackgroundDrawList());
	CombatFeedback::Render(ImGui::GetBackgroundDrawList());
	RenderESP(ImGui::GetBackgroundDrawList());

	if (Options::ESP::Arrows) RenderArrows(ImGui::GetBackgroundDrawList());
	if (Options::ESP::Radar) RenderRadar(ImGui::GetBackgroundDrawList());
	
	if (Options::Desync::Enabled && Options::Desync::ShowVisual)
		DesyncVisual::RenderDesyncVisual(ImGui::GetBackgroundDrawList());
	
	RenderRageGhost(ImGui::GetBackgroundDrawList());
	
	if (Options::Orbit::Enabled && Options::Orbit::ShowRadius)
		RenderOrbitRadiusVisual(ImGui::GetBackgroundDrawList());

if (menu_open && MenuWeather::Enabled)
{
const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
MenuWeather::Render(ImGui::GetBackgroundDrawList(), ImVec2(0.0f, 0.0f), displaySize);
}

RenderKeybindList(ImGui::GetBackgroundDrawList());
}

if (Options::Misc::ExplorerEnabled)
    gui::render_explorer_window(&Options::Misc::ExplorerEnabled);

if (Options::Misc::PlayerListEnabled)
    RenderPlayerListWindow(&Options::Misc::PlayerListEnabled);

// Render draw.* items produced by running scripts (before the final commit).
Executor::RenderOverlay(ImGui::GetBackgroundDrawList());

ImGui::Render();
const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

HRESULT hr = g_pSwapChain->Present(1, 0);
g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_OverlayWheelAccum = 0;

    UninstallWheelForwarder();

    gui::explorer_shutdown();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();

    Globals::overlayDone = true;
    SeraphLog("[S] ShowImgui: overlay teardown complete, overlayDone=true");
}

bool CreateDeviceD3D(HWND hWnd)
{
// Opaque bitblt-model swapchain. This system cannot create
// DXGI_ALPHA_MODE_PREMULTIPLIED flip-model swapchains (every SwapEffect /
// BufferCount / layered-style permutation returns DXGI_ERROR_INVALID_CALL),
// so transparency is done with an opaque bitblt surface + LWA_COLORKEY
// (pure black -> see-through), matching the loader's proven opaque recipe.
DXGI_SWAP_CHAIN_DESC1 sd = {};
sd.Width = 0;
sd.Height = 0;
sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
sd.Stereo = FALSE;
sd.SampleDesc.Count = 1;
sd.SampleDesc.Quality = 0;
sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
sd.BufferCount = 1;
sd.Scaling = DXGI_SCALING_STRETCH;
sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
sd.Flags = 0;

D3D_FEATURE_LEVEL featureLevel;
const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
HRESULT res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 2, D3D11_SDK_VERSION,
    &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
if (res != S_OK) return false;

IDXGIDevice* dxgiDevice = nullptr;
IDXGIAdapter* adapter = nullptr;
IDXGIFactory2* factory = nullptr;
res = g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
if (res == S_OK) res = dxgiDevice->GetAdapter(&adapter);
if (res == S_OK) res = adapter->GetParent(IID_PPV_ARGS(&factory));
if (res != S_OK)
{
    if (dxgiDevice) dxgiDevice->Release();
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    return false;
}

IDXGISwapChain1* baseSwapChain = nullptr;
res = factory->CreateSwapChainForHwnd(g_pd3dDevice, hWnd, &sd, nullptr, nullptr, &baseSwapChain);
if (dxgiDevice) dxgiDevice->Release();
if (adapter) adapter->Release();
factory->Release();
if (res != S_OK || !baseSwapChain) return false;

res = baseSwapChain->QueryInterface(IID_PPV_ARGS(&g_pSwapChain));
baseSwapChain->Release();
if (res != S_OK) return false;

CreateRenderTarget();
return true;
}

void CleanupDeviceD3D()
{
Executor::Shutdown();
CleanupRenderTarget();
if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
ID3D11Texture2D* pBackBuffer;
g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
pBackBuffer->Release();
}

void CleanupRenderTarget()
{
if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
return true;

switch (msg)
{
case WM_SIZE:
if (wParam == SIZE_MINIMIZED) return 0;
g_ResizeWidth = (UINT)LOWORD(lParam);
g_ResizeHeight = (UINT)HIWORD(lParam);
return 0;
case WM_SYSCOMMAND:
if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
break;
case WM_DESTROY:
::PostQuitMessage(0);
return 0;
}
return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
