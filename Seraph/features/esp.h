#pragma once

#include <urlmon.h>
#include <wininet.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <cmath>
#include <d3d11.h>
#include "avatar3d.h"

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "gdiplus.lib")

extern ID3D11Device* g_pd3dDevice;

namespace ESPPreviewAvatar
{
    inline ID3D11ShaderResourceView* g_LocalPlayerAvatarSRV = nullptr;
    inline int g_LocalPlayerAvatarWidth = 0;
    inline int g_LocalPlayerAvatarHeight = 0;
    inline uint64_t g_LastUserId = 0;
    inline bool g_IsDownloadingAvatar = false;
    inline bool g_GdiplusInitialized = false;
    inline ULONG_PTR g_GdiplusToken = 0;
    // Cooldown gate: prevents us from spawning a new avatar-fetch thread
    // every frame when the thumbnail API keeps returning no imageUrl
    // (e.g. private/deleted accounts). 5 seconds is a reasonable balance
    // between being responsive on success and not hammering the endpoint.
    inline std::chrono::steady_clock::time_point g_LastAttemptTime =
        std::chrono::steady_clock::time_point{};

    inline void InitGDIPlus()
    {
        if (!g_GdiplusInitialized)
        {
            Gdiplus::GdiplusStartupInput gdiplusStartupInput;
            Gdiplus::GdiplusStartup(&g_GdiplusToken, &gdiplusStartupInput, NULL);
            g_GdiplusInitialized = true;
        }
    }

    inline void ShutdownGDIPlus()
    {
        if (g_GdiplusInitialized)
        {
            Gdiplus::GdiplusShutdown(g_GdiplusToken);
            g_GdiplusInitialized = false;
        }
    }

    inline std::string FetchUrl(const std::string& url)
    {
        std::string response;
        HINTERNET hInternet = InternetOpenA("Seraph", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInternet)
        {
            HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
            if (hConnect)
            {
                char buffer[1024];
                DWORD bytesRead = 0;
                while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    response += buffer;
                }
                InternetCloseHandle(hConnect);
            }
            InternetCloseHandle(hInternet);
        }
        return response;
    }

    inline bool LoadTextureWithGDIPlus(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
    {
        InitGDIPlus();

        Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(filename);
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            if (bitmap) delete bitmap;
            return false;
        }

        UINT width = bitmap->GetWidth();
        UINT height = bitmap->GetHeight();

        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData bitmapData;
        
        if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok)
        {
            delete bitmap;
            return false;
        }

        std::vector<unsigned char> rgba(width * height * 4);
        unsigned char* src = (unsigned char*)bitmapData.Scan0;
        for (UINT i = 0; i < width * height; i++)
        {
            rgba[i * 4 + 0] = src[i * 4 + 2]; // R
            rgba[i * 4 + 1] = src[i * 4 + 1]; // G
            rgba[i * 4 + 2] = src[i * 4 + 0]; // B
            rgba[i * 4 + 3] = src[i * 4 + 3]; // A
        }
        bitmap->UnlockBits(&bitmapData);
        delete bitmap;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = rgba.data();
        initData.SysMemPitch = width * 4;

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &initData, &texture);
        if (FAILED(hr))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        hr = device->CreateShaderResourceView(texture, &srvDesc, out_srv);
        texture->Release();

        if (SUCCEEDED(hr))
        {
            *out_width = width;
            *out_height = height;
            return true;
        }
        return false;
    }
}


#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cfloat>

#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/KeyBind.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "combatfeedback.h"
#include "visibility.h"
#include "chams.h"

inline float EspClamp(float value, float minVal, float maxVal)
{
    if (value < minVal)
        return minVal;
    if (value > maxVal)
        return maxVal;
    return value;
}

inline float EspMin(float a, float b)
{
    return a < b ? a : b;
}

inline float EspMax(float a, float b)
{
    return a > b ? a : b;
}

inline void GetPlayerHealth(const RobloxPlayer& player, float& health, float& maxHealth)
{
    health = player.Health;
    maxHealth = player.MaxHealth;

    if (maxHealth <= 0.f)
        maxHealth = (health > 0.f) ? health : 100.f;
}

inline bool EspAnyEnabled()
{
    return Options::ESP::BoxType != 0
        || Options::ESP::BoxFill
        || Options::ESP::Tracers
        || Options::ESP::Skeleton
        || Options::ESP::Name
        || Options::ESP::Distance
        || Options::ESP::Health
        || Options::ESP::GradientHealthbar
        || Options::ESP::HeadCircle
        || Options::ESP::HeadDot
        || Options::ESP::CornerESP
        || Options::ESP::HealthText
        || Options::ESP::EnemyHealthIndicator
        || Options::ESP::RigType
        || Options::Chams::Enabled
        || Options::Combat::HitChams
        || Options::ESP::VisibilityCheck
        || Options::ESP::VisibilityChams
        || Options::ESP::Arrows
        || Options::ESP::Radar;
}

