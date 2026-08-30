#pragma once

#include <urlmon.h>
#include <wininet.h>
#include <gdiplus.h>
#include "obfuscate.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <cmath>
#include <d3d11.h>
#include "avatar3d.h"
#include "hud_editor.h"
#include "preview3d/preview3d.h"
#include "playerfilter.h"

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
        HINTERNET hInternet = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64)", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
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
#include "../overlay/ui.h"
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

// World-space trail point used by the motion-trail system.
struct EspTrailPoint
{
    Vectors::Vector3 pos;
    float timestamp = 0.f;
};

// Catmull-Rom spline interpolation for smooth trail curves. Returns the point
// at parameter t in [0,1] between p1 and p2, guided by p0 and p3.
inline Vectors::Vector3 EspCatmullRom(const Vectors::Vector3& p0, const Vectors::Vector3& p1,
                                      const Vectors::Vector3& p2, const Vectors::Vector3& p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    Vectors::Vector3 r;
    r.x = 0.5f * ((2.f * p1.x) + (-p0.x + p2.x) * t +
        (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 +
        (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3);
    r.y = 0.5f * ((2.f * p1.y) + (-p0.y + p2.y) * t +
        (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 +
        (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3);
    r.z = 0.5f * ((2.f * p1.z) + (-p0.z + p2.z) * t +
        (2.f * p0.z - 5.f * p1.z + 4.f * p2.z - p3.z) * t2 +
        (-p0.z + 3.f * p1.z - 3.f * p2.z + p3.z) * t3);
    return r;
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
    return (Options::ESP::Box && Options::ESP::BoxType != 0)
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

    // Per-player world-space position history for motion trails. World-space
    // (not screen-space) so points survive cache rebuilds and camera moves.
    static std::unordered_map<uint64_t, std::vector<EspTrailPoint>> s_TrailHistory;
    static std::unordered_map<uint64_t, float> s_TrailLastUpdate;
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

        // Player filter: hide excluded players and friends from the ESP.
        if (!player.Name.empty() && !PlayerFilter::EspVisible(player.Name))
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
        const ImU32 activeBoxColor = Visibility::MakeColor(Options::ESP::BoxColor);
        const ImU32 activeSkeletonColor = playerVisible
            ? Visibility::MakeColor(Options::ESP::SkeletonVisibleColor) : Visibility::MakeColor(Options::ESP::SkeletonOccludedColor);
        const ImU32 activeTracerColor = Visibility::MakeColor(Options::ESP::TracerColor);
        const ImU32 activeNameColor = Visibility::MakeColor(Options::ESP::Color);
        const ImU32 activeHeadCircleColor = playerVisible
            ? Visibility::MakeColor(Options::ESP::HeadCircleVisibleColor) : Visibility::MakeColor(Options::ESP::HeadCircleOccludedColor);
        const ImU32 activeHeadDotColor = Visibility::MakeColor(Options::ESP::HeadDotColor);

        if (Options::ESP::VisibilityChams)
            Visibility::DrawPlayerVisibilityChams(drawList, player, distance3D);

        if (Options::ESP::CornerESP)
        {
            const ImU32 cornerColor = Visibility::MakeColor(Options::ESP::CornerColor);

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

        if (Options::ESP::Box && Options::ESP::BoxFill && Options::ESP::BoxType == 1)
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

        if (Options::ESP::Box && Options::ESP::BoxType == 1)
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
            Vectors::Vector3 feet = targetHRP.address ? targetHRP.Position() : targetHead.Position();

            // Follow point: 0 = HumanoidRootPart, 1 = Feet (average of left/right foot)
            if (Options::ESP::RingFollow == 1)
            {
                auto character = player.Character;
                auto leftFoot = character.FindFirstChild("LeftFoot");
                auto rightFoot = character.FindFirstChild("RightFoot");
                if (leftFoot.address && rightFoot.address)
                {
                    Vectors::Vector3 lp = leftFoot.Position();
                    Vectors::Vector3 rp = rightFoot.Position();
                    feet = {
                        (lp.x + rp.x) * 0.5f,
                        (lp.y + rp.y) * 0.5f,
                        (lp.z + rp.z) * 0.5f
                    };
                }
            }
            feet.y += Options::ESP::RingHeightOffset;

            // Color mode: 0 = Box color, 1 = Health gradient, 2 = Visibility colors
            ImU32 ringCol;
            if (Options::ESP::RingHealthGradient || Options::ESP::RingColorMode == 1)
            {
                float hpPct = liveMaxHealth > 0.f ? liveHealth / liveMaxHealth : 1.f;
                hpPct = (std::max)(0.0f, (std::min)(1.0f, hpPct));
                ringCol = IM_COL32(
                    static_cast<int>((1.f - hpPct) * 255.f),
                    static_cast<int>(hpPct * 255.f),
                    0, 200);
            }
            else if (Options::ESP::RingColorMode == 2)
            {
                ringCol = playerVisible ? Visibility::GetVisibleColor() : Visibility::GetHiddenColor();
            }
            else
            {
                ringCol = IM_COL32(
                    static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
                    static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
                    static_cast<int>(Options::ESP::BoxColor[2] * 255.f), 200);
            }

            float breath = 1.0f + 0.08f * sinf(ImGui::GetTime() * 2.5f);
            float r = Options::ESP::RingRadius * scale * breath;
            const int segments = 32;
            ImVec2 pts[segments];
            int validPts = 0;
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

        // ---- Trails (world-space motion history) ----
        if (Options::ESP::Trails)
        {
            const float now = ImGui::GetTime();
            auto& hist = s_TrailHistory[player.address];
            float& lastUpdate = s_TrailLastUpdate[player.address];

            // Remove expired points first so stale history can't linger.
            for (auto it = hist.begin(); it != hist.end(); )
            {
                if ((now - it->timestamp) > Options::ESP::TrailDuration)
                    it = hist.erase(it);
                else
                    ++it;
            }

            // Current position based on the chosen follow point.
            Vectors::Vector3 hrpPos = targetHRP.address ? targetHRP.Position() : targetHead.Position();
            Vectors::Vector3 currentPos = hrpPos;
            if (Options::ESP::TrailFollow == 1 && player.Left_Foot.address && player.Right_Foot.address)
            {
                const Vectors::Vector3 lp = player.Left_Foot.Position();
                const Vectors::Vector3 rp = player.Right_Foot.Position();
                currentPos = {
                    (lp.x + rp.x) * 0.5f,
                    (lp.y + rp.y) * 0.5f,
                    (lp.z + rp.z) * 0.5f
                };
            }

            // only_moving: skip recording while the player is stationary.
            if (!Options::ESP::TrailOnlyMoving ||
                player.Velocity.x * player.Velocity.x +
                player.Velocity.y * player.Velocity.y +
                player.Velocity.z * player.Velocity.z > 0.5f)
            {
                // Throttle new points to the update interval.
                if ((now - lastUpdate) >= Options::ESP::TrailUpdateInterval)
                {
                    // Minimum movement before recording a new point.
                    bool enoughMovement = hist.empty();
                    if (!enoughMovement)
                    {
                        const Vectors::Vector3& lastPos = hist.back().pos;
                        const float dx = currentPos.x - lastPos.x;
                        const float dy = currentPos.y - lastPos.y;
                        const float dz = currentPos.z - lastPos.z;
                        enoughMovement = (dx * dx + dy * dy + dz * dz) >=
                            Options::ESP::TrailMinMovement * Options::ESP::TrailMinMovement;
                    }

                    if (enoughMovement)
                    {
                        hist.push_back({ currentPos, now });
                        lastUpdate = now;
                        if ((int)hist.size() > Options::ESP::TrailLength)
                            hist.erase(hist.begin());
                    }
                }
            }

            if (hist.size() < 2)
                continue;

            // Base color by mode: 0 = custom, 1 = team, 2 = health, 3 = rainbow.
            ImU32 baseColor;
            switch (Options::ESP::TrailColorMode)
            {
            case 1:
            {
                baseColor = IsTeammate(player)
                    ? IM_COL32(0, 200, 120, 255)
                    : IM_COL32(255, 70, 70, 255);
                break;
            }
            case 2:
            {
                float hpPct = liveMaxHealth > 0.f ? liveHealth / liveMaxHealth : 1.f;
                hpPct = (std::max)(0.f, (std::min)(hpPct, 1.f));
                baseColor = IM_COL32(
                    (int)((1.f - hpPct) * 255.f),
                    (int)(hpPct * 255.f), 0, 255);
                break;
            }
            case 3:
            {
                const float hue = std::fmod(now * 0.5f, 1.0f);
                baseColor = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(ImColor::HSV(hue, 0.8f, 1.f)));
                break;
            }
            default:
                baseColor = IM_COL32(
                    static_cast<int>(Options::ESP::TrailColor[0] * 255.f),
                    static_cast<int>(Options::ESP::TrailColor[1] * 255.f),
                    static_cast<int>(Options::ESP::TrailColor[2] * 255.f), 255);
                break;
            }

            const int baseR = (baseColor >> IM_COL32_R_SHIFT) & 0xFF;
            const int baseG = (baseColor >> IM_COL32_G_SHIFT) & 0xFF;
            const int baseB = (baseColor >> IM_COL32_B_SHIFT) & 0xFF;

            // Build the point curve (linear or Catmull-Rom spline).
            std::vector<Vectors::Vector3> curve;
            if (Options::ESP::TrailSmoothingMode == 1 && hist.size() >= 4)
            {
                for (size_t i = 0; i < hist.size() - 1; ++i)
                {
                    const Vectors::Vector3& p0 = (i == 0) ? hist[i].pos : hist[i - 1].pos;
                    const Vectors::Vector3& p1 = hist[i].pos;
                    const Vectors::Vector3& p2 = hist[i + 1].pos;
                    const Vectors::Vector3& p3 = (i + 2 < hist.size()) ? hist[i + 2].pos : hist[i + 1].pos;
                    for (int seg = 0; seg < Options::ESP::TrailSplineSegments; ++seg)
                    {
                        const float t = static_cast<float>(seg) / static_cast<float>(Options::ESP::TrailSplineSegments);
                        curve.push_back(EspCatmullRom(p0, p1, p2, p3, t));
                    }
                }
                curve.push_back(hist.back().pos);
            }
            else
            {
                curve.reserve(hist.size());
                for (const auto& pt : hist)
                    curve.push_back(pt.pos);
            }

            // Distance-based thickness: closer = thicker, farther = thinner.
            float distScale = 1.0f;
            if (distance3D > 0.f)
            {
                const float minScale = 0.3f;
                const float maxScale = 1.0f;
                const float distanceFactor = 100.0f;
                distScale = maxScale - (distance3D / distanceFactor) * (maxScale - minScale);
                distScale = (std::max)(minScale, (std::min)(distScale, maxScale));
            }
            const float scaledThickness = Options::ESP::TrailThickness * distScale;

            for (size_t i = 0; i + 1 < curve.size(); ++i)
            {
                const auto w1 = curve[i];
                const auto w2 = curve[i + 1];
                const auto s1 = WorldToScreen(w1);
                const auto s2 = WorldToScreen(w2);
                if (s1.x == -1.f && s1.y == -1.f) continue;
                if (s2.x == -1.f && s2.y == -1.f) continue;
                const ImVec2 p1(s1.x, s1.y);
                const ImVec2 p2(s2.x, s2.y);

                // Age-based fade for this segment.
                size_t histIdx = (Options::ESP::TrailSmoothingMode == 1 && hist.size() >= 4)
                    ? (i / static_cast<size_t>(Options::ESP::TrailSplineSegments))
                    : i;
                histIdx = (std::min)(histIdx, hist.size() - 1);
                const float age = now - hist[histIdx].timestamp;
                float lifePct = 1.f - (age / Options::ESP::TrailDuration);
                lifePct = (std::max)(0.f, (std::min)(lifePct, 1.f));

                float alpha = 1.f;
                if (Options::ESP::TrailFade && lifePct < Options::ESP::TrailFadeStart)
                    alpha = lifePct / Options::ESP::TrailFadeStart;
                alpha = (std::max)(0.f, (std::min)(alpha, 1.f));
                const int segAlpha = static_cast<int>(alpha * 255.f);

                // Glow layers (outermost first).
                if (Options::ESP::TrailGlow)
                {
                    for (int glow = Options::ESP::TrailGlowLayers; glow >= 1; --glow)
                    {
                        const float glowThickness = scaledThickness + glow * 1.5f * distScale;
                        const float glowFactor = std::pow(Options::ESP::TrailGlowIntensity, static_cast<float>(glow));
                        const int glowAlpha = static_cast<int>(segAlpha * glowFactor);
                        drawList->AddLine(p1, p2, IM_COL32(baseR, baseG, baseB, glowAlpha), glowThickness);
                    }
                }

                // Main line.
                drawList->AddLine(p1, p2, IM_COL32(baseR, baseG, baseB, segAlpha), scaledThickness);
            }
        }

        if (Options::ESP::Box && Options::ESP::BoxType == 2)
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
                const ImU32 active3DColor = Visibility::MakeColor(Options::ESP::ESP3DColor);

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
            const bool perBone = true;
            const uintptr_t skelIgnore = player.Character.address;

            auto W2S = [&](const Vectors::Vector3& worldPos, ImVec2& out) -> bool
            {
                auto screenPos = WorldToScreen(worldPos);
                if (screenPos.x <= 0.f || screenPos.y <= 0.f) return false;
                out.x = std::roundf(screenPos.x);
                out.y = std::roundf(screenPos.y);
                return true;
            };

            auto DrawSeg = [&](const ImVec2& a, const ImVec2& b, const Vectors::Vector3& wa, const Vectors::Vector3& wb)
            {
                if (perBone)
                {
                    Vectors::Vector3 mid = { (wa.x + wb.x) * 0.5f, (wa.y + wb.y) * 0.5f, (wa.z + wb.z) * 0.5f };
                    const uintptr_t key = (wa.x != 0.f) ? (uintptr_t)(wa.x * 1000.f) ^ ((uintptr_t)(wa.z * 1000.f) << 3) : 0;
                    bool vis = Visibility::IsPointVisibleCached(key, mid, skelIgnore);
                    ImU32 segCol = vis
                        ? Visibility::MakeColor(Options::ESP::SkeletonVisibleColor)
                        : Visibility::MakeColor(Options::ESP::SkeletonOccludedColor);
                    drawList->AddLine(a, b, outlineCol, thickness + 2.f);
                    drawList->AddLine(a, b, segCol, thickness);
                }
                else
                {
                    drawList->AddLine(a, b, outlineCol, thickness + 2.f);
                    drawList->AddLine(a, b, skelCol, thickness);
                }
            };

            auto DrawPoly = [&](const ImVec2* points, const Vectors::Vector3* worldPts, int count)
            {
                if (count < 2) return;
                for (int i = 0; i + 1 < count; ++i)
                    DrawSeg(points[i], points[i + 1], worldPts[i], worldPts[i + 1]);
            };

            auto ProcessR6Chain = [&](const RobloxInstance* instances, int count)
            {
                ImVec2 screenPoints[8];
                Vectors::Vector3 worldPoints[8];
                int validCount = 0;
                for (int i = 0; i < count; ++i)
                {
                    if (!instances[i].address)
                    {
                        DrawPoly(screenPoints, worldPoints, validCount);
                        validCount = 0;
                        continue;
                    }
                    ImVec2 screenPos;
                    if (!W2S(instances[i].Position(), screenPos))
                    {
                        DrawPoly(screenPoints, worldPoints, validCount);
                        validCount = 0;
                        continue;
                    }
                    worldPoints[validCount] = instances[i].Position();
                    screenPoints[validCount++] = screenPos;
                }
                DrawPoly(screenPoints, worldPoints, validCount);
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
                if (perBone && validCount >= 2)
                {
                    for (int i = 0; i + 1 < validCount; ++i)
                    {
                        Vectors::Vector3 mid = { (points[i].x + points[i+1].x) * 0.5f, (points[i].y + points[i+1].y) * 0.5f, (points[i].z + points[i+1].z) * 0.5f };
                        const uintptr_t key = (points[i].x != 0.f) ? (uintptr_t)(points[i].x * 1000.f) ^ ((uintptr_t)(points[i].z * 1000.f) << 3) : 0;
                        bool vis = Visibility::IsPointVisibleCached(key, mid, skelIgnore);
                        ImU32 segCol = vis
                            ? Visibility::MakeColor(Options::ESP::SkeletonVisibleColor)
                            : Visibility::MakeColor(Options::ESP::SkeletonOccludedColor);
                        drawList->AddLine(screenPoints[i], screenPoints[i + 1], outlineCol, thickness + 2.f);
                        drawList->AddLine(screenPoints[i], screenPoints[i + 1], segCol, thickness);
                    }
                }
                else
                {
                    DrawPoly(screenPoints, points, validCount);
                }
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
            const ImVec2 namePos(head2D.x - textSize.x * 0.5f + Options::ESP::NameOffsetX, top - textSize.y - 2.f + Options::ESP::NameOffsetY);
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
                    OutputDebugStringA(("[S] Failed to load custom image: " + s_CustomImgPath + "\n").c_str());
                }
                else
                {
                    OutputDebugStringA(("[S] Loaded custom image: " + s_CustomImgPath + " (" + std::to_string(w) + "x" + std::to_string(h) + ")\n").c_str());
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

            const float fontSize = (Options::ESP::DistanceSize * scale > 10.f) ? Options::ESP::DistanceSize * scale : 10.f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, distText);
            const ImVec2 distPos((left + right) * 0.5f - textSize.x * 0.5f + Options::ESP::DistanceOffsetX, bottom + 4.f + Options::ESP::DistanceOffsetY);
            const ImU32 activeDistanceColor = Visibility::MakeColor(Options::ESP::DistanceColor);
            float dt = Options::ESP::DistanceThickness;
            if (dt > 0.0f)
            {
                ImU32 dob = IM_COL32(0, 0, 0, 180);
                drawList->AddText(font, fontSize, ImVec2(distPos.x + dt, distPos.y + dt), dob, distText);
                drawList->AddText(font, fontSize, ImVec2(distPos.x - dt, distPos.y - dt), dob, distText);
                drawList->AddText(font, fontSize, ImVec2(distPos.x + dt, distPos.y - dt), dob, distText);
                drawList->AddText(font, fontSize, ImVec2(distPos.x - dt, distPos.y + dt), dob, distText);
            }
            drawList->AddText(font, fontSize, distPos, activeDistanceColor, distText);

            if (Options::ESP::ShowWeapon && !player.ToolName.empty())
            {
                const float wpnFontSize = (11.f * scale > 9.f) ? 11.f * scale : 9.f;
                const ImVec2 wpnTextSize = font->CalcTextSizeA(wpnFontSize, FLT_MAX, 0.f, player.ToolName.c_str());
                const float chipH = wpnTextSize.y + 5.f;
                const float chipW = wpnTextSize.x + 12.f;
                const float chipX = (left + right) * 0.5f - chipW * 0.5f;
                const float chipY = distPos.y + textSize.y + 2.f;

                const ImU32 chipBg = IM_COL32(16, 16, 20, 205);
                const ImU32 chipBorder = IM_COL32(
                    static_cast<int>(Options::Misc::MenuAccentColor[0] * 255.f),
                    static_cast<int>(Options::Misc::MenuAccentColor[1] * 255.f),
                    static_cast<int>(Options::Misc::MenuAccentColor[2] * 255.f),
                    220);
                const ImU32 chipText = IM_COL32(255, 210, 150, 235);

                drawList->AddRectFilled(ImVec2(chipX, chipY), ImVec2(chipX + chipW, chipY + chipH), chipBg, chipH * 0.5f);
                drawList->AddRect(ImVec2(chipX, chipY), ImVec2(chipX + chipW, chipY + chipH), chipBorder, chipH * 0.5f, 0, 1.0f);
                drawList->AddText(font, wpnFontSize,
                    ImVec2(chipX + (chipW - wpnTextSize.x) * 0.5f, chipY + (chipH - wpnTextSize.y) * 0.5f),
                    chipText, player.ToolName.c_str());
            }
        }

        if (Options::ESP::Health)
        {
            const float healthPercent = EspClamp(liveHealth / liveMaxHealth, 0.f, 1.f);
            const float barWidth = Options::ESP::HealthBarWidth;
            const float boxHeight = bottom - top;

            const float hbOX = Options::ESP::HealthOffsetX;
            const float hbOY = Options::ESP::HealthOffsetY;
            const ImVec2 barTopLeft(right + 3.f + hbOX, top + hbOY);
            const ImVec2 barBottomRight(right + 3.f + barWidth + hbOX, bottom + hbOY);

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
                const float fontSize = (Options::ESP::RigTypeSize * scale > 10.0f) ? Options::ESP::RigTypeSize * scale : 10.0f;
                const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, rigStr);
                const ImVec2 rigPos(right + 10.f + Options::ESP::RigTypeOffsetX, top + (bottom - top) * 0.5f - textSize.y * 0.5f + Options::ESP::RigTypeOffsetY);
                const ImU32 rigColor = IM_COL32(
                    static_cast<int>(Options::ESP::RigTypeColor[0] * 255.f),
                    static_cast<int>(Options::ESP::RigTypeColor[1] * 255.f),
                    static_cast<int>(Options::ESP::RigTypeColor[2] * 255.f),
                    255);
                float rt = Options::ESP::RigTypeThickness;
                if (rt > 0.0f)
                {
                    ImU32 rob = IM_COL32(0, 0, 0, 180);
                    drawList->AddText(font, fontSize, ImVec2(rigPos.x + rt, rigPos.y + rt), rob, rigStr);
                    drawList->AddText(font, fontSize, ImVec2(rigPos.x - rt, rigPos.y - rt), rob, rigStr);
                    drawList->AddText(font, fontSize, ImVec2(rigPos.x + rt, rigPos.y - rt), rob, rigStr);
                    drawList->AddText(font, fontSize, ImVec2(rigPos.x - rt, rigPos.y + rt), rob, rigStr);
                }
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

inline void RenderESPPreview(ImDrawList* drawList, ImVec2 origin, ImVec2 size, bool espEdit = false)
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

    // ?????? Interactive 3D model controls overlay (drawn on top in both modes) ??????
    static ImVec2 s_LastDrag = ImVec2(0, 0);
    static bool   s_Dragging = false;
    static bool   s_SettingsOpen = false;
    static double s_SettingsOpenAt = 0.0;   // ImGui time when the settings panel opened (for entrance anim)

    // ?????? Draggable feature state (always-on drag in the preview) ??????
    // Dragging a feature in the preview writes the global offset options, so the
    // moved position also drives the real in-game ESP. Hovering outlines the
    // feature in white; right-clicking opens a per-feature customize popup.
    enum PreviewFeat : int { kName = 0, kDistance = 1, kHealth = 2, kRigType = 3 };
    static int s_DragFeat = -1;
    static double s_FeatOpenAt = 0.0;   // ImGui time when the feature popup opened (for entrance anim)
    static ImVec2 s_DragOrigin;
    static int s_PopupFeat = -1;
    static int s_HoverFeat = -1;

    const float barH = 26.0f * UI::sc;   // top title / settings bar

    auto DrawControls = [&]()
    {
        ImDrawList* fdl = drawList;
        const float gap = 6.0f * UI::sc;

        const ImVec2 barMin(rectMin.x, rectMin.y);
        const ImVec2 barMax(rectMax.x, rectMin.y + barH);

        // Top bar background
        fdl->AddRectFilled(barMin, barMax, IM_COL32(12, 14, 18, 240), 4.0f);
        fdl->AddLine(ImVec2(barMin.x, barMax.y), ImVec2(barMax.x, barMax.y), IM_COL32(34, 43, 53, 255), 1.0f);

        // Title (left)
        {
            const char* t = "3D MODEL PREVIEW";
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, t);
            fdl->AddText(ImVec2(barMin.x + gap, barMin.y + (barH - ts.y) * 0.5f),
                         IM_COL32(180, 190, 205, 255), t);
        }

        // Settings button (top-right)
        const float sbW = 78.0f * UI::sc;
        const ImVec2 sbMin(rectMax.x - sbW - gap, barMin.y + gap);
        const ImVec2 sbMax(rectMax.x - gap, barMax.y - gap);
        const bool sbHov = ImGui::IsMouseHoveringRect(sbMin, sbMax, false);
        fdl->AddRectFilled(sbMin, sbMax, sbHov ? IM_COL32(28, 36, 45, 255) : IM_COL32(18, 23, 29, 240), 4.0f);
        fdl->AddRect(sbMin, sbMax, s_SettingsOpen ? IM_COL32(59, 157, 255, 255) : IM_COL32(34, 43, 53, 255), 4.0f, 0, 1.0f);
        {
            const ImVec2 c(sbMin.x + sbW * 0.5f, sbMin.y + (sbMax.y - sbMin.y) * 0.5f);
            const float rs = 5.5f * UI::sc;
            const float rt = 8.0f * UI::sc;
            const ImU32 col = s_SettingsOpen ? IM_COL32(241, 245, 250, 255) : IM_COL32(139, 150, 165, 255);
            fdl->AddCircle(c, rs, col, 24, 1.5f * UI::sc);
            for (int i = 0; i < 6; ++i)
            {
                const float a = (float)i / 6.f * 6.2832f;
                const float a1 = a - .24f, a2 = a + .24f;
                fdl->AddQuadFilled(
                    ImVec2(c.x + cosf(a1) * rs, c.y + sinf(a1) * rs),
                    ImVec2(c.x + cosf(a2) * rs, c.y + sinf(a2) * rs),
                    ImVec2(c.x + cosf(a2) * rt, c.y + sinf(a2) * rt),
                    ImVec2(c.x + cosf(a1) * rt, c.y + sinf(a1) * rt), col);
            }
        }
        if (sbHov && !s_SettingsOpen && ImGui::IsMouseClicked(0))
        {
            s_SettingsOpen = true;
            s_SettingsOpenAt = ImGui::GetTime();
        }

        // Mouse drag-to-rotate (middle-mouse drag over the model viewport, so it
        // never conflicts with left-drag feature editing) + scroll-to-zoom.
        if (Options::Preview3D::Enabled)
        {
            const ImVec2 vpMin(rectMin.x, barMax.y);
            const ImVec2 vpMax(rectMax.x, rectMax.y);
            const bool overBtn = ImGui::IsMouseHoveringRect(sbMin, sbMax, false);
            const bool hover = ImGui::IsMouseHoveringRect(vpMin, vpMax, false);
            if (hover && !overBtn && ImGui::IsMouseDown(2))
            {
                ImVec2 m = ImGui::GetIO().MousePos;
                if (!s_Dragging) { s_Dragging = true; s_LastDrag = m; Preview3D::NotifyManual(); }
                else {
                    Preview3D::AddRotation((m.x - s_LastDrag.x) * 0.012f, -(m.y - s_LastDrag.y) * 0.012f);
                    s_LastDrag = m;
                }
            }
            else s_Dragging = false;
            if (hover && !overBtn && ImGui::GetIO().MouseWheel != 0.0f)
                Preview3D::AddZoom(-ImGui::GetIO().MouseWheel * 0.18f);
        }
    };

    s_HoverFeat = -1;

    auto FeatureDrag = [&](int feat, float x, float y, float w, float h, float& offX, float& offY) -> bool
    {
        ImGuiIO& io = ImGui::GetIO();
        bool hover = ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h), false);
        if (hover) s_HoverFeat = feat;

        if (hover && ImGui::IsMouseClicked(1)) { s_PopupFeat = feat; s_FeatOpenAt = ImGui::GetTime(); }
        if (hover && ImGui::IsMouseClicked(0) && s_DragFeat < 0)
        {
            s_DragFeat = feat;
            s_DragOrigin = io.MousePos;
        }
        if (s_DragFeat == feat)
        {
            if (io.MouseDown[0])
            {
                ImVec2 d(io.MousePos.x - s_DragOrigin.x, io.MousePos.y - s_DragOrigin.y);
                offX += d.x; offY += d.y;
                s_DragOrigin = io.MousePos;
            }
            else { s_DragFeat = -1; s_HoverFeat = -1; }
        }
        return hover;
    };
    auto FeatureOutline = [&](float x, float y, float w, float h)
    {
        drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 255, 255, 255), 2.0f, 0, 1.2f);
    };

    // Shared ESP bounding box, computed by whichever model/avatar is shown
    // (3D model or 2D avatar image). Cubed so all ESP overlays use it.
    float bLeft = 0, bRight = 0, bTop = 0, bBot = 0;
    float cx = (rectMin.x + rectMax.x) * 0.5f;
    float cy = (rectMin.y + rectMax.y) * 0.5f;
    float boxW = rectMax.x - rectMin.x;
    float boxH = rectMax.y - rectMin.y;
    // Viewport the 3D model is drawn into (== the preview rect offset for the
    // title bar in 3D mode, == the full preview rect otherwise). Used to place
    // the projected 3D box corners back onto screen.
    ImVec2 modelRectMin(rectMin.x, rectMin.y);
    ImVec2 modelRectMax(rectMax.x, rectMax.y);
    // Actual 2D avatar image size (set in the 2D branch below). The None
    // skeleton is anchored to this so it fits the real avatar figure instead of
    // the larger ESP box or the window.
    float avW = 0.f, avH = 0.f;

    if (Options::Preview3D::Enabled && (int)Options::Preview3D::Model != (int)Preview3D::Model::None)
    {
        // ?????? 3D model path ??????
        const ImVec2 vpMin(rectMin.x, rectMin.y + barH);
        const ImVec2 vpMax(rectMax.x, rectMax.y);
        modelRectMin = vpMin;
        modelRectMax = vpMax;
        Preview3D::DrawPreview(drawList, vpMin, vpMax);

        // Map the projected UV bounds of the rendered model onto the viewport
        // to build the ESP box around the actual 3D model on screen.
        float u0, v0, u1, v1;
        bool gotBounds = false;
        // The union of the model's part AABBs (GetProjectedBounds) encloses the
        // whole mesh - base, hats, accessories - so the box ends up far larger
        // than the character. The skeleton silhouette hugs the body, so derive
        // the box from its projected extent instead, for every 3D model.
        {
            std::vector<float> skel;
            if (Preview3D::GetProjectedSkeleton(skel) && skel.size() >= 4)
            {
                float mnU = 1.f, mxU = 0.f, mnV = 1.f, mxV = 0.f;
                for (size_t i = 0; i + 1 < skel.size(); i += 2)
                {
                    mnU = (std::min)(mnU, skel[i]);     mxU = (std::max)(mxU, skel[i]);
                    mnV = (std::min)(mnV, skel[i + 1]); mxV = (std::max)(mxV, skel[i + 1]);
                }
                const float padU = (mxU - mnU) * 0.12f;
                const float padV = (mxV - mnV) * 0.12f;
                u0 = mnU - padU; v0 = mnV - padV; u1 = mxU + padU; v1 = mxV + padV;
                gotBounds = true;
            }
        }
        if (!gotBounds && Preview3D::GetProjectedBounds(u0, v0, u1, v1))
            gotBounds = true;
        if (gotBounds)
        {
            const float W = vpMax.x - vpMin.x;
            const float H = vpMax.y - vpMin.y;
            bLeft  = vpMin.x + u0 * W;
            bRight = vpMin.x + u1 * W;
            bTop   = vpMin.y + v0 * H;
            bBot   = vpMin.y + v1 * H;
        }
        else
        {
            bLeft = vpMin.x; bRight = vpMax.x;
            bTop  = vpMin.y; bBot   = vpMax.y;
        }
        cx = (bLeft + bRight) * 0.5f;
        cy = (bTop + bBot) * 0.5f;
        boxW = bRight - bLeft;
        boxH = bBot - bTop;
    }
    else
    {

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
                            std::string localFile = std::string(tempPath) + SX("seraph_avatar.png");
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
        const ImVec2 pMin(origin.x, origin.y);
        const ImVec2 pMax(origin.x + size.x, origin.y + size.y);
        // owner=false so the preview stays interactive even when the menu
        // window overlaps it (otherwise the menu captures the mouse and the
        // preview can never be grabbed).
        const bool hover = ImGui::IsMouseHoveringRect(pMin, pMax, false);

        // Right-drag = rotate the avatar model
        {
            static ImVec2 s_RM = {}; static bool s_RD = false;
            if (hover && ImGui::IsMouseDown(1))
            {
                ImVec2 m = ImGui::GetIO().MousePos;
                if (!s_RD) { s_RM = m; s_RD = true; }
                else { s_RotAngle += (m.x - s_RM.x) * 0.4f; s_RM = m; }
            }
            else s_RD = false;
        }

        // Middle-drag = move
        {
            static ImVec2 s_RM2 = {}; static bool s_RD2 = false;
            if (hover && ImGui::IsMouseDown(2))
            {
                ImVec2 m = ImGui::GetIO().MousePos;
                if (!s_RD2) { s_RM2 = m; s_RD2 = true; }
                else { s_DragOffX += m.x - s_RM2.x; s_DragOffY += m.y - s_RM2.y; s_RM2 = m; }
            }
            else s_RD2 = false;
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

    // --- Projected preview center (2D avatar path overrides the shared box) ---
    cx = (left + right) * 0.5f + s_DragOffX;
    cy = (top + bottom) * 0.5f + s_DragOffY;
    boxW = right - left;
    boxH = bottom - top;
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
    bool has3D = false;

    uint64_t curUserId = 0;
    if (Globals::Roblox::LocalPlayer.address)
        curUserId = Memory->read<uint64_t>(Globals::Roblox::LocalPlayer.address + Offsets::Player::UserId);

    // When model is None, always show 2D avatar image (skip Avatar3D wireframe).
    const Avatar3D::Model* mdl = nullptr;
    if (Options::Preview3D::Model != (int)Preview3D::Model::None)
        mdl = Avatar3D::GetModel(curUserId);
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
        avW = avatarW;
        avH = avatarH;

        float bboxCX = (left + right) * 0.5f + s_DragOffX;
        float bboxCY = (top + bottom) * 0.5f + s_DragOffY;
        // Box hugs the character figure (head -> feet), not the whole avatar image
        // or preview panel, which left it far too large.
        float bboxHalfW = avatarW * 0.18f;
        float bboxHalfH = avatarH * 0.42f;
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
    }

    // --- ESP overlays use the bounding box from 3D or 2D ---

    const ImU32 boxColor = IM_COL32(
        static_cast<int>(Options::ESP::BoxColor[0] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[1] * 255.f),
        static_cast<int>(Options::ESP::BoxColor[2] * 255.f),
        255);

    if (Options::ESP::Box && Options::ESP::BoxFill && Options::ESP::BoxType == 1)
    {
        if (Options::ESP::BoxFillGradient)
        {
            const float time = Options::ESP::BoxFillGradientRotate
                ? static_cast<float>(ImGui::GetTime()) * Options::ESP::BoxFillSpeed
                : 0.0f;

            const float s = sinf(time);
            const float c = cosf(time);
            const float t1 = (s + 1.0f) * 0.5f;
            const float t2 = (c + 1.0f) * 0.5f;
            const float t3 = (-s + 1.0f) * 0.5f;
            const float t4 = (-c + 1.0f) * 0.5f;

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

            drawList->AddRectFilledMultiColor(ImVec2(bLeft, bTop), ImVec2(bRight, bBot), c_tl, c_tr, c_br, c_bl);
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

    if (Options::ESP::Box && Options::ESP::BoxType == 1)
        drawList->AddRect(ImVec2(bLeft, bTop), ImVec2(bRight, bBot), boxColor, 0, 0, Options::ESP::BoxThickness);

    // 3D box (BoxType == 2): project the 3D model's axis-aligned bounding box
    // through the preview camera and draw its 12 edges in screen space, so the
    // box tracks the model as it rotates. In 2D avatar mode (or while Edit Mode
    // is on, so the box stays draggable) fall back to a pseudo-3D offset box.
    if (Options::ESP::Box && Options::ESP::BoxType == 2)
    {
        const ImU32 b3d = IM_COL32(
            static_cast<int>(Options::ESP::ESP3DColor[0] * 255.f),
            static_cast<int>(Options::ESP::ESP3DColor[1] * 255.f),
            static_cast<int>(Options::ESP::ESP3DColor[2] * 255.f),
            255);
        const float th = Options::ESP::ESP3DThickness;
        const bool in3D = Options::Preview3D::Enabled
            && (int)Options::Preview3D::Model != (int)Preview3D::Model::None;
        if (in3D && !Options::Preview3D::EditMode)
        {
            std::vector<float> corners;
            if (Preview3D::GetProjectedModelBox(corners) && corners.size() >= 24)
            {
                const float W = modelRectMax.x - modelRectMin.x;
                const float H = modelRectMax.y - modelRectMin.y;
                auto P = [&](int i) {
                    return ImVec2(modelRectMin.x + corners[i * 2] * W,
                                  modelRectMin.y + corners[i * 2 + 1] * H);
                };
                static const int edges[24] = {
                    0,1, 1,2, 2,3, 3,0,   // near face
                    4,5, 5,6, 6,7, 7,4,   // far face
                    0,4, 1,5, 2,6, 3,7     // connecting edges
                };
                for (int e = 0; e < 24; e += 2)
                    drawList->AddLine(P(edges[e]), P(edges[e + 1]), b3d, th);
            }
        }
        else
        {
            const float dw = boxW * 0.13f;
            const float dh = boxH * 0.10f;
            const ImVec2 a1(bLeft, bTop), b1(bRight, bTop), c1(bRight, bBot), d1(bLeft, bBot);
            const ImVec2 a2(bLeft + dw, bTop + dh), b2(bRight + dw, bTop + dh),
                         c2(bRight + dw, bBot + dh), d2(bLeft + dw, bBot + dh);
            drawList->AddLine(a1, b1, b3d, th); drawList->AddLine(b1, c1, b3d, th);
            drawList->AddLine(c1, d1, b3d, th); drawList->AddLine(d1, a1, b3d, th);
            drawList->AddLine(a2, b2, b3d, th); drawList->AddLine(b2, c2, b3d, th);
            drawList->AddLine(c2, d2, b3d, th); drawList->AddLine(d2, a2, b3d, th);
            drawList->AddLine(a1, a2, b3d, th); drawList->AddLine(b1, b2, b3d, th);
            drawList->AddLine(c1, c2, b3d, th); drawList->AddLine(d1, d2, b3d, th);
        }
    }

    if (Options::ESP::CornerESP || (Options::ESP::Box && Options::ESP::BoxType == 1))
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

    // Projected head screen position (3D model) so head-anchored labels (Name,
    // RigType) follow the model as it rotates, instead of staying glued to the
    // axis-aligned box top. Falls back to the box top centre when no 3D model.
    bool headAvail = false;
    ImVec2 headAnchor = ImVec2(cx, bTop);
    {
        const bool in3D = Options::Preview3D::Enabled
            && (int)Options::Preview3D::Model != (int)Preview3D::Model::None;
        float hu = 0.5f, hv = 0.0f;
        // Edit Mode keeps features on a fixed 2D box overlay (labels don't swim
        // with the rotating model), so skip the 3D head projection while editing.
        if (in3D && !Options::Preview3D::EditMode && Preview3D::GetProjectedHead(hu, hv))
        {
            const float W = rectMax.x - rectMin.x;
            const float H = rectMax.y - (rectMin.y + barH);
            headAnchor = ImVec2(rectMin.x + hu * W, (rectMin.y + barH) + hv * H);
            headAvail = true;
        }
    }

    if (Options::ESP::Health)
    {
        const float barW = Options::ESP::HealthBarWidth;
        const float healthPct = 0.65f;
        const float bx = bRight + 3.0f + Options::ESP::HealthOffsetX;
        const float by = bTop + Options::ESP::HealthOffsetY;
        const float bbY = bBot + Options::ESP::HealthOffsetY;
        drawList->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, bbY), IM_COL32(30, 30, 30, 200));
        const float filledTop = bbY - (bBot - bTop) * healthPct;

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

            float midY = (by + bbY) * 0.5f;
            if (filledTop < midY)
            {
                drawList->AddRectFilledMultiColor(
                    ImVec2(bx, filledTop),
                    ImVec2(bx + barW, midY),
                    topCol, topCol, midCol, midCol);
            }
            drawList->AddRectFilledMultiColor(
                ImVec2(bx, filledTop < midY ? midY : filledTop),
                ImVec2(bx + barW, bbY),
                midCol, midCol, botCol, botCol);
        }
        else
        {
            drawList->AddRectFilled(ImVec2(bx, filledTop), ImVec2(bx + barW, bbY), IM_COL32(80, 220, 60, 230));
        }
        drawList->AddRect(ImVec2(bx, by), ImVec2(bx + barW, bbY), IM_COL32(0, 0, 0, 255));
        FeatureDrag(kHealth, bx, by, barW, (bBot - bTop), Options::ESP::HealthOffsetX, Options::ESP::HealthOffsetY);
        if (s_HoverFeat == kHealth || s_DragFeat == kHealth)
            FeatureOutline(bx - 2.0f, by - 2.0f, barW + 4.0f, (bBot - bTop) + 4.0f);
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
        float nax = headAvail ? headAnchor.x : cx;
        float nay = headAvail ? (headAnchor.y - ts.y - 6.0f) : (bTop - 34.0f);
        float nx = nax - ts.x * 0.5f + Options::ESP::NameOffsetX;
        float ny = nay + Options::ESP::NameOffsetY;
        drawList->AddText(ImVec2(nx, ny), IM_COL32(255, 255, 255, 255), previewName.c_str());
        FeatureDrag(kName, nx, ny, ts.x, ts.y, Options::ESP::NameOffsetX, Options::ESP::NameOffsetY);
        if (s_HoverFeat == kName || s_DragFeat == kName)
            FeatureOutline(nx - 2.0f, ny - 2.0f, ts.x + 4.0f, ts.y + 4.0f);
    }

    if (Options::ESP::Distance)
    {
        const char* distText = "42 studs";
        ImFont* df = ImGui::GetFont();
        const float dsz = Options::ESP::DistanceSize;
        const ImVec2 ts = df->CalcTextSizeA(dsz, FLT_MAX, 0.0f, distText);
        float distX = cx - ts.x * 0.5f + Options::ESP::DistanceOffsetX;
        float distY = bBot + 4.0f + Options::ESP::DistanceOffsetY;
        drawList->AddText(df, dsz, ImVec2(distX, distY), IM_COL32(200, 200, 200, 255), distText);
        FeatureDrag(kDistance, distX, distY, ts.x, ts.y, Options::ESP::DistanceOffsetX, Options::ESP::DistanceOffsetY);
        if (s_HoverFeat == kDistance || s_DragFeat == kDistance)
            FeatureOutline(distX - 2.0f, distY - 2.0f, ts.x + 4.0f, ts.y + 4.0f);
    }

    if (Options::ESP::RigType)
    {
        const char* rigStr = "[R15]";
        ImFont* rf = ImGui::GetFont();
        const float rsz = Options::ESP::RigTypeSize;
        const ImVec2 ts = rf->CalcTextSizeA(rsz, FLT_MAX, 0.0f, rigStr);
        const ImU32 rigCol = IM_COL32(
            static_cast<int>(Options::ESP::RigTypeColor[0] * 255.f),
            static_cast<int>(Options::ESP::RigTypeColor[1] * 255.f),
            static_cast<int>(Options::ESP::RigTypeColor[2] * 255.f),
            255);
        float rax = headAvail ? headAnchor.x : (bRight + 10.0f);
        float ray = headAvail ? (headAnchor.y + ts.y + 4.0f) : (bTop + (bBot - bTop) * 0.5f - ts.y * 0.5f);
        float rigX = rax + Options::ESP::RigTypeOffsetX;
        float rigY = ray + Options::ESP::RigTypeOffsetY;
        drawList->AddText(rf, rsz, ImVec2(rigX, rigY), rigCol, rigStr);
        FeatureDrag(kRigType, rigX, rigY, ts.x, ts.y, Options::ESP::RigTypeOffsetX, Options::ESP::RigTypeOffsetY);
        if (s_HoverFeat == kRigType || s_DragFeat == kRigType)
            FeatureOutline(rigX - 2.0f, rigY - 2.0f, ts.x + 4.0f, ts.y + 4.0f);
    }

    // Tracers ??? drawn from the preview's bottom (or top, per TracersStart) to the
    // centre of the bounding box. In-game tracer start 3 uses the player's own
    // screen-space torso, which maps naturally to the bottom of the preview.
    if (Options::ESP::Tracers)
    {
        const ImU32 tracerCol = IM_COL32(
            static_cast<int>(Options::ESP::TracerColor[0] * 255.f),
            static_cast<int>(Options::ESP::TracerColor[1] * 255.f),
            static_cast<int>(Options::ESP::TracerColor[2] * 255.f),
            255);
        ImVec2 tStart((rectMin.x + rectMax.x) * 0.5f, rectMax.y);
        if (Options::ESP::TracersStart == 1)
            tStart.y = rectMin.y;
        ImVec2 tEnd((bLeft + bRight) * 0.5f, (bTop + bBot) * 0.5f);
        drawList->AddLine(tStart, tEnd, tracerCol, Options::ESP::TracerThickness);
    }

    // Head circle. Anchored to the 3D model's projected head when a 3D model
    // is active (so it tracks the actual head as the model rotates instead of
    // sitting at a fixed box-centre-top point); otherwise falls back to the top
    // of the projected box.
    if (Options::ESP::HeadCircle)
    {
        const ImU32 hcc = IM_COL32(
            static_cast<int>(Options::ESP::HeadCircleColor[0] * 255.f),
            static_cast<int>(Options::ESP::HeadCircleColor[1] * 255.f),
            static_cast<int>(Options::ESP::HeadCircleColor[2] * 255.f),
            230);
        float headR = EspClamp(boxW * Options::ESP::HeadCircleScale * 1.6f, 6.f, 26.f);
        float hcX, hcY;
        const bool in3D = Options::Preview3D::Enabled
            && (int)Options::Preview3D::Model != (int)Preview3D::Model::None;
        float headU, headV;
        if (in3D && Preview3D::GetProjectedHead(headU, headV))
        {
            const float W = rectMax.x - rectMin.x;
            const float H = rectMax.y - (rectMin.y + barH);
            hcX = rectMin.x + headU * W;
            hcY = (rectMin.y + barH) + headV * H;
        }
        else
        {
            hcX = (bLeft + bRight) * 0.5f;
            // 2D avatar: the character is smaller than the avatar image (empty
            // grid margin below), so anchor head/body inside the image. Head sits
            // centred between the image top edge and the neck; radius comes from
            // the projected head height so it matches the actual head size.
            float cAvH2 = (avH > 0.f) ? avH : (bBot - bTop);
            const float hTop   = cy - cAvH2 * 0.36f; // head top
            const float hHeadH = cAvH2 * 0.22f;      // head height (top -> neck)
            hcY = hTop + hHeadH * 0.50f;             // head centre (on the face)
            float hr = hHeadH * 0.5f * 1.05f;        // half the head height
            headR = EspClamp(hr, 4.f, 30.f);
        }
        drawList->AddCircle(ImVec2(hcX, hcY), headR, hcc, 32, Options::ESP::HeadCircleThickness);
    }

    // Skeleton ??? all joints projected through the same rotation
    if (Options::ESP::Skeleton)
    {
        const ImU32 skel = IM_COL32(
            static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
            static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
            static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
            255);
        const float thick = EspClamp(Options::ESP::SkeletonThickness, 1.0f, 10.0f);

        const bool in3D = Options::Preview3D::Enabled
            && (int)Options::Preview3D::Model != (int)Preview3D::Model::None;

        // The single "Skeleton" checkbox on the Visuals tab (ESP::Skeleton)
        // controls the in-world skeleton, the 3D projected skeleton, and the
        // skeleton drawn inside the 3D preview box. When a 3D model is active we
        // draw the projected model-space skeleton; otherwise the 2D avatar.
        const bool skelIn3D = in3D;
        if (skelIn3D)
        {
            // ?????? 3D model: use the projected R6 skeleton so the bones follow the
            //    model's 3D rotation (and match the actual geometry proportions,
            //    avoiding the stretched look of a flat box-relative skeleton).
            std::vector<float> segs;
            if (Preview3D::GetProjectedSkeleton(segs))
            {
                const float W = rectMax.x - rectMin.x;
                const float H = rectMax.y - (rectMin.y + barH);
                const float ox = rectMin.x, oy = rectMin.y + barH;
                for (size_t i = 0; i + 3 < segs.size(); i += 4)
                {
                    drawList->AddLine(ImVec2(ox + segs[i] * W, oy + segs[i + 1] * H),
                                      ImVec2(ox + segs[i + 2] * W, oy + segs[i + 3] * H),
                                      skel, thick);
                }
            }
        }
        else
        {
            // 2D avatar portrait: skeleton anchored to the avatar image, drawn as
            // a clean front-facing stick figure so bones stay connected. jx is the
            // lateral fraction across the body width, jy the vertical fraction from
            // the top of the body down to the feet.
            const float cAvW = (avW > 0.f) ? avW : boxW;
            const float cAvH = (avH > 0.f) ? avH : boxH;
            const float halfW = cAvW * 0.22f;
            const float top   = cy - cAvH * 0.40f;
            const float bodyH = cAvH * 0.84f;
            auto proj = [&](float jx, float jy) -> ImVec2 {
                return ImVec2(cx + jx * halfW, top + jy * bodyH);
            };

            const ImVec2 neck   = proj(0.0f, 0.33f);
            const ImVec2 hip    = proj(0.0f, 0.62f);
            const ImVec2 lShoulder = proj(-0.62f, 0.40f);
            const ImVec2 rShoulder = proj( 0.62f, 0.40f);
            const ImVec2 lHand     = proj(-0.66f, 0.58f);
            const ImVec2 rHand     = proj( 0.66f, 0.58f);
            const ImVec2 lHipJ     = proj(-0.20f, 0.62f);
            const ImVec2 rHipJ     = proj( 0.20f, 0.62f);
            const ImVec2 lKnee     = proj(-0.20f, 0.78f);
            const ImVec2 rKnee     = proj( 0.20f, 0.78f);
            const ImVec2 lFoot     = proj(-0.20f, 0.92f);
            const ImVec2 rFoot     = proj( 0.20f, 0.92f);

            // Spine + shoulder bar keep the figure connected end to end.
            drawList->AddLine(neck, hip, skel, thick);
            drawList->AddLine(lShoulder, rShoulder, skel, thick);
            // Straight arms (shoulder -> hand, no bent elbow).
            drawList->AddLine(lShoulder, lHand, skel, thick);
            drawList->AddLine(rShoulder, rHand, skel, thick);
            // Legs: pelvis links the hips back to the spine so they don't float.
            drawList->AddLine(hip, lHipJ, skel, thick);
            drawList->AddLine(hip, rHipJ, skel, thick);
            drawList->AddLine(lHipJ, lKnee, skel, thick);
            drawList->AddLine(lKnee, lFoot, skel, thick);
            drawList->AddLine(rHipJ, rKnee, skel, thick);
            drawList->AddLine(rKnee, rFoot, skel, thick);
        }
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

    DrawControls();

    drawList->PopClipRect();

    // Settings panel - drawn on the SAME (foreground) draw list as the preview so
    // it always renders on top, opening right under the "3D MODEL PREVIEW" bar.
    static bool s_ModelOpen = false;
    if (s_SettingsOpen)
    {
        ImVec2 display = ImGui::GetIO().DisplaySize;
        const float sc = UI::sc;
        const float pW = 158.0f * sc;
        // Open under the title bar, aligned to the left edge of the preview.
        ImVec2 pos(rectMin.x + 8.0f, rectMin.y + barH + 4.0f);
        if (pos.x + pW > display.x - 8.0f) pos.x = display.x - pW - 8.0f;

        // Entrance animation: fade in + rise over ~0.28s (smoothstep eased).
        float age = (float)(ImGui::GetTime() - s_SettingsOpenAt);
        age = age < 0.f ? 0.f : (age > 0.28f ? 1.0f : age / 0.28f);
        const float easeA = age * age * (3.f - 2.f * age);
        const float slideY = (1.f - easeA) * 10.f;
        pos.y += slideY;
        const float alpha = 0.30f + 0.70f * easeA;

        // Local alias matching ui.h's bare `U` color helper.
        const auto U = UI::U;

        const float padX = 10.0f * sc;
        const float rowH = 24.0f * sc;

        const char* models[] = { "None", "Roblox R15", "Tung Tung Sahur", "Mario" };
        const bool is3D = Options::Preview3D::Model != (int)Preview3D::Model::None;

        // Panel height: model row, enabled, (auto rotate + edit when 3D), reset button
        float pHeight = padX * 2.0f + rowH + rowH;
        if (is3D) pHeight += rowH * 2.0f + 6.0f * sc;
        pHeight += rowH + 8.0f * sc;

        const ImVec2 pMin = pos;
        const ImVec2 pMax(pos.x + pW, pos.y + pHeight);

        // Panel background + border (drawn like the preview chrome so it sits in front)
        drawList->AddRectFilled(pMin, pMax, IM_COL32(16, 21, 27, (int)(238.0f * alpha)), 6.0f);
        drawList->AddRect(pMin, pMax, IM_COL32(42, 53, 66, (int)(255.0f * alpha)), 6.0f, 0, 1.0f);

        float curY = pMin.y + padX;

        auto CenterY = [&]() { return curY + (rowH - ImGui::GetFont()->FontSize) * 0.5f; };

        // ---- Model row (click to expand a dropdown right below) ----
        {
            const ImVec2 rmin(pos.x, curY), rmax(pos.x + pW, curY + rowH);
            const bool hover = ImGui::IsMouseHoveringRect(rmin, rmax, false);
            if (hover) drawList->AddRectFilled(rmin, rmax, IM_COL32(30, 39, 50, (int)(165.0f * alpha)), 4.0f);

            // Close X (top-right corner) - dismisses the panel.
            {
                const float xs = 4.0f * sc;
                const ImVec2 xc(pMax.x - padX - 5.0f * sc, curY + rowH * 0.5f);
                const bool xHov = ImGui::IsMouseHoveringRect(ImVec2(xc.x - 8.0f * sc, curY),
                                                             ImVec2(xc.x + 8.0f * sc, curY + rowH), false);
                if (xHov)
                    drawList->AddCircleFilled(xc, 8.0f * sc, IM_COL32(60, 70, 84, 200), 24);
                const ImU32 xCol = xHov ? U(UI::P.textStrong) : U(UI::P.textDim);
                drawList->AddLine(ImVec2(xc.x - xs, xc.y - xs), ImVec2(xc.x + xs, xc.y + xs), xCol, 1.6f);
                drawList->AddLine(ImVec2(xc.x + xs, xc.y - xs), ImVec2(xc.x - xs, xc.y + xs), xCol, 1.6f);
                if (xHov && ImGui::IsMouseClicked(0)) s_SettingsOpen = false;
            }

            drawList->AddText(ImVec2(pMin.x + padX, CenterY()), U(UI::P.textMid), "Model");
            ImVec2 vT = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f,
                                                         models[Options::Preview3D::Model]);
            // Dropdown arrow (right of the value text, left of the close X)
            const ImVec2 aC(pMax.x - padX - 24.0f * sc, curY + rowH * 0.5f);
            const float as = 3.2f * sc;
            const float arc = 3.0f * sc;
            drawList->AddTriangleFilled(ImVec2(aC.x - as, aC.y - arc),
                                        ImVec2(aC.x + as, aC.y - arc),
                                        ImVec2(aC.x, aC.y + arc),
                                        U(UI::Mix(UI::P.textDim, UI::P.accent, s_ModelOpen ? 1.0f : 0.0f)));
            drawList->AddText(ImVec2(aC.x - vT.x - 7.0f * sc, CenterY()), U(UI::P.textStrong),
                              models[Options::Preview3D::Model]);
            if (hover && ImGui::IsMouseClicked(0)) s_ModelOpen = !s_ModelOpen;
            curY += rowH;
        }

        // ---- Enabled, Auto Rotate, Edit Mode (checkbox rows) ----
        auto CheckboxRow = [&](const char* label, bool* v) -> bool
        {
            const ImVec2 rmin(pos.x, curY), rmax(pos.x + pW, curY + rowH);
            const bool hover = ImGui::IsMouseHoveringRect(rmin, rmax, false);
            if (hover) drawList->AddRectFilled(rmin, rmax, IM_COL32(30, 39, 50, (int)(165.0f * alpha)), 4.0f);
            drawList->AddText(ImVec2(pMin.x + padX, CenterY()), U(*v ? UI::P.textStrong : UI::P.textMid), label);
            // Checkbox box aligned right
            const float box = 15.0f * sc;
            const ImVec2 bMin(pos.x + pW - padX - box, curY + (rowH - box) * 0.5f);
            const ImVec2 bMax(bMin.x + box, bMin.y + box);
            if (*v)
            {
                drawList->AddRectFilled(bMin, bMax, U(UI::P.accent), 4.0f);
                const ImVec2 bc((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);
                drawList->AddLine(ImVec2(bMin.x + 3.5f * sc, bc.y + 0.5f * sc),
                                  ImVec2(bMin.x + 6.0f * sc, bc.y + 3.0f * sc), IM_COL32(0, 0, 0, 230), 2.0f);
                drawList->AddLine(ImVec2(bMin.x + 6.0f * sc, bc.y + 3.0f * sc),
                                  ImVec2(bMin.x + 10.5f * sc, bc.y - 3.5f * sc), IM_COL32(0, 0, 0, 230), 2.0f);
            }
            else
                drawList->AddRect(bMin, bMax, U(UI::P.borderDim), 4.0f, 0, 1.4f);
            const bool clicked = hover && ImGui::IsMouseClicked(0);
            curY += rowH;
            return clicked;
        };

        if (CheckboxRow("Enabled", &Options::Preview3D::Enabled)) {}

        if (is3D)
        {
            if (Options::Preview3D::EditMode) Options::Preview3D::AutoSpin = false;
            if (CheckboxRow("Auto Rotate", &Options::Preview3D::AutoSpin)) {}
            curY += 6.0f * sc; // thin gap before Edit Mode
            if (CheckboxRow("Edit Mode", &Options::Preview3D::EditMode))
            {
                if (Options::Preview3D::EditMode)
                {
                    Options::Preview3D::AutoSpin = false;
                    Preview3D::ResetView();
                    Preview3D::NotifyManual();
                }
            }
        }

        // ---- Reset View button ----
        {
            const float btnW = pW - padX * 2.0f;
            const ImVec2 r0(pos.x + padX, curY + 2.0f * sc);
            const ImVec2 r1(r0.x + btnW, r0.y + rowH);
            const bool hover = ImGui::IsMouseHoveringRect(r0, r1, false);
            drawList->AddRectFilled(r0, r1,
                hover ? U(UI::Mix(UI::P.surfaceAlt, UI::P.accentGlow, 0.45f)) : U(UI::P.surfaceAlt), 5.0f);
            const char* lbl = "Reset View";
            ImVec2 lT = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, lbl);
            drawList->AddText(ImVec2((r0.x + r1.x - lT.x) * 0.5f,
                                     r0.y + (rowH - lT.y) * 0.5f),
                              U(hover ? UI::P.textStrong : UI::P.textMid), lbl);
            if (hover && ImGui::IsMouseClicked(0))
            {
                Preview3D::ResetView();
                Preview3D::NotifyManual();
            }
            curY += rowH + 4.0f * sc;
        }

        // ---- Model dropdown (overlay drawn last so it's on top) ----
        if (s_ModelOpen)
        {
            const float dH = 4.0f * rowH;
            const ImVec2 dMin(pos.x, pMin.y + padX + rowH);
            const ImVec2 dMax(pos.x + pW, dMin.y + dH);
            drawList->AddRectFilled(dMin, dMax, IM_COL32(20, 26, 33, (int)(240.0f)), 5.0f);
            drawList->AddRect(dMin, dMax, U(UI::P.borderDim), 5.0f, 0, 1.0f);
            for (int i = 0; i < 4; ++i)
            {
                const float y0 = dMin.y + i * rowH;
                const ImVec2 r0(dMin.x, y0), r1(dMax.x, y0 + rowH);
                const bool sel = Options::Preview3D::Model == i;
                const bool hover = ImGui::IsMouseHoveringRect(r0, r1, false);
                if (hover) drawList->AddRectFilled(r0, r1, IM_COL32(32, 42, 54, 255), 4.0f);
                if (sel) drawList->AddRectFilled(r0, r1, IM_COL32(38, 54, 74, 150), 4.0f);
                drawList->AddText(ImVec2(dMin.x + padX, y0 + (rowH - ImGui::GetFont()->FontSize) * 0.5f),
                                  U(sel ? UI::P.textStrong : UI::P.textMid), models[i]);
                if (hover && ImGui::IsMouseClicked(0))
                {
                    Options::Preview3D::Model = i;
                    s_ModelOpen = false;
                }
            }
        }
    }

    // ?????? Per-feature customize popup (right-click a feature) ??????
    if (s_PopupFeat >= 0)
    {
        const char* featName = s_PopupFeat == kName ? "Name"
            : s_PopupFeat == kDistance ? "Distance"
            : s_PopupFeat == kHealth ? "Health Bar"
            : "RigType";
        bool open = true;
        ImVec2 pos(rectMax.x + 8.0f, rectMin.y);
        ImVec2 display = ImGui::GetIO().DisplaySize;
        const float winW = 250.0f * UI::sc;
        if (pos.x + winW > display.x - 8.0f) pos.x = display.x - winW - 8.0f;

        float age = (float)(ImGui::GetTime() - s_FeatOpenAt);
        age = age < 0.f ? 0.f : (age > 0.28f ? 1.0f : age / 0.28f);
        const float easeA = age * age * (3.f - 2.f * age);      // smoothstep
        const float slideY = (1.f - easeA) * 8.f;
        if (easeA < 1.f)
            ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + slideY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f + 0.55f * easeA);
        char title[64];
        snprintf(title, sizeof(title), "Customize: %s###previewFeatPopup", featName);
        if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        {
            // Right-aligned row helpers so every feature panel shares the same
            // symmetric layout (label left, control right at a fixed column).
            const float ctrX = winW - 128.0f;
            const float ctrlW = (winW - 128.0f) - 10.0f;
            auto RowSlider = [&](const char* label, float min, float max, float* v, const char* fmt) {
                ImGui::TextUnformatted(label);
                ImGui::SameLine(ctrX);
                ImGui::SetNextItemWidth(ctrlW);
                ImGui::SliderFloat(("##" + std::string(label)).c_str(), v, min, max, fmt);
            };
            auto RowCheck  = [&](const char* label, bool* b) {
                ImGui::TextUnformatted(label);
                ImGui::SameLine(ctrX);
                ImGui::SetNextItemWidth(ctrlW);
                ImGui::Checkbox(("##" + std::string(label)).c_str(), b);
            };
            auto RowColor  = [&](const char* label, float* col, int comps) {
                ImGui::TextUnformatted(label);
                ImGui::SameLine(ctrX);
                ImVec4 sw = (comps == 4) ? ImVec4(col[0], col[1], col[2], col[3])
                                         : ImVec4(col[0], col[1], col[2], 1.0f);
                char popId[96]; snprintf(popId, sizeof(popId), "##cp_%s", label);
                ImGui::PushID(label);
                if (ImGui::ColorButton(popId, sw, ImGuiColorEditFlags_NoTooltip, ImVec2(ctrlW, 0.0f)))
                    ImGui::OpenPopup(popId);
                if (ImGui::BeginPopup(popId))
                {
                    if (comps == 4)
                        ImGui::ColorPicker4("##picker", col,
                            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview);
                    else
                        ImGui::ColorPicker3("##picker", col, ImGuiColorEditFlags_NoSidePreview);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            };

            if (s_PopupFeat == kName)
            {
                RowSlider("Size", 8.0f, 32.0f, &Options::ESP::NameSize, "%.0f");
                RowSlider("Thickness", 0.0f, 5.0f, &Options::ESP::NameThickness, "%.1f");
                ImGui::Separator();
                RowColor("Color", Options::ESP::Color, 3);
            }
            else if (s_PopupFeat == kDistance)
            {
                RowSlider("Size", 8.0f, 32.0f, &Options::ESP::DistanceSize, "%.0f");
                RowSlider("Thickness", 0.0f, 5.0f, &Options::ESP::DistanceThickness, "%.1f");
                ImGui::Separator();
                RowColor("Color", Options::ESP::DistanceColor, 3);
            }
            else if (s_PopupFeat == kHealth)
            {
                RowSlider("Width", 1.0f, 12.0f, &Options::ESP::HealthBarWidth, "%.1f");
                RowCheck("Gradient", &Options::ESP::GradientHealthbar);
                ImGui::Separator();
                RowColor("Top", Options::ESP::HealthbarTopColor, 4);
                RowColor("Middle", Options::ESP::HealthbarMiddleColor, 4);
                RowColor("Bottom", Options::ESP::HealthbarBottomColor, 4);
            }
            else if (s_PopupFeat == kRigType)
            {
                RowSlider("Size", 8.0f, 32.0f, &Options::ESP::RigTypeSize, "%.0f");
                RowSlider("Thickness", 0.0f, 5.0f, &Options::ESP::RigTypeThickness, "%.1f");
                ImGui::Separator();
                RowColor("Color", Options::ESP::RigTypeColor, 3);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        if (!open || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) s_PopupFeat = -1;
    }
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

        if (!player.Name.empty() && !PlayerFilter::EspVisible(player.Name))
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

    // ?????? Background / frame per theme ??????
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

        if (!player.Name.empty() && !PlayerFilter::EspVisible(player.Name))
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
