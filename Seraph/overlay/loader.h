#pragma once
#ifdef _MSC_VER
#pragma warning(disable: 26812)
#endif

#include <windows.h>
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

    static float LerpF(float a, float b, float t) { return a + (b - a) * t; }
    static float ClampF(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
    static float Pulse() { return (sinf(g_Time * 2.5f) + 1.0f) * 0.5f; }

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

    // ── Push/pop current font ──────────────────────────────────────────
    static void PushLoaderFont()
    {
        if (Options::Misc::MenuFont >= 0 && Options::Misc::MenuFont < LoaderFontCount
            && LoaderFonts[Options::Misc::MenuFont].font)
            ImGui::PushFont(LoaderFonts[Options::Misc::MenuFont].font);
    }
    static void PopLoaderFont()
    {
        if (Options::Misc::MenuFont >= 0 && Options::Misc::MenuFont < LoaderFontCount
            && LoaderFonts[Options::Misc::MenuFont].font)
            ImGui::PopFont();
    }

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
            ? Pal(LerpF(0.12f, 0.0f, anim), LerpF(0.15f, 0.682f, anim), LerpF(0.18f, 1.0f, anim))
            : Pal(0.10f, 0.12f, 0.15f);
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, radius);
        float knobR = radius - 2.5f;
        float knobX = on ? pos.x + w - knobR - 2.5f : pos.x + knobR + 2.5f;
        dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), knobR,
            Pal(LerpF(0.5f, 1.0f, anim), LerpF(0.5f, 1.0f, anim), LerpF(0.5f, 1.0f, anim)));
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
        ImGui::PushStyleColor(ImGuiCol_WindowBg, PalV(0.010f, 0.018f, 0.030f, 1.0f));

        ImGui::Begin("##Loader", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float pulse = Pulse();
        float slideY = g_EntranceSlide;

        // ── Background gradient overlay ────────────────────────────
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
            Pal(0.008f, 0.014f, 0.024f));

        // ── Header ─────────────────────────────────────────────────
        float headerH = 68.0f;
        ImVec2 hdrMin(wp.x, wp.y);
        ImVec2 hdrMax(wp.x + ws.x, wp.y + headerH);

        // Header bg with subtle gradient
        dl->AddRectFilled(hdrMin, hdrMax, Pal(0.014f, 0.026f, 0.044f));

        // Top accent line (gradient)
        float accentW = ws.x * ClampF(0.6f + pulse * 0.1f, 0.5f, 0.7f);
        ImU32 ac1 = Pal(0.0f, 0.682f, 1.0f);
        ImU32 ac2 = Pal(0.41f, 0.71f, 1.0f);
        dl->AddRectFilled(wp, ImVec2(wp.x + accentW, wp.y + 2.5f), ac1, 0.0f);
        dl->AddRectFilled(ImVec2(wp.x + accentW, wp.y), ImVec2(wp.x + ws.x, wp.y + 2.5f), ac2, 0.0f);

        // Logo mark — stylised S with dove
        {
            float ox = wp.x + 20.0f + slideY;
            float oy = wp.y + 8.0f;
            float W = 18.0f, H = 40.0f;

            // S-curve via two quadratic bezier arcs
            float sW = 16.0f, sH = 36.0f;
            float sx = ox + (W - sW) * 0.5f;
            float sy = oy + 2.0f;
            float topY = sy;
            float midY = sy + sH * 0.5f;
            float botY = sy + sH;

            ImU32 pink = Pal(0.30f, 0.55f, 1.0f);
            float thick = 3.0f;

            // Top arc: starts right, curves left to center
            ImVec2 topStart(sx + sW, topY);
            ImVec2 topCtrl(sx - sW * 0.15f, topY + sH * 0.28f);
            ImVec2 topEnd(sx + sW * 0.5f, midY);

            // Bottom arc: starts center, curves right to left
            ImVec2 botStart(sx + sW * 0.5f, midY);
            ImVec2 botCtrl(sx + sW * 1.15f, midY + sH * 0.22f);
            ImVec2 botEnd(sx, botY);

            // Draw top arc
            int segs = 24;
            ImVec2 prev = topStart;
            for (int i = 1; i <= segs; i++) {
                float t = (float)i / (float)segs;
                float u = 1.0f - t;
                float px = u * u * topStart.x + 2.0f * u * t * topCtrl.x + t * t * topEnd.x;
                float py = u * u * topStart.y + 2.0f * u * t * topCtrl.y + t * t * topEnd.y;
                dl->AddLine(prev, ImVec2(px, py), pink, thick);
                prev = ImVec2(px, py);
            }

            // Draw bottom arc
            prev = botStart;
            for (int i = 1; i <= segs; i++) {
                float t = (float)i / (float)segs;
                float u = 1.0f - t;
                float px = u * u * botStart.x + 2.0f * u * t * botCtrl.x + t * t * botEnd.x;
                float py = u * u * botStart.y + 2.0f * u * t * botCtrl.y + t * t * botEnd.y;
                dl->AddLine(prev, ImVec2(px, py), pink, thick);
                prev = ImVec2(px, py);
            }

            // Dove — perched on top-right of the S
            {
                float dx = sx + sW + 2.0f, dy = sy - 4.0f;
                ImU32 white = Pal(0.96f, 0.97f, 1.0f);
                ImU32 whiteSoft = Pal(0.96f, 0.97f, 1.0f, 0.55f);

                // Wing (swept-back feathers)
                dl->AddTriangleFilled(
                    ImVec2(dx - 2.0f, dy + 4.0f),
                    ImVec2(dx - 10.0f, dy - 2.0f),
                    ImVec2(dx + 2.0f, dy + 6.5f), whiteSoft);
                dl->AddTriangleFilled(
                    ImVec2(dx - 1.0f, dy + 3.0f),
                    ImVec2(dx - 8.0f, dy - 4.5f),
                    ImVec2(dx + 3.0f, dy + 5.5f), white);

                // Body
                dl->AddCircleFilled(ImVec2(dx + 1.5f, dy + 6.0f), 4.0f, white);

                // Head
                dl->AddCircleFilled(ImVec2(dx + 5.5f, dy + 3.0f), 2.8f, white);

                // Eye
                dl->AddCircleFilled(ImVec2(dx + 6.3f, dy + 2.6f), 0.7f, Pal(0.1f, 0.1f, 0.15f));

                // Beak
                dl->AddTriangleFilled(
                    ImVec2(dx + 8.0f, dy + 2.8f),
                    ImVec2(dx + 11.5f, dy + 3.2f),
                    ImVec2(dx + 8.0f, dy + 4.0f),
                    Pal(0.92f, 0.68f, 0.25f));

                // Tail feathers
                dl->AddTriangleFilled(
                    ImVec2(dx - 3.0f, dy + 7.0f),
                    ImVec2(dx - 9.0f, dy + 9.0f),
                    ImVec2(dx - 1.0f, dy + 8.5f), whiteSoft);
            }
        }

        // Title
        PushLoaderFont();
        dl->AddText(ImGui::GetFont(), 24.0f, ImVec2(wp.x + 44.0f + slideY, wp.y + 12.0f),
            Pal(0.95f, 0.97f, 1.0f), "SERAPH");
        PopLoaderFont();
        dl->AddText(ImGui::GetFont(), 9.5f, ImVec2(wp.x + 44.0f + slideY, wp.y + 40.0f),
            Pal(0.45f, 0.55f, 0.62f), "External  v0.1  |  Roblox");

        // Status badge
        float badgeW = 58.0f, badgeH = 20.0f;
        float badgeX = wp.x + ws.x - badgeW - 14.0f, badgeY = wp.y + (headerH - badgeH) * 0.5f;
        dl->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH),
            Pal(0.0f, 0.682f, 1.0f, 0.08f), 10.0f);
        dl->AddCircleFilled(ImVec2(badgeX + 12.0f, badgeY + badgeH * 0.5f), 3.0f, Pal(0.28f, 0.82f, 0.50f));
        dl->AddText(ImGui::GetFont(), 9.0f,
            ImVec2(badgeX + 19.0f, badgeY + (badgeH - 9.0f) * 0.5f),
            Pal(0.0f, 0.682f, 1.0f), "Ready");

        // ── Divider ────────────────────────────────────────────────
        float divY = wp.y + headerH;
        dl->AddRectFilled(ImVec2(wp.x, divY), ImVec2(wp.x + ws.x, divY + 1.0f),
            Pal(0.04f, 0.07f, 0.11f));

        // ── Tab bar ────────────────────────────────────────────────
        float tabBarY = divY + 1.0f;
        float tabBarH = 38.0f;
        const char* tabs[] = { "Stealth", "Theme", "Font", "Config" };
        float tabW = ws.x / 4.0f;

        for (int i = 0; i < 4; i++)
        {
            bool active = (l_SelectedTab == i);
            ImVec2 tMin(wp.x + i * tabW, tabBarY);
            ImVec2 tMax(tMin.x + tabW, tabBarY + tabBarH);
            bool hov = ImGui::IsMouseHoveringRect(tMin, tMax) && !active;

            // Animate tab transition
            float targetFade = active ? 1.0f : 0.0f;
            g_TabFade[i] = LerpF(g_TabFade[i], targetFade, dt * 12.0f);

            // Active background
            if (g_TabFade[i] > 0.01f) {
                dl->AddRectFilled(tMin, tMax,
                    Pal(0.0f, 0.55f, 0.85f, 0.08f * g_TabFade[i]));
            }
            if (hov) {
                dl->AddRectFilled(tMin, tMax, Pal(0.06f, 0.09f, 0.13f, 0.4f));
            }

            // Active bottom bar
            if (g_TabFade[i] > 0.01f) {
                float barW = (tMax.x - tMin.x) * 0.4f * g_TabFade[i];
                float barX = tMin.x + ((tMax.x - tMin.x) - barW) * 0.5f;
                dl->AddRectFilled(ImVec2(barX, tMax.y - 2.0f), ImVec2(barX + barW, tMax.y), ac1);
            }

            // Tab label
            PushLoaderFont();
            ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
            float tx = tMin.x + (tabW - ts.x) * 0.5f;
            float ty = tMin.y + (tabBarH - ts.y) * 0.5f;
            ImU32 textCol = active ? Pal(0.0f, 0.682f, 1.0f)
                : hov ? Pal(0.80f, 0.88f, 0.94f)
                : Pal(0.42f, 0.52f, 0.58f);
            dl->AddText(ImGui::GetFont(), 10.5f, ImVec2(tx, ty), textCol, tabs[i]);
            PopLoaderFont();

            // Click
            if (hov && ImGui::IsMouseClicked(0))
                l_SelectedTab = i;
        }

        // ── Content divider ────────────────────────────────────────
        float contentDivY = tabBarY + tabBarH;
        dl->AddRectFilled(ImVec2(wp.x, contentDivY), ImVec2(wp.x + ws.x, contentDivY + 1.0f),
            Pal(0.04f, 0.07f, 0.11f));

        // ── Content area ───────────────────────────────────────────
        float contentPad = 18.0f;
        float btnH = 40.0f;
        float btnPad = 12.0f;
        float contentH = ws.y - headerH - tabBarH - 1.0f - btnH - btnPad * 2.0f;

        ImGui::SetCursorPos(ImVec2(contentPad, headerH + tabBarH + 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild("##tab_content", ImVec2(ws.x - contentPad * 2, contentH), false);
        ImGui::PopStyleVar(1);

        ImVec2 cs = ImGui::GetContentRegionAvail();
        g_ConfigListWidth = cs.x;
        PushLoaderFont();

        // ── STEALTH TAB ───────────────────────────────────────────
        if (l_SelectedTab == 0)
        {
            ImGui::SetCursorPosY(4.0f);
            dl->AddText(ImGui::GetFont(), 12.0f, ImGui::GetCursorScreenPos(),
                Pal(0.0f, 0.682f, 1.0f), "Process Camouflage");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);

            // Toggle card helper
            auto ToggleCard = [&](const char* label, bool* v) {
                float cH = 34.0f;
                ImVec2 mn = ImGui::GetCursorScreenPos();
                ImVec2 mx(mn.x + cs.x, mn.y + cH);
                DrawRoundedRectFilled(dl, mn, mx, Pal(0.020f, 0.036f, 0.058f), 6.0f);
                dl->AddText(ImGui::GetFont(), 10.5f,
                    ImVec2(mn.x + 10.0f, mn.y + (cH - 10.5f) * 0.5f),
                    Pal(0.88f, 0.92f, 0.96f), label);
                DrawTogglePill(dl,
                    ImVec2(mx.x - 40.0f, mn.y + (cH - 16.0f) * 0.5f),
                    36.0f, 16.0f, *v);
                ImGui::SetCursorScreenPos(mn);
                if (ImGui::InvisibleButton(label, ImVec2(cs.x, cH)))
                    *v = !*v;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
            };

            ToggleCard("Hide Process", &Options::Misc::HideProcess);

            // Process name
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            dl->AddText(ImGui::GetFont(), 9.5f, ImGui::GetCursorScreenPos(), Pal(0.42f, 0.52f, 0.58f), "Process Name");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.020f, 0.036f, 0.058f, 1.0f));
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##procname", Options::Misc::ProcessName, sizeof(Options::Misc::ProcessName));
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

            // Presets
            dl->AddText(ImGui::GetFont(), 9.5f, ImGui::GetCursorScreenPos(), Pal(0.42f, 0.52f, 0.58f), "Presets");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
            for (int i = 0; i < ProcessPresetCount; i++)
            {
                if (i > 0) ImGui::SameLine(0, 4.0f);
                ImVec2 bp = ImGui::GetCursorScreenPos();
                ImVec2 bs = ImGui::CalcTextSize(ProcessPresets[i]);
                float pW = bs.x + 14.0f, pH = 24.0f;
                bool hov = ImGui::IsMouseHoveringRect(bp, ImVec2(bp.x + pW, bp.y + pH));
                DrawRoundedRectFilled(dl, bp, ImVec2(bp.x + pW, bp.y + pH),
                    hov ? Pal(0.035f, 0.06f, 0.09f) : Pal(0.020f, 0.036f, 0.058f), 4.0f);
                dl->AddText(ImGui::GetFont(), 9.0f,
                    ImVec2(bp.x + 7.0f, bp.y + (pH - 9.0f) * 0.5f),
                    hov ? Pal(0.0f, 0.682f, 1.0f) : Pal(0.50f, 0.58f, 0.64f), ProcessPresets[i]);
                ImGui::Dummy(ImVec2(pW, pH));
                if (hov && ImGui::IsMouseClicked(0))
                    strncpy_s(Options::Misc::ProcessName, ProcessPresets[i], sizeof(Options::Misc::ProcessName) - 1);
                ImGui::SameLine(0, 0);
                ImGui::Dummy(ImVec2(0, 0));
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

            // Separator
            ImVec2 sep1 = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(sep1, ImVec2(sep1.x + cs.x, sep1.y + 1.0f), Pal(0.04f, 0.07f, 0.11f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

            ToggleCard("Hide from Taskbar", &Options::Misc::HideFromTabs);
            ToggleCard("Stream Proof", &Options::Misc::StreamProof);

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            ImVec2 sep2 = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(sep2, ImVec2(sep2.x + cs.x, sep2.y + 1.0f), Pal(0.04f, 0.07f, 0.11f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

            dl->AddText(ImGui::GetFont(), 9.5f, ImGui::GetCursorScreenPos(), Pal(0.42f, 0.52f, 0.58f), "Exclusion Path");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.020f, 0.036f, 0.058f, 1.0f));
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##exclpath", Options::Misc::ExclusionPath, sizeof(Options::Misc::ExclusionPath));
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(2);
        }

        // ── THEME TAB ─────────────────────────────────────────────
        else if (l_SelectedTab == 1)
        {
            ImGui::SetCursorPosY(4.0f);
            dl->AddText(ImGui::GetFont(), 12.0f, ImGui::GetCursorScreenPos(),
                Pal(0.0f, 0.682f, 1.0f), "Color Theme");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);

            const float cardW = (cs.x - 6.0f) * 0.5f;
            const float cardH = 58.0f;

            for (int i = 0; i < ThemeCount; i++)
            {
                bool sel = (Options::Misc::MenuTheme == i);
                ImVec2 cardMin = ImGui::GetCursorScreenPos();

                // Card bg
                ImU32 bg = sel ? Pal(0.0f, 0.55f, 0.85f, 0.12f) : Pal(0.020f, 0.036f, 0.058f);
                DrawRoundedRectFilled(dl, cardMin, ImVec2(cardMin.x + cardW, cardMin.y + cardH), bg, 7.0f);

                // Border
                ImU32 border = sel ? Pal(0.0f, 0.682f, 1.0f, 0.6f) : Pal(0.04f, 0.07f, 0.11f);
                DrawRoundedRectStroke(dl, cardMin, ImVec2(cardMin.x + cardW, cardMin.y + cardH), border, 7.0f, sel ? 1.5f : 1.0f);

                // Theme name
                dl->AddText(ImGui::GetFont(), 10.0f,
                    ImVec2(cardMin.x + 9.0f, cardMin.y + 7.0f),
                    sel ? Pal(0.0f, 0.682f, 1.0f) : Pal(0.88f, 0.92f, 0.96f), Themes[i].name);

                // Swatches
                float sx = cardMin.x + 9.0f, sy = cardMin.y + 25.0f;
                for (int c = 0; c < 4; c++) {
                    ImU32 sw = Pal(Themes[i].bg[c], Themes[i].bg[c], Themes[i].bg[c]);
                    if (c == 0) sw = Pal(Themes[i].bg[0], Themes[i].bg[1], Themes[i].bg[2]);
                    else if (c == 1) sw = Pal(Themes[i].panel[0], Themes[i].panel[1], Themes[i].panel[2]);
                    else if (c == 2) sw = Pal(Themes[i].accent[0], Themes[i].accent[1], Themes[i].accent[2]);
                    else sw = Pal(Themes[i].accent2[0], Themes[i].accent2[1], Themes[i].accent2[2]);
                    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + 14.0f, sy + 14.0f), sw, 3.0f);
                    dl->AddRect(ImVec2(sx, sy), ImVec2(sx + 14.0f, sy + 14.0f),
                        Pal(0.06f, 0.09f, 0.13f), 3.0f, 0, 0.5f);
                    sx += 17.5f;
                }

                // Gradient bar
                if (Themes[i].gradient) {
                    ImVec2 gp(cardMin.x + cardW - 34.0f, cardMin.y + cardH - 17.0f);
                    dl->AddRectFilled(gp, ImVec2(gp.x + 26.0f, gp.y + 5.0f),
                        Pal(Themes[i].accent[0], Themes[i].accent[1], Themes[i].accent[2]), 2.5f);
                    dl->AddRectFilled(ImVec2(gp.x + 9.0f, gp.y), ImVec2(gp.x + 26.0f, gp.y + 5.0f),
                        Pal(Themes[i].accent2[0], Themes[i].accent2[1], Themes[i].accent2[2]), 2.5f);
                }

                // Click
                ImGui::SetCursorScreenPos(cardMin);
                char tid[32]; sprintf_s(tid, "##th_%d", i);
                if (ImGui::InvisibleButton(tid, ImVec2(cardW, cardH)))
                    Options::Misc::MenuTheme = i;

                // Hover
                if (ImGui::IsItemHovered() && !sel)
                    dl->AddRectFilled(cardMin, ImVec2(cardMin.x + cardW, cardMin.y + cardH),
                        Pal(0.06f, 0.09f, 0.13f, 0.3f), 7.0f);

                if (i % 2 == 0) ImGui::SameLine(0, 6.0f);
                else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
            }
        }

        // ── FONT TAB ──────────────────────────────────────────────
        else if (l_SelectedTab == 2)
        {
            ImGui::SetCursorPosY(4.0f);
            dl->AddText(ImGui::GetFont(), 12.0f, ImGui::GetCursorScreenPos(),
                Pal(0.0f, 0.682f, 1.0f), "Menu Font");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);

            // Scale card
            {
                float cH = 50.0f;
                ImVec2 mn = ImGui::GetCursorScreenPos();
                ImVec2 mx(mn.x + cs.x, mn.y + cH);
                DrawRoundedRectFilled(dl, mn, mx, Pal(0.020f, 0.036f, 0.058f), 7.0f);
                dl->AddText(ImGui::GetFont(), 10.0f,
                    ImVec2(mn.x + 10.0f, mn.y + 9.0f), Pal(0.88f, 0.92f, 0.96f), "Scale");
                ImGui::SetCursorScreenPos(ImVec2(mn.x + 10.0f, mn.y + 26.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.030f, 0.052f, 0.080f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.0f, 0.682f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.22f, 0.80f, 1.0f, 1.0f));
                ImGui::PushItemWidth(cs.x - 20.0f);
                ImGui::SliderFloat("##scale", &Options::Misc::MenuScale, 0.6f, 2.0f, "%.1fx");
                ImGui::PopItemWidth();
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
                ImGui::SetCursorScreenPos(ImVec2(mn.x, mn.y + cH + 6.0f));
            }

            dl->AddText(ImGui::GetFont(), 9.5f, ImGui::GetCursorScreenPos(), Pal(0.42f, 0.52f, 0.58f), "Select Font");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0f);

            const float itemH = 36.0f;
            for (int i = 0; i < LoaderFontCount && i < FontNameCount; i++)
            {
                bool sel = (Options::Misc::MenuFont == i);
                ImVec2 mn = ImGui::GetCursorScreenPos();
                ImVec2 mx(mn.x + cs.x, mn.y + itemH);

                ImU32 bg = sel ? Pal(0.0f, 0.55f, 0.85f, 0.12f) : Pal(0.020f, 0.036f, 0.058f);
                DrawRoundedRectFilled(dl, mn, mx, bg, 5.0f);
                if (sel)
                    DrawRoundedRectStroke(dl, mn, mx, Pal(0.0f, 0.682f, 1.0f, 0.5f), 5.0f, 1.5f);

                // Accent bar
                if (sel) {
                    dl->AddRectFilled(ImVec2(mn.x + 6.0f, mn.y + 8.0f),
                        ImVec2(mn.x + 9.0f, mn.y + itemH - 8.0f), Pal(0.0f, 0.682f, 1.0f));
                }

                dl->AddText(ImGui::GetFont(), 10.5f,
                    ImVec2(mn.x + 16.0f, mn.y + (itemH - 10.5f) * 0.5f),
                    sel ? Pal(0.0f, 0.682f, 1.0f) : Pal(0.80f, 0.86f, 0.92f), FontNames[i]);

                ImGui::SetCursorScreenPos(mn);
                char fid[32]; sprintf_s(fid, "##fn_%d", i);
                if (ImGui::InvisibleButton(fid, ImVec2(cs.x, itemH)))
                    Options::Misc::MenuFont = i;

                if (ImGui::IsItemHovered() && !sel)
                    dl->AddRectFilled(mn, mx, Pal(0.06f, 0.09f, 0.13f, 0.25f), 5.0f);

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
            }
        }

        // ── CONFIG TAB ────────────────────────────────────────────
        else if (l_SelectedTab == 3)
        {
            ImGui::SetCursorPosY(4.0f);
            dl->AddText(ImGui::GetFont(), 12.0f, ImGui::GetCursorScreenPos(),
                Pal(0.0f, 0.682f, 1.0f), "Configuration");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);

            // Autoload toggle
            {
                float cH = 34.0f;
                ImVec2 mn = ImGui::GetCursorScreenPos();
                ImVec2 mx(mn.x + cs.x, mn.y + cH);
                DrawRoundedRectFilled(dl, mn, mx, Pal(0.020f, 0.036f, 0.058f), 6.0f);
                dl->AddText(ImGui::GetFont(), 10.5f,
                    ImVec2(mn.x + 10.0f, mn.y + (cH - 10.5f) * 0.5f),
                    Pal(0.88f, 0.92f, 0.96f), "Autoload on Start");
                DrawTogglePill(dl,
                    ImVec2(mx.x - 40.0f, mn.y + (cH - 16.0f) * 0.5f),
                    36.0f, 16.0f, Options::Loader::AttachOnStart);
                ImGui::SetCursorScreenPos(mn);
                if (ImGui::InvisibleButton("##autoload", ImVec2(cs.x, cH)))
                    Options::Loader::AttachOnStart = !Options::Loader::AttachOnStart;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
            }

            dl->AddText(ImGui::GetFont(), 9.5f, ImGui::GetCursorScreenPos(), Pal(0.42f, 0.52f, 0.58f), "Autoload Config");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

            if (ConfigFiles.empty()) ScanConfigs();

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3, 3));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.014f, 0.026f, 0.044f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.06f, 0.09f, 0.13f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.0f, 0.682f, 1.0f, 0.4f));

            float listH = ImGui::GetContentRegionAvail().y - 6.0f;
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
                ImU32 bg = sel ? Pal(0.0f, 0.55f, 0.85f, 0.10f)
                    : hov ? Pal(0.030f, 0.050f, 0.078f)
                    : Pal(0.018f, 0.032f, 0.052f);
                cdl->AddRectFilled(mn, mx, bg, 4.0f);

                // Accent bar
                if (sel) {
                    cdl->AddRectFilled(ImVec2(mn.x + 2.0f, mn.y + 5.0f),
                        ImVec2(mn.x + 4.5f, mn.y + itemH - 5.0f), Pal(0.0f, 0.682f, 1.0f));
                }

                cdl->AddText(ImGui::GetFont(), 9.5f,
                    ImVec2(mn.x + 10.0f, mn.y + (itemH - 9.5f) * 0.5f),
                    sel ? Pal(0.0f, 0.682f, 1.0f) : Pal(0.78f, 0.84f, 0.90f),
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

        PopLoaderFont();
        ImGui::PopStyleVar(1); // ChildRounding
        ImGui::EndChild();

        // ── Inject button ──────────────────────────────────────────
        {
            float btnW = ws.x - contentPad * 2;
            ImVec2 btnMin(wp.x + contentPad, wp.y + ws.y - btnH - btnPad);
            ImVec2 btnMax(btnMin.x + btnW, btnMin.y + btnH);
            bool hov = ImGui::IsMouseHoveringRect(btnMin, btnMax);

            // Animate
            float targetGlow = hov ? 1.0f : 0.0f;
            g_InjectGlow = LerpF(g_InjectGlow, targetGlow, dt * 10.0f);

            // Button bg
            float intens = 0.06f + g_InjectGlow * 0.08f;
            ImU32 btnBg = Pal(0.0f, intens + 0.02f, intens + 0.06f);
            DrawRoundedRectFilled(dl, btnMin, btnMax, btnBg, 7.0f);

            // Border (animated pulse)
            float bAlpha = 0.20f + pulse * 0.15f + g_InjectGlow * 0.15f;
            DrawRoundedRectStroke(dl, btnMin, btnMax, Pal(0.0f, 0.682f, 1.0f, bAlpha), 7.0f, 1.5f);

            // Inner glow
            if (g_InjectGlow > 0.01f) {
                dl->AddRectFilled(
                    ImVec2(btnMin.x + 1.0f, btnMin.y + 1.0f),
                    ImVec2(btnMax.x - 1.0f, btnMax.y - 1.0f),
                    Pal(0.0f, 0.682f, 1.0f, 0.03f * g_InjectGlow), 6.0f);
            }

            // Label
            PushLoaderFont();
            ImVec2 ts = ImGui::CalcTextSize("INJECT");
            dl->AddText(ImGui::GetFont(), 12.0f,
                ImVec2(btnMin.x + (btnW - ts.x) * 0.5f, btnMin.y + (btnH - ts.y) * 0.5f),
                Pal(0.92f, 0.96f, 1.0f), "INJECT");
            PopLoaderFont();

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
        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
            GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
            L"SeraphLoader", nullptr };
        RegisterClassExW(&wc);

        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        int ww = 480, wh = 580;
        l_Hwnd = CreateWindowW(wc.lpszClassName, L"Seraph", WS_POPUP | WS_VISIBLE,
            (sw - ww) / 2, (sh - wh) / 2, ww, wh, nullptr, nullptr, wc.hInstance, nullptr);

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

        // Autopsy palette
        s.Colors[ImGuiCol_WindowBg]             = PalV(0.010f, 0.018f, 0.030f);
        s.Colors[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_Border]               = PalV(0.04f, 0.07f, 0.11f);
        s.Colors[ImGuiCol_Text]                 = PalV(0.90f, 0.94f, 0.97f);
        s.Colors[ImGuiCol_TextDisabled]         = PalV(0.42f, 0.52f, 0.58f);
        s.Colors[ImGuiCol_FrameBg]              = PalV(0.020f, 0.036f, 0.058f);
        s.Colors[ImGuiCol_FrameBgHovered]       = PalV(0.030f, 0.052f, 0.080f);
        s.Colors[ImGuiCol_FrameBgActive]        = PalV(0.0f, 0.18f, 0.28f);
        s.Colors[ImGuiCol_SliderGrab]           = PalV(0.0f, 0.682f, 1.0f);
        s.Colors[ImGuiCol_SliderGrabActive]     = PalV(0.22f, 0.80f, 1.0f);
        s.Colors[ImGuiCol_CheckMark]            = PalV(0.0f, 0.682f, 1.0f);
        s.Colors[ImGuiCol_Header]               = PalV(0.0f, 0.18f, 0.28f, 0.3f);
        s.Colors[ImGuiCol_HeaderHovered]        = PalV(0.0f, 0.18f, 0.28f, 0.4f);
        s.Colors[ImGuiCol_HeaderActive]         = PalV(0.0f, 0.18f, 0.28f, 0.5f);
        s.Colors[ImGuiCol_Separator]            = PalV(0.04f, 0.07f, 0.11f);
        s.Colors[ImGuiCol_Button]               = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ButtonHovered]        = PalV(0.0f, 0.18f, 0.28f, 0.15f);
        s.Colors[ImGuiCol_ButtonActive]         = PalV(0.0f, 0.18f, 0.28f, 0.25f);
        s.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
        s.Colors[ImGuiCol_ScrollbarGrab]        = PalV(0.06f, 0.09f, 0.13f, 0.8f);
        s.Colors[ImGuiCol_ScrollbarGrabHovered] = PalV(0.0f, 0.682f, 1.0f, 0.4f);
        s.Colors[ImGuiCol_PopupBg]              = PalV(0.014f, 0.026f, 0.044f, 0.96f);

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

            struct { const char* path; float size; } fontPaths[] = {
                { "C:\\Windows\\Fonts\\verdana.ttf",  14.0f },
                { "C:\\Windows\\Fonts\\segoeui.ttf",  14.0f },
                { "C:\\Windows\\Fonts\\tahoma.ttf",   14.0f },
                { "C:\\Windows\\Fonts\\arial.ttf",    14.0f },
                { "C:\\Windows\\Fonts\\georgia.ttf",  14.0f },
                { "C:\\Windows\\Fonts\\calibri.ttf",  14.0f },
                { "C:\\Windows\\Fonts\\consola.ttf",  14.0f },
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
            RenderUI();
            ImGui::Render();
            const float cc[4] = { 0.010f, 0.018f, 0.030f, 1.0f };
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