inline void RenderESP(ImDrawList* drawList)
{
    if (!Options::ESP::Enabled)
        return;

    if (Options::ESP::ESPKey != 0)
    {
        static bool wasKeyPressed = false;
        bool isKeyPressed = KeyBind::IsPressed(Options::ESP::ESPKey);
        
        if (Options::ESP::ToggleType == 2)
        {
            // Always On
            Options::ESP::Toggled = true;
        }
        else if (Options::ESP::ToggleType == 1)
        {
            if (isKeyPressed && !wasKeyPressed)
                Options::ESP::Toggled = !Options::ESP::Toggled;
            wasKeyPressed = isKeyPressed;
            
            if (!Options::ESP::Toggled)
                return;
        }
        else
        {
            if (!isKeyPressed)
            {
                Options::ESP::Toggled = false;
                return;
            }
        }
    }
    else
    {
        // No key set
        if (Options::ESP::ToggleType == 2)
            Options::ESP::Toggled = true;
    }

    if (!drawList || !Globals::Viewport::Valid || !EspAnyEnabled())
        return;

    if (Globals::Caches::CachedPlayerObjects.empty())
        return;

    const auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    if (!localHRP.address) localHRP = localCharacter.FindFirstChild("Torso");
    if (!localHRP.address) localHRP = localCharacter.FindFirstChild("UpperTorso");

    auto localHead = localCharacter.FindFirstChild("Head");
    if (!localHead.address) localHead = localHRP;

    Vectors::Vector3 localPos = {0,0,0};
    if (localHRP.address) localPos = localHRP.Position();
    else if (localHead.address) localPos = localHead.Position();

    Visibility::BeginFrame();

    // Per-player screen-position history for motion trails.
    static std::unordered_map<uint64_t, std::vector<ImVec2>> s_TrailHistory;
    if (Options::ESP::Trails)
    {
        // Prune dead players occasionally to avoid unbounded growth.
        static int s_PruneTick = 0;
        if ((++s_PruneTick % 120) == 0)
        {
            for (auto it = s_TrailHistory.begin(); it != s_TrailHistory.end(); )
            {
                bool alive = false;
                for (const auto& p : Globals::Caches::CachedPlayerObjects)
                    if (p.address == it->first) { alive = true; break; }
                if (!alive) it = s_TrailHistory.erase(it); else ++it;
            }
        }
    }

    if (Options::ESP::VisibilityCheck || Options::ESP::VisibilityChams)
        Visibility::RefreshOccludersIfNeeded();

    ImFont* font = ImGui::GetFont();
    const ImGuiIO& io = ImGui::GetIO();

    const ImU32 boxColor = IM_COL32(
        static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[2] * 255.f),
        255);

    const ImU32 nameColor = IM_COL32(
        static_cast<int>(Options::ESP::Color[0] * 255.f),
        static_cast<int>(Options::ESP::Color[1] * 255.f),
        static_cast<int>(Options::ESP::Color[2] * 255.f),
        255);

    const ImU32 distanceColor = IM_COL32(
        static_cast<int>(Options::ESP::DistanceColor[0] * 255.f),
        static_cast<int>(Options::ESP::DistanceColor[1] * 255.f),
        static_cast<int>(Options::ESP::DistanceColor[2] * 255.f),
        255);

    const ImU32 tracerColor = IM_COL32(
        static_cast<int>(Options::ESP::TracerColor[0] * 255.f),
        static_cast<int>(Options::ESP::TracerColor[1] * 255.f),
        static_cast<int>(Options::ESP::TracerColor[2] * 255.f),
        255);

    const ImU32 skeletonColor = IM_COL32(
        static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
        static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
        static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
        255);

    const ImU32 headCircleColor = IM_COL32(
        static_cast<int>(Options::ESP::HeadCircleColor[0] * 255.f),
        static_cast<int>(Options::ESP::HeadCircleColor[1] * 255.f),
        static_cast<int>(Options::ESP::HeadCircleColor[2] * 255.f),
        255);

    const ImU32 headDotColor = IM_COL32(
        static_cast<int>(Options::ESP::HeadDotColor[0] * 255.f),
        static_cast<int>(Options::ESP::HeadDotColor[1] * 255.f),
        static_cast<int>(Options::ESP::HeadDotColor[2] * 255.f),
        255);

    const ImU32 esp3DColor = IM_COL32(
        static_cast<int>(Options::ESP::ESP3DColor[0] * 255.f),
        static_cast<int>(Options::ESP::ESP3DColor[1] * 255.f),
        static_cast<int>(Options::ESP::ESP3DColor[2] * 255.f),
        255);

    const Vectors::Vector3 localHeadPos = localHead.address ? localHead.Position() : localPos;

    auto projectPart = [](const RobloxInstance& part, float& left, float& top, float& right, float& bottom, bool& any) -> bool
    {
        if (!part.address)
            return false;

        const auto screen = WorldToScreen(part.Position());
        if (screen.x < 0.f || screen.y < 0.f)
            return false;

        any = true;
        left = EspMin(left, screen.x);
        top = EspMin(top, screen.y);
        right = EspMax(right, screen.x);
        bottom = EspMax(bottom, screen.y);
        return true;
    };

    for (const auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        float skipHealth = 0.f;
        float skipMaxHealth = 0.f;
        GetPlayerHealth(player, skipHealth, skipMaxHealth);
        if (skipMaxHealth > 0.f && skipHealth <= 0.f)
            continue;

        if (Options::ESP::TeamCheck && IsTeammate(player))
            continue;

        auto targetHead = player.Head;
        if (!targetHead.address) targetHead = player.Character.FindFirstChild("Head");

        auto targetHRP = player.HumanoidRootPart;
        if (!targetHRP.address) targetHRP = player.Torso;
        if (!targetHRP.address) targetHRP = player.Upper_Torso;
        if (!targetHRP.address) targetHRP = player.Character.FindFirstChild("Torso");
        if (!targetHRP.address) targetHRP = player.Character.FindFirstChild("HumanoidRootPart");
        if (!targetHRP.address) targetHRP = player.Character.FindFirstChild("UpperTorso");

        if (!targetHead.address) targetHead = targetHRP;
        if (!targetHRP.address) targetHRP = targetHead;

        if (!targetHead.address || !targetHRP.address)
            continue;

        float left = FLT_MAX;
        float top = FLT_MAX;
        float right = -FLT_MAX;
        float bottom = -FLT_MAX;
        bool hasBounds = false;

        projectPart(targetHead, left, top, right, bottom, hasBounds);
        projectPart(targetHRP, left, top, right, bottom, hasBounds);

        if (player.RigType == 0)
        {
            projectPart(player.Torso, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Arm, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Arm, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Leg, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Leg, left, top, right, bottom, hasBounds);
        }
        else
        {
            projectPart(player.Upper_Torso, left, top, right, bottom, hasBounds);
            projectPart(player.Lower_Torso, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Hand, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Hand, left, top, right, bottom, hasBounds);
            projectPart(player.Left_Foot, left, top, right, bottom, hasBounds);
            projectPart(player.Right_Foot, left, top, right, bottom, hasBounds);
        }

        if (!hasBounds)
            continue;

        float width = right - left;
        float height = bottom - top;

        // Fallback: If only vertically aligned parts (Head/HRP) projected successfully, width will be near 0.
        // Standard character proportion is roughly 1:2 (width:height).
        if (width == 0.f && height == 0.f)
        {
            width = 10.f; // Arbitrary small size to show a dot or box at the point
            height = 10.f;
            left -= width / 2.0f;
            right += width / 2.0f;
            top -= height / 2.0f;
            bottom += height / 2.0f;
        }
        else if (width < height * 0.2f)
        {
            float center_x = left + (width / 2.0f);
            width = height * 0.55f;
            left = center_x - (width / 2.0f);
            right = center_x + (width / 2.0f);
        }

        left -= width * 0.1f;
        right += width * 0.1f;
        top -= height * 0.08f;
        bottom += height * 0.05f;

        const bool validBounds = left < right && top < bottom;
        if (!validBounds)
            continue;

        float liveHealth = 0.f;
        float liveMaxHealth = 0.f;
        GetPlayerHealth(player, liveHealth, liveMaxHealth);

        const float distance3D = localPos.x != 0 ? targetHRP.Position().Distance(localPos) : 0.f;
        if (distance3D > Options::ESP::MaxRenderDistance) continue;
        if (distance3D > 2000.f) continue;
        const float scale = EspClamp(450.f / EspClamp(distance3D, 1.f, 4500.f), 0.65f, 2.5f);

        const bool playerVisible = Visibility::IsPlayerVisible(player);
        const ImU32 activeBoxColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : boxColor;
        const ImU32 activeSkeletonColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : skeletonColor;
        const ImU32 activeTracerColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : tracerColor;
        const ImU32 activeNameColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : nameColor;
        const ImU32 activeHeadCircleColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : headCircleColor;
        const ImU32 activeHeadDotColor = Options::ESP::VisibilityCheck
            ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
            : headDotColor;

        if (Options::ESP::VisibilityChams)
            Visibility::DrawPlayerVisibilityChams(drawList, player, distance3D);

        const auto headScreen = WorldToScreen(targetHead.Position());
        const ImVec2 head2D(headScreen.x, headScreen.y);
        ImVec2 newHeadName(head2D.x, top - 4.f);

        const bool hitFlash = Options::Combat::HitChams && CombatFeedback::IsHitFlashing(player.address);
        const float hitAlpha = CombatFeedback::HitFlashAlpha(player.address);

        if (hitFlash)
        {
            const ImU32 hitFill = IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                static_cast<int>(140.f * hitAlpha));
            const ImU32 hitOutline = IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                static_cast<int>(220.f * hitAlpha));

            drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), hitFill);
            drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), hitOutline, 0.f, 0, 2.0f);
        }

        if (Options::ESP::BoxFill && Options::ESP::BoxType == 1)
        {
            if (Options::ESP::BoxFillGradient)
            {
                float time = Options::ESP::BoxFillGradientRotate
                    ? static_cast<float>(ImGui::GetTime()) * Options::ESP::BoxFillSpeed
                    : 0.0f;

                float s = sinf(time);
                float c = cosf(time);
                float t1 = (s + 1.0f) * 0.5f;
                float t2 = (c + 1.0f) * 0.5f;
                float t3 = (-s + 1.0f) * 0.5f;
                float t4 = (-c + 1.0f) * 0.5f;

                auto lerpCol = [](const float a[4], const float b[4], float t) -> ImU32 {
                    return IM_COL32(
                        static_cast<int>((a[0] + (b[0] - a[0]) * t) * 255.f),
                        static_cast<int>((a[1] + (b[1] - a[1]) * t) * 255.f),
                        static_cast<int>((a[2] + (b[2] - a[2]) * t) * 255.f),
                        static_cast<int>((a[3] + (b[3] - a[3]) * t) * 255.f));
                };

                ImU32 c_tl, c_tr, c_br, c_bl;

                if (Options::ESP::BoxFillType == 0)
                {
                    c_tl = c_bl = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t1);
                    c_tr = c_br = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t2);
                }
                else if (Options::ESP::BoxFillType == 1)
                {
                    c_tl = c_tr = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t1);
                    c_bl = c_br = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t2);
                }
                else
                {
                    c_tl = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t1);
                    c_tr = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t2);
                    c_br = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t3);
                    c_bl = lerpCol(Options::ESP::BoxFillTopColor, Options::ESP::BoxFillBottomColor, t4);
                }

                drawList->AddRectFilledMultiColor(ImVec2(left, top), ImVec2(right, bottom), c_tl, c_tr, c_br, c_bl);
            }
            else
            {
                const ImU32 fillColor = IM_COL32(
                    static_cast<int>(Options::ESP::BoxFillColor[0] * 255.f),
                    static_cast<int>(Options::ESP::BoxFillColor[1] * 255.f),
                    static_cast<int>(Options::ESP::BoxFillColor[2] * 255.f),
                    static_cast<int>(Options::ESP::BoxFillColor[3] * 255.f));
                drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), fillColor);
            }
        }

        if (Options::ESP::BoxType == 1)
        {
            ImU32 boxCol = activeBoxColor;
            // Pulse: modulate the box alpha with a sine wave.
            if (Options::ESP::Pulse)
            {
                float a = 0.45f + 0.55f * (0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * Options::ESP::PulseSpeed * 3.0f));
                boxCol = (boxCol & 0x00FFFFFF) | (static_cast<int>(a * 255) << 24);
            }

            // Glow: a few thicker, low-alpha passes just outside the box.
            if (Options::ESP::Glow)
            {
                for (int g = 3; g >= 1; g--)
                {
                    ImU32 gc = (boxCol & 0x00FFFFFF) | (static_cast<int>(30 / g) << 24);
                    drawList->AddRect(
                        ImVec2(left - g * 2.0f, top - g * 2.0f),
                        ImVec2(right + g * 2.0f, bottom + g * 2.0f),
                        gc, 0.f, 0, Options::ESP::BoxThickness + g * 2.0f);
                }
            }

            if (!Options::ESP::RemoveBorders)
                drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(0, 0, 0, 255), 0.f, 0, Options::ESP::BoxThickness + 1.5f);
            drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxCol, 0.f, 0, Options::ESP::BoxThickness);
        }

        // ---- Rings (horizontal ellipse on the ground under the player, breathing) ----
        if (Options::ESP::Rings)
        {
            auto feet = targetHRP.address ? targetHRP.Position() : targetHead.Position();
            float breath = 1.0f + 0.08f * sinf(ImGui::GetTime() * 2.5f);
            float r = Options::ESP::RingRadius * scale * breath;
            const int segments = 32;
            ImVec2 pts[segments];
            int validPts = 0;
            ImU32 ringCol = Options::ESP::VisibilityCheck
                ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                : IM_COL32(
                    static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
                    static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
                    static_cast<int>(Options::ESP::BoxColor[2] * 255.f), 200);
            for (int i = 0; i < segments; i++)
            {
                float a = (6.2831855f / segments) * i;
                Vectors::Vector3 wp = { feet.x + r * cosf(a), feet.y, feet.z + r * sinf(a) };
                auto sp = WorldToScreen(wp);
                if (sp.x == -1.f || sp.y == -1.f) continue;
                pts[validPts++] = ImVec2(sp.x, sp.y);
            }
            if (validPts >= 3)
                drawList->AddPolyline(pts, validPts, ringCol, 0, 2.0f);
        }

        // ---- Trails (motion history) ----
        if (Options::ESP::Trails)
        {
            auto& hist = s_TrailHistory[player.address];
            const auto hrScreen = WorldToScreen(targetHRP.address ? targetHRP.Position() : targetHead.Position());
            if (hrScreen.x != -1.f && hrScreen.y != -1.f)
            {
                hist.push_back(ImVec2(hrScreen.x, hrScreen.y));
                if ((int)hist.size() > Options::ESP::TrailLength)
                    hist.erase(hist.begin());
            }
            if (hist.size() >= 2)
            {
                ImU32 trailCol = Options::ESP::VisibilityCheck
                    ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                    : activeBoxColor;
                for (size_t i = 1; i < hist.size(); i++)
                {
                    float a = static_cast<float>(i) / static_cast<float>(hist.size());
                    ImU32 seg = (trailCol & 0x00FFFFFF) | (static_cast<int>(a * 180) << 24);
                    drawList->AddLine(hist[i - 1], hist[i], seg, 2.0f);
                }
            }
        }

        if (Options::ESP::CornerESP)
        {
            const ImU32 cornerColor = Options::ESP::VisibilityCheck
                ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                : IM_COL32(
                    static_cast<int>(Options::ESP::CornerColor[0] * 255.f),
                    static_cast<int>(Options::ESP::CornerColor[1] * 255.f),
                    static_cast<int>(Options::ESP::CornerColor[2] * 255.f),
                    255);

            const float cornerLen = EspClamp((right - left) * 0.22f, 6.0f, 24.0f);
            const float t = Options::ESP::BoxThickness;

            auto corner = [&](ImVec2 a, ImVec2 b, ImVec2 c)
            {
                drawList->AddLine(a, b, cornerColor, t);
                drawList->AddLine(a, c, cornerColor, t);
            };

            corner(ImVec2(left, top), ImVec2(left + cornerLen, top), ImVec2(left, top + cornerLen));
            corner(ImVec2(right, top), ImVec2(right - cornerLen, top), ImVec2(right, top + cornerLen));
            corner(ImVec2(left, bottom), ImVec2(left + cornerLen, bottom), ImVec2(left, bottom - cornerLen));
            corner(ImVec2(right, bottom), ImVec2(right - cornerLen, bottom), ImVec2(right, bottom - cornerLen));
        }

        if (Options::ESP::BoxType == 2)
        {
            const auto& hrp = player.HumanoidRootPart;
            const Vectors::Vector3 partPos = hrp.Position();
            const Vectors::Vector3 partSize = hrp.Size();
            const sCFrame partCFrame = hrp.CFrame();

            const Vectors::Vector3 rightVec = partCFrame.GetRightVector();
            const Vectors::Vector3 upVec = partCFrame.GetUpVector();
            const Vectors::Vector3 lookVec = partCFrame.GetLookVector();

            const float halfX = partSize.x * 0.5f;
            const float halfY = partSize.y * 0.5f;
            const float halfZ = partSize.z * 0.5f;

            const std::vector<Vectors::Vector3> corners3D = {
                partPos + rightVec * halfX + upVec * halfY + lookVec * halfZ,
                partPos - rightVec * halfX + upVec * halfY + lookVec * halfZ,
                partPos + rightVec * halfX - upVec * halfY + lookVec * halfZ,
                partPos - rightVec * halfX - upVec * halfY + lookVec * halfZ,
                partPos + rightVec * halfX + upVec * halfY - lookVec * halfZ,
                partPos - rightVec * halfX + upVec * halfY - lookVec * halfZ,
                partPos + rightVec * halfX - upVec * halfY - lookVec * halfZ,
                partPos - rightVec * halfX - upVec * halfY - lookVec * halfZ
            };

            std::vector<ImVec2> corners2D;
            corners2D.reserve(8);
            for (const auto& corner : corners3D)
            {
                const auto screenPos = WorldToScreen(corner);
                if (screenPos.x != -1.f && screenPos.y != -1.f)
                    corners2D.emplace_back(screenPos.x, screenPos.y);
            }

            if (corners2D.size() >= 8)
            {
                const float thickness = Options::ESP::ESP3DThickness;
                const ImU32 active3DColor = Options::ESP::VisibilityCheck
                    ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                    : esp3DColor;

                drawList->AddLine(corners2D[0], corners2D[1], active3DColor, thickness);
                drawList->AddLine(corners2D[1], corners2D[3], active3DColor, thickness);
                drawList->AddLine(corners2D[3], corners2D[2], active3DColor, thickness);
                drawList->AddLine(corners2D[2], corners2D[0], active3DColor, thickness);

                drawList->AddLine(corners2D[4], corners2D[5], active3DColor, thickness);
                drawList->AddLine(corners2D[5], corners2D[7], active3DColor, thickness);
                drawList->AddLine(corners2D[7], corners2D[6], active3DColor, thickness);
                drawList->AddLine(corners2D[6], corners2D[4], active3DColor, thickness);

                drawList->AddLine(corners2D[0], corners2D[4], active3DColor, thickness);
                drawList->AddLine(corners2D[1], corners2D[5], active3DColor, thickness);
                drawList->AddLine(corners2D[2], corners2D[6], active3DColor, thickness);
                drawList->AddLine(corners2D[3], corners2D[7], active3DColor, thickness);
            }
        }

        if (Options::ESP::Tracers)
        {
            ImVec2 tracerStart(io.DisplaySize.x * 0.5f, io.DisplaySize.y);
            switch (Options::ESP::TracersStart)
            {
            case 1:
                tracerStart = ImVec2(io.DisplaySize.x * 0.5f, 0.f);
                break;
            case 2:
            {
                POINT cursor{};
                GetCursorPos(&cursor);
                tracerStart = ImVec2(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
                break;
            }
            case 3:
            {
                const auto torsoScreen = WorldToScreen(localPos);
                if (torsoScreen.x != -1.f && torsoScreen.y != -1.f)
                    tracerStart = ImVec2(torsoScreen.x, torsoScreen.y);
                break;
            }
            default:
                break;
            }

            const auto targetScreen = WorldToScreen(targetHRP.Position());
            if (targetScreen.x != -1.f && targetScreen.y != -1.f)
                drawList->AddLine(tracerStart, ImVec2(targetScreen.x, targetScreen.y), activeTracerColor, Options::ESP::TracerThickness);
        }

        if (Options::ESP::Skeleton)
        {
            const ImU32 skelCol = IM_COL32(
                static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
                static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
                static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
                255);
            const ImU32 outlineCol = IM_COL32(0, 0, 0, 255);
            const float thickness = Options::ESP::SkeletonThickness;

            auto W2S = [&](const Vectors::Vector3& worldPos, ImVec2& out) -> bool
            {
                auto screenPos = WorldToScreen(worldPos);
                if (screenPos.x <= 0.f || screenPos.y <= 0.f) return false;
                out.x = std::roundf(screenPos.x);
                out.y = std::roundf(screenPos.y);
                return true;
            };

            auto DrawPoly = [&](const ImVec2* points, int count)
            {
                if (count < 2) return;
                drawList->AddPolyline(points, count, outlineCol, false, thickness + 2.f);
                drawList->AddPolyline(points, count, skelCol, false, thickness);
            };

            auto ProcessR6Chain = [&](const RobloxInstance* instances, int count)
            {
                ImVec2 screenPoints[8];
                int validCount = 0;
                for (int i = 0; i < count; ++i)
                {
                    if (!instances[i].address)
                    {
                        DrawPoly(screenPoints, validCount);
                        validCount = 0;
                        continue;
                    }
                    ImVec2 screenPos;
                    if (!W2S(instances[i].Position(), screenPos))
                    {
                        DrawPoly(screenPoints, validCount);
                        validCount = 0;
                        continue;
                    }
                    screenPoints[validCount++] = screenPos;
                }
                DrawPoly(screenPoints, validCount);
            };

            auto ProcessR15Chain = [&](const Vectors::Vector3* points, int count)
            {
                ImVec2 screenPoints[8];
                int validCount = 0;
                for (int i = 0; i < count; ++i)
                {
                    ImVec2 screenPos;
                    if (W2S(points[i], screenPos))
                        screenPoints[validCount++] = screenPos;
                }
                DrawPoly(screenPoints, validCount);
            };

            if (player.Upper_Torso.address && player.Lower_Torso.address)
            {
                const RobloxInstance spine[] = { player.Head, player.Upper_Torso, player.Lower_Torso };
                ProcessR6Chain(spine, 3);

                const RobloxInstance leftArm[] = { player.Upper_Torso, player.Left_Upper_Arm, player.Left_Lower_Arm, player.Left_Hand };
                ProcessR6Chain(leftArm, 4);

                const RobloxInstance rightArm[] = { player.Upper_Torso, player.Right_Upper_Arm, player.Right_Lower_Arm, player.Right_Hand };
                ProcessR6Chain(rightArm, 4);

                const RobloxInstance leftLeg[] = { player.Lower_Torso, player.Left_Upper_Leg, player.Left_Lower_Leg, player.Left_Foot };
                ProcessR6Chain(leftLeg, 4);

                const RobloxInstance rightLeg[] = { player.Lower_Torso, player.Right_Upper_Leg, player.Right_Lower_Leg, player.Right_Foot };
                ProcessR6Chain(rightLeg, 4);
            }
            else if (player.Torso.address && player.Head.address)
            {
                const auto torsoPos = player.Torso.Position();
                const auto torsoSize = player.Torso.Size();
                const auto torsoCf = player.Torso.CFrame();
                const auto headPos = player.Head.Position();
                const auto headSize = player.Head.Size();

                const Vectors::Vector3 up = torsoCf.GetUpVector();
                const Vectors::Vector3 right = torsoCf.GetRightVector();

                const Vectors::Vector3 shoulderCenter = torsoPos + up * (torsoSize.y * 0.2f);
                const Vectors::Vector3 hipCenter = torsoPos - up * (torsoSize.y * 0.4f);
                const Vectors::Vector3 headBottom = headPos - Vectors::Vector3{ 0, headSize.y * 0.5f, 0 };
                const Vectors::Vector3 shoulderLeft = shoulderCenter - right * (torsoSize.x * 0.5f);
                const Vectors::Vector3 shoulderRight = shoulderCenter + right * (torsoSize.x * 0.5f);

                {
                    const Vectors::Vector3 spinePts[] = { headPos, headBottom, shoulderCenter, hipCenter };
                    ProcessR15Chain(spinePts, 4);
                }

                {
                    Vectors::Vector3 armPts[4];
                    int count = 0;
                    armPts[count++] = shoulderCenter;
                    armPts[count++] = shoulderLeft;

                    if (player.Left_Arm.address)
                    {
                        const auto armPos = player.Left_Arm.Position();
                        const auto armSize = player.Left_Arm.Size();
                        const auto armUp = player.Left_Arm.CFrame().GetUpVector();
                        armPts[count++] = armPos + armUp * (armSize.y * 0.2f);
                        armPts[count++] = armPos - armUp * (armSize.y * 0.5f);
                    }
                    ProcessR15Chain(armPts, count);
                }

                {
                    Vectors::Vector3 armPts[4];
                    int count = 0;
                    armPts[count++] = shoulderCenter;
                    armPts[count++] = shoulderRight;

                    if (player.Right_Arm.address)
                    {
                        const auto armPos = player.Right_Arm.Position();
                        const auto armSize = player.Right_Arm.Size();
                        const auto armUp = player.Right_Arm.CFrame().GetUpVector();
                        armPts[count++] = armPos + armUp * (armSize.y * 0.2f);
                        armPts[count++] = armPos - armUp * (armSize.y * 0.5f);
                    }
                    ProcessR15Chain(armPts, count);
                }

                {
                    Vectors::Vector3 legPts[3];
                    int count = 0;
                    legPts[count++] = hipCenter;

                    if (player.Left_Leg.address)
                    {
                        const auto legPos = player.Left_Leg.Position();
                        const auto legSize = player.Left_Leg.Size();
                        const auto legUp = player.Left_Leg.CFrame().GetUpVector();
                        legPts[count++] = legPos + legUp * (legSize.y * 0.5f);
                        legPts[count++] = legPos - legUp * (legSize.y * 0.5f);
                    }
                    ProcessR15Chain(legPts, count);
                }

                {
                    Vectors::Vector3 legPts[3];
                    int count = 0;
                    legPts[count++] = hipCenter;

                    if (player.Right_Leg.address)
                    {
                        const auto legPos = player.Right_Leg.Position();
                        const auto legSize = player.Right_Leg.Size();
                        const auto legUp = player.Right_Leg.CFrame().GetUpVector();
                        legPts[count++] = legPos + legUp * (legSize.y * 0.5f);
                        legPts[count++] = legPos - legUp * (legSize.y * 0.5f);
                    }
                    ProcessR15Chain(legPts, count);
                }
            }
        }

        Chams::RenderChams(drawList, player);

        if (Options::ESP::HeadCircle && headScreen.x != -1.f && headScreen.y != -1.f)
        {
            const float boxHeight = bottom - top;
            const float radius = EspClamp(boxHeight * Options::ESP::HeadCircleScale, 3.f, 14.f);
            drawList->AddCircle(head2D, radius, activeHeadCircleColor, 0, Options::ESP::HeadCircleThickness);
        }

        if (Options::ESP::HeadDot && headScreen.x != -1.f && headScreen.y != -1.f)
        {
            const float dotRadius = EspClamp((bottom - top) * 0.04f, 2.f, 5.f);
            drawList->AddCircleFilled(head2D, dotRadius, activeHeadDotColor, 12);
        }
        if (Options::ESP::Name)
        {
            std::string displayName;
            switch (Options::ESP::NameMode)
            {
            case 1:
            {
                float hp = liveMaxHealth > 0.f ? (liveHealth / liveMaxHealth) * 100.f : 0.f;
                char buf[32]; snprintf(buf, sizeof(buf), "%.0f%%", hp);
                displayName = buf;
                break;
            }
            default: displayName = player.Name; break;
            }

            const float baseSize = Options::ESP::NameSize;
            const float fontSize = (baseSize * scale > 11.f) ? baseSize * scale : 11.f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, displayName.c_str());
            const ImVec2 namePos(head2D.x - textSize.x * 0.5f, top - textSize.y - 2.f);
            newHeadName = namePos;

            float t = Options::ESP::NameThickness;
            if (t > 0.0f)
            {
                ImU32 outlineColor = IM_COL32(0, 0, 0, 255);
                drawList->AddText(font, fontSize, ImVec2(namePos.x + t, namePos.y + t), outlineColor, displayName.c_str());
                drawList->AddText(font, fontSize, ImVec2(namePos.x - t, namePos.y - t), outlineColor, displayName.c_str());
                drawList->AddText(font, fontSize, ImVec2(namePos.x + t, namePos.y - t), outlineColor, displayName.c_str());
                drawList->AddText(font, fontSize, ImVec2(namePos.x - t, namePos.y + t), outlineColor, displayName.c_str());
            }

            drawList->AddText(font, fontSize, namePos, activeNameColor, displayName.c_str());

            // Avatar icon: draw the player's cached thumbnail to the left of the name.
            if (Options::ESP::AvatarIcon && ESPPreviewAvatar::g_LocalPlayerAvatarSRV && player.address == Globals::Roblox::LocalPlayer.address)
            {
                float iconH = textSize.y + 4.f;
                float iconW = iconH;
                ImVec2 iconPos(namePos.x - iconW - 3.f, namePos.y - 2.f);
                drawList->AddImage(ESPPreviewAvatar::g_LocalPlayerAvatarSRV, iconPos, ImVec2(iconPos.x + iconW, iconPos.y + iconH));
            }
        }

        // Custom image drawn near the box (e.g. a logo / chams decal).
        if (Options::ESP::CustomImage && Options::ESP::CustomImagePath[0])
        {
            static ID3D11ShaderResourceView* s_CustomImg = nullptr;
            static std::string s_CustomImgPath;
            if (s_CustomImgPath != Options::ESP::CustomImagePath)
            {
                if (s_CustomImg) { s_CustomImg->Release(); s_CustomImg = nullptr; }
                s_CustomImgPath = Options::ESP::CustomImagePath;
                int w = 0, h = 0;
                wchar_t widePath[256];
                MultiByteToWideChar(CP_UTF8, 0, Options::ESP::CustomImagePath, -1, widePath, 256);
                if (!ESPPreviewAvatar::LoadTextureWithGDIPlus(g_pd3dDevice, widePath, &s_CustomImg, &w, &h))
                {
                    // Debug output
                    OutputDebugStringA(("[Seraph] Failed to load custom image: " + s_CustomImgPath + "\n").c_str());
                }
                else
                {
                    OutputDebugStringA(("[Seraph] Loaded custom image: " + s_CustomImgPath + " (" + std::to_string(w) + "x" + std::to_string(h) + ")\n").c_str());
                }
            }
            if (s_CustomImg)
            {
                float imgH = 48.f * Options::ESP::CustomImageScale * scale;
                float imgW = imgH;
                ImVec2 imgPos(left - imgW - 4.f, top);
                drawList->AddImage(s_CustomImg, imgPos, ImVec2(imgPos.x + imgW, imgPos.y + imgH));
            }
        }

        if (Options::ESP::Distance)
        {
            const float studs = player.Head.address
                ? player.Head.Position().Distance(localHeadPos)
                : distance3D;

            char distText[32];
            snprintf(distText, sizeof(distText), "%.0f studs", studs);

            const float fontSize = (12.f * scale > 10.f) ? 12.f * scale : 10.f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, distText);
            const ImVec2 distPos((left + right) * 0.5f - textSize.x * 0.5f, bottom + 4.f);
            const ImU32 activeDistanceColor = Options::ESP::VisibilityCheck
                ? (playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor())
                : distanceColor;
            drawList->AddText(font, fontSize, distPos, activeDistanceColor, distText);

            if (Options::ESP::ShowWeapon && !player.ToolName.empty())
            {
                const float wpnFontSize = (11.f * scale > 9.f) ? 11.f * scale : 9.f;
                const ImVec2 wpnTextSize = font->CalcTextSizeA(wpnFontSize, FLT_MAX, 0.f, player.ToolName.c_str());
                const ImVec2 wpnPos((left + right) * 0.5f - wpnTextSize.x * 0.5f, distPos.y + textSize.y + 2.f);
                drawList->AddText(font, wpnFontSize, wpnPos, IM_COL32(255, 200, 100, 220), player.ToolName.c_str());
            }
        }

        if (Options::ESP::Health)
        {
            const float healthPercent = EspClamp(liveHealth / liveMaxHealth, 0.f, 1.f);
            const float barWidth = 4.f;
            const float boxHeight = bottom - top;

            const ImVec2 barTopLeft(right + 3.f, top);
            const ImVec2 barBottomRight(right + 3.f + barWidth, bottom);

            drawList->AddRectFilled(barTopLeft, barBottomRight, IM_COL32(30, 30, 30, 200));

            const float filledHeight = boxHeight * healthPercent;
            const ImVec2 filledTopLeft(barTopLeft.x, barBottomRight.y - filledHeight);
            const ImVec2 filledBottomRight(barBottomRight.x, barBottomRight.y);

            if (Options::ESP::GradientHealthbar)
            {
                ImU32 topCol = IM_COL32(
                    static_cast<int>(Options::ESP::HealthbarTopColor[0] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarTopColor[1] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarTopColor[2] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarTopColor[3] * 255.f));

                ImU32 midCol = IM_COL32(
                    static_cast<int>(Options::ESP::HealthbarMiddleColor[0] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarMiddleColor[1] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarMiddleColor[2] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarMiddleColor[3] * 255.f));

                ImU32 botCol = IM_COL32(
                    static_cast<int>(Options::ESP::HealthbarBottomColor[0] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarBottomColor[1] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarBottomColor[2] * 255.f),
                    static_cast<int>(Options::ESP::HealthbarBottomColor[3] * 255.f));

                float fillMinY = barBottomRight.y - filledHeight;
                float midY = (barTopLeft.y + barBottomRight.y) * 0.5f;

                if (fillMinY < midY)
                {
                    drawList->AddRectFilledMultiColor(
                        ImVec2(barTopLeft.x, fillMinY),
                        ImVec2(barBottomRight.x, midY),
                        topCol, topCol, midCol, midCol);
                }

                drawList->AddRectFilledMultiColor(
                    ImVec2(barTopLeft.x, fillMinY < midY ? midY : fillMinY),
                    ImVec2(barBottomRight.x, barBottomRight.y),
                    midCol, midCol, botCol, botCol);
            }
            else
            {
                const int r = static_cast<int>((1.0f - healthPercent) * 255.0f);
                const int g = static_cast<int>(healthPercent * 255.0f);
                const ImU32 barColor = IM_COL32(r, g, 0, 230);

                drawList->AddRectFilled(filledTopLeft, filledBottomRight, barColor);
            }

            drawList->AddRect(barTopLeft, barBottomRight, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.2f);
        }

        if (Options::ESP::HealthText)
        {
            char hpText[32];
            snprintf(hpText, sizeof(hpText), "%.0f/%.0f", liveHealth, liveMaxHealth);
            const float fontSize = (11.f * scale > 10.0f) ? 11.f * scale : 10.0f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, hpText);
            const ImVec2 hpPos(right + 10.f, top + (bottom - top) * 0.5f - textSize.y * 0.5f);
            drawList->AddText(font, fontSize, hpPos, IM_COL32(255, 255, 255, 230), hpText);
        }

        if (Options::ESP::EnemyHealthIndicator && liveMaxHealth > 0.f)
        {
            char hpText[24];
            snprintf(hpText, sizeof(hpText), "%.0f HP", liveHealth);
            const float fontSize = (13.f * scale > 11.0f) ? 13.f * scale : 11.0f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, hpText);
            const ImVec2 hpPos(head2D.x - textSize.x * 0.5f, newHeadName.y - textSize.y - 2.0f);
            const float hpPct = EspClamp(liveHealth / liveMaxHealth, 0.f, 1.f);
            const ImU32 hpColor = IM_COL32(
                static_cast<int>((1.0f - hpPct) * 255.0f),
                static_cast<int>(hpPct * 255.0f),
                0,
                255);
            drawList->AddText(font, fontSize, hpPos, hpColor, hpText);
        }

        if (Options::ESP::RigType)
        {
            const char* rigStr = nullptr;
            if (player.RigType == 1)
                rigStr = "[R15]";
            else if (player.RigType == 0)
                rigStr = "[R6]";

            if (rigStr)
            {
                const float fontSize = (11.f * scale > 10.0f) ? 11.f * scale : 10.0f;
                const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, rigStr);
                const ImVec2 rigPos(right + 10.f, top + (bottom - top) * 0.5f - textSize.y * 0.5f);
                const ImU32 rigColor = IM_COL32(
                    static_cast<int>(Options::ESP::RigTypeColor[0] * 255.f),
                    static_cast<int>(Options::ESP::RigTypeColor[1] * 255.f),
                    static_cast<int>(Options::ESP::RigTypeColor[2] * 255.f),
                    255);
                drawList->AddText(font, fontSize, rigPos, rigColor, rigStr);
            }
        }
    }

    // Local-only effects: when enabled, draw a ring / glow around the local player.
    if (Options::ESP::LocalOnly && localHRP.address)
    {
        const auto localScreen = WorldToScreen(localHRP.Position());
        if (localScreen.x != -1.f && localScreen.y != -1.f)
        {
            float dist = localPos.x != 0 ? localHRP.Position().Distance(localPos) : 1.f;
            float lscale = EspClamp(450.f / EspClamp(dist, 1.f, 4500.f), 0.65f, 2.5f);
            ImU32 localCol = IM_COL32(
                static_cast<int>(Options::ESP::RadarLocalColor[0] * 255.f),
                static_cast<int>(Options::ESP::RadarLocalColor[1] * 255.f),
                static_cast<int>(Options::ESP::RadarLocalColor[2] * 255.f), 220);

            if (Options::ESP::Glow)
            {
                auto lpos = localHRP.Position();
                const int gsegs = 32;
                for (int g = 3; g >= 1; g--)
                {
                    float gr = (Options::ESP::RingRadius * lscale + g * 2.0f);
                    ImU32 gc = (localCol & 0x00FFFFFF) | (static_cast<int>(30 / g) << 24);
                    ImVec2 gpts[gsegs];
                    int gv = 0;
                    for (int i = 0; i < gsegs; i++)
                    {
                        float a = (6.2831855f / gsegs) * i;
                        Vectors::Vector3 wp = { lpos.x + gr * cosf(a), lpos.y, lpos.z + gr * sinf(a) };
                        auto sp = WorldToScreen(wp);
                        if (sp.x == -1.f || sp.y == -1.f) continue;
                        gpts[gv++] = ImVec2(sp.x, sp.y);
                    }
                    if (gv >= 3)
                        drawList->AddPolyline(gpts, gv, gc, 0, 2.0f + g * 2.0f);
                }
            }
            if (Options::ESP::Rings)
            {
                float breath = 1.0f + 0.08f * sinf(ImGui::GetTime() * 2.5f);
                float lr = Options::ESP::RingRadius * lscale * breath;
                const int segs = 32;
                ImVec2 lpts[segs];
                int lv = 0;
                auto lpos = localHRP.Position();
                for (int i = 0; i < segs; i++)
                {
                    float a = (6.2831855f / segs) * i;
                    Vectors::Vector3 wp = { lpos.x + lr * cosf(a), lpos.y, lpos.z + lr * sinf(a) };
                    auto sp = WorldToScreen(wp);
                    if (sp.x == -1.f || sp.y == -1.f) continue;
                    lpts[lv++] = ImVec2(sp.x, sp.y);
                }
                if (lv >= 3)
                    drawList->AddPolyline(lpts, lv, localCol, 0, 2.0f);
            }
        }
    }
}

