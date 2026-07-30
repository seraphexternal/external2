// Windows headers define the `min` / `max` preprocessor macros
// which clash with `std::max` / `std::min` used by the
// MenuWeather particle engine. Defining NOMINMAX FIRST ensures
// every windows-family header (transitively pulled in by the
// project headers on lines below) respects the opt-out.
#define NOMINMAX
#include "renderer.h"
#include "ragesubtabs.h"
#include "ui.h"
#include "../features/chams.h"
#include "../rbx/configs/configs.h"
#include "../features/desync.h"
#include "../features/ragebot.h"
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")

#include <random>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning (disable: 26812)    // [Static Analyzer] The enum type 'xxx' is unscoped. Prefer 'enum class' over 'enum' (Enum.3). ImGui uses unscoped enum flag bitmasks heavily.
#endif

ID3D11Device* g_pd3dDevice = nullptr;
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

void ApplyOverlayWindowStyle(HWND hwnd, bool clickThrough)
{
LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

if (Options::Misc::HideFromTabs)
{
exStyle |= WS_EX_TOOLWINDOW;
exStyle &= ~WS_EX_APPWINDOW;
}
else
{
exStyle &= ~WS_EX_TOOLWINDOW;
}

if (clickThrough)
exStyle |= WS_EX_TRANSPARENT;
else
exStyle &= ~WS_EX_TRANSPARENT;

SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

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
ImGui::SetCursorPosX(UI::ContentX);
if (UI::card::begin("##configs_manage", ImVec2(UI::CardW, 470 * sc), "MANAGE CONFIGS"))
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
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(UI::ContentX + UI::CardW + 6.0f * sc);
if (UI::card::begin("##configs_actions", ImVec2(UI::CardW, 470 * sc), "ACTIONS"))
{
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
ImGui::InputText("##seraph_configname", configNameBuffer, IM_ARRAYSIZE(configNameBuffer));
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
if (OpenWindowsFileDialog(true, pickedPath, "*.json\0*.json\0All Files\0*.*\0", "Import Seraph config"))
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
if (OpenWindowsFileDialog(false, pickedPath, "*.json\0*.json\0All Files\0*.*\0", "Export Seraph config", suggestedName))
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
UI::card::end();
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

void ShowImgui()
{
    OutputDebugStringA("[Seraph] ShowImgui: START\n");
    InitializeConfigPaths();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    size_t width = (size_t)GetSystemMetrics(SM_CXSCREEN);
    size_t height = (size_t)GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);

    // Benign, non-descript window title so the overlay doesn't advertise itself.
    HWND hwnd = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        0, 0, (int)width + 1, (int)height + 1,
        nullptr, nullptr, wc.hInstance, nullptr);

    OutputDebugStringA("[Seraph] ShowImgui: Window created\n");

    // Publish the overlay HWND so the file-dialog helpers (configs.h)
    // can present Import/Export as modal-to-owner dialogs. This is what
    // makes the OS dialog actually clickable when the overlay is open:
    // ownership forces Windows to route mouse + focus through the
    // dialog above our WS_EX_LAYERED + WS_EX_TOPMOST overlay.
    g_OverlayHWND = hwnd;

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS Margin = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &Margin);

    HideFromTaskbar(hwnd);

    // Apply streamproof if enabled (WDA_EXCLUDEFROMCAPTURE = 0x00000011)
    if (Options::Misc::StreamProof)
    {
        SetWindowDisplayAffinity(hwnd, 0x00000011);
    }

    if (!CreateDeviceD3D(hwnd))
    {
        OutputDebugStringA("[Seraph] ShowImgui: CreateDeviceD3D FAILED\n");
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CoUninitialize();
        return;
    }

    OutputDebugStringA("[Seraph] ShowImgui: D3D device created\n");

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    HideFromTaskbar(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    OutputDebugStringA("[Seraph] ShowImgui: ImGui context created\n");

    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

ImFontConfig config;
config.MergeMode = false;
config.PixelSnapH = true;

ImFont* baseFont = io.Fonts->AddFontDefault(&config);

// Pre-load a curated set of Windows system fonts so users can switch
// the menu font at runtime via Options::Misc::MenuFont without us
// having to rebuild the font atlas on the fly (which would stall the
// overlay for ~50ms each time). Each entry must have its file on disk
// in C:\Windows\Fonts\ (resolvable via LoadSystemFont below).
struct MenuFontEntry { ImFont* font; const char* path; const char* name; };
static MenuFontEntry menuFonts[7];
int menuFontCount = 0;

auto LoadSystemFont = [&](const char* path, float size) -> ImFont*
{
const std::wstring widePath = UTF8ToWide(path);
if (widePath.empty()) return nullptr;
if (GetFileAttributesW(widePath.c_str()) == INVALID_FILE_ATTRIBUTES)
return nullptr;
return io.Fonts->AddFontFromFileTTF(path, size, &config, io.Fonts->GetGlyphRangesJapanese());
};

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
// range value falls back gracefully to the first entry (Verdana).
if (Options::Misc::MenuFont >= menuFontCount || Options::Misc::MenuFont < 0)
Options::Misc::MenuFont = 0;
ImFont* font = (menuFontCount > 0 && menuFonts[Options::Misc::MenuFont].font)
? menuFonts[Options::Misc::MenuFont].font
: baseFont;
io.FontDefault = font;

config.MergeMode = true;
ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ImGui_ImplDX11_CreateDeviceObjects();

    OutputDebugStringA("[Seraph] ShowImgui: ImGui backends initialized\n");

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

    SetTransparency(hwnd, true);

    OutputDebugStringA("[Seraph] ShowImgui: Entering render loop\n");
    int frameCount = 0;
    while (!done && Globals::running)
    {
        if (frameCount == 0) {
            OutputDebugStringA("[Seraph] ShowImgui: First frame\n");
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
ImGui::NewFrame();

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
        SetTransparency(hwnd, !menu_open);
    }

    // Fade animation
    static float menuAlpha = 0.0f;
    static float backgroundAlpha = 0.0f;
    float fadeSpeed = 0.08f;

    if (menu_open)
    {
        if (menuAlpha < 1.0f) menuAlpha += fadeSpeed;
        if (menuAlpha > 1.0f) menuAlpha = 1.0f;

        if (backgroundAlpha < 0.7f) backgroundAlpha += fadeSpeed;
        if (backgroundAlpha > 0.7f) backgroundAlpha = 0.7f;
    }
    else
    {
        if (menuAlpha > 0.0f) menuAlpha -= fadeSpeed;
        if (menuAlpha < 0.0f) menuAlpha = 0.0f;

        if (backgroundAlpha > 0.0f) backgroundAlpha -= fadeSpeed;
        if (backgroundAlpha < 0.0f) backgroundAlpha = 0.0f;
    }

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
// Always use preset accent colors (even for Custom) to prevent old saved configs from overriding
main_color = ImVec4(themeAccent[0], themeAccent[1], themeAccent[2], 1.0f);
main_color2 = ImVec4(themeAccent2[0], themeAccent2[1], themeAccent2[2], 1.0f);
// Gradient flag follows the preset (or the custom toggle).
const bool useGradient = themeGradient;

if (menu_open || menuAlpha > 0.0f)
{
// Draw dark background overlay
if (backgroundAlpha > 0.0f)
{
ImGui::GetBackgroundDrawList()->AddRectFilled(
ImVec2(0, 0),
ImVec2(io.DisplaySize.x, io.DisplaySize.y),
IM_COL32(0, 0, 0, static_cast<int>(backgroundAlpha * 180))
);
}
// Visuals tab uses a wider layout so the ESP preview has a dedicated side panel.
        // MenuScale acts as a uniform zoom factor for the whole UI.
        const float sc = std::clamp(Options::Misc::MenuScale, 0.6f, 2.5f);
        // keep every subtab pill a uniform width, aligned inside the left rail
        UI::sc = sc;
        UI::SidebarX = 0.0f;
        UI::SidebarW = 178.0f * sc;
        UI::ContentX = UI::SidebarW + 1.0f;
        UI::ContentW = 960.0f - UI::ContentX;
        UI::CardW = (UI::ContentW - 32.0f * sc) * 0.5f;
        const float menuWidth = 960.0f;
        const float menuHeight = 620.0f;
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
UI::ApplyStyle(main_color,
ImVec4(themeBg[0], themeBg[1], themeBg[2], 1.0f),
ImVec4(themePanel[0], themePanel[1], themePanel[2], 1.0f));

        // ── Autopsy-style chrome ──────────────────────────────────────────
        // Multi-layer drop shadow, deep dark blue fill, accent glow borders,
        // sidebar panel with cyan divider.
        const float time = (float)ImGui::GetTime();
        const float glow = (sinf(time * 2.3f) + 1.0f) * 0.5f;

        // Drop shadow (5 layers, rounded) — drawn on background drawlist so they extend beyond menu bounds
        auto bgDraw = ImGui::GetBackgroundDrawList();
        for (int i = 4; i >= 0; i--)
        {
            float spread = 18.0f + i * 11.0f;
            int shAlpha = (int)((12.0f - i * 1.85f) * menuAlpha);
            bgDraw->AddRectFilled(
                ImVec2(p.x - spread, p.y - spread * 0.55f),
                ImVec2(p.x + s.x + spread, p.y + s.y + spread),
                IM_COL32(7, 13, 21, shAlpha), (14.0f + spread) * sc);
        }

        // Accent glow behind window — single subtle layer
        {
            float spread = 30.0f * sc;
            float alpha = 0.04f * menuAlpha;
            bgDraw->AddRectFilled(
                ImVec2(p.x - spread, p.y - spread * 0.5f),
                ImVec2(p.x + s.x + spread, p.y + s.y + spread * 0.5f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, alpha)),
                (14.0f + spread * 0.5f) * sc);
        }

        // Main window fill — fully opaque dark
        draw->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + s.y),
            IM_COL32(3, 6, 10, 255), 14.0f * sc);
        // Inner accent tint (subtle theme accent wash)
        draw->AddRectFilled(
            ImVec2(p.x + 1.0f * sc, p.y + 1.0f * sc),
            ImVec2(p.x + s.x - 1.0f * sc, p.y + s.y - 1.0f * sc),
            ImGui::ColorConvertFloat4ToU32(ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.007f)),
            (14.0f - 1.0f) * sc);

        // Outer border (dynamic palette color)
        draw->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + s.y),
            ImGui::ColorConvertFloat4ToU32(UI::P.line), 14.0f * sc, 0, 1.0f * sc);
        // Inner glow border (accent pulse)
        float gA = 0.11f + 0.09f * glow;
        ImU32 glowCol = ImGui::ColorConvertFloat4ToU32(
            ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, gA));
        draw->AddRect(
            ImVec2(p.x + 1.0f * sc, p.y + 1.0f * sc),
            ImVec2(p.x + s.x - 1.0f * sc, p.y + s.y - 1.0f * sc),
            glowCol, (14.0f - 1.0f) * sc, 0, 1.0f * sc);

        // Sidebar panel (left column) — autopsy-style 178px, slightly transparent
        const float sbW = UI::SidebarW;
        const ImVec2 sideMin = ImVec2(p.x, p.y);
        const ImVec2 sideMax = ImVec2(p.x + sbW, p.y + s.y);
        // Dark fill with left-rounded corners
        draw->AddRectFilled(sideMin, sideMax,
            IM_COL32(4, 10, 17, 220), 14.0f * sc, ImDrawFlags_RoundCornersLeft);
        // Top white highlight + bottom accent tint
        draw->AddRectFilled(sideMin, sideMax,
            IM_COL32(255, 255, 255, 10), 14.0f * sc, ImDrawFlags_RoundCornersLeft);
        // Inner top glow (first 92px)
        draw->AddRectFilled(
            ImVec2(sideMin.x + 1.0f * sc, sideMin.y + 1.0f * sc),
            ImVec2(sideMax.x - 1.0f * sc, sideMin.y + 92.0f * sc),
            IM_COL32(255, 255, 255, 8), (14.0f - 1.0f) * sc, ImDrawFlags_RoundCornersTopLeft);
        // White hairline border
        draw->AddRect(sideMin, sideMax,
            IM_COL32(255, 255, 255, 18), 14.0f * sc, ImDrawFlags_RoundCornersLeft, 1.0f * sc);
        // Divider line (right edge of sidebar)
        draw->AddLine(
            ImVec2(p.x + sbW, p.y + 10.0f * sc),
            ImVec2(p.x + sbW, p.y + s.y - 10.0f * sc),
            ImGui::ColorConvertFloat4ToU32(ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.16f)), 1.0f * sc);

        // Logo area: accent bar + "SERAPH" + "BETA" badge
        {
            const float logoY = p.y + 25.0f * sc;
            const float logoX = p.x + 22.0f * sc;
            // Accent bar 3×22px
            draw->AddRectFilled(
                ImVec2(logoX, logoY + 3.0f * sc),
                ImVec2(logoX + 3.0f * sc, logoY + 25.0f * sc),
                ImGui::ColorConvertFloat4ToU32(UI::P.accent), 2.0f * sc);
            // Title text
            ImFont* logoFont = (MenuFonts::Count > 0 && Options::Misc::MenuFont >= 0
                && Options::Misc::MenuFont < MenuFonts::Count && MenuFonts::Fonts[Options::Misc::MenuFont])
                ? MenuFonts::Fonts[Options::Misc::MenuFont] : io.FontDefault;
            draw->AddText(logoFont, 21.0f * sc,
                ImVec2(logoX + 8.0f * sc, logoY),
                IM_COL32(255, 255, 255, 255), "SERAPH");
            ImVec2 textSize = logoFont->CalcTextSizeA(21.0f * sc, FLT_MAX, 0, "SERAPH");
            draw->AddRectFilled(
                ImVec2(logoX + 8.0f * sc + textSize.x + 7.0f * sc, logoY + 2.0f * sc),
                ImVec2(logoX + 8.0f * sc + textSize.x + 7.0f * sc + 36.0f * sc, logoY + 15.0f * sc),
                ImGui::ColorConvertFloat4ToU32(UI::P.accent), 4.0f * sc);
            draw->AddText(io.FontDefault, 10.0f * sc,
                ImVec2(logoX + 8.0f * sc + textSize.x + 11.0f * sc, logoY + 1.5f * sc),
                IM_COL32(255, 255, 255, 255), "BETA");
            // Separator line below logo
            draw->AddLine(
                ImVec2(logoX - 2.0f * sc, logoY + 59.0f * sc),
                ImVec2(p.x + sbW - 20.0f * sc, logoY + 59.0f * sc),
                ImGui::ColorConvertFloat4ToU32(UI::P.divider), 1.0f * sc);
        }

        // Header panel (right of sidebar, top strip)
        const float contentX = p.x + sbW;
        const float headerH = 62.0f * sc;
        draw->AddRectFilled(
            ImVec2(contentX, p.y),
            ImVec2(p.x + s.x, p.y + headerH),
            ImGui::ColorConvertFloat4ToU32(UI::P.surface), 14.0f * sc, ImDrawFlags_RoundCornersTopRight);
        // Subtle accent tint on header
        draw->AddRectFilled(
            ImVec2(contentX, p.y),
            ImVec2(p.x + s.x, p.y + headerH),
            ImGui::ColorConvertFloat4ToU32(ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.005f)),
            14.0f * sc, ImDrawFlags_RoundCornersTopRight);
        // Divider under header
        draw->AddLine(
            ImVec2(contentX, p.y + headerH),
            ImVec2(p.x + s.x, p.y + headerH),
            ImGui::ColorConvertFloat4ToU32(UI::P.divider), 1.0f * sc);

        // Content area background (below header, right of sidebar)
        draw->AddRectFilled(
            ImVec2(contentX, p.y + headerH + 1.0f * sc),
            ImVec2(p.x + s.x, p.y + s.y),
            IM_COL32(5, 9, 15, 255), 14.0f * sc, ImDrawFlags_RoundCornersBottomRight);

