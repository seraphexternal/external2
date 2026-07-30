#pragma once
#ifdef _MSC_VER
#pragma warning(disable: 26812)
#endif

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../rbx/configs/configs.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Loader
{
    // ── Theme presets ──────────────────────────────────────────────────
    struct ThemePreset { const char* name; float bg[3]; float panel[3]; float accent[3]; float accent2[3]; bool gradient; };
    static const ThemePreset Themes[] = {
        { "Custom",   {0.031f,0.031f,0.031f}, {0.078f,0.078f,0.090f}, {1.000f,0.410f,0.710f}, {0.410f,0.710f,1.000f}, false },
        { "Midnight", {0.015f,0.020f,0.038f}, {0.038f,0.050f,0.085f}, {0.340f,0.560f,1.000f}, {0.640f,0.400f,1.000f}, true  },
        { "Carbon",   {0.014f,0.014f,0.017f}, {0.052f,0.052f,0.060f}, {0.870f,0.880f,0.920f}, {0.520f,0.550f,0.620f}, false },
        { "Sunset",   {0.045f,0.022f,0.035f}, {0.090f,0.042f,0.068f}, {1.000f,0.480f,0.340f}, {1.000f,0.780f,0.300f}, true  },
        { "Matrix",   {0.010f,0.028f,0.016f}, {0.028f,0.062f,0.038f}, {0.240f,1.000f,0.380f}, {0.580f,1.000f,0.240f}, false },
        { "Ice",      {0.018f,0.035f,0.052f}, {0.060f,0.098f,0.130f}, {0.480f,0.870f,1.000f}, {0.760f,0.940f,1.000f}, true  },
        { "Crimson",  {0.048f,0.010f,0.016f}, {0.115f,0.028f,0.048f}, {1.000f,0.200f,0.280f}, {1.000f,0.530f,0.260f}, true  },
        { "Lilac",    {0.032f,0.020f,0.048f}, {0.080f,0.060f,0.125f}, {0.700f,0.480f,1.000f}, {1.000f,0.560f,0.900f}, true  },
    };
    static const int ThemeCount = sizeof(Themes) / sizeof(Themes[0]);

    // ── Font entries ───────────────────────────────────────────────────
    struct FontEntry { ImFont* font; const char* path; const char* name; };
    static FontEntry LoaderFonts[7] = {};
    static int LoaderFontCount = 0;

    static const char* FontNames[] = {
        "Verdana", "Segoe UI", "Tahoma", "Arial", "Georgia", "Calibri", "Consolas"
    };
    static const int FontNameCount = 7;

    // ── Palette ────────────────────────────────────────────────────────
    static inline ImU32 Pal(float r, float g, float b, float a = 1.0f) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    }
    static inline ImVec4 PalV(float r, float g, float b, float a = 1.0f) {
        return ImVec4(r, g, b, a);
    }

    // ── State ──────────────────────────────────────────────────────────
    static ID3D11Device*           l_Device = nullptr;
    static ID3D11DeviceContext*    l_Context = nullptr;
    static IDXGISwapChain*         l_SwapChain = nullptr;
    static ID3D11RenderTargetView* l_RTV = nullptr;
    static HWND                    l_Hwnd = nullptr;
    static bool                    l_Running = true;
    static bool                    l_Inject = false;
    static int                     l_SelectedTab = 0;
    static bool                    l_Splash = true;
    static float                   l_SplashTimer = 0.0f;
    static float                   l_SplashFade = 0.0f;
    static float                   l_LogoPulse = 0.0f;

    static const char* ProcessPresets[] = {
        "RuntimeBroker", "SearchProtocolHost", "SearchFilterHost",
        "DllHost", "backgroundTaskHost", "ApplicationFrameHost",
        "CompPkgSusp", "smartscreen",
    };
    static const int ProcessPresetCount = 8;

    static std::vector<std::string> ConfigFiles;

    // ── Animation state ────────────────────────────────────────────────
    static float g_Time = 0.0f;
    static float g_EntranceAlpha = 0.0f;
    static float g_EntranceSlide = 12.0f;
    static float g_TabFade[4] = {};
    static float g_InjectGlow = 0.0f;
    static float g_ScrollY = 0.0f;
    static float g_ConfigListWidth = 0.0f;

    static float s_MessageFade = 1.0f;
    static int   s_CurrentMessage = 0;
    static float s_MessageTimer = 0.0f;
    static float s_BarProgress = 0.0f;
    static float s_ShimmerX = 0.0f;

    static float LerpF(float a, float b, float t) { return a + (b - a) * t; }
    static float ClampF(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
    static float Pulse() { return (sinf(g_Time * 2.5f) + 1.0f) * 0.5f; }
    static float EaseInOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) * 0.5f; }
    static float EaseOutCubic(float t) { return 1.0f - powf(1.0f - t, 3.0f); }

    // ── D3D11 helpers ──────────────────────────────────────────────────
    static bool LoaderCreateDevice(HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_EFFECT_DISCARD;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL fl;
        const D3D_FEATURE_LEVEL flArr[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            flArr, 2, D3D11_SDK_VERSION, &sd, &l_SwapChain, &l_Device, &fl, &l_Context)))
            return false;
        ID3D11Texture2D* bb;
        l_SwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
        l_Device->CreateRenderTargetView(bb, nullptr, &l_RTV);
        bb->Release();
        return true;
    }

    static void LoaderCleanup()
    {
        if (l_RTV) { l_RTV->Release(); l_RTV = nullptr; }
        if (l_SwapChain) { l_SwapChain->Release(); l_SwapChain = nullptr; }
        if (l_Context) { l_Context->Release(); l_Context = nullptr; }
        if (l_Device) { l_Device->Release(); l_Device = nullptr; }
    }

    static void LoaderCreateRT()
    {
        if (!l_SwapChain || !l_Device) return;
        ID3D11Texture2D* bb;
        l_SwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
        l_Device->CreateRenderTargetView(bb, nullptr, &l_RTV);
        bb->Release();
    }

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
        switch (msg)
        {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            if (l_Device)
            {
                if (l_RTV) { l_RTV->Release(); l_RTV = nullptr; }
                l_SwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                LoaderCreateRT();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            l_Running = false;
            ::PostQuitMessage(0);
            return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    static void ScanConfigs()
    {
        ConfigFiles.clear();
        ConfigFiles.push_back("(None)");
        auto files = ListConfigFiles();
        for (auto& f : files)
            ConfigFiles.push_back(f);
    }

    // ── Hierarchy font pointers ─────────────────────────────────────────
    static ImFont* l_Font_Display = nullptr;  // 22px Bold   — SERAPH title
    static ImFont* l_Font_Section = nullptr;   // 14px SemiBold — section titles
    static ImFont* l_Font_Label   = nullptr;   // 12px Medium   — field labels
    static ImFont* l_Font_Body    = nullptr;   // 13px Regular  — body / nav
    static ImFont* l_Font_Small   = nullptr;   // 11px Regular  — badges / secondary
    static ImFont* l_Font_Button  = nullptr;   // 13px SemiBold — inject button

    // ── Push/pop current font ──────────────────────────────────────────
    static void PushBodyFont() {
        if (Options::Misc::MenuFont >= 0 && Options::Misc::MenuFont < LoaderFontCount
            && LoaderFonts[Options::Misc::MenuFont].font)
            ImGui::PushFont(LoaderFonts[Options::Misc::MenuFont].font);
        else if (l_Font_Body)
            ImGui::PushFont(l_Font_Body);
    }
    static void PopBodyFont() { ImGui::PopFont(); }

    // ── Drawing helpers ────────────────────────────────────────────────
    static void DrawRoundedRectFilled(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col, float r) {
        dl->AddRectFilled(mn, mx, col, r);
    }
    static void DrawRoundedRectStroke(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col, float r, float w = 1.0f) {
        dl->AddRect(mn, mx, col, r, 0, w);
    }

    // Toggle pill
    static void DrawTogglePill(ImDrawList* dl, ImVec2 pos, float w, float h, bool on, float anim = 1.0f) {
        float radius = h * 0.5f;
        ImU32 bg = on
            ? Pal(LerpF(0.10f, 0.0f, anim), LerpF(0.12f, 0.55f, anim), LerpF(0.15f, 0.85f, anim))
            : Pal(0.08f, 0.10f, 0.12f);
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, radius);
        float knobR = radius - 2.5f;
        float knobX = on ? pos.x + w - knobR - 2.5f : pos.x + knobR + 2.5f;
        dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), knobR,
            Pal(LerpF(0.5f, 1.0f, anim), LerpF(0.5f, 1.0f, anim), LerpF(0.5f, 1.0f, anim)));
    }

    // Animated toggle switch with smooth transition
    static float l_ToggleAnim[8] = {};
    static void DrawSwitch(ImDrawList* dl, ImVec2 pos, float w, float h, bool* v, int id) {
        float radius = h * 0.5f;
        float target = *v ? 1.0f : 0.0f;
        l_ToggleAnim[id] = LerpF(l_ToggleAnim[id], target, ImGui::GetIO().DeltaTime * 12.0f);
        float a = l_ToggleAnim[id];
        ImU32 bg = Pal(LerpF(0.08f, 0.0f, a), LerpF(0.10f, 0.50f, a), LerpF(0.12f, 0.78f, a));
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, radius);
        float knobR = radius - 2.5f;
        float knobX = pos.x + knobR + 2.5f + (w - knobR * 2.0f - 5.0f) * a;
        ImU32 knobCol = Pal(LerpF(0.50f, 1.0f, a), LerpF(0.50f, 1.0f, a), LerpF(0.50f, 1.0f, a));
        dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), knobR, knobCol);
        // Subtle glow when on
        if (a > 0.01f)
            dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), knobR * 1.8f,
                Pal(0.118f, 0.655f, 1.0f, 0.05f * a));
    }

    // Draw a card background with gradient fill, soft shadow, and border
    static void DrawCard(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float r = 10.0f, bool selected = false) {
        // Layered soft shadow
        dl->AddRectFilled(ImVec2(mn.x + 2.0f, mn.y + 2.0f), ImVec2(mx.x + 2.0f, mx.y + 3.0f),
            Pal(0.0f, 0.0f, 0.0f, 0.10f), r);
        dl->AddRectFilled(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x + 1.0f, mx.y + 2.0f),
            Pal(0.0f, 0.0f, 0.0f, 0.06f), r);
        // Gradient fill: top #171F2D → bottom #131A27
        {
            int gSteps = 12;
            float r1 = 0.090f, g1 = 0.122f, b1 = 0.176f; // #171F2D
            float r2 = 0.075f, g2 = 0.102f, b2 = 0.153f; // #131A27
            float h = mx.y - mn.y;
            for (int i = 0; i < gSteps; i++) {
                float t = (float)i / (float)gSteps;
                float y0 = mn.y + h * t;
                float y1 = mn.y + h * (t + 1.0f / (float)gSteps);
                float rr = r1 + (r2 - r1) * t;
                float gg = g1 + (g2 - g1) * t;
                float bb = b1 + (b2 - b1) * t;
                dl->AddRectFilled(ImVec2(mn.x, y0), ImVec2(mx.x, y1), Pal(rr, gg, bb), r);
                // Clip corners for all but first/last strip
                if (i > 0 && i < gSteps - 1)
                    dl->AddRectFilled(ImVec2(mn.x, y0), ImVec2(mx.x, y1), Pal(rr, gg, bb));
            }
        }
        // Border: #2A3C55
        dl->AddRect(mn, mx, Pal(0.165f, 0.235f, 0.333f), r, 0, 1.0f);
        if (selected)
            dl->AddRect(mn, mx, Pal(0.118f, 0.655f, 1.0f, 0.6f), r, 0, 1.5f);
    }

    // Section title with monochrome icon and section font (14px)
    static void DrawSectionHeader(ImDrawList* dl, ImVec2 pos, const char* title, int icon) {
        ImU32 col = Pal(0.961f, 0.969f, 0.980f);
        float ix = pos.x, iy = pos.y + 1.0f;
        ImU32 iconCol = Pal(0.604f, 0.659f, 0.737f);
        if (icon == 0) { // Shield
            dl->AddRect(ImVec2(ix, iy + 2.0f), ImVec2(ix + 9.0f, iy + 9.0f), iconCol, 2.0f);
            dl->AddTriangleFilled(ImVec2(ix - 0.5f, iy + 2.0f), ImVec2(ix + 4.5f, iy - 1.0f),
                ImVec2(ix + 9.5f, iy + 2.0f), iconCol);
        } else if (icon == 1) { // Eye
            dl->AddCircle(ImVec2(ix + 4.5f, iy + 4.5f), 3.5f, iconCol, 0, 1.2f);
            dl->AddCircleFilled(ImVec2(ix + 4.5f, iy + 4.5f), 1.2f, iconCol);
        } else if (icon == 2) { // Monitor
            dl->AddRect(ImVec2(ix, iy + 1.0f), ImVec2(ix + 9.0f, iy + 7.0f), iconCol, 1.0f);
            dl->AddRectFilled(ImVec2(ix + 3.0f, iy + 7.0f), ImVec2(ix + 6.0f, iy + 9.5f), iconCol);
        } else if (icon == 3) { // Folder
            dl->AddRect(ImVec2(ix, iy + 2.0f), ImVec2(ix + 9.0f, iy + 9.0f), iconCol, 2.0f);
            dl->AddLine(ImVec2(ix, iy + 2.0f), ImVec2(ix + 4.5f, iy + 2.0f), iconCol, 1.5f);
            dl->AddLine(ImVec2(ix + 4.5f, iy + 2.0f), ImVec2(ix + 6.5f, iy + 4.5f), iconCol, 1.5f);
        }
        if (l_Font_Section)
            dl->AddText(l_Font_Section, 0, ImVec2(pos.x + 14.0f, pos.y), col, title);
        else
            dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(pos.x + 14.0f, pos.y), col, title);
    }

    // ── Splash Screen ─────────────────────────────────────────────────
    static void RenderSplashScreen()
    {
        float dt = ImGui::GetIO().DeltaTime;
        l_SplashTimer += dt;

        // ── Configuration ──────────────────────────────────────────
        const float splashDuration = 3.8f;
        const float fadeInDuration = 0.7f;
        const float fadeOutDuration = 0.5f;
        const float messageInterval = 0.75f;

        static const char* loadingMessages[] = {
            "Initializing...", "Locating Roblox...", "Bypassing Byfron...",
            "Loading Modules...", "Connecting to Client...",
            "Preparing Interface...", "Finalizing...", "Launching Seraph..."
        };
        const int messageCount = 8;

        // ── Animations ─────────────────────────────────────────────
        l_LogoPulse = sinf(l_SplashTimer * 1.8f);
        float logoFloat = sinf(l_SplashTimer * 0.9f) * 2.5f;
        s_ShimmerX = fmodf(s_ShimmerX + dt * 0.12f, 1.0f);

        // Loading bar progress with easing
        float rawProgress = fminf(l_SplashTimer / (splashDuration - fadeOutDuration), 1.0f);
        s_BarProgress = EaseInOutQuad(rawProgress);

        // Message rotation
        s_MessageTimer += dt;
        if (s_MessageTimer >= messageInterval)
        {
            s_MessageTimer = 0.0f;
            s_CurrentMessage = (s_CurrentMessage + 1) % messageCount;
            s_MessageFade = 0.0f;
        }
        s_MessageFade = fminf(s_MessageFade + dt * 5.0f, 1.0f);

        // Overall splash fade
        if (l_SplashTimer < fadeInDuration)
            l_SplashFade = EaseOutCubic(l_SplashTimer / fadeInDuration);
        else if (l_SplashTimer > splashDuration - fadeOutDuration)
        {
            float t = (l_SplashTimer - (splashDuration - fadeOutDuration)) / fadeOutDuration;
            l_SplashFade = 1.0f - EaseOutCubic(t);
        }
        else
            l_SplashFade = 1.0f;

        if (l_SplashTimer >= splashDuration)
        {
            l_Splash = false;
            l_SplashFade = 0.0f;
            return;
        }

        // ── Window ─────────────────────────────────────────────────
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, l_SplashFade);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.012f, 0.027f, 0.071f, 1.0f));

        ImGui::Begin("##Splash", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        ImVec2 center(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f);
        float maxDim = fmaxf(ws.x, ws.y);

        // ── Background layers ──────────────────────────────────────
        // Base deep navy (#030712) to near-black (#010205)
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
            Pal(0.012f, 0.027f, 0.071f));
        // Bottom gradient to near-black
        {
            float gradSteps = 40;
            for (int i = 0; i < (int)gradSteps; i++)
            {
                float t = (float)i / gradSteps;
                float a = (1.0f - t) * 0.04f;
                float y = wp.y + t * ws.y;
                dl->AddRectFilled(ImVec2(wp.x, y), ImVec2(wp.x + ws.x, y + ws.y / gradSteps),
                    Pal(0.004f, 0.008f, 0.020f, a));
            }
        }

        // Soft ambient radial glow behind logo (reduced intensity)
        float gradTime = l_SplashTimer * 0.12f;
        ImVec2 glowCenter(center.x + sinf(gradTime) * 30.0f, center.y - 35.0f + cosf(gradTime * 0.6f) * 20.0f);
        for (int i = 0; i < 40; i++)
        {
            float r = (float)i / 40.0f * maxDim * 0.4f;
            float a = (1.0f - (float)i / 40.0f) * 0.012f;
            dl->AddCircleFilled(glowCenter, r, Pal(0.08f, 0.32f, 0.65f, a), 64);
        }

        // ── Constellation pattern (left side) ─────────────────────
        {
            float cx = wp.x + 55.0f;
            float cy = wp.y + ws.y * 0.2f;
            struct { float dx, dy; } nodes[] = {
                {0, 0}, {18, 50}, {-8, 90}, {22, 140}, {-5, 190}, {12, 240}, {0, 290}
            };
            int nCount = 7;
            ImU32 dotCol = Pal(0.25f, 0.55f, 1.0f, 0.10f * l_SplashFade);
            ImU32 lineCol = Pal(0.25f, 0.55f, 1.0f, 0.05f * l_SplashFade);
            for (int i = 0; i < nCount; i++)
            {
                float px = cx + nodes[i].dx;
                float py = cy + nodes[i].dy;
                float pulseA = 1.0f + 0.3f * sinf(l_SplashTimer * 0.5f + (float)i * 0.8f);
                dl->AddCircleFilled(ImVec2(px, py), 1.5f * pulseA, dotCol);
                if (i > 0)
                    dl->AddLine(ImVec2(cx + nodes[i-1].dx, cy + nodes[i-1].dy),
                        ImVec2(px, py), lineCol, 0.5f);
                // Branch connections
                if (i < nCount - 2 && i % 2 == 0)
                    dl->AddLine(ImVec2(px, py),
                        ImVec2(cx + nodes[i+2].dx, cy + nodes[i+2].dy), lineCol, 0.3f);
            }
        }

        // Floating particles (slower, fewer)
        {
            static struct { float x, y, speed, size, phase; } part[20];
            static bool init = false;
            if (!init) {
                for (int i = 0; i < 20; i++) {
                    part[i].x = (float)((i * 137 + 53) % 10000) / 10000.0f;
                    part[i].y = (float)((i * 271 + 89) % 10000) / 10000.0f;
                    part[i].speed = 0.08f + (float)((i * 31) % 1000) / 10000.0f * 0.15f;
                    part[i].size = 0.4f + (float)((i * 53) % 1000) / 10000.0f * 1.0f;
                    part[i].phase = (float)((i * 97) % 10000) / 10000.0f * 6.28f;
                }
                init = true;
            }
            float t = l_SplashTimer * 0.08f;
            for (int i = 0; i < 20; i++) {
                float py = fmodf(part[i].y - t * part[i].speed + 1.0f, 1.0f);
                float alph = 0.2f + 0.2f * sinf(t * 1.5f + part[i].phase);
                dl->AddCircleFilled(
                    ImVec2(wp.x + part[i].x * ws.x, wp.y + py * ws.y),
                    part[i].size, Pal(0.3f, 0.6f, 1.0f, alph * 0.25f));
            }
        }

        // Vignette
        for (int i = 0; i < 16; i++)
        {
            float t = (float)i / 16.0f;
            float inset = t * maxDim * 0.3f;
            float a = (1.0f - t) * 0.06f;
            dl->AddRectFilled(
                ImVec2(wp.x + inset, wp.y + inset),
                ImVec2(wp.x + ws.x - inset, wp.y + ws.y - inset),
                Pal(0.0f, 0.0f, 0.0f, a));
        }

        // Grain overlay (very subtle)
        for (int n = 0; n < 60; n++)
        {
            float nx = fmodf((float)(n * 137) * 0.000317f + l_SplashTimer * 0.0015f, 1.0f);
            float ny = fmodf((float)(n * 271) * 0.000273f + l_SplashTimer * 0.0008f, 1.0f);
            dl->AddRectFilled(
                ImVec2(wp.x + nx * ws.x, wp.y + ny * ws.y),
                ImVec2(wp.x + nx * ws.x + 1.0f, wp.y + ny * ws.y + 1.0f),
                Pal(1.0f, 1.0f, 1.0f, 0.008f));
        }

        // Thin accent border
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
            Pal(0.20f, 0.45f, 0.75f, 0.15f), 14.0f, 0, 1.0f);

        // ── Circular emblem logo ────────────────────────────────────
        {
            float lx = center.x;
            float ly = center.y - 40.0f + logoFloat;
            float radius = 28.0f;

            // Gentle radial glow (reduced ~25% from previous)
            float glowAlpha = (0.18f + 0.10f * sinf(l_SplashTimer * 1.6f + 0.5f)) * l_SplashFade;
            for (int g = 4; g >= 1; g--)
            {
                float r = radius * (1.0f + g * 0.20f);
                dl->AddCircleFilled(ImVec2(lx, ly), r,
                    Pal(0.06f, 0.30f, 0.70f, glowAlpha / (float)(g + 1)), 64);
            }

            // Expanding ripple ring
            float ripple = fmodf(l_SplashTimer * 0.25f, 1.0f);
            float rippleR = radius * (1.0f + ripple * 0.45f);
            float rippleA = (1.0f - ripple) * 0.12f * l_SplashFade;
            dl->AddCircle(ImVec2(lx, ly), rippleR,
                Pal(0.25f, 0.55f, 1.0f, rippleA), 64, 1.0f);

            // Soft outer ring
            dl->AddCircle(ImVec2(lx, ly), radius,
                Pal(0.40f, 0.70f, 1.0f, 0.50f * l_SplashFade), 64, 1.5f);

            // Crisp inner ring
            dl->AddCircle(ImVec2(lx, ly), radius * 0.78f,
                Pal(0.50f, 0.80f, 1.0f, 0.60f * l_SplashFade), 64, 1.2f);

            // Monoline S — Windows-style proportions
            {
                const float tilt = 7.0f * 3.14159f / 180.0f;
                float ct = cosf(tilt), st = sinf(tilt);
                float s = radius * 0.46f;
                ImU32 sCol = Pal(0.55f, 0.82f, 1.0f, 0.92f * l_SplashFade);
                float sw = 3.0f;
                // Control points create a proper S: top-right → far left → far right → bottom-left
                struct { float x, y; } pts[4] = {
                    { s * 1.0f,  -s * 1.0f },
                    { -s * 0.85f, -s * 0.55f },
                    { s * 0.85f,  s * 0.55f },
                    { -s * 1.0f,  s * 1.0f }
                };
                ImVec2 rpts[4];
                for (int i = 0; i < 4; i++)
                    rpts[i] = ImVec2(lx + pts[i].x * ct - pts[i].y * st,
                                     ly + pts[i].x * st + pts[i].y * ct);
                dl->AddBezierCubic(rpts[0], rpts[1], rpts[2], rpts[3], sCol, sw, 48);
            }
        }

        // ── Welcome to Seraph ──────────────────────────────────────
        {
            const char* welcome = "Welcome to Seraph";
            float welcomeSize = 22.0f;
            float sw = ImGui::GetFont()->CalcTextSizeA(welcomeSize, FLT_MAX, 0.0f, welcome).x;
            dl->AddText(ImGui::GetFont(), welcomeSize,
                ImVec2(center.x - sw * 0.5f, center.y + 14.0f),
                Pal(0.88f, 0.92f, 0.96f, l_SplashFade * 0.95f), welcome);
        }

        // ── Loading bar ────────────────────────────────────────────
        {
            float barW = 280.0f;
            float barH = 6.0f;
            float barX = center.x - barW * 0.5f;
            float barY = center.y + 58.0f;
            float barR = barH * 0.5f;

            // Dark translucent track with subtle border
            dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                Pal(0.015f, 0.030f, 0.055f, 0.7f), barR);
            dl->AddRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                Pal(0.08f, 0.15f, 0.25f, 0.2f), barR, 0, 0.5f);

            // Soft glow under fill
            float glowW = fmaxf(barW * s_BarProgress, 20.0f);
            dl->AddRectFilled(ImVec2(barX, barY + barH), ImVec2(barX + glowW, barY + barH + 4.0f),
                Pal(0.15f, 0.45f, 0.85f, 0.08f), 2.0f);

            // Electric blue fill (#3BA8FF)
            ImU32 fillCol = Pal(0.23f, 0.66f, 1.0f);
            float fillW = barW * s_BarProgress;
            if (fillW > barR * 2.0f)
            {
                dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH), fillCol, barR);
                dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barR, barY + barH), fillCol);
                if (fillW < barW)
                    dl->AddCircleFilled(ImVec2(barX + fillW, barY + barH * 0.5f), barR, fillCol);
            }
            else if (fillW > 0.0f)
            {
                float cr = fminf(fillW, barR);
                dl->AddCircleFilled(ImVec2(barX + cr, barY + barH * 0.5f), cr, fillCol);
            }

            // Gentle shimmer
            if (s_BarProgress > 0.05f && s_BarProgress < 0.95f)
            {
                float sx = barX + s_ShimmerX * barW;
                float sw2 = 40.0f;
                for (int si = 0; si < 5; si++)
                {
                    float t = (float)si / 5.0f;
                    dl->AddRectFilled(
                        ImVec2(sx + si * (sw2 / 5.0f) - sw2 * 0.5f, barY),
                        ImVec2(sx + (si + 1) * (sw2 / 5.0f) - sw2 * 0.5f, barY + barH),
                        Pal(1.0f, 1.0f, 1.0f, (1.0f - t) * 0.15f));
                }
            }
        }

        // ── Completion percentage ──────────────────────────────────
        {
            char pct[16];
            sprintf_s(pct, "%.0f%% Complete", s_BarProgress * 100.0f);
            float pctSize = 11.0f;
            ImVec2 pctS = ImGui::CalcTextSize(pct);
            dl->AddText(ImGui::GetFont(), pctSize,
                ImVec2(center.x - pctS.x * 0.5f, center.y + 74.0f),
                Pal(0.35f, 0.60f, 0.85f, l_SplashFade * 0.65f), pct);
        }

        // ── Loading message ────────────────────────────────────────
        {
            const char* msg = loadingMessages[s_CurrentMessage];
            float msgSize = 15.0f;
            ImVec2 msgS = ImGui::CalcTextSize(msg);
            dl->AddText(ImGui::GetFont(), msgSize,
                ImVec2(center.x - msgS.x * 0.5f, center.y + 95.0f),
                Pal(0.45f, 0.62f, 0.78f, l_SplashFade * s_MessageFade), msg);
        }

        // ── Version at bottom ──────────────────────────────────────
        {
            const char* ver = "Version 0.1 Beta";
            float verSize = 9.5f;
            ImVec2 verS = ImGui::CalcTextSize(ver);
            dl->AddText(ImGui::GetFont(), verSize,
                ImVec2(center.x - verS.x * 0.5f, wp.y + ws.y - 22.0f),
                Pal(0.28f, 0.38f, 0.45f, l_SplashFade * 0.38f), ver);
        }

        ImGui::PopStyleColor(); // WindowBg
        ImGui::PopStyleVar(4);
        ImGui::End();
    }

    // ── Main UI ────────────────────────────────────────────────────────
    static void RenderUI()
    {
        float dt = ImGui::GetIO().DeltaTime;
        g_Time += dt;

        // Entrance animation
        g_EntranceAlpha = LerpF(g_EntranceAlpha, 1.0f, dt * 4.0f);
        g_EntranceSlide = LerpF(g_EntranceSlide, 0.0f, dt * 6.0f);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(480, 580));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_EntranceAlpha);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, PalV(0.043f, 0.059f, 0.078f, 1.0f));

        ImGui::Begin("##Loader", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float pulse = Pulse();
        float slideY = g_EntranceSlide;

        // ── Background — blue-black gradient ──────────────────────────
        // Top #111827 to bottom #0B1220
        {
            int steps = 60;
            float topR = 0.067f, topG = 0.094f, topB = 0.153f;
            float botR = 0.043f, botG = 0.071f, botB = 0.125f;
            for (int i = 0; i < steps; i++) {
                float t = (float)i / (float)steps;
                float y0 = wp.y + ws.y * t;
                float y1 = wp.y + ws.y * (t + 1.0f / (float)steps);
                float r = topR + (botR - topR) * t;
                float g = topG + (botG - topG) * t;
                float b = topB + (botB - topB) * t;
                dl->AddRectFilled(ImVec2(wp.x, y0), ImVec2(wp.x + ws.x, y1), Pal(r, g, b));
            }
        }

        // ── Header — slightly brighter, with soft bottom reflection ────
        float headerH = 56.0f;
        ImVec2 hdrMin(wp.x, wp.y);
        ImVec2 hdrMax(wp.x + ws.x, wp.y + headerH);
        // Header gradient
        {
            int hSteps = 20;
            for (int i = 0; i < hSteps; i++) {
                float t = (float)i / (float)hSteps;
                float y0 = hdrMin.y + headerH * t;
                float y1 = hdrMin.y + headerH * (t + 1.0f / (float)hSteps);
                float r = 0.078f + (0.055f - 0.078f) * t;
                float g = 0.114f + (0.078f - 0.114f) * t;
                float b = 0.169f + (0.118f - 0.169f) * t;
                dl->AddRectFilled(ImVec2(wp.x, y0), ImVec2(wp.x + ws.x, y1), Pal(r, g, b));
            }
        }
        // Bottom divider
        dl->AddRectFilled(ImVec2(wp.x, wp.y + headerH), ImVec2(wp.x + ws.x, wp.y + headerH + 1.0f),
            Pal(0.149f, 0.212f, 0.302f));  // #26364D

        // Top accent line — softer blue
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + 2.0f),
            Pal(0.118f, 0.655f, 1.0f, 0.7f));  // #1EA7FF

        // SERAPH logo mark — larger, filling top-left corner
        {
            float ox = wp.x + 16.0f + slideY;
            float oy = wp.y + 16.0f;
            ImU32 sCol = Pal(0.118f, 0.655f, 1.0f);
            float sc = 14.0f;
            dl->AddBezierCubic(
                ImVec2(ox + sc*0.70f, oy - sc*0.85f),  // P0: top-right
                ImVec2(ox - sc*0.90f, oy - sc*0.25f),  // P1: pull left
                ImVec2(ox + sc*0.90f, oy + sc*0.25f),  // P2: pull right
                ImVec2(ox - sc*0.70f, oy + sc*0.85f),  // P3: bottom-left
                sCol, 2.5f, 24);
        }

        // SERAPH title — 22px Bold (display font)
        {
            float tx = wp.x + 40.0f + slideY;
            float ty = wp.y + 7.0f;
            if (l_Font_Display)
                dl->AddText(l_Font_Display, 0, ImVec2(tx, ty), Pal(0.961f, 0.969f, 0.980f), "SERAPH");
            else
                dl->AddText(ImGui::GetFont(), 22.0f, ImVec2(tx, ty), Pal(0.961f, 0.969f, 0.980f), "SERAPH");
        }

        // Version and platform badges — 11px small font
        {
            float bx = wp.x + 40.0f + slideY;
            float by = wp.y + 34.0f;
            auto DrawBadge = [&](const char* label, ImU32 bg, ImU32 fg) {
                ImVec2 ts = l_Font_Small ? ImGui::CalcTextSize(label) : ImGui::CalcTextSize(label);
                float bw = ts.x + 8.0f, bh = 13.0f;
                dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), bg, 3.0f);
                if (l_Font_Small)
                    dl->AddText(l_Font_Small, 0, ImVec2(bx + (bw - ts.x) * 0.5f, by + (bh - ts.y) * 0.5f), fg, label);
                else
                    dl->AddText(ImGui::GetFont(), 11.0f, ImVec2(bx + (bw - ts.x) * 0.5f, by + (bh - ts.y) * 0.5f), fg, label);
                bx += bw + 4.0f;
            };
            DrawBadge("v0.1", Pal(0.04f, 0.06f, 0.10f), Pal(0.604f, 0.659f, 0.737f));
            DrawBadge("External", Pal(0.04f, 0.06f, 0.10f), Pal(0.604f, 0.659f, 0.737f));
            DrawBadge("Roblox", Pal(0.0f, 0.10f, 0.20f), Pal(0.118f, 0.655f, 1.0f));
        }

        // Status badge (pulsing green dot + "Ready") — 12px label font
        {
            float badgeW = 68.0f, badgeH = 20.0f;
            float badgeX = wp.x + ws.x - badgeW - 16.0f;
            float badgeY = wp.y + (headerH - badgeH) * 0.5f;
            dl->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH),
                Pal(0.03f, 0.08f, 0.05f), 10.0f);
            dl->AddRect(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH),
                Pal(0.10f, 0.25f, 0.15f, 0.4f), 10.0f, 0, 1.0f);
            float dotPulse = 0.6f + 0.4f * sinf(g_Time * 2.0f);
            dl->AddCircleFilled(ImVec2(badgeX + 14.0f, badgeY + badgeH * 0.5f), 3.0f,
                Pal(0.15f + 0.40f * dotPulse, 0.60f + 0.30f * dotPulse, 0.20f + 0.20f * dotPulse));
            dl->AddCircleFilled(ImVec2(badgeX + 14.0f, badgeY + badgeH * 0.5f), 1.8f,
                Pal(0.25f, 0.78f, 0.38f));
            if (l_Font_Label)
                dl->AddText(l_Font_Label, 0,
                    ImVec2(badgeX + 22.0f, badgeY + (badgeH - l_Font_Label->FontSize) * 0.5f),
                    Pal(0.35f, 0.72f, 0.48f), "Ready");
            else
                dl->AddText(ImGui::GetFont(), 12.0f,
                    ImVec2(badgeX + 22.0f, badgeY + (badgeH - 12.0f) * 0.5f),
                    Pal(0.35f, 0.72f, 0.48f), "Ready");
        }

        // ── Tab bar (segmented navigation) ──────────────────────────
        float tabBarY = headerH + 1.0f;
        float tabBarH = 40.0f;
        const char* tabs[] = { "Stealth", "Theme", "Font", "Config" };
        float tabW = ws.x / 4.0f;
        float tabGap = 8.0f;
        float segW = ws.x - tabGap * 2;
        float segX = wp.x + tabGap;
        float segY = tabBarY + 5.0f;
        float segH = tabBarH - 10.0f;

        // Segmented control background — subtle card
        dl->AddRectFilled(ImVec2(segX, segY), ImVec2(segX + segW, segY + segH),
            Pal(0.055f, 0.075f, 0.105f), 6.0f);
        dl->AddRect(ImVec2(segX, segY), ImVec2(segX + segW, segY + segH),
            Pal(0.149f, 0.212f, 0.302f, 0.3f), 6.0f, 0, 1.0f);

        float itemW = segW / 4.0f;
        for (int i = 0; i < 4; i++)
        {
            bool active = (l_SelectedTab == i);
            ImVec2 tMin(segX + i * itemW, segY);
            ImVec2 tMax(tMin.x + itemW, segY + segH);
            bool hov = ImGui::IsMouseHoveringRect(tMin, tMax) && !active;

            // Animate tab transition
            float targetFade = active ? 1.0f : 0.0f;
            g_TabFade[i] = LerpF(g_TabFade[i], targetFade, dt * 14.0f);

            // Hover background
            if (hov) {
                dl->AddRectFilled(tMin, tMax, Pal(0.078f, 0.114f, 0.169f, 0.5f), 6.0f);
            }

            // Active pill — gradient with accent border
            if (g_TabFade[i] > 0.01f) {
                float pad = 3.0f;
                float a = g_TabFade[i];
                // Gradient top-to-bottom for active pill
                int pSteps = 8;
                float pR1 = 0.090f, pG1 = 0.122f, pB1 = 0.176f; // #171F2D
                float pR2 = 0.075f, pG2 = 0.102f, pB2 = 0.153f; // #131A27
                for (int pi = 0; pi < pSteps; pi++) {
                    float pt = (float)pi / (float)pSteps;
                    float py0 = tMin.y + pad + (tMax.y - tMin.y - pad * 2) * pt;
                    float py1 = tMin.y + pad + (tMax.y - tMin.y - pad * 2) * (pt + 1.0f / (float)pSteps);
                    float pr = pR1 + (pR2 - pR1) * pt;
                    float pg = pG1 + (pG2 - pG1) * pt;
                    float pb = pB1 + (pB2 - pB1) * pt;
                    dl->AddRectFilled(ImVec2(tMin.x + pad, py0), ImVec2(tMax.x - pad, py1),
                        Pal(pr, pg, pb, a));
                }
                // Accent border
                dl->AddRect(
                    ImVec2(tMin.x + pad, tMin.y + pad),
                    ImVec2(tMax.x - pad, tMax.y - pad),
                    Pal(0.118f, 0.655f, 1.0f, 0.25f * a), 4.0f, 0, 1.0f);
            }

            // Tab label — 13px body font
            PushBodyFont();
            ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
            float tx = tMin.x + (itemW - ts.x) * 0.5f;
            float ty = tMin.y + (segH - ts.y) * 0.5f;
            ImU32 textCol = active ? Pal(0.118f, 0.655f, 1.0f)
                : hov ? Pal(0.75f, 0.82f, 0.90f)
                : Pal(0.604f, 0.659f, 0.737f);
            dl->AddText(ImGui::GetFont(), 0, ImVec2(tx, ty), textCol, tabs[i]);
            PopBodyFont();

            // Click
            if (hov && ImGui::IsMouseClicked(0))
                l_SelectedTab = i;
        }

        // ── Content area ───────────────────────────────────────────
        float contentPad = 16.0f;
        float btnH = 48.0f;
        float btnPad = 12.0f;
        float contentH = ws.y - headerH - tabBarH - 1.0f - btnH - btnPad * 2.0f;

        ImGui::SetCursorPos(ImVec2(contentPad, headerH + tabBarH + 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::BeginChild("##tab_content", ImVec2(ws.x - contentPad * 2, contentH), false);
        ImGui::PopStyleVar(1);
        ImVec2 cs = ImGui::GetContentRegionAvail();
        g_ConfigListWidth = cs.x;
        PushBodyFont();

        // ── STEALTH TAB ───────────────────────────────────────────
        if (l_SelectedTab == 0)
        {
            // Card: Process Camouflage
            ImVec2 c1m = ImGui::GetCursorScreenPos();
            float c1H = 164.0f;
            ImVec2 c1M(c1m.x + cs.x, c1m.y + c1H);
            DrawCard(dl, c1m, c1M, 10.0f);
            DrawSectionHeader(dl, ImVec2(c1m.x + 16.0f, c1m.y + 12.0f), "Process Camouflage", 0);

            // Hide Process toggle — 13px label
            {
                float ty = c1m.y + 36.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(c1m.x + 16.0f, ty + (26.0f - l_Font_Label->FontSize) * 0.5f),
                        Pal(0.90f, 0.92f, 0.95f), "Hide Process");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(c1m.x + 16.0f, ty + (26.0f - 13.0f) * 0.5f),
                        Pal(0.90f, 0.92f, 0.95f), "Hide Process");
                DrawSwitch(dl, ImVec2(c1M.x - 48.0f, ty + (26.0f - 18.0f) * 0.5f),
                    40.0f, 18.0f, &Options::Misc::HideProcess, 0);
                ImGui::SetCursorScreenPos(ImVec2(c1m.x + 16.0f, ty));
                if (ImGui::InvisibleButton("##hp", ImVec2(cs.x - 32.0f, 26.0f)))
                    Options::Misc::HideProcess = !Options::Misc::HideProcess;
            }

            // Process Name input with dropdown arrow
            {
                float iy = c1m.y + 68.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(c1m.x + 16.0f, iy), Pal(0.55f, 0.62f, 0.70f), "Process Name");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(c1m.x + 16.0f, iy), Pal(0.55f, 0.62f, 0.70f), "Process Name");
                ImVec2 inpMin(c1m.x + 16.0f, iy + 18.0f);
                ImVec2 inpMax(inpMin.x + cs.x - 32.0f, inpMin.y + 32.0f);
                dl->AddRectFilled(inpMin, inpMax, Pal(0.035f, 0.055f, 0.075f), 5.0f);
                dl->AddRect(inpMin, inpMax, Pal(0.165f, 0.235f, 0.333f), 5.0f, 0, 1.0f);
                // Dropdown arrow
                float arrowCX = inpMax.x - 12.0f, arrowCY = inpMin.y + 16.0f;
                dl->AddTriangleFilled(ImVec2(arrowCX - 3.5f, arrowCY - 1.5f),
                    ImVec2(arrowCX + 3.5f, arrowCY - 1.5f),
                    ImVec2(arrowCX, arrowCY + 3.0f), Pal(0.55f, 0.62f, 0.70f));
                // Text — 14px body
                const char* txt = Options::Misc::ProcessName;
                if (txt[0] == '\0') txt = "Select a process...";
                ImU32 txtCol = Options::Misc::ProcessName[0]
                    ? Pal(0.80f, 0.86f, 0.92f) : Pal(0.40f, 0.48f, 0.55f);
                if (l_Font_Body)
                    dl->AddText(l_Font_Body, 0, ImVec2(inpMin.x + 10.0f, inpMin.y + (32.0f - l_Font_Body->FontSize) * 0.5f),
                        txtCol, txt);
                else
                    dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(inpMin.x + 10.0f, inpMin.y + (32.0f - 14.0f) * 0.5f),
                        txtCol, txt);
                // Click to expand presets
                static bool l_ProcDropdown = false;
                ImGui::SetCursorScreenPos(inpMin);
                if (ImGui::InvisibleButton("##procname", ImVec2(cs.x - 32.0f, 32.0f)))
                    l_ProcDropdown = !l_ProcDropdown;
                if (!ImGui::IsMouseHoveringRect(inpMin, ImVec2(inpMax.x + 16.0f, l_ProcDropdown ? inpMax.y + ProcessPresetCount * 26.0f + 4.0f : inpMax.y))
                    && ImGui::IsMouseClicked(0))
                    l_ProcDropdown = false;

                if (l_ProcDropdown)
                {
                    float ddY = inpMax.y + 4.0f;
                    ImVec2 ddMin(inpMin.x, ddY);
                    float ddW = inpMax.x - inpMin.x;
                    float ddH = ProcessPresetCount * 26.0f + 6.0f;
                    float maxDY = c1m.y + c1H - 4.0f;
                    if (ddY + ddH > maxDY) ddH = maxDY - ddY;
                    ImVec2 ddMax(ddMin.x + ddW, ddMin.y + ddH);
                    // Shadow
                    dl->AddRectFilled(ImVec2(ddMin.x + 1.0f, ddMin.y + 2.0f), ImVec2(ddMax.x + 1.0f, ddMax.y + 3.0f),
                        Pal(0.0f, 0.0f, 0.0f, 0.15f), 6.0f);
                    dl->AddRectFilled(ImVec2(ddMin.x + 2.0f, ddMin.y + 1.0f), ImVec2(ddMax.x + 2.0f, ddMax.y + 2.0f),
                        Pal(0.0f, 0.0f, 0.0f, 0.08f), 6.0f);
                    // Solid bg — #141D2B
                    dl->AddRectFilled(ddMin, ddMax, Pal(0.078f, 0.114f, 0.169f), 6.0f);
                    dl->AddRect(ddMin, ddMax, Pal(0.165f, 0.235f, 0.333f), 6.0f, 0, 1.0f);
                    for (int p = 0; p < ProcessPresetCount; p++)
                    {
                        float py = ddY + 3.0f + p * 26.0f;
                        ImVec2 pMin(inpMin.x + 3.0f, py);
                        ImVec2 pMax(inpMin.x + ddW - 3.0f, py + 24.0f);
                        if (py + 24.0f > maxDY) break;
                        bool hov = ImGui::IsMouseHoveringRect(pMin, pMax);
                        bool sel = strcmp(Options::Misc::ProcessName, ProcessPresets[p]) == 0;
                        if (hov) dl->AddRectFilled(pMin, pMax, Pal(0.118f, 0.655f, 1.0f, 0.10f), 4.0f);
                        if (sel) {
                            dl->AddRectFilled(ImVec2(pMin.x, pMin.y + 4.0f),
                                ImVec2(pMin.x + 2.5f, pMin.y + 20.0f), Pal(0.118f, 0.655f, 1.0f));
                        }
                        if (l_Font_Body)
                            dl->AddText(l_Font_Body, 0, ImVec2(pMin.x + 10.0f, pMin.y + (24.0f - l_Font_Body->FontSize) * 0.5f),
                                sel ? Pal(0.118f, 0.655f, 1.0f) : Pal(0.604f, 0.659f, 0.737f), ProcessPresets[p]);
                        else
                            dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(pMin.x + 10.0f, pMin.y + (24.0f - 13.0f) * 0.5f),
                                sel ? Pal(0.118f, 0.655f, 1.0f) : Pal(0.604f, 0.659f, 0.737f), ProcessPresets[p]);
                        ImGui::SetCursorScreenPos(pMin);
                        char pID[32]; sprintf_s(pID, "##pp_%d", p);
                        if (ImGui::InvisibleButton(pID, ImVec2(ddW - 6.0f, 24.0f)))
                        {
                            strncpy_s(Options::Misc::ProcessName, ProcessPresets[p], sizeof(Options::Misc::ProcessName) - 1);
                            l_ProcDropdown = false;
                        }
                    }
                }
            }

            // Preset chips — 12px small font
            {
                float chy = c1m.y + 122.0f;
                if (l_Font_Small)
                    dl->AddText(l_Font_Small, 0, ImVec2(c1m.x + 16.0f, chy), Pal(0.55f, 0.62f, 0.70f), "Quick Presets");
                else
                    dl->AddText(ImGui::GetFont(), 12.0f, ImVec2(c1m.x + 16.0f, chy), Pal(0.55f, 0.62f, 0.70f), "Quick Presets");
                float cx = c1m.x + 16.0f, cy = chy + 14.0f;
                for (int i = 0; i < ProcessPresetCount; i++)
                {
                    ImVec2 bs = ImGui::CalcTextSize(ProcessPresets[i]);
                    float pW = bs.x + 14.0f, pH = 22.0f;
                    if (cx + pW > c1M.x - 16.0f) break;
                    bool hov = ImGui::IsMouseHoveringRect(ImVec2(cx, cy), ImVec2(cx + pW, cy + pH));
                    bool sel = strcmp(Options::Misc::ProcessName, ProcessPresets[i]) == 0;
                    ImU32 bg = sel ? Pal(0.118f, 0.655f, 1.0f, 0.15f) : Pal(0.04f, 0.06f, 0.09f);
                    dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + pW, cy + pH), bg, 11.0f);
                    if (sel)
                        dl->AddRect(ImVec2(cx, cy), ImVec2(cx + pW, cy + pH),
                            Pal(0.0f, 0.659f, 1.0f, 0.4f), 11.0f, 0, 1.0f);
                    if (l_Font_Small)
                        dl->AddText(l_Font_Small, 0, ImVec2(cx + 7.0f, cy + (pH - l_Font_Small->FontSize) * 0.5f),
                            sel ? Pal(0.118f, 0.655f, 1.0f) : Pal(0.55f, 0.62f, 0.70f), ProcessPresets[i]);
                    else
                        dl->AddText(ImGui::GetFont(), 12.0f, ImVec2(cx + 7.0f, cy + (pH - 12.0f) * 0.5f),
                            sel ? Pal(0.118f, 0.655f, 1.0f) : Pal(0.55f, 0.62f, 0.70f), ProcessPresets[i]);
                    ImGui::SetCursorScreenPos(ImVec2(cx, cy));
                    char cID[32]; sprintf_s(cID, "##pc_%d", i);
                    if (ImGui::InvisibleButton(cID, ImVec2(pW, pH)))
                        strncpy_s(Options::Misc::ProcessName, ProcessPresets[i], sizeof(Options::Misc::ProcessName) - 1);
                    cx += pW + 5.0f;
                }
            }

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + c1H + 8.0f);

            // Card: Visibility
            ImVec2 c2m = ImGui::GetCursorScreenPos();
            float c2H = 70.0f;
            ImVec2 c2M(c2m.x + cs.x, c2m.y + c2H);
            DrawCard(dl, c2m, c2M, 10.0f);
            DrawSectionHeader(dl, ImVec2(c2m.x + 16.0f, c2m.y + 12.0f), "Visibility", 1);

            // Hide from Taskbar — 13px label
            {
                float ty = c2m.y + 36.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(c2m.x + 16.0f, ty + (26.0f - l_Font_Label->FontSize) * 0.5f),
                        Pal(0.80f, 0.86f, 0.92f), "Hide from Taskbar");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(c2m.x + 16.0f, ty + (26.0f - 13.0f) * 0.5f),
                        Pal(0.80f, 0.86f, 0.92f), "Hide from Taskbar");
                DrawSwitch(dl, ImVec2(c2M.x - 48.0f, ty + (26.0f - 18.0f) * 0.5f),
                    40.0f, 18.0f, &Options::Misc::HideFromTabs, 1);
                ImGui::SetCursorScreenPos(ImVec2(c2m.x + 16.0f, ty));
                if (ImGui::InvisibleButton("##hft", ImVec2(cs.x - 32.0f, 26.0f)))
                    Options::Misc::HideFromTabs = !Options::Misc::HideFromTabs;
            }

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + c2H + 8.0f);

            // Card: Stream Protection
            ImVec2 c3m = ImGui::GetCursorScreenPos();
            float c3H = 48.0f;
            ImVec2 c3M(c3m.x + cs.x, c3m.y + c3H);
            DrawCard(dl, c3m, c3M, 10.0f);
            DrawSectionHeader(dl, ImVec2(c3m.x + 16.0f, c3m.y + 12.0f), "Stream Protection", 2);

            {
                float ty = c3m.y + 12.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(c3m.x + 32.0f, ty + (36.0f - l_Font_Label->FontSize) * 0.5f),
                        Pal(0.80f, 0.86f, 0.92f), "Stream Proof");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(c3m.x + 32.0f, ty + (36.0f - 13.0f) * 0.5f),
                        Pal(0.80f, 0.86f, 0.92f), "Stream Proof");
                DrawSwitch(dl, ImVec2(c3M.x - 48.0f, ty + (36.0f - 18.0f) * 0.5f),
                    40.0f, 18.0f, &Options::Misc::StreamProof, 2);
                ImGui::SetCursorScreenPos(ImVec2(c3m.x + 32.0f, ty));
                if (ImGui::InvisibleButton("##sp", ImVec2(cs.x - 64.0f, 36.0f)))
                    Options::Misc::StreamProof = !Options::Misc::StreamProof;
            }

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + c3H + 8.0f);

            // Card: Exclusions
            ImVec2 c4m = ImGui::GetCursorScreenPos();
            float c4H = 82.0f;
            ImVec2 c4M(c4m.x + cs.x, c4m.y + c4H);
            DrawCard(dl, c4m, c4M, 10.0f);
            DrawSectionHeader(dl, ImVec2(c4m.x + 16.0f, c4m.y + 12.0f), "Exclusions", 3);

            {
                float iy = c4m.y + 36.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(c4m.x + 16.0f, iy), Pal(0.55f, 0.62f, 0.70f), "Exclusion Path");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(c4m.x + 16.0f, iy), Pal(0.55f, 0.62f, 0.70f), "Exclusion Path");
                ImVec2 inpMin(c4m.x + 16.0f, iy + 18.0f);
                ImVec2 inpMax(inpMin.x + cs.x - 32.0f, inpMin.y + 30.0f);
                dl->AddRectFilled(inpMin, inpMax, Pal(0.035f, 0.055f, 0.075f), 5.0f);
                dl->AddRect(inpMin, inpMax, Pal(0.165f, 0.235f, 0.333f), 5.0f, 0, 1.0f);
                const char* exTxt = Options::Misc::ExclusionPath;
                if (exTxt[0] == '\0') exTxt = "e.g. C:\\Program Files\\Game";
                ImU32 extCol = Options::Misc::ExclusionPath[0]
                    ? Pal(0.80f, 0.86f, 0.92f) : Pal(0.40f, 0.48f, 0.55f);
                if (l_Font_Body)
                    dl->AddText(l_Font_Body, 0, ImVec2(inpMin.x + 10.0f, inpMin.y + (30.0f - l_Font_Body->FontSize) * 0.5f),
                        extCol, exTxt);
                else
                    dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(inpMin.x + 10.0f, inpMin.y + (30.0f - 14.0f) * 0.5f),
                        extCol, exTxt);
                ImGui::SetCursorScreenPos(inpMin);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.86f, 0.92f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.40f, 0.48f, 0.55f, 1.0f));
                ImGui::PushItemWidth(inpMax.x - inpMin.x);
                ImGui::InputText("##exclpath", Options::Misc::ExclusionPath, sizeof(Options::Misc::ExclusionPath));
                ImGui::PopItemWidth();
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
            }
        }

        // ── THEME TAB ─────────────────────────────────────────────
        else if (l_SelectedTab == 1)
        {
            ImVec2 ct1m = ImGui::GetCursorScreenPos();
            float ct1H = cs.y - 4.0f;
            ImVec2 ct1M(ct1m.x + cs.x, ct1m.y + ct1H);
            DrawCard(dl, ct1m, ct1M, 10.0f);
            DrawSectionHeader(dl, ImVec2(ct1m.x + 16.0f, ct1m.y + 12.0f), "Color Theme", -1);

            const float cardW = (cs.x - 40.0f) * 0.5f;
            const float cardH = 48.0f;
            float gy = ct1m.y + 38.0f;
            int colCount = 0;

            for (int i = 0; i < ThemeCount; i++)
            {
                bool sel = (Options::Misc::MenuTheme == i);
                float gx = ct1m.x + 16.0f + (colCount % 2) * (cardW + 8.0f);
                ImVec2 cardMin(gx, gy + (colCount / 2) * (cardH + 6.0f));
                ImVec2 cardMax(cardMin.x + cardW, cardMin.y + cardH);

                ImU32 bg = sel ? Pal(0.0f, 0.659f, 1.0f, 0.10f) : Pal(0.035f, 0.055f, 0.075f);
                dl->AddRectFilled(cardMin, cardMax, bg, 7.0f);
                ImU32 border = sel ? Pal(0.0f, 0.659f, 1.0f, 0.5f) : Pal(0.141f, 0.200f, 0.278f);
                dl->AddRect(cardMin, cardMax, border, 7.0f, 0, sel ? 1.5f : 1.0f);

                if (l_Font_Body)
                    dl->AddText(l_Font_Body, 0, ImVec2(cardMin.x + 9.0f, cardMin.y + 6.0f),
                        sel ? Pal(0.0f, 0.659f, 1.0f) : Pal(0.85f, 0.90f, 0.95f), Themes[i].name);
                else
                    dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(cardMin.x + 9.0f, cardMin.y + 6.0f),
                        sel ? Pal(0.0f, 0.659f, 1.0f) : Pal(0.85f, 0.90f, 0.95f), Themes[i].name);

                float sx = cardMin.x + 9.0f, sy = cardMin.y + 26.0f;
                for (int c = 0; c < 4; c++) {
                    ImU32 sw = Pal(Themes[i].bg[0], Themes[i].bg[1], Themes[i].bg[2]);
                    if (c == 1) sw = Pal(Themes[i].panel[0], Themes[i].panel[1], Themes[i].panel[2]);
                    else if (c == 2) sw = Pal(Themes[i].accent[0], Themes[i].accent[1], Themes[i].accent[2]);
                    else if (c == 3) sw = Pal(Themes[i].accent2[0], Themes[i].accent2[1], Themes[i].accent2[2]);
                    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + 12.0f, sy + 12.0f), sw, 2.0f);
                    dl->AddRect(ImVec2(sx, sy), ImVec2(sx + 12.0f, sy + 12.0f),
                        Pal(0.06f, 0.10f, 0.15f), 2.0f, 0, 0.5f);
                    sx += 15.5f;
                }

                if (Themes[i].gradient) {
                    ImVec2 gp(cardMax.x - 28.0f, cardMax.y - 13.0f);
                    dl->AddRectFilled(gp, ImVec2(gp.x + 20.0f, gp.y + 3.0f),
                        Pal(Themes[i].accent[0], Themes[i].accent[1], Themes[i].accent[2]), 2.0f);
                    dl->AddRectFilled(ImVec2(gp.x + 7.0f, gp.y), ImVec2(gp.x + 20.0f, gp.y + 3.0f),
                        Pal(Themes[i].accent2[0], Themes[i].accent2[1], Themes[i].accent2[2]), 2.0f);
                }

                ImGui::SetCursorScreenPos(cardMin);
                char tid[32]; sprintf_s(tid, "##th_%d", i);
                if (ImGui::InvisibleButton(tid, ImVec2(cardW, cardH)))
                    Options::Misc::MenuTheme = i;
                if (ImGui::IsItemHovered() && !sel)
                    dl->AddRectFilled(cardMin, cardMax, Pal(0.06f, 0.10f, 0.15f, 0.25f), 7.0f);

                colCount++;
            }
        }

                // ── FONT TAB ──────────────────────────────────────────────
        else if (l_SelectedTab == 2)
        {
            ImVec2 cf1m = ImGui::GetCursorScreenPos();
            float cf1H = cs.y - 4.0f;
            ImVec2 cf1M(cf1m.x + cs.x, cf1m.y + cf1H);
            DrawCard(dl, cf1m, cf1M, 10.0f);
            DrawSectionHeader(dl, ImVec2(cf1m.x + 16.0f, cf1m.y + 12.0f), "Menu Font", -1);

            // Scale slider
            {
                float sy = cf1m.y + 38.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(cf1m.x + 16.0f, sy), Pal(0.80f, 0.86f, 0.92f), "Scale");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(cf1m.x + 16.0f, sy), Pal(0.80f, 0.86f, 0.92f), "Scale");
                ImGui::SetCursorScreenPos(ImVec2(cf1m.x + 16.0f, sy + 18.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.035f, 0.055f, 0.075f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.0f, 0.659f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.22f, 0.80f, 1.0f, 1.0f));
                ImGui::PushItemWidth(cs.x - 32.0f);
                ImGui::SliderFloat("##scale", &Options::Misc::MenuScale, 0.6f, 2.0f, "%.1fx");
                ImGui::PopItemWidth();
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
            }

            // Font list
            {
                float ly = cf1m.y + 92.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(cf1m.x + 16.0f, ly), Pal(0.55f, 0.62f, 0.70f), "Select Font");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(cf1m.x + 16.0f, ly), Pal(0.55f, 0.62f, 0.70f), "Select Font");
                float fy = ly + 18.0f;
                for (int i = 0; i < LoaderFontCount && i < FontNameCount; i++)
                {
                    bool sel = (Options::Misc::MenuFont == i);
                    ImVec2 mn(cf1m.x + 16.0f, fy);
                    ImVec2 mx(mn.x + cs.x - 32.0f, mn.y + 32.0f);
                    ImU32 bg = sel ? Pal(0.0f, 0.659f, 1.0f, 0.10f) : Pal(0.035f, 0.055f, 0.075f);
                    dl->AddRectFilled(mn, mx, bg, 5.0f);
                    if (sel) {
                        dl->AddRect(mn, mx, Pal(0.0f, 0.659f, 1.0f, 0.4f), 5.0f, 0, 1.5f);
                        dl->AddRectFilled(ImVec2(mn.x + 2.0f, mn.y + 6.0f),
                            ImVec2(mn.x + 4.0f, mn.y + 26.0f), Pal(0.0f, 0.659f, 1.0f));
                    }
                    if (l_Font_Body)
                        dl->AddText(l_Font_Body, 0, ImVec2(mn.x + 12.0f, mn.y + (32.0f - l_Font_Body->FontSize) * 0.5f),
                            sel ? Pal(0.0f, 0.659f, 1.0f) : Pal(0.80f, 0.86f, 0.92f), FontNames[i]);
                    else
                        dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(mn.x + 12.0f, mn.y + (32.0f - 14.0f) * 0.5f),
                            sel ? Pal(0.0f, 0.659f, 1.0f) : Pal(0.80f, 0.86f, 0.92f), FontNames[i]);
                    ImGui::SetCursorScreenPos(mn);
                    char fid[32]; sprintf_s(fid, "##fn_%d", i);
                    if (ImGui::InvisibleButton(fid, ImVec2(cs.x - 32.0f, 32.0f)))
                        Options::Misc::MenuFont = i;
                    if (ImGui::IsItemHovered() && !sel)
                        dl->AddRectFilled(mn, mx, Pal(0.06f, 0.10f, 0.15f, 0.25f), 5.0f);
                    fy += 36.0f;
                }
            }
        }

        // ── CONFIG TAB ────────────────────────────────────────────
        else if (l_SelectedTab == 3)
        {
            ImVec2 cc1m = ImGui::GetCursorScreenPos();
            float cc1H = cs.y - 4.0f;
            ImVec2 cc1M(cc1m.x + cs.x, cc1m.y + cc1H);
            DrawCard(dl, cc1m, cc1M, 10.0f);
            DrawSectionHeader(dl, ImVec2(cc1m.x + 16.0f, cc1m.y + 12.0f), "Configuration", -1);

            // Autoload toggle — 13px label
            {
                float ty = cc1m.y + 36.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(cc1m.x + 16.0f, ty + (26.0f - l_Font_Label->FontSize) * 0.5f),
                        Pal(0.80f, 0.86f, 0.92f), "Autoload on Start");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(cc1m.x + 16.0f, ty + (26.0f - 13.0f) * 0.5f),
                        Pal(0.80f, 0.86f, 0.92f), "Autoload on Start");
                DrawSwitch(dl, ImVec2(cc1M.x - 48.0f, ty + (26.0f - 18.0f) * 0.5f),
                    40.0f, 18.0f, &Options::Loader::AttachOnStart, 3);
                ImGui::SetCursorScreenPos(ImVec2(cc1m.x + 16.0f, ty));
                if (ImGui::InvisibleButton("##autoload", ImVec2(cs.x - 32.0f, 26.0f)))
                    Options::Loader::AttachOnStart = !Options::Loader::AttachOnStart;
            }

            // Config list
            {
                float ly = cc1m.y + 74.0f;
                if (l_Font_Label)
                    dl->AddText(l_Font_Label, 0, ImVec2(cc1m.x + 16.0f, ly), Pal(0.55f, 0.62f, 0.70f), "Autoload Config");
                else
                    dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(cc1m.x + 16.0f, ly), Pal(0.55f, 0.62f, 0.70f), "Autoload Config");
                if (ConfigFiles.empty()) ScanConfigs();

                float listY = ly + 18.0f;
                float listH = cc1M.y - listY - 10.0f;
                ImGui::SetCursorScreenPos(ImVec2(cc1m.x + 16.0f, listY));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3, 3));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.055f, 0.075f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.06f, 0.09f, 0.14f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.0f, 0.659f, 1.0f, 0.4f));

                ImGui::BeginChild("##config_list", ImVec2(-1, listH), false);
                ImDrawList* cdl = ImGui::GetWindowDrawList();
                ImVec2 cwp = ImGui::GetWindowPos();
                ImVec2 cws = ImGui::GetWindowSize();
                float innerW = cws.x - 6.0f;

                for (size_t i = 0; i < ConfigFiles.size(); i++)
                {
                    bool sel = (ConfigFiles[i] == Options::Loader::AutoloadConfig);
                    ImVec2 mn = ImGui::GetCursorScreenPos();
                    float itemH = 28.0f;
                    ImVec2 mx(mn.x + innerW, mn.y + itemH);
                    bool hov = ImGui::IsMouseHoveringRect(mn, mx);
                    ImU32 bg = sel ? Pal(0.0f, 0.659f, 1.0f, 0.10f)
                        : hov ? Pal(0.045f, 0.065f, 0.090f)
                        : Pal(0.030f, 0.048f, 0.068f);
                    cdl->AddRectFilled(mn, mx, bg, 4.0f);
                    if (sel) {
                        cdl->AddRectFilled(ImVec2(mn.x + 2.0f, mn.y + 5.0f),
                            ImVec2(mn.x + 4.0f, mn.y + itemH - 5.0f), Pal(0.0f, 0.659f, 1.0f));
                    }
                    if (l_Font_Body)
                        cdl->AddText(l_Font_Body, 0, ImVec2(mn.x + 10.0f, mn.y + (itemH - l_Font_Body->FontSize) * 0.5f),
                            sel ? Pal(0.0f, 0.659f, 1.0f) : Pal(0.78f, 0.84f, 0.90f),
                            ConfigFiles[i].c_str());
                    else
                        cdl->AddText(ImGui::GetFont(), 14.0f, ImVec2(mn.x + 10.0f, mn.y + (itemH - 14.0f) * 0.5f),
                            sel ? Pal(0.0f, 0.659f, 1.0f) : Pal(0.78f, 0.84f, 0.90f),
                            ConfigFiles[i].c_str());
                    ImGui::SetCursorScreenPos(mn);
                    char cid[32]; sprintf_s(cid, "##cfg_%d", (int)i);
                    if (ImGui::InvisibleButton(cid, ImVec2(innerW, itemH)))
                    {
                        if (ConfigFiles[i] == "(None)")
                            Options::Loader::AutoloadConfig[0] = '\0';
                        else
                            strncpy_s(Options::Loader::AutoloadConfig, ConfigFiles[i].c_str(),
                                sizeof(Options::Loader::AutoloadConfig) - 1);
                    }
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar(2);
            }
        }

        PopBodyFont();
        ImGui::PopStyleVar(1);
        ImGui::EndChild();

        // ── Inject button ──────────────────────────────────────────
        {
            float btnW = ws.x - contentPad * 2;
            ImVec2 btnMin(wp.x + contentPad, wp.y + ws.y - btnH - btnPad);
            ImVec2 btnMax(btnMin.x + btnW, btnMin.y + btnH);
            bool hov = ImGui::IsMouseHoveringRect(btnMin, btnMax);

            float targetGlow = hov ? 1.0f : 0.0f;
            g_InjectGlow = LerpF(g_InjectGlow, targetGlow, dt * 10.0f);

            // Shadow
            dl->AddRectFilled(ImVec2(btnMin.x + 2.0f, btnMin.y + 2.0f), ImVec2(btnMax.x + 2.0f, btnMax.y + 3.0f),
                Pal(0.0f, 0.0f, 0.0f, 0.12f), 10.0f);
            dl->AddRectFilled(ImVec2(btnMin.x + 1.0f, btnMin.y + 1.0f), ImVec2(btnMax.x + 1.0f, btnMax.y + 2.0f),
                Pal(0.0f, 0.0f, 0.0f, 0.06f), 10.0f);

            // Button bg with gradient — subtle blue
            float g = g_InjectGlow;
            // Gradient: slightly brighter top, slightly darker bottom
            {
                int bSteps = 8;
                float br1 = 0.01f, bg1 = 0.30f + 0.06f * g, bb1 = 0.45f + 0.08f * g; // top
                float br2 = 0.005f, bg2 = 0.25f + 0.04f * g, bb2 = 0.38f + 0.06f * g; // bottom
                for (int bi = 0; bi < bSteps; bi++) {
                    float bt = (float)bi / (float)bSteps;
                    float by0 = btnMin.y + btnH * bt;
                    float by1 = btnMin.y + btnH * (bt + 1.0f / (float)bSteps);
                    float r = br1 + (br2 - br1) * bt;
                    float gg = bg1 + (bg2 - bg1) * bt;
                    float b = bb1 + (bb2 - bb1) * bt;
                    dl->AddRectFilled(ImVec2(btnMin.x, by0), ImVec2(btnMax.x, by1), Pal(r, gg, b), 10.0f);
                    if (bi > 0 && bi < bSteps - 1)
                        dl->AddRectFilled(ImVec2(btnMin.x, by0), ImVec2(btnMax.x, by1), Pal(r, gg, b));
                }
            }

            // Subtle glow halo
            if (g_InjectGlow > 0.01f) {
                dl->AddRectFilled(ImVec2(btnMin.x + 3.0f, btnMin.y - 3.0f),
                    ImVec2(btnMax.x - 3.0f, btnMax.y + 3.0f),
                    Pal(0.118f, 0.655f, 1.0f, 0.035f * g), 10.0f);
            }

            // Border
            float bAlpha = 0.20f + 0.18f * g;
            dl->AddRect(btnMin, btnMax, Pal(0.118f, 0.655f, 1.0f, bAlpha), 10.0f, 0, 1.5f);

            // Label — 13px SemiBold
            if (l_Font_Button) {
                ImVec2 ts = l_Font_Button->CalcTextSizeA(l_Font_Button->FontSize, FLT_MAX, -1.0f, "INJECT");
                dl->AddText(l_Font_Button, 0,
                    ImVec2(btnMin.x + (btnW - ts.x) * 0.5f, btnMin.y + (btnH - ts.y) * 0.5f),
                    Pal(0.961f, 0.969f, 0.980f), "INJECT");
            } else {
                PushBodyFont();
                ImVec2 ts = ImGui::CalcTextSize("INJECT");
                dl->AddText(ImGui::GetFont(), 13.0f,
                    ImVec2(btnMin.x + (btnW - ts.x) * 0.5f, btnMin.y + (btnH - ts.y) * 0.5f),
                    Pal(0.961f, 0.969f, 0.980f), "INJECT");
                PopBodyFont();
            }

            // Click
            if (hov && ImGui::IsMouseClicked(0))
                l_Inject = true;
        }

        ImGui::PopStyleColor(); // WindowBg
        ImGui::PopStyleVar(4);  // padding, rounding, border, alpha
        ImGui::End();
    }

    // ── Main entry ─────────────────────────────────────────────────────
    static bool Run()
    {
        // Window class with drop shadow
        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC | CS_DROPSHADOW, WndProc, 0, 0,
            GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
            L"SeraphLoader", nullptr };
        RegisterClassExW(&wc);

        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        int ww = 480, wh = 580;
        l_Hwnd = CreateWindowW(wc.lpszClassName, L"Seraph", WS_POPUP | WS_VISIBLE,
            (sw - ww) / 2, (sh - wh) / 2, ww, wh, nullptr, nullptr, wc.hInstance, nullptr);

        // Enable rounded corners on Windows 11 (silently ignored on older builds)
        const DWORD WCA_ROUND_CORNER = 33;
        const DWORD DWMWCP_ROUND = 2;
        if (l_Hwnd) {
            HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
            if (hDwm) {
                auto pDwmSet = (HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD))
                    GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (pDwmSet) {
                    DWORD val = DWMWCP_ROUND;
                    pDwmSet(l_Hwnd, WCA_ROUND_CORNER, &val, sizeof(val));
                }
                FreeLibrary(hDwm);
            }
        }

        if (!LoaderCreateDevice(l_Hwnd)) { LoaderCleanup(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return false; }

        ShowWindow(l_Hwnd, SW_SHOWDEFAULT);
        UpdateWindow(l_Hwnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().IniFilename = nullptr;

        ImGui::StyleColorsDark();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 0; s.FrameRounding = 5; s.GrabRounding = 3;
        s.ChildRounding = 8; s.ScrollbarRounding = 4; s.ScrollbarSize = 3;
        s.WindowPadding = ImVec2(0, 0); s.FramePadding = ImVec2(8, 5); s.ItemSpacing = ImVec2(8, 5);
        s.PopupRounding = 6;

        // Loader palette — premium blue-black gradient
        s.Colors[ImGuiCol_WindowBg]             = PalV(0.067f, 0.094f, 0.153f);  // #111827
        s.Colors[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_Border]               = PalV(0.149f, 0.212f, 0.302f);  // #26364D
        s.Colors[ImGuiCol_Text]                 = PalV(0.961f, 0.969f, 0.980f);  // #F5F7FA
        s.Colors[ImGuiCol_TextDisabled]         = PalV(0.604f, 0.659f, 0.737f);  // #9AA8BC
        s.Colors[ImGuiCol_FrameBg]              = PalV(0.035f, 0.055f, 0.075f);
        s.Colors[ImGuiCol_FrameBgHovered]       = PalV(0.045f, 0.065f, 0.090f);
        s.Colors[ImGuiCol_FrameBgActive]        = PalV(0.0f, 0.18f, 0.28f);
        s.Colors[ImGuiCol_SliderGrab]           = PalV(0.118f, 0.655f, 1.0f);    // #1EA7FF
        s.Colors[ImGuiCol_SliderGrabActive]     = PalV(0.165f, 0.706f, 1.0f);    // #2AB4FF
        s.Colors[ImGuiCol_CheckMark]            = PalV(0.118f, 0.655f, 1.0f);    // #1EA7FF
        s.Colors[ImGuiCol_Header]               = PalV(0.0f, 0.18f, 0.28f, 0.3f);
        s.Colors[ImGuiCol_HeaderHovered]        = PalV(0.0f, 0.18f, 0.28f, 0.4f);
        s.Colors[ImGuiCol_HeaderActive]         = PalV(0.0f, 0.18f, 0.28f, 0.5f);
        s.Colors[ImGuiCol_Separator]            = PalV(0.04f, 0.07f, 0.11f);
        s.Colors[ImGuiCol_Button]               = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ButtonHovered]        = PalV(0.0f, 0.18f, 0.28f, 0.15f);
        s.Colors[ImGuiCol_ButtonActive]         = PalV(0.0f, 0.18f, 0.28f, 0.25f);
        s.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ScrollbarGrab]        = PalV(0.06f, 0.09f, 0.13f, 0.8f);
        s.Colors[ImGuiCol_ScrollbarGrabHovered] = PalV(0.118f, 0.655f, 1.0f, 0.4f);
        s.Colors[ImGuiCol_PopupBg]              = PalV(0.078f, 0.114f, 0.169f, 0.98f); // #141D2B

        // ── Load fonts BEFORE backends (atlas must contain fonts when built) ──
        {
            ImFontConfig fc;
            fc.MergeMode = false;
            fc.PixelSnapH = false;
            fc.OversampleH = 2;
            fc.OversampleV = 2;

            auto LoadSys = [&](const char* path, float size) -> ImFont* {
                return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &fc,
                    ImGui::GetIO().Fonts->GetGlyphRangesDefault());
            };

            // Hierarchy fonts — premium Segoe UI
            l_Font_Display  = LoadSys("C:\\Windows\\Fonts\\segoeuib.ttf", 22.0f);
            l_Font_Section  = LoadSys("C:\\Windows\\Fonts\\segoeuib.ttf", 14.0f);
            l_Font_Label    = LoadSys("C:\\Windows\\Fonts\\segoeui.ttf",  12.0f);
            l_Font_Body     = LoadSys("C:\\Windows\\Fonts\\segoeui.ttf",  13.0f);
            l_Font_Small    = LoadSys("C:\\Windows\\Fonts\\segoeui.ttf",  11.0f);
            l_Font_Button   = LoadSys("C:\\Windows\\Fonts\\segoeuib.ttf", 13.0f);

            struct { const char* path; float size; } fontPaths[] = {
                { "C:\\Windows\\Fonts\\verdana.ttf",  13.0f },
                { "C:\\Windows\\Fonts\\segoeui.ttf",  13.0f },
                { "C:\\Windows\\Fonts\\tahoma.ttf",   13.0f },
                { "C:\\Windows\\Fonts\\arial.ttf",    13.0f },
                { "C:\\Windows\\Fonts\\georgia.ttf",  13.0f },
                { "C:\\Windows\\Fonts\\calibri.ttf",  13.0f },
                { "C:\\Windows\\Fonts\\consola.ttf",  13.0f },
            };

            LoaderFontCount = 0;
            for (int i = 0; i < 7; i++) {
                ImFont* f = LoadSys(fontPaths[i].path, fontPaths[i].size);
                if (f && LoaderFontCount < 7) {
                    LoaderFonts[LoaderFontCount].font = f;
                    LoaderFonts[LoaderFontCount].path = fontPaths[i].path;
                    LoaderFonts[LoaderFontCount].name = FontNames[i];
                    LoaderFontCount++;
                }
            }

            // Set default font
            if (Options::Misc::MenuFont < 0 || Options::Misc::MenuFont >= LoaderFontCount)
                Options::Misc::MenuFont = 0;
            if (LoaderFontCount > 0 && LoaderFonts[Options::Misc::MenuFont].font)
                ImGui::GetIO().FontDefault = LoaderFonts[Options::Misc::MenuFont].font;
        }

        ImGui_ImplWin32_Init(l_Hwnd);
        ImGui_ImplDX11_Init(l_Device, l_Context);

        ScanConfigs();

        l_Running = true; l_Inject = false;
        l_Splash = true;
        l_SplashTimer = 0.0f;
        l_SplashFade = 0.0f;
        l_LogoPulse = 0.0f;
        MSG msg = {};
        while (l_Running && !l_Inject)
        {
            while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT) l_Running = false;
            }
            if (!l_Running) break;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            if (l_Splash)
                RenderSplashScreen();
            else
                RenderUI();
            ImGui::Render();
            const float cc[4] = { 0.067f, 0.094f, 0.153f, 1.0f }; // #111827
            l_Context->OMSetRenderTargets(1, &l_RTV, nullptr);
            l_Context->ClearRenderTargetView(l_RTV, cc);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            l_SwapChain->Present(1, 0);
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        LoaderCleanup();
        DestroyWindow(l_Hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return l_Inject;
    }
}