inline void RenderESPPreview(ImDrawList* drawList, ImVec2 origin, ImVec2 size)
{
    const ImVec2 rectMin = origin;
    const ImVec2 rectMax(origin.x + size.x, origin.y + size.y);

    drawList->PushClipRect(rectMin, rectMax, true);
    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(8, 8, 8, 255), 4.0f);
    drawList->AddRect(rectMin, rectMax, IM_COL32(27, 27, 27, 255), 4.0f);

    // Perspective grid floor (3D-like depth)
    {
        const float floorTopY = rectMin.y + (rectMax.y - rectMin.y) * 0.40f;
        const float vpX = (rectMin.x + rectMax.x) * 0.5f;
        const ImU32 gridColor = IM_COL32(45, 45, 55, 110);
        const ImU32 gridColorHot = IM_COL32(
            static_cast<int>(Options::Misc::MenuAccentColor[0] * 255.f),
            static_cast<int>(Options::Misc::MenuAccentColor[1] * 255.f),
            static_cast<int>(Options::Misc::MenuAccentColor[2] * 255.f),
            70);

        // Horizontal floor lines (denser near the back / vanishing point)
        for (int i = 1; i <= 8; ++i)
        {
            float t = (float)i / 9.0f;
            float y = floorTopY + (rectMax.y - floorTopY) * t * t;
            ImU32 col = (i == 4 || i == 8) ? gridColorHot : gridColor;
            drawList->AddLine(ImVec2(rectMin.x, y), ImVec2(rectMax.x, y), col, 1.0f);
        }

        // Vertical lines fanning out from the vanishing point down to the floor
        const int vLines = 9;
        for (int i = -vLines; i <= vLines; ++i)
        {
            float t = (float)i / (float)vLines;
            float xBottom = vpX + t * (rectMax.x - rectMin.x) * 0.62f;
            ImU32 col = (i == 0) ? gridColorHot : gridColor;
            drawList->AddLine(ImVec2(vpX, floorTopY), ImVec2(xBottom, rectMax.y), col, 1.0f);
        }

        // Horizon line at the back (subtle accent)
        drawList->AddLine(ImVec2(rectMin.x, floorTopY), ImVec2(rectMax.x, floorTopY),
            IM_COL32(
                static_cast<int>(Options::Misc::MenuAccentColor[0] * 255.f),
                static_cast<int>(Options::Misc::MenuAccentColor[1] * 255.f),
                static_cast<int>(Options::Misc::MenuAccentColor[2] * 255.f),
                90),
            1.2f);
    }

    // Dynamic local player avatar updater
    if (Globals::Roblox::LocalPlayer.address)
    {
        uint64_t userId = Memory->read<uint64_t>(Globals::Roblox::LocalPlayer.address + Offsets::Player::UserId);
        Avatar3D::RequestModel(userId);
        // We do NOT mark g_LastUserId here -- we only update it after the texture
        // is actually loaded successfully, so failed lookups (Roblox returning no
        // imageUrl, network errors, GDI+ decode failures) will be retried on the
        // next frame instead of being silently skipped forever.
        //
        // Cooldown: we enforce a minimum retry interval (5s) when re-fetching the
        // SAME userId (i.e. pit-stop-style retries for an account that never loads
        // successfully), but a real account switch (userId != g_LastUserId) is
        // fetched immediately even if 5s hasn't elapsed, so logging into a
        // different account right after one failed lookup still updates the avatar
        // without waiting.
        const auto now = std::chrono::steady_clock::now();
        const bool userChanged = (userId != 0 && userId != ESPPreviewAvatar::g_LastUserId);
        const bool cooldownOk = (now - ESPPreviewAvatar::g_LastAttemptTime) >= std::chrono::seconds(5);
        const bool needAvatar = (ESPPreviewAvatar::g_LocalPlayerAvatarSRV == nullptr);
        const bool shouldFetch = userId != 0
            && g_pd3dDevice != nullptr
            && !ESPPreviewAvatar::g_IsDownloadingAvatar
            && (needAvatar || userChanged)
            && (userChanged || cooldownOk);

        if (shouldFetch)
        {
            ESPPreviewAvatar::g_IsDownloadingAvatar = true;
            ESPPreviewAvatar::g_LastAttemptTime = now;
            const uint64_t requestedUserId = userId;
            std::thread([requestedUserId]() {
                bool succeeded = false;
                try {
                    std::string url = "https://thumbnails.roblox.com/v1/users/avatar?userIds=" + std::to_string(requestedUserId) + "&size=352x352&format=Png&isCircular=false";
                    std::string response = ESPPreviewAvatar::FetchUrl(url);
                    size_t imgUrlPos = response.find("\"imageUrl\":\"");
                    if (imgUrlPos != std::string::npos)
                    {
                        size_t start = imgUrlPos + 12;
                        size_t end = response.find("\"", start);
                        if (end != std::string::npos)
                        {
                            std::string imageUrl = response.substr(start, end - start);
                            char tempPath[MAX_PATH];
                            GetTempPathA(MAX_PATH, tempPath);
                            std::string localFile = std::string(tempPath) + "seraph_avatar.png";
                            DeleteFileA(localFile.c_str());
                            HRESULT hr = URLDownloadToFileA(NULL, imageUrl.c_str(), localFile.c_str(), 0, NULL);
                            if (SUCCEEDED(hr))
                            {
                                std::wstring wideFile(localFile.begin(), localFile.end());
                                ID3D11ShaderResourceView* newSrv = nullptr;
                                int w = 0, h = 0;
                                if (ESPPreviewAvatar::LoadTextureWithGDIPlus(g_pd3dDevice, wideFile.c_str(), &newSrv, &w, &h))
                                {
                                    if (ESPPreviewAvatar::g_LocalPlayerAvatarSRV)
                                    {
                                        ESPPreviewAvatar::g_LocalPlayerAvatarSRV->Release();
                                    }
                                    ESPPreviewAvatar::g_LocalPlayerAvatarSRV = newSrv;
                                    ESPPreviewAvatar::g_LocalPlayerAvatarWidth = w;
                                    ESPPreviewAvatar::g_LocalPlayerAvatarHeight = h;
                                    ESPPreviewAvatar::g_LastUserId = requestedUserId;
                                    succeeded = true;
                                }
                            }
                        }
                    }
                } catch(...) {}
                ESPPreviewAvatar::g_IsDownloadingAvatar = false;
                (void)succeeded; // currently informational; g_LastUserId tracks the real success state
            }).detach();
        }
    }

    const float pad = 22.0f;
    const float left = origin.x + pad;
    const float right = origin.x + size.x - pad;
    const float top = origin.y + pad + 15.0f;
    const float bottom = origin.y + size.y - pad - 10.0f;

    // --- Rotation state ---
    static float s_RotAngle = 0.0f;
    static float s_DragOffX = 0.0f;
    static float s_DragOffY = 0.0f;
    static bool  s_WasDrag = false;
    static ImVec2 s_LastM = {};
    {
        // Rotation disabled — preview stays at 0°

        const ImVec2 pMin(origin.x, origin.y);
        const ImVec2 pMax(origin.x + size.x, origin.y + size.y);
        const bool hover = ImGui::IsMouseHoveringRect(pMin, pMax);

        // Rotation disabled

        // Middle-drag = move
        {
            static ImVec2 s_RM = {}; static bool s_RD = false;
            if (hover && ImGui::IsMouseDown(2))
            {
                ImVec2 m = ImGui::GetIO().MousePos;
                if (!s_RD) { s_RM = m; s_RD = true; }
                else { s_DragOffX += m.x - s_RM.x; s_DragOffY += m.y - s_RM.y; s_RM = m; }
            }
            else s_RD = false;
        }

        // Clamp
        float mx = (right - left) * 0.35f, my = (bottom - top) * 0.35f;
        if (s_DragOffX > mx) s_DragOffX = mx; if (s_DragOffX < -mx) s_DragOffX = -mx;
        if (s_DragOffY > my) s_DragOffY = my; if (s_DragOffY < -my) s_DragOffY = -my;

        // Double-click reset
        if (hover && ImGui::IsMouseDoubleClicked(0)) { s_DragOffX = 0; s_DragOffY = 0; }

        // Drift back when not interacting
        if (!hover && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
        {
            s_DragOffX *= 0.92f; s_DragOffY *= 0.92f;
            if (fabsf(s_DragOffX) < 0.1f) s_DragOffX = 0;
            if (fabsf(s_DragOffY) < 0.1f) s_DragOffY = 0;
        }
    }

    // --- Projected preview center ---
    const float cx = (left + right) * 0.5f + s_DragOffX;
    const float cy = (top + bottom) * 0.5f + s_DragOffY;
    const float boxW = right - left;
    const float boxH = bottom - top;
    const float cosA = cosf(s_RotAngle * 3.14159265f / 180.0f);

    // Character half-extents (normalized to box size)
    const float charHalfW = boxW * 0.22f;
    const float charTop = cy - boxH * 0.44f;
    const float charBot = cy + boxH * 0.44f;
    const float charH = charBot - charTop;

    // Helper: project a 3D joint to 2D screen pos
    // jointX: -1..1 lateral offset, jointY: 0..1 from top to bottom of character
    auto project = [&](float jointX, float jointY) -> ImVec2 {
        return ImVec2(cx + jointX * charHalfW * cosA, charTop + jointY * charH);
    };

    // --- Draw avatar: 3D model if available, else 2D fallback ---
    float bLeft = 0, bRight = 0, bTop = 0, bBot = 0;
    bool has3D = false;

    uint64_t curUserId = 0;
    if (Globals::Roblox::LocalPlayer.address)
        curUserId = Memory->read<uint64_t>(Globals::Roblox::LocalPlayer.address + Offsets::Player::UserId);

    const Avatar3D::Model* mdl = Avatar3D::GetModel(curUserId);
    if (mdl && mdl->valid)
    {
        has3D = true;
        ImVec2 previewOrigin(origin.x, origin.y);
        ImVec2 previewSize(size.x, size.y);
        Avatar3D::RenderModel(drawList, *mdl, previewOrigin, previewSize, s_RotAngle, Options::Misc::MenuAccentColor);
        auto bounds = Avatar3D::GetProjectedBounds(*mdl, previewOrigin, previewSize, s_RotAngle);
        bLeft = bounds.left - 8.0f;
        bRight = bounds.right + 8.0f;
        bTop = bounds.top - 8.0f;
        bBot = bounds.bottom + 8.0f;
    }
    else
    {
        has3D = false;
        float avatarH = boxH * 0.85f;
        float avatarW = avatarH;
        if (ESPPreviewAvatar::g_LocalPlayerAvatarSRV)
        {
            float aspect = (float)ESPPreviewAvatar::g_LocalPlayerAvatarWidth / (float)ESPPreviewAvatar::g_LocalPlayerAvatarHeight;
            avatarW = avatarH * aspect;
        }
        if (avatarW > boxW * 0.85f) { avatarW = boxW * 0.85f; avatarH = avatarW / (ESPPreviewAvatar::g_LocalPlayerAvatarSRV ? ((float)ESPPreviewAvatar::g_LocalPlayerAvatarWidth / (float)ESPPreviewAvatar::g_LocalPlayerAvatarHeight) : 1.0f); }

        float bboxHalfW = avatarW * 0.5f + boxW * 0.04f;
        float bboxHalfH = avatarH * 0.5f + boxH * 0.03f;
        float bboxCX = (left + right) * 0.5f + s_DragOffX;
        float bboxCY = (top + bottom) * 0.5f + s_DragOffY;
        bLeft = bboxCX - bboxHalfW;
        bRight = bboxCX + bboxHalfW;
        bTop = bboxCY - bboxHalfH;
        bBot = bboxCY + bboxHalfH;

        if (ESPPreviewAvatar::g_LocalPlayerAvatarSRV)
        {
            float tintFactor = fmaxf(0.3f, (cosA * 0.35f + 0.65f));
            int c = (int)(tintFactor * 255);
            bool back = cosA < -0.1f;
            drawList->AddImage(ESPPreviewAvatar::g_LocalPlayerAvatarSRV,
                ImVec2(bboxCX - avatarW * 0.5f, bboxCY - avatarH * 0.5f),
                ImVec2(bboxCX + avatarW * 0.5f, bboxCY + avatarH * 0.5f),
                ImVec2(back ? 1 : 0, 0), ImVec2(back ? 0 : 1, 1),
                IM_COL32(c, c, back ? c/3 : c, 230));
        }
        else
        {
            drawList->AddCircle(ImVec2(bboxCX, bboxCY - avatarH * 0.3f), avatarW * 0.18f, IM_COL32(60, 60, 65, 120), 24);
            ImVec2 tl(bboxCX - avatarW * 0.25f, bboxCY - avatarH * 0.05f);
            ImVec2 tr(bboxCX + avatarW * 0.25f, bboxCY - avatarH * 0.05f);
            ImVec2 bl(bboxCX - avatarW * 0.25f, bboxCY + avatarH * 0.3f);
            ImVec2 br(bboxCX + avatarW * 0.25f, bboxCY + avatarH * 0.3f);
            drawList->AddQuad(tl, tr, br, bl, IM_COL32(60, 60, 65, 80));
        }
    }

    // --- ESP overlays use the bounding box from 3D or 2D ---

    const ImU32 boxColor = IM_COL32(
        static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[2] * 255.f),
        255);

    if (Options::ESP::BoxFill && Options::ESP::BoxType == 1)
    {
        if (Options::ESP::BoxFillGradient)
        {
            ImU32 col1 = IM_COL32(
                static_cast<int>(Options::ESP::BoxFillTopColor[0] * 255.f),
                static_cast<int>(Options::ESP::BoxFillTopColor[1] * 255.f),
                static_cast<int>(Options::ESP::BoxFillTopColor[2] * 255.f),
                static_cast<int>(Options::ESP::BoxFillTopColor[3] * 255.f));
            ImU32 col2 = IM_COL32(
                static_cast<int>(Options::ESP::BoxFillBottomColor[0] * 255.f),
                static_cast<int>(Options::ESP::BoxFillBottomColor[1] * 255.f),
                static_cast<int>(Options::ESP::BoxFillBottomColor[2] * 255.f),
                static_cast<int>(Options::ESP::BoxFillBottomColor[3] * 255.f));
            drawList->AddRectFilledMultiColor(ImVec2(bLeft, bTop), ImVec2(bRight, bBot), col1, col1, col2, col2);
        }
        else
        {
            const ImU32 fillCol = IM_COL32(
                static_cast<int>(Options::ESP::BoxFillColor[0] * 255.f),
                static_cast<int>(Options::ESP::BoxFillColor[1] * 255.f),
                static_cast<int>(Options::ESP::BoxFillColor[2] * 255.f),
                static_cast<int>(Options::ESP::BoxFillColor[3] * 255.f));
            drawList->AddRectFilled(ImVec2(bLeft, bTop), ImVec2(bRight, bBot), fillCol);
        }
    }

    if (Options::ESP::BoxType == 1)
        drawList->AddRect(ImVec2(bLeft, bTop), ImVec2(bRight, bBot), boxColor, 0, 0, Options::ESP::BoxThickness);

    if (Options::ESP::CornerESP || Options::ESP::BoxType == 1)
    {
        const float cornerLen = 18.0f;
        const ImU32 cornerColor = IM_COL32(
            static_cast<int>(Options::ESP::CornerColor[0] * 255.f),
            static_cast<int>(Options::ESP::CornerColor[1] * 255.f),
            static_cast<int>(Options::ESP::CornerColor[2] * 255.f),
            255);
        auto corner = [&](ImVec2 a, ImVec2 b, ImVec2 c) {
            drawList->AddLine(a, b, cornerColor, 1.8f);
            drawList->AddLine(a, c, cornerColor, 1.8f);
        };
        corner(ImVec2(bLeft, bTop), ImVec2(bLeft + cornerLen, bTop), ImVec2(bLeft, bTop + cornerLen));
        corner(ImVec2(bRight, bTop), ImVec2(bRight - cornerLen, bTop), ImVec2(bRight, bTop + cornerLen));
        corner(ImVec2(bLeft, bBot), ImVec2(bLeft + cornerLen, bBot), ImVec2(bLeft, bBot - cornerLen));
        corner(ImVec2(bRight, bBot), ImVec2(bRight - cornerLen, bBot), ImVec2(bRight, bBot - cornerLen));
    }

    if (Options::ESP::Health)
    {
        const float barW = 4.0f;
        const float healthPct = 0.65f;
        drawList->AddRectFilled(ImVec2(bRight + 3.0f, bTop), ImVec2(bRight + 3.0f + barW, bBot), IM_COL32(30, 30, 30, 200));
        const float filledTop = bBot - (bBot - bTop) * healthPct;

        if (Options::ESP::GradientHealthbar)
        {
            ImU32 topCol = IM_COL32(
                static_cast<int>(Options::ESP::HealthbarTopColor[0] * 255.f),
                static_cast<int>(Options::ESP::HealthbarTopColor[1] * 255.f),
                static_cast<int>(Options::ESP::HealthbarTopColor[2] * 255.f),
                static_cast<int>(Options::ESP::HealthbarTopColor[3] * 255.f));
            ImU32 midCol = IM_COL32(
                static_cast<int>(Options::ESP::HealthbarMiddleColor[0] * 255.f),
                static_cast<int>(Options::ESP::HealthbarMiddleColor[1] * 255.f),
                static_cast<int>(Options::ESP::HealthbarMiddleColor[2] * 255.f),
                static_cast<int>(Options::ESP::HealthbarMiddleColor[3] * 255.f));
            ImU32 botCol = IM_COL32(
                static_cast<int>(Options::ESP::HealthbarBottomColor[0] * 255.f),
                static_cast<int>(Options::ESP::HealthbarBottomColor[1] * 255.f),
                static_cast<int>(Options::ESP::HealthbarBottomColor[2] * 255.f),
                static_cast<int>(Options::ESP::HealthbarBottomColor[3] * 255.f));

            float midY = (bTop + bBot) * 0.5f;
            if (filledTop < midY)
            {
                drawList->AddRectFilledMultiColor(
                    ImVec2(bRight + 3.0f, filledTop),
                    ImVec2(bRight + 3.0f + barW, midY),
                    topCol, topCol, midCol, midCol);
            }
            drawList->AddRectFilledMultiColor(
                ImVec2(bRight + 3.0f, filledTop < midY ? midY : filledTop),
                ImVec2(bRight + 3.0f + barW, bBot),
                midCol, midCol, botCol, botCol);
        }
        else
        {
            drawList->AddRectFilled(ImVec2(bRight + 3.0f, filledTop), ImVec2(bRight + 3.0f + barW, bBot), IM_COL32(80, 220, 60, 230));
        }
        drawList->AddRect(ImVec2(bRight + 3.0f, bTop), ImVec2(bRight + 3.0f + barW, bBot), IM_COL32(0, 0, 0, 255));
    }

    if (Options::ESP::HealthText)
    {
        const char* hpText = "65/100";
        const ImVec2 ts = ImGui::CalcTextSize(hpText);
        drawList->AddText(ImVec2(bRight + 10.0f, (bTop + bBot) * 0.5f - ts.y * 0.5f), IM_COL32(255, 255, 255, 230), hpText);
    }

    if (Options::ESP::EnemyHealthIndicator)
    {
        const char* hpText = "65 HP";
        const ImVec2 ts = ImGui::CalcTextSize(hpText);
        drawList->AddText(ImVec2(cx - ts.x * 0.5f, bTop - 18.0f), IM_COL32(0, 220, 60, 255), hpText);
    }

    if (Options::ESP::Name)
    {
        std::string previewName = Globals::Roblox::LocalPlayer.address ? Globals::Roblox::LocalPlayer.Name() : "Player";
        const ImVec2 ts = ImGui::CalcTextSize(previewName.c_str());
        drawList->AddText(ImVec2(cx - ts.x * 0.5f, bTop - 34.0f), IM_COL32(255, 255, 255, 255), previewName.c_str());
    }

    if (Options::ESP::Distance)
    {
        const char* distText = "42 studs";
        const ImVec2 ts = ImGui::CalcTextSize(distText);
        drawList->AddText(ImVec2(cx - ts.x * 0.5f, bBot + 4.0f), IM_COL32(200, 200, 200, 255), distText);
    }

    if (Options::ESP::RigType)
    {
        const char* rigStr = "[R15]";
        const ImVec2 ts = ImGui::CalcTextSize(rigStr);
        const ImU32 rigCol = IM_COL32(
            static_cast<int>(Options::ESP::RigTypeColor[0] * 255.f),
            static_cast<int>(Options::ESP::RigTypeColor[1] * 255.f),
            static_cast<int>(Options::ESP::RigTypeColor[2] * 255.f),
            255);
        drawList->AddText(ImVec2(bRight + 10.0f, bTop + (bBot - bTop) * 0.5f - ts.y * 0.5f), rigCol, rigStr);
    }

    // Head circle
    if (Options::ESP::HeadCircle)
    {
        const ImU32 hcc = IM_COL32(
            static_cast<int>(Options::ESP::HeadCircleColor[0] * 255.f),
            static_cast<int>(Options::ESP::HeadCircleColor[1] * 255.f),
            static_cast<int>(Options::ESP::HeadCircleColor[2] * 255.f),
            230);
        const float headR = EspClamp(boxW * Options::ESP::HeadCircleScale * 1.6f, 6.f, 26.f);
        const float hcX = (bLeft + bRight) * 0.5f;
        const float hcY = bTop + headR + 2.0f;
        drawList->AddCircle(ImVec2(hcX, hcY), headR, hcc, 32, Options::ESP::HeadCircleThickness);
    }

    // Skeleton — all joints projected through the same rotation
    if (Options::ESP::Skeleton)
    {
        const ImU32 skel = IM_COL32(
            static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
            static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
            static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
            255);
        const float thick = EspClamp(Options::ESP::SkeletonThickness, 1.0f, 10.0f);

        // Project joints relative to the bounding box (works for both 2D and 3D)
        auto proj = [&](float jx, float jy) -> ImVec2 {
            float px = bLeft + (bRight - bLeft) * (jx * 0.5f + 0.5f);
            float py = bTop + (bBot - bTop) * jy;
            return ImVec2(px, py);
        };

        const ImVec2 headTop    = proj(0.0f,  0.00f);
        const ImVec2 headCenter = proj(0.0f,  0.08f);
        const ImVec2 neck       = proj(0.0f,  0.18f);
        const ImVec2 lShoulder  = proj(-0.5f, 0.22f);
        const ImVec2 rShoulder  = proj(0.5f,  0.22f);
        const ImVec2 lElbow     = proj(-0.5f, 0.38f);
        const ImVec2 rElbow     = proj(0.5f,  0.38f);
        const ImVec2 lHand      = proj(-0.5f, 0.52f);
        const ImVec2 rHand      = proj(0.5f,  0.52f);
        const ImVec2 hip        = proj(0.0f,  0.55f);
        const ImVec2 lHip       = proj(-0.28f, 0.55f);
        const ImVec2 rHip       = proj(0.28f,  0.55f);
        const ImVec2 lKnee      = proj(-0.28f, 0.75f);
        const ImVec2 rKnee      = proj(0.28f,  0.75f);
        const ImVec2 lFoot      = proj(-0.28f, 0.95f);
        const ImVec2 rFoot      = proj(0.28f,  0.95f);

        // Spine
        drawList->AddLine(headTop, neck, skel, thick);
        drawList->AddLine(neck, hip, skel, thick);
        // Arms
        drawList->AddLine(lShoulder, lElbow, skel, thick);
        drawList->AddLine(lElbow, lHand, skel, thick);
        drawList->AddLine(rShoulder, rElbow, skel, thick);
        drawList->AddLine(rElbow, rHand, skel, thick);
        // Legs
        drawList->AddLine(hip, lKnee, skel, thick);
        drawList->AddLine(lKnee, lFoot, skel, thick);
        drawList->AddLine(hip, rKnee, skel, thick);
        drawList->AddLine(rKnee, rFoot, skel, thick);
    }

    if (Options::Combat::HitChams)
    {
        drawList->AddRectFilled(ImVec2(bLeft, bTop), ImVec2(bRight, bBot),
            IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                100));
        drawList->AddRect(ImVec2(bLeft, bTop), ImVec2(bRight, bBot),
            IM_COL32(
                static_cast<int>(Options::Combat::HitChamsColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitChamsColor[2] * 255.f),
                200),
            0.f, 0, 2.0f);
    }

    drawList->PopClipRect();
}