// Animated top accent line: gradient (main_color -> main_color2) when
// enabled, otherwise a single fading accent.
{
const int fade_line_count = 60;
const float center_point = s.x / 2.0f;
for (int i = 0; i < fade_line_count; i++)
{
float alpha = 1.0f - (i * (1.0f / fade_line_count));
ImVec2 start_right = ImVec2(p.x + s.x - i * (center_point / fade_line_count), p.y + 25 * sc);
ImVec2 end_right = ImVec2(p.x + s.x - (i + 1) * (center_point / fade_line_count), p.y + 25 * sc);
ImColor fade_color;
if (useGradient)
{
float mix = static_cast<float>(i) / static_cast<float>(fade_line_count);
fade_color = ImColor(
main_color.x + (main_color2.x - main_color.x) * mix,
main_color.y + (main_color2.y - main_color.y) * mix,
main_color.z + (main_color2.z - main_color.z) * mix,
alpha);
}
else
{
fade_color = ImColor(main_color.x, main_color.y, main_color.z, alpha * 0.9f);
}
draw->AddLine(start_right, end_right, fade_color);
}
}

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


// â”€â”€ Footer status bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
{
const float fH = 32 * sc;
const float fy = p.y + s.y - fH;
// dedicated status bar background + top hairline
draw->AddRectFilled(ImVec2(p.x, fy), ImVec2(p.x + s.x, p.y + s.y),
IM_COL32(
static_cast<int>(themePanel[0] * 255 * 0.45f),
static_cast<int>(themePanel[1] * 255 * 0.45f),
static_cast<int>(themePanel[2] * 255 * 0.45f), 255), 0.0f, 0);
draw->AddLine(ImVec2(p.x, fy), ImVec2(p.x + s.x, fy),
ImGui::ColorConvertFloat4ToU32(UI::P.line), 1.0f * sc);

        const float ty = fy + (fH - 11 * sc) / 2.0f;
        const ImU32 dim = ImGui::ColorConvertFloat4ToU32(UI::P.textDim);
        const ImU32 txt = ImGui::ColorConvertFloat4ToU32(UI::P.text);
        const ImU32 sep = ImGui::ColorConvertFloat4ToU32(UI::P.line);

        // ── Footer: Version · FPS ──
        char verBuf[64];    sprintf_s(verBuf, "Version: v0.1 Beta");
        char fpsBuf[64];    sprintf_s(fpsBuf, "FPS: %d", (int)ImGui::GetIO().Framerate);

        float x = p.x + 16 * sc;
        const float segGap = 6.0f * sc;
        ImFont* safeFont = font ? font : ImGui::GetFont();
        auto drawSeg = [&](const char* label, const ImU32 col) {
            ImVec2 sz = safeFont->CalcTextSizeA(11.0f * sc, FLT_MAX, 0.f, label);
            draw->AddText(ImVec2(x, ty), col, label);
            x += sz.x + segGap;
            if (label != fpsBuf) {
                draw->AddText(ImVec2(x, ty), sep, "|");
                x += safeFont->CalcTextSizeA(11.0f * sc, FLT_MAX, 0.f, "|").x + segGap;
            }
        };
        drawSeg(verBuf, txt);
        drawSeg(fpsBuf, txt);
        }

        // â”€â”€ Top tab bar: full width, 7 equal segments, even gaps â”€â”€â”€â”€â”€
        {
            // Sidebar vertical nav tabs (autopsy-style)
        {
            const float tabStartY = p.y + 100.0f * sc;
            const float tabX = p.x + UI::SidebarX;
            const float tabW = UI::SidebarW;

                struct TabDef { int icon; const char* label; };
                static const TabDef tabs[] = {
                    {1,"Aim"}, {2,"Visuals"}, {3,"Rage"}, {4,"Misc"},
                    {5,"Movement"}, {6,"Configs"}, {7,"Game"}
                };

                for (int i = 0; i < 7; i++)
                {
                    if (UI::SidebarTab(tabs[i].icon, tabs[i].label, tab == i,
                        tabX, tabStartY + i * 42.0f * sc, tabW))
                        tab = i;
                }

                // Footer separator + user info
                draw->AddLine(
                    ImVec2(tabX + 20.0f * sc, p.y + s.y - 66.0f * sc),
                    ImVec2(tabX + tabW - 20.0f * sc, p.y + s.y - 66.0f * sc),
                    ImGui::ColorConvertFloat4ToU32(UI::P.divider), 1.0f * sc);

                // Status dot (green if connected, red if not)
                bool connected = (Globals::Roblox::LocalPlayer.address != 0);
                ImU32 dotCol = connected
                    ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.28f, 0.82f, 0.50f, 1.0f))
                    : ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.31f, 0.41f, 1.0f));
                draw->AddCircleFilled(
                    ImVec2(tabX + 26.0f * sc, p.y + s.y - 38.0f * sc),
                    3.5f * sc, dotCol);

                // Username text
                std::string uname;
                if (Globals::Roblox::LocalPlayer.address)
                    uname = Globals::Roblox::LocalPlayer.Name();
                draw->AddText(
                    ImVec2(tabX + 36.0f * sc, p.y + s.y - 44.0f * sc),
                    ImGui::ColorConvertFloat4ToU32(UI::P.text),
                    uname.empty() ? "Not connected" : uname.c_str());
        }

// Reset cursor to content area top (after header)
ImGui::SetCursorPos(ImVec2(UI::ContentX, 58.0f * sc));
        }

if (tab != lastTab)
{
tab2 = 0;
lastTab = tab;
}

        // ── Content area layout constants (autopsy-style) ────────
        const float ctX = UI::ContentX + 16.0f * sc;
        const float ctPad = 16.0f * sc;
        const float ctW = s.x - UI::ContentX - 32.0f * sc;
        const float halfW = (ctW - 12.0f * sc) * 0.5f;
        const float fullW = ctW;
        const float hdrY = 58.0f * sc;

if (tab == 0)
{
        // ── Content header + horizontal subtab bar ────────────────
        UI::ContentHeader("AIM");
        {
            static float sa[4] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Aimbot", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Triggerbot", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Hitbox", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

        if (tab2 == 0)
        {
            // ── Aimbot: General + Smoothing (left column) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::card::begin("##aim_general", ImVec2(halfW, 485 * sc), "GENERAL"))
            {
                UI::labelsection("AIMBOT");
                UI::Checkbox("Enabled", &Options::Aimbot::Aimbot);
                UI::Checkbox("Team Check", &Options::Aimbot::TeamCheck);
                UI::Checkbox("Knocked Check", &Options::Aimbot::DownedCheck);
                UI::Checkbox("Sticky Aim", &Options::Aimbot::StickyAim);
                UI::Checkbox("Only Visible", &Options::Aimbot::OnlyVisible);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only lock onto players that are not behind walls.");
                UI::Checkbox("Prediction", &Options::Aimbot::Prediction);

                UI::labelsection("RANGE & FOV");
                UI::SliderFloat("Range", &Options::Aimbot::Range, 1.f, 1000.f, "%.0f");
                UI::SliderFloat("FOV", &Options::Aimbot::FOV, 10.f, 360.f, "%.0f");

                UI::labelsection("SMOOTHING & FEEL");
                static const char* aimingMethods[]{ "Camera", "Mouse", "Silent" };
                UI::Combo("Method", &Options::Aimbot::AimingType, aimingMethods, IM_ARRAYSIZE(aimingMethods));

                static const char* smoothnessCurves[]{ "Linear", "Ease In", "Ease Out", "Ease In-Out", "Custom" };
                UI::Combo("Curve", &Options::Aimbot::SmoothnessCurve, smoothnessCurves, IM_ARRAYSIZE(smoothnessCurves));

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

                    drawList->AddRectFilled(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), IM_COL32(8, 8, 8, 255), 2.0f);
                    drawList->AddRect(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), IM_COL32(27, 27, 27, 255), 2.0f);

                    for (int i = 1; i < 4; i++)
                    {
                        float y = graphPos.y + (graphSize.y / 4.0f) * i;
                        drawList->AddLine(ImVec2(graphPos.x, y), ImVec2(graphPos.x + graphSize.x, y), IM_COL32(20, 20, 20, 255), 1.0f);
                    }
                    for (int i = 1; i < 4; i++)
                    {
                        float x = graphPos.x + (graphSize.x / 4.0f) * i;
                        drawList->AddLine(ImVec2(x, graphPos.y), ImVec2(x, graphPos.y + graphSize.y), IM_COL32(20, 20, 20, 255), 1.0f);
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
            UI::card::end();

            // ── Aimbot: Targeting + Silent Aim (right column) ──
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
            if (UI::card::begin("##aim_targeting", ImVec2(halfW, 470 * sc), "TARGETING"))
            {
                UI::labelsection("HITBOX");
                static const char* hitboxModes[]{ "Fixed Bone", "Closest Part" };
                UI::Combo("Hitbox Mode", &Options::Aimbot::HitboxMode, hitboxModes, IM_ARRAYSIZE(hitboxModes));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fixed Bone uses the Hit Part / Air Hit Part selectors.\nClosest Part aims at the body part nearest your cursor.");
                Options::Aimbot::ClosestPart = (Options::Aimbot::HitboxMode == 1);

                static const char* hitParts[]{ "Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Lower Torso", "Upper Torso" };
                if (!Options::Aimbot::ClosestPart)
                {
                    UI::Combo("Hit Part", &Options::Aimbot::TargetBone, hitParts, IM_ARRAYSIZE(hitParts));
                    UI::Combo("Air Hit Part", &Options::Aimbot::AirTargetBone, hitParts, IM_ARRAYSIZE(hitParts));
                }

                static const char* priorities[]{ "Closest Part", "Crosshair", "Lowest Health", "Farthest", "Highest Health" };
                UI::Combo("Target Priority", &Options::Aimbot::TargetPriority, priorities, IM_ARRAYSIZE(priorities));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which enemy wins when several are inside FOV.");

                UI::labelsection("SWITCHING");
                UI::SliderFloat("Switch Delay (ms)", &Options::Aimbot::TargetSwitchDelay, 0.f, 1000.f, "%.0f");

                UI::labelsection("SILENT AIM");
                UI::Checkbox("Silent Aim", &Options::Aimbot::SilentAim);

                if (Options::Aimbot::SilentAim || Options::Aimbot::AimingType == 2)
                {
                    static const char* silentModes[]{ "Camera Only", "Camera + Mouse Spoof" };
                    UI::Combo("Silent Mode", &Options::Aimbot::SilentAimMode, silentModes, IM_ARRAYSIZE(silentModes));
                    UI::Checkbox("Real Cursor Snap (hits on Overkill)", &Options::Aimbot::SilentAimRealCursor);
                    UI::Tooltip("Snaps your real cursor onto the target while firing so the shot lands. Visible flick on Overkill.");
                    UI::Checkbox("Teleport (no crosshair move)", &Options::Aimbot::SilentAimTeleport);
                    UI::Tooltip("Teleports your character next to the target so the shot registers without moving the crosshair.");
                }

                UI::labelsection("SILENT LOCK");
                UI::Checkbox("Silent Lock", &Options::Aimbot::SilentLock);
                UI::Tooltip("Silently keeps aim on the closest target. Camera Rotation writes the camera matrix; Viewport Offset shifts the hit point.");
                UI::Bind("##silentlock_key", &Options::Aimbot::SilentLockKey, &Options::Aimbot::SilentLockMode);

                if (Options::Aimbot::SilentLock)
                {
                    static const char* lockModes[]{ "Camera Rotation", "Viewport Offset" };
                    UI::Combo("Lock Mode", &Options::Aimbot::SilentLockMode, lockModes, IM_ARRAYSIZE(lockModes));
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
            UI::card::end();

            // ── Aimbot: FOV Visuals (third panel, full width below) ──
            ImGui::SetCursorPosY(panelY + 470 * sc + 12.0f * sc);
            ImGui::SetCursorPosX(ctX);
            if (UI::card::begin("##aim_fov_visuals", ImVec2(ctW, 200 * sc), "FOV VISUALS"))
            {
                UI::labelsection("DISPLAY");
                UI::Checkbox("Show FOV", &Options::Aimbot::ShowFOV);
                UI::Checkbox("Show FOV Fill", &Options::Aimbot::ShowFOVFill);
                UI::Checkbox("Show FOV Text", &Options::Aimbot::ShowFOVText);

                static const char* fovPositions[]{ "Screen Center", "Follow Target" };
                UI::Combo("FOV Position", &Options::Aimbot::FOVPositionMode, fovPositions, IM_ARRAYSIZE(fovPositions));

                static const char* fovShapes[]{ "Circle", "Square", "Triangle", "Hexagon" };
                UI::Combo("FOV Shape", &Options::Aimbot::FOVShape, fovShapes, IM_ARRAYSIZE(fovShapes));

                static const char* fovColorModes[]{ "Solid", "Gradient", "Shift", "Pulse" };
                UI::Combo("FOV Color Mode", &Options::Aimbot::FOVColorMode, fovColorModes, IM_ARRAYSIZE(fovColorModes));

                UI::Checkbox("FOV Glow", &Options::Aimbot::FOVGlow);
                UI::Checkbox("FOV Breathing", &Options::Aimbot::FOVBreathing);
                UI::Checkbox("FOV Spin", &Options::Aimbot::FOVSpin);

                UI::labelsection("STYLING");
                UI::SliderFloat("FOV Thickness", &Options::Aimbot::FOVThickness, 1.0f, 10.0f, "%.1f");
                if (Options::Aimbot::FOVColorMode == 1 || Options::Aimbot::FOVColorMode == 2)
                    UI::SliderFloat("Gradient Speed", &Options::Aimbot::FOVGradientSpeed, 0.1f, 5.0f, "%.2f");
                if (Options::Aimbot::FOVSpin)
                    UI::SliderFloat("Spin Speed", &Options::Aimbot::FOVSpinSpeed, 0.1f, 5.0f, "%.2f");

                UI::ColorEdit3("FOV Color", Options::Aimbot::FOVColor, ImGuiColorEditFlags_NoInputs);
                UI::ColorEdit3("FOV Fill", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs);
            }
            UI::card::end();
        }
        else if (tab2 == 1)
        {
            // ── Triggerbot: Main (left) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::card::begin("##trigger_main", ImVec2(halfW, 470 * sc), "TRIGGERBOT"))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::Triggerbot::Enabled);
                UI::Checkbox("Team Check", &Options::Triggerbot::TeamCheck);
                UI::Checkbox("Knocked Check", &Options::Triggerbot::DownedCheck);
                UI::Checkbox("Prediction", &Options::Triggerbot::Prediction);
                UI::Checkbox("Advanced FOV", &Options::Triggerbot::AdvancedFOV);
                if (Options::Triggerbot::AdvancedFOV)
                    UI::Checkbox("Show FOV", &Options::Triggerbot::ShowAdvancedFOV);

                UI::labelsection("KEYBIND");
                UI::Bind("##triggerbot_key", &Options::Triggerbot::TriggerbotKey);
            }
            UI::card::end();

            // ── Triggerbot: Settings (right) ──
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
            if (UI::card::begin("##trigger_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
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
            UI::card::end();
        }
        else if (tab2 == 2)
        {
            // ── Hitbox Expander ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::card::begin("##hitbox_main", ImVec2(halfW, 470 * sc), "HITBOX EXPANDER"))
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
            UI::card::end();

            // Preview panel
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
            // (preview rendered in ESP overlay)
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
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Orbit", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Anti-Aim", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Desync", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("VoidHide", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 4.0f * sc);
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
            static float sa[5] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("ESP", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Combat", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("World", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Colours", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Crosshair", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##esp_features", ImVec2(halfW, 470 * sc), "ESP FEATURES"))
{
UI::labelsection("FILTER");
UI::Checkbox("Master Enable", &Options::ESP::Enabled);
UI::Checkbox("Team Check", &Options::ESP::TeamCheck);
UI::Checkbox("Visibility Colors", &Options::ESP::VisibilityCheck);
UI::Checkbox("Visibility Bones", &Options::ESP::VisibilityChams);

UI::labelsection("PLAYER INFO");
CheckboxWithColorPicker("Names", &Options::ESP::Name, Options::ESP::Color);
CheckboxWithColorPicker("Distance", &Options::ESP::Distance, Options::ESP::DistanceColor);
UI::Checkbox("Health Bar", &Options::ESP::Health);
UI::Checkbox("Health Text", &Options::ESP::HealthText);
UI::Checkbox("HP Above Head", &Options::ESP::EnemyHealthIndicator);

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
UI::Checkbox("Local Only", &Options::ESP::LocalOnly);
UI::Checkbox("Avatar Icon", &Options::ESP::AvatarIcon);

static const char* nameModes[]{ "Username", "Health%" };
UI::Combo("Name Mode", &Options::ESP::NameMode, nameModes, IM_ARRAYSIZE(nameModes));

UI::Checkbox("Custom Image", &Options::ESP::CustomImage);
if (Options::ESP::CustomImage)
{
UI::SliderFloat("Image Scale", &Options::ESP::CustomImageScale, 0.2f, 4.0f, "%.2f");
char imgBuf[256]; strncpy_s(imgBuf, Options::ESP::CustomImagePath, sizeof(imgBuf) - 1);
if (ImGui::InputText("Image Path", imgBuf, sizeof(imgBuf)))
strncpy_s(Options::ESP::CustomImagePath, imgBuf, sizeof(Options::ESP::CustomImagePath) - 1);
}

UI::labelsection("OVERLAYS");
UI::Checkbox("Corner ESP", &Options::ESP::CornerESP);
CheckboxWithColorPicker("Tracers", &Options::ESP::Tracers, Options::ESP::TracerColor);
CheckboxWithColorPicker("Skeleton", &Options::ESP::Skeleton, Options::ESP::SkeletonColor);
CheckboxWithColorPicker("Head Circle", &Options::ESP::HeadCircle, Options::ESP::HeadCircleColor);
CheckboxWithColorPicker("Head Dot", &Options::ESP::HeadDot, Options::ESP::HeadDotColor);

UI::labelsection("EXTRAS");
CheckboxWithColorPicker("Arrows", &Options::ESP::Arrows, Options::ESP::ArrowColor);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows directional arrows for off-screen players.");
CheckboxWithColorPicker("Radar", &Options::ESP::Radar, Options::ESP::RadarEnemyColor);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows a circular radar overlay with nearby players.");
if (Options::ESP::Radar)
{
static const char* radarThemes[]{ "Classic", "Minimal", "Neon", "Compass" };
UI::Combo("Radar Theme", &Options::ESP::RadarTheme, radarThemes, IM_ARRAYSIZE(radarThemes));
}
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##esp_settings", ImVec2(halfW, 470 * sc), "ESP SETTINGS"))
{
static const char* boxTypes[]{ "None", "Normal Box", "3D Box" };
UI::labelsection("BOX");
UI::Combo("Type", &Options::ESP::BoxType, boxTypes, IM_ARRAYSIZE(boxTypes));
UI::SliderFloat("Box Thickness", &Options::ESP::BoxThickness, 1.0f, 10.0f);
UI::SliderFloat("3D Box Thickness", &Options::ESP::ESP3DThickness, 1.0f, 10.0f);

UI::labelsection("LINES");
UI::SliderFloat("Tracer Thickness", &Options::ESP::TracerThickness, 1.0f, 10.0f);
UI::SliderFloat("Skeleton Thickness", &Options::ESP::SkeletonThickness, 1.0f, 10.0f);

UI::labelsection("TEXT");
UI::SliderFloat("Name Size", &Options::ESP::NameSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Name Thickness", &Options::ESP::NameThickness, 0.0f, 5.0f, "%.1f");

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
}
UI::card::end();
}
else if (tab2 == 1) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##combat_feedback", ImVec2(halfW, 470 * sc), "HIT FEEDBACK"))
{
UI::labelsection("HITS");
UI::Checkbox("Hit Sounds", &Options::Combat::HitSounds);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays a sound whenever you hit someone.");
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

auto& soundFiles = Globals::HitSounds::Files;
if (soundFiles.empty())
{
ImGui::TextDisabled("No sounds found");
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Place .wav files in the 'hitsounds' folder next to the exe.");
}
else
{
static auto getter = [](void*, int idx, const char** out_text) -> bool
{
auto& files = Globals::HitSounds::Files;
if (idx < 0 || idx >= (int)files.size()) return false;
*out_text = files[idx].c_str();
return true;
};
UI::labelsection("SOUND");
ImGui::Combo("Sound", &Options::Combat::HitSoundType, getter, nullptr, (int)soundFiles.size());
if (Options::Combat::HitSoundType >= 0 && Options::Combat::HitSoundType < (int)soundFiles.size())
{
if (ImGui::Button("Preview", ImVec2(-1, 20)))
{
std::string path = Globals::HitSounds::FolderPath + "\\" + soundFiles[Options::Combat::HitSoundType];
PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}
}
}
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##combat_settings", ImVec2(halfW, 470 * sc), "HIT SETTINGS"))
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
UI::card::end();
}
else if (tab2 == 2) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##world_main", ImVec2(halfW, 470 * sc), "WORLD"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::World::Enabled);
UI::Checkbox("Fullbright", &Options::World::Fullbright);
UI::Checkbox("No Fog", &Options::World::NoFog);
UI::Checkbox("Skybox Changer", &Options::World::SkyboxChanger);

static const char* skyPresets[]{ "Default", "Night", "Space", "Sunset", "Storm" };
UI::Combo("Sky Preset", &Options::World::SkyboxPreset, skyPresets, IM_ARRAYSIZE(skyPresets));

UI::labelsection("MATERIAL CHANGER (CHAMS)");
UI::Checkbox("Chams Enabled", &Options::Chams::Enabled);
UI::Checkbox("Engine Chams", &Options::Chams::EngineChams);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Force the selected material onto every player part for the glowing 'engine chams' look.");

static const char* matNames[Chams::materialCount];
static bool matNamesInit = false;
if (!matNamesInit)
{
for (int i = 0; i < Chams::materialCount; i++)
matNames[i] = Chams::materialList[i].name;
matNamesInit = true;
}
int matIndex = Options::Chams::Material;
if (matIndex < 0 || matIndex >= Chams::materialCount) matIndex = Chams::materialCount - 1;
if (UI::Combo("Chams Material", &matIndex, matNames, Chams::materialCount))
Options::Chams::Material = matIndex;

UI::ColorEdit4("Chams Color", Options::Chams::VisibleColor, ImGuiColorEditFlags_NoInputs);
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##world_lighting", ImVec2(halfW, 470 * sc), "LIGHTING"))
{
UI::labelsection("TIME & BRIGHTNESS");
UI::SliderFloat("Clock Time", &Options::World::ClockTime, 0.0f, 24.0f, "%.1f");
UI::SliderFloat("Brightness", &Options::World::Brightness, 0.0f, 5.0f, "%.1f");

UI::labelsection("FOG");
UI::SliderFloat("Fog Start", &Options::World::FogStart, 0.0f, 1000.0f, "%.0f");
UI::SliderFloat("Fog End", &Options::World::FogEnd, 50.0f, 100000.0f, "%.0f");
UI::ColorEdit3("Fog Color", Options::World::FogColor, ImGuiColorEditFlags_NoInputs);

UI::labelsection("AMBIENT");
UI::ColorEdit3("Ambient", Options::World::Ambient, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Outdoor Ambient", Options::World::OutdoorAmbient, ImGuiColorEditFlags_NoInputs);
}
UI::card::end();
}
else if (tab2 == 3) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##colours_esp", ImVec2(halfW, 470 * sc), "ESP COLOURS"))
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

UI::labelsection("VISIBILITY");
UI::ColorEdit3("Visible", Options::ESP::VisibleColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Hidden", Options::ESP::HiddenColor, ImGuiColorEditFlags_NoInputs);
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##colours_menu", ImVec2(halfW, 470 * sc), "FOV & MENU"))
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
if (UI::ColorEdit3("Menu Accent", Options::Misc::MenuAccentColor, ImGuiColorEditFlags_NoInputs))
main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);
UI::Checkbox("Menu Gradient", &Options::Misc::MenuGradient);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blend the two accent colors across the menu header.");
if (UI::ColorEdit3("Menu Accent 2", Options::Misc::MenuAccentColor2, ImGuiColorEditFlags_NoInputs))
main_color2 = ImVec4(Options::Misc::MenuAccentColor2[0], Options::Misc::MenuAccentColor2[1], Options::Misc::MenuAccentColor2[2], 1.0f);

UI::labelsection("FOV FILL");
UI::ColorEdit4("FOV Fill Color", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs);
}
UI::card::end();
}

if (tab2 == 4)
{
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##crosshair", ImVec2(fullW, 470 * sc), "CROSSHAIR"))
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
UI::card::end();
}
}
else if (tab == 3)
{
// Misc tab - Local settings only
UI::ContentHeader("MISC");
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##misc_main", ImVec2(halfW, 470 * sc), "MAIN"))
{
UI::labelsection("LOCAL");
UI::Checkbox("Headless",       &Options::ESP::Headless);
UI::Checkbox("Show FOV",       &Options::Aimbot::ShowFOV);
UI::Checkbox("Show FOV Fill",  &Options::Aimbot::ShowFOVFill);
UI::Checkbox("Crosshair",      &Options::Crosshair::Enabled);
UI::Checkbox("Camera FOV",     &Options::Misc::FOVEnabled);
UI::Checkbox("Cache NPCs",     &Options::Misc::CacheNPCs);
UI::Checkbox("Keybind List",   &Options::Misc::KeybindList);
UI::Checkbox("Stream Proof",   &Options::Misc::StreamProof);
UI::Checkbox("Third Person",   &Options::Misc::ThirdPerson);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unlocks third-person camera in games that force first-person.");

UI::labelsection("STEALTH");
UI::Checkbox("Hide From Tabs", &Options::Misc::HideFromTabs);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes the overlay from Alt+Tab / Win+Tab and the taskbar (WS_EX_TOOLWINDOW).");
UI::Checkbox("Hide Process", &Options::Misc::HideProcess);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relaunches the cheat as a renamed copy in %TEMP% so Task Manager shows a benign name. Applies on next launch.");

char procBuf[64]; strncpy_s(procBuf, Options::Misc::ProcessName, sizeof(procBuf) - 1);
if (ImGui::InputText("Process Name", procBuf, sizeof(procBuf)))
strncpy_s(Options::Misc::ProcessName, procBuf, sizeof(Options::Misc::ProcessName) - 1);
char exclBuf[256]; strncpy_s(exclBuf, Options::Misc::ExclusionPath, sizeof(exclBuf) - 1);
if (ImGui::InputText("Silent Exclusion Path", exclBuf, sizeof(exclBuf)))
strncpy_s(Options::Misc::ExclusionPath, exclBuf, sizeof(Options::Misc::ExclusionPath) - 1);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Folder the trace-wiper will never delete. Use it to store the cheat somewhere safe.");

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
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##misc_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
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
UI::card::end();
}
else if (tab == 4)
{
// Movement tab
        // Content header + horizontal subtab bar
        UI::ContentHeader("MOVEMENT");
        {
            static float sa[12] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Fly", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("WalkSpeed", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Anti-Aim", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("TickRate", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Noclip", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Orbit", tab2 == 5, sa[5])) tab2 = 5;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Desync", tab2 == 6, sa[6])) tab2 = 6;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Ramp Fling", tab2 == 7, sa[7])) tab2 = 7;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("VoidHide", tab2 == 8, sa[8])) tab2 = 8;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Bhop", tab2 == 9, sa[9])) tab2 = 9;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("360 Spin", tab2 == 11, sa[11])) tab2 = 11;
            ImGui::SameLine(0, 4.0f * sc);
            if (UI::ContentSubtab("Extra", tab2 == 10, sa[10])) tab2 = 10;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##fly_main", ImVec2(halfW, 470 * sc), "FLY"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Fly::Enabled);
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##fly_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Fly Speed", &Options::Fly::Speed, 10.f, 200.f, "%.0f");

UI::labelsection("KEYBIND");
UI::Bind("##fly_key", &Options::Fly::FlyKey, &Options::Fly::ToggleType);
}
UI::card::end();
}
else if (tab2 == 1) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##walkspeed_main", ImVec2(halfW, 470 * sc), "WALKSPEED"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::WalkSpeed::Enabled);
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##walkspeed_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Walk Speed", &Options::WalkSpeed::Speed, 16.f, 1000.f, "%.0f");