inline void RenderArrows(ImDrawList* drawList)
{
    if (!Options::ESP::Arrows || !Options::ESP::Enabled)
        return;

    if (!drawList || !Globals::Viewport::Valid)
        return;

    if (Options::ESP::ESPKey != 0)
    {
        if (Options::ESP::ToggleType == 1 && !Options::ESP::Toggled)
            return;
        if (Options::ESP::ToggleType == 0 && !KeyBind::IsPressed(Options::ESP::ESPKey))
            return;
    }

    const auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    if (!localHRP.address)
        return;

    const auto localPos = localHRP.Position();
    const ImVec2 screenCenter(
        Globals::Viewport::Dimensions.x * 0.5f + Globals::Viewport::ScreenPos.x,
        Globals::Viewport::Dimensions.y * 0.5f + Globals::Viewport::ScreenPos.y
    );

    const ImU32 arrowColor = IM_COL32(
        static_cast<int>(Options::ESP::ArrowColor[0] * 255.f),
        static_cast<int>(Options::ESP::ArrowColor[1] * 255.f),
        static_cast<int>(Options::ESP::ArrowColor[2] * 255.f),
        255);

    const float radius = Options::ESP::ArrowRadius;
    const float size = Options::ESP::ArrowSize;
    const float thickness = Options::ESP::ArrowThickness;

    auto camCFrame = Globals::Roblox::Camera.CFrame();
    auto camForward = camCFrame.GetLookVector();
    auto camRight = camCFrame.GetRightVector();
    auto camPos = camCFrame.Position();

    for (const auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (Options::ESP::TeamCheck && IsTeammate(player))
            continue;

        auto hrp = player.HumanoidRootPart;
        if (!hrp.address)
            continue;

        auto targetPos = hrp.Position();
        float dxT = targetPos.x - camPos.x;
        float dyT = targetPos.y - camPos.y;
        float dzT = targetPos.z - camPos.z;
        if (dxT * dxT + dyT * dyT + dzT * dzT > 1000000.f)
            continue;

        auto targetPos2D = WorldToScreen(targetPos);

        bool isOnScreen = targetPos2D.x > 0.f && targetPos2D.y > 0.f;
        if (isOnScreen)
        {
            float dx = targetPos2D.x - screenCenter.x;
            float dy = targetPos2D.y - screenCenter.y;
            float distFromCenter = sqrtf(dx * dx + dy * dy);
            float maxDist = (Globals::Viewport::Dimensions.x * 0.5f) - 30.f;
            if (distFromCenter < maxDist)
                continue;
        }

        // Project target into camera space
        auto toTarget = hrp.Position() - camPos;
        float fwd = toTarget.x * camForward.x + toTarget.y * camForward.y + toTarget.z * camForward.z;
        float rgt = toTarget.x * camRight.x + toTarget.y * camRight.y + toTarget.z * camRight.z;

        float screenAngle = atan2f(rgt, fwd);

        // Arrow position: sin for X (right), -cos for Y (up on screen)
        float arrowX = screenCenter.x + sinf(screenAngle) * radius;
        float arrowY = screenCenter.y - cosf(screenAngle) * radius;

        // Arrow tip points outward from center
        float tipX = arrowX + sinf(screenAngle) * size;
        float tipY = arrowY - cosf(screenAngle) * size;

        // Perpendicular for the base wings
        float perpX = cosf(screenAngle);
        float perpY = sinf(screenAngle);

        float baseX = arrowX - sinf(screenAngle) * size * 0.5f;
        float baseY = arrowY + cosf(screenAngle) * size * 0.5f;

        ImVec2 tip(tipX, tipY);
        ImVec2 left(baseX + perpX * size * 0.4f, baseY + perpY * size * 0.4f);
        ImVec2 right(baseX - perpX * size * 0.4f, baseY - perpY * size * 0.4f);

        drawList->AddTriangleFilled(tip, left, right, arrowColor);
        drawList->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 255), thickness);
    }
}