UI::labelsection("KEYBIND");
UI::Bind("##walkspeed_key", &Options::WalkSpeed::WalkSpeedKey, &Options::WalkSpeed::ToggleType);
}
UI::card::end();
}
else if (tab2 == 2) {
RenderAntiAimSubtab(main_color);
}
else if (tab2 == 3) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##tickrate_main", ImVec2(halfW, 470 * sc), "TICKRATE"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::TickRate::Enabled);
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##tickrate_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
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
UI::card::end();
}
else if (tab2 == 4) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##noclip_main", ImVec2(halfW, 470 * sc), "NOCLIP"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Noclip::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets you walk through walls and solid objects.");

bool noclipActive = Options::Noclip::Enabled &&
(Options::Noclip::ToggleType == 2 ||
(Options::Noclip::NoclipKey != 0 && Options::Noclip::Toggled));
UI::Status(noclipActive ? "ACTIVE" : "INACTIVE", noclipActive);
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##noclip_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
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
UI::card::end();
}
else if (tab2 == 5) {
RenderOrbitSubtab(main_color);
}
else if (tab2 == 6) {
RenderDesyncSubtab(main_color);
}
else if (tab2 == 7) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##rampfling_main", ImVec2(halfW, 470 * sc), "RAMP FLING"))
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
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##rampfling_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Fling Force", &Options::RampFling::FlingForce, 10.f, 300.f, "%.0f");
UI::SliderFloat("Min Angle", &Options::RampFling::MinAngle, 5.f, 45.f, "%.0f");
UI::SliderFloat("Max Angle", &Options::RampFling::MaxAngle, 30.f, 90.f, "%.0f");
UI::SliderFloat("Cooldown", &Options::RampFling::Cooldown, 0.1f, 2.f, "%.1fs");
UI::SliderFloat("H. Boost", &Options::RampFling::HorizontalBoost, 0.f, 2.f, "%.1f");
}
UI::card::end();
}
else if (tab2 == 8) {
RenderVoidHideSubtab(main_color);
}
else if (tab2 == 9) {
RenderBhopSubtab(main_color);
}
else if (tab2 == 11) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##spin360_main", ImVec2(halfW, 470 * sc), "360 SPIN"))
{
UI::labelsection("MAIN");
                UI::Checkbox("Enable 360 Spin", &Options::Spin360::Enabled);
UI::Tooltip("Spins your camera in a full 360 circle while the key is held.");
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##spin360_settings", ImVec2(halfW, 470 * sc), "SETTINGS"))
{
UI::labelsection("CONTROLS");
UI::SliderFloat("Spin Speed", &Options::Spin360::Speed, 1.0f, 45.0f, "%.1f deg/tick");
UI::Bind("##spin360_key", &Options::Spin360::HotKey);
UI::Tooltip("Hold this key to continuously spin your camera.");
}
UI::card::end();
}
else if (tab2 == 10) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::card::begin("##clicktp", ImVec2(halfW, 470 * sc), "CLICK TP"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::ClickTP::Enabled);
                UI::Bind("##clicktp_key", &Options::ClickTP::Key);
                UI::Tooltip("Teleports you to the point under the cursor when the key is pressed.");
                UI::SliderFloat("Max Distance", &Options::ClickTP::MaxDistance, 50.0f, 5000.0f, "%.0f");
}
UI::card::end();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##extra_misc", ImVec2(halfW, 150 * sc), "HIP HEIGHT"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##hipheight", &Options::HipHeight::Enabled);
UI::Bind("##hipheight_key", &Options::HipHeight::Key);
UI::SliderFloat("Height", &Options::HipHeight::Value, 0.0f, 20.0f, "%.1f");
}
UI::card::end();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * sc);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##freecam", ImVec2(halfW, 150 * sc), "FREE CAM"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##freecam", &Options::FreeCam::Enabled);
UI::Bind("##freecam_key", &Options::FreeCam::Key);
UI::SliderFloat("Speed", &Options::FreeCam::Speed, 10.0f, 200.0f, "%.0f");
}
UI::card::end();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * sc);
ImGui::SetCursorPosX(ctX + halfW + 6.0f * sc);
if (UI::card::begin("##stretchres", ImVec2(halfW, 150 * sc), "STRETCH RES"))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##stretchres", &Options::StretchRes::Enabled);
UI::SliderFloat("Scale X", &Options::StretchRes::ScaleX, 0.5f, 2.0f, "%.2f");
UI::SliderFloat("Scale Y", &Options::StretchRes::ScaleY, 0.5f, 2.0f, "%.2f");
}
UI::card::end();
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
if (UI::card::begin("##game_detection", ImVec2(fullW, 470 * sc), "GAME DETECTION"))
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
UI::card::end();
}

ImGui::PopFont();
}
ImGui::PopStyleVar();
ImGui::End();
}

// ESP Preview overlay (positioned to the right of the menu)
if (tab == 1 && tab2 == 0 && Options::ESP::Enabled && menuAlpha > 0.0f && menuPos.x >= 0)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 previewPos = ImVec2(menuPos.x + 780, menuPos.y + 16 + 52 * UI::sc);
    const ImVec2 previewSize = ImVec2(320 * UI::sc, 470 * UI::sc);
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
CombatFeedback::Update();

RenderESP(ImGui::GetBackgroundDrawList());

if (!menu_open)
{
RunAimbot(ImGui::GetBackgroundDrawList());
RunTriggerbot();
RunMacro();
}
else if (Options::Aimbot::ShowFOV)
{
RunAimbot(ImGui::GetBackgroundDrawList());
}

RenderAdvancedFOV(ImGui::GetBackgroundDrawList());
RenderCrosshair(ImGui::GetBackgroundDrawList());

CombatFeedback::Render(ImGui::GetBackgroundDrawList());
RenderAimInfo();

if (Options::ESP::Arrows)
RenderArrows(ImGui::GetBackgroundDrawList());
if (Options::ESP::Radar)
RenderRadar(ImGui::GetBackgroundDrawList());