inline void RenderRadar(ImDrawList* drawList)
{
    if (!Options::ESP::Radar || !Options::ESP::Enabled)
        return;

    if (!drawList || !Globals::Viewport::Valid)
        return;

    if (Options::ESP::ESPKey != 0)
    {
        if (Options::ESP::ToggleType == 1 && !Options::ESP::Toggled)
            return;
        if (Options::ESP::ToggleType == 0 && !KeyBind::IsPressed(Options::ESP::ESPKey))
            return;
    }

    const auto localCharacter = Globals::Roblox::LocalPlayer.Character();
    auto localHRP = localCharacter.FindFirstChild("HumanoidRootPart");
    if (!localHRP.address)
        return;

    const auto localPos = localHRP.Position();
    const float radarSize = Options::ESP::RadarSize;
    const float radarRange = Options::ESP::RadarRange;
    const float dotRadius = 3.0f;

    const ImVec2 radarCenter(
        Options::ESP::RadarX + radarSize + Globals::Viewport::ScreenPos.x,
        Options::ESP::RadarY + radarSize + Globals::Viewport::ScreenPos.y
    );

    const ImU32 bgColor = IM_COL32(
        static_cast<int>(Options::ESP::RadarBgColor[0] * 255.f),
        static_cast<int>(Options::ESP::RadarBgColor[1] * 255.f),
        static_cast<int>(Options::ESP::RadarBgColor[2] * 255.f),
        150);

    const ImU32 enemyColor = IM_COL32(
        static_cast<int>(Options::ESP::RadarEnemyColor[0] * 255.f),
        static_cast<int>(Options::ESP::RadarEnemyColor[1] * 255.f),
        static_cast<int>(Options::ESP::RadarEnemyColor[2] * 255.f),
        255);

    const ImU32 localColor = IM_COL32(
        static_cast<int>(Options::ESP::RadarLocalColor[0] * 255.f),
        static_cast<int>(Options::ESP::RadarLocalColor[1] * 255.f),
        static_cast<int>(Options::ESP::RadarLocalColor[2] * 255.f),
        255);

    const int theme = Options::ESP::RadarTheme;

    // ── Background / frame per theme ──
    if (theme == 2) // Neon
    {
        drawList->AddCircleFilled(radarCenter, radarSize, IM_COL32(5, 10, 20, 180));
        drawList->AddCircle(radarCenter, radarSize, IM_COL32(0, 255, 200, 200), 64);
        // Glow rings
        for (int g = 1; g <= 3; g++)
            drawList->AddCircle(radarCenter, radarSize - g * (radarSize / 4.0f),
                IM_COL32(0, 255, 200, 40), 48);
    }
    else if (theme == 1) // Minimal
    {
        drawList->AddCircleFilled(radarCenter, radarSize, IM_COL32(0, 0, 0, 120));
        drawList->AddCircle(radarCenter, radarSize, IM_COL32(255, 255, 255, 60), 64);
    }
    else if (theme == 3) // Compass
    {
        drawList->AddCircleFilled(radarCenter, radarSize, bgColor);
        drawList->AddCircle(radarCenter, radarSize, IM_COL32(255, 255, 255, 140), 64);
        // Range rings
        for (int r = 1; r <= 3; r++)
            drawList->AddCircle(radarCenter, radarSize * r / 3.0f, IM_COL32(255, 255, 255, 40), 48);
        // Compass ticks (N/E/S/W) using camera-relative right/forward
        auto cf = Globals::Roblox::Camera.CFrame();
        auto fwd = cf.GetLookVector();
        auto rgt = cf.GetRightVector();
        auto drawTick = [&](float fx, float fy, const char* label, ImU32 c)
        {
            float mx = (fx / radarRange) * radarSize;
            float my = -(fy / radarRange) * radarSize;
            ImVec2 p(radarCenter.x + mx, radarCenter.y + my);
            drawList->AddText(ImVec2(p.x - 4, p.y - 8), c, label);
        };
        drawTick(0, radarSize * 0.9f, "N", IM_COL32(255, 255, 255, 200));
        drawTick(radarSize * 0.9f, 0, "E", IM_COL32(255, 255, 255, 200));
        drawTick(0, -radarSize * 0.9f, "S", IM_COL32(255, 255, 255, 200));
        drawTick(-radarSize * 0.9f, 0, "W", IM_COL32(255, 255, 255, 200));
    }
    else // Classic
    {
        drawList->AddCircleFilled(radarCenter, radarSize, bgColor);
        drawList->AddCircle(radarCenter, radarSize, IM_COL32(255, 255, 255, 100), 64);
        drawList->AddLine(ImVec2(radarCenter.x - radarSize, radarCenter.y),
                          ImVec2(radarCenter.x + radarSize, radarCenter.y),
                          IM_COL32(255, 255, 255, 50));
        drawList->AddLine(ImVec2(radarCenter.x, radarCenter.y - radarSize),
                          ImVec2(radarCenter.x, radarCenter.y + radarSize),
                          IM_COL32(255, 255, 255, 50));
    }

    // Local player dot (square in minimal/neon themes for a sharper look)
    if (theme == 1 || theme == 2)
        drawList->AddRectFilled(ImVec2(radarCenter.x - (dotRadius + 1.f), radarCenter.y - (dotRadius + 1.f)),
            ImVec2(radarCenter.x + (dotRadius + 1.f), radarCenter.y + (dotRadius + 1.f)), localColor);
    else
        drawList->AddCircleFilled(radarCenter, dotRadius + 1.f, localColor);

    auto camCFrame = Globals::Roblox::Camera.CFrame();
    auto camForward = camCFrame.GetLookVector();
    auto camRight = camCFrame.GetRightVector();
    auto camPos = camCFrame.Position();

    for (const auto& player : Globals::Caches::CachedPlayerObjects)
    {
        if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
            continue;

        if (Options::ESP::TeamCheck && IsTeammate(player))
            continue;

        auto hrp = player.HumanoidRootPart;
        if (!hrp.address)
            continue;

        auto targetPos = hrp.Position();
        float dxT = targetPos.x - camPos.x;
        float dyT = targetPos.y - camPos.y;
        float dzT = targetPos.z - camPos.z;
        if (dxT * dxT + dyT * dyT + dzT * dzT > 1000000.f)
            continue;

        // Project target into camera space
        auto toTarget = targetPos - camPos;
        float fwd = toTarget.x * camForward.x + toTarget.y * camForward.y + toTarget.z * camForward.z;
        float rgt = toTarget.x * camRight.x + toTarget.y * camRight.y + toTarget.z * camRight.z;

        // Clamp to radar range
        float dist = sqrtf(rgt * rgt + fwd * fwd);
        if (dist > radarRange)
        {
            rgt = rgt / dist * radarRange;
            fwd = fwd / dist * radarRange;
        }

        // Map to radar: right = X, forward = -Y (up on screen)
        float mapX = (rgt / radarRange) * radarSize;
        float mapY = -(fwd / radarRange) * radarSize;

        ImVec2 dotPos(radarCenter.x + mapX, radarCenter.y + mapY);
        if (theme == 2) // Neon: outer glow + bright core
        {
            drawList->AddCircleFilled(dotPos, dotRadius + 3.f, IM_COL32(
                static_cast<int>(Options::ESP::RadarEnemyColor[0] * 255.f),
                static_cast<int>(Options::ESP::RadarEnemyColor[1] * 255.f),
                static_cast<int>(Options::ESP::RadarEnemyColor[2] * 255.f), 60));
            drawList->AddCircleFilled(dotPos, dotRadius, enemyColor);
        }
        else if (theme == 1) // Minimal: small squares
        {
            drawList->AddRectFilled(ImVec2(dotPos.x - dotRadius, dotPos.y - dotRadius),
                ImVec2(dotPos.x + dotRadius, dotPos.y + dotRadius), enemyColor);
        }
        else
        {
            drawList->AddCircleFilled(dotPos, dotRadius, enemyColor);
        }
    }
}