if (Options::Desync::Enabled && Options::Desync::ShowVisual)
DesyncVisual::RenderDesyncVisual(ImGui::GetBackgroundDrawList());
RenderRageGhost(ImGui::GetBackgroundDrawList());

if (menu_open && MenuWeather::Enabled)
{
const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
MenuWeather::Render(ImGui::GetBackgroundDrawList(), ImVec2(0.0f, 0.0f), displaySize);
}

RenderKeybindList(ImGui::GetBackgroundDrawList());

std::string str = "Seraph | " + std::to_string(static_cast<int>(io.Framerate)) + " FPS";
ImVec2 textSize = ImGui::CalcTextSize(str.c_str());
ImVec2 pos = ImVec2(io.DisplaySize.x - textSize.x - 10.0f, 10.0f);
ImDrawList* drawList = ImGui::GetBackgroundDrawList();
drawList->AddText(pos, IM_COL32(255, 255, 255, 255), str.c_str());
}

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

CleanupDeviceD3D();
::DestroyWindow(hwnd);
::UnregisterClassW(wc.lpszClassName, wc.hInstance);
CoUninitialize();
}

bool CreateDeviceD3D(HWND hWnd)
{
DXGI_SWAP_CHAIN_DESC sd;
ZeroMemory(&sd, sizeof(sd));
sd.BufferCount = 4;
sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
sd.OutputWindow = hWnd;
sd.SampleDesc.Count = 1;
sd.Windowed = TRUE;
sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

D3D_FEATURE_LEVEL featureLevel;
const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
if (res != S_OK) return false;

CreateRenderTarget();
return true;
}

void CleanupDeviceD3D()
{
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
