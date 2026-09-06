#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include "../rbx/math/math.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/utils/W2S.h"
#include "imgui/imgui.h"

namespace WorldWeather
{
    static std::mt19937& rng()
    {
        static std::mt19937 g{ std::random_device{}() };
        return g;
    }

    static float randf(float lo, float hi)
    {
        std::uniform_real_distribution<float> d(lo, hi);
        return d(rng());
    }

    static Vectors::Vector3 GetLocalPlayerPos()
    {
        auto localChar = Globals::Roblox::LocalPlayer.Character();
        auto hrp = localChar.FindFirstChild("HumanoidRootPart");
        if (!hrp.address)
            hrp = localChar.FindFirstChild("Torso");
        if (!hrp.address)
            return { 0, 0, 0 };
        return hrp.Position();
    }

    static bool OnScreen(Vectors::Vector2 sc)
    {
        if (sc.x < -1.f && sc.y < -1.f) return false;
        const auto& dims = Globals::Viewport::Dimensions;
        if (dims.x <= 0 || dims.y <= 0) return false;
        if (sc.x < 0 || sc.y < 0) return false;
        if (sc.x > dims.x || sc.y > dims.y) return false;
        return true;
    }

    // =========================================================================
    //  SNOW
    // =========================================================================
    struct Snowflake
    {
        Vectors::Vector3 pos;
        float target_y;
        float drift_x, drift_z;
        float spin, spin_vel;
        float size;
        float land_timer;
        bool  landed;
        bool  active;
    };

    static std::vector<Snowflake> s_flakes;
    static float s_snow_last_t = 0.f;

    static void SpawnFlake(Snowflake& f, const Vectors::Vector3& player)
    {
        const float R = 180.f;
        f.pos.x = player.x + randf(-R, R);
        f.pos.z = player.z + randf(-R, R);
        f.target_y = player.y - 3.f;
        f.pos.y = f.target_y + randf(6.f, 80.f);
        f.drift_x = randf(-0.4f, 0.4f);
        f.drift_z = randf(-0.4f, 0.4f);
        f.spin = randf(0.f, 6.2832f);
        f.spin_vel = randf(0.8f, 2.8f) * (randf(0.f, 1.f) > 0.5f ? 1.f : -1.f);
        f.size = randf(0.14f, 0.40f);
        f.land_timer = 0.f;
        f.landed = false;
        f.active = true;
    }

    static void DrawSnowflakeShape(ImDrawList* dl, float cx, float cy,
        float r, float angle, ImU32 col, ImU32 col_core)
    {
        const float PI3 = 3.14159265f / 3.f;
        for (int i = 0; i < 6; ++i)
        {
            float a = angle + i * PI3;
            float ca = cosf(a), sa = sinf(a);
            float ex = cx + ca * r, ey = cy + sa * r;
            dl->AddLine(ImVec2(cx, cy), ImVec2(ex, ey), col, 1.0f);
            float bl = r * 0.42f;
            float bx = cx + ca * r * 0.55f;
            float by_ = cy + sa * r * 0.55f;
            for (int b = -1; b <= 1; b += 2)
            {
                float ba = a + b * PI3;
                dl->AddLine(ImVec2(bx, by_),
                    ImVec2(bx + cosf(ba) * bl, by_ + sinf(ba) * bl),
                    col, 0.8f);
            }
        }
        dl->AddCircleFilled(ImVec2(cx, cy), r * 0.20f, col_core, 6);
    }

    static void TickSnow(ImDrawList* dl)
    {
        float now = (float)ImGui::GetTime();
        float dt = now - s_snow_last_t;
        if (dt > 0.1f) dt = 0.1f;
        s_snow_last_t = now;

        Vectors::Vector3 player = GetLocalPlayerPos();
        if (player.x == 0.f && player.y == 0.f && player.z == 0.f) return;

        int desired = std::clamp(Options::Weather::Intensity, 50, 2000);
        if ((int)s_flakes.size() != desired)
        {
            int old = (int)s_flakes.size();
            s_flakes.resize(desired);
            for (int i = old; i < desired; ++i) s_flakes[i].active = false;
        }

        const float R = 180.f;
        const float spd = Options::Weather::Speed * 15.f;

        for (auto& f : s_flakes)
        {
            if (!f.active) { SpawnFlake(f, player); continue; }

            if (f.landed)
            {
                f.land_timer -= dt;
                if (f.land_timer <= 0.f) { SpawnFlake(f, player); continue; }
            }
            else
            {
                f.pos.y -= spd * dt;
                f.pos.x += f.drift_x * dt + Options::Weather::Wind * dt;
                f.pos.z += f.drift_z * dt;
                f.spin += f.spin_vel * dt;
                if (f.pos.y <= f.target_y + 0.05f)
                {
                    f.pos.y = f.target_y + 0.05f;
                    f.landed = true;
                    f.land_timer = randf(0.5f, 2.0f);
                }
                float dx = f.pos.x - player.x, dz = f.pos.z - player.z;
                if (dx * dx + dz * dz > R * R) { SpawnFlake(f, player); continue; }
            }

            Vectors::Vector2 sc = WorldToScreen(f.pos);
            if (!OnScreen(sc)) continue;

            float ddx = f.pos.x - player.x, ddy = f.pos.y - player.y, ddz = f.pos.z - player.z;
            float dist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            float alpha = 1.f - dist / (R * 0.85f);
            if (alpha < 0.02f) continue;
            alpha = std::clamp(alpha, 0.f, 1.f);

            float screen_r = Options::Weather::SnowSize * 110.f / (dist + 0.5f);
            screen_r = std::clamp(screen_r, 1.f, 13.f);

            float cr = Options::Weather::Color[0] * 255.f;
            float cg = Options::Weather::Color[1] * 255.f;
            float cb = Options::Weather::Color[2] * 255.f;

            if (f.landed)
            {
                float fade = std::clamp(f.land_timer / 0.6f, 0.f, 1.f);
                alpha *= fade;
                if (alpha < 0.02f) continue;
                int a = (int)(alpha * 220.f);
                float rx = screen_r * 2.2f;
                float ry = std::max(screen_r * 0.3f, 0.5f);
                float avg_r = (rx + ry) * 0.5f;
                dl->AddCircleFilled(ImVec2(sc.x, sc.y), avg_r + 1.5f,
                    IM_COL32((int)(cr * 0.63f), (int)(cg * 0.75f), (int)cb, a / 5), 10);
                dl->AddCircleFilled(ImVec2(sc.x, sc.y), avg_r,
                    IM_COL32((int)cr, (int)cg, (int)cb, a), 10);
            }
            else
            {
                int a = (int)(alpha * 220.f);
                int a_core = (int)(alpha * 255.f);
                ImU32 arm = IM_COL32((int)(cr * 0.78f), (int)(cg * 0.86f), (int)cb, a);
                ImU32 core = IM_COL32(255, 255, 255, a_core);
                if (screen_r > 2.5f)
                    DrawSnowflakeShape(dl, sc.x, sc.y, screen_r, f.spin, arm, core);
                else
                {
                    dl->AddCircleFilled(ImVec2(sc.x, sc.y), screen_r, arm, 6);
                    dl->AddCircleFilled(ImVec2(sc.x, sc.y), screen_r * 0.3f, core, 4);
                }
            }
        }
    }

    // =========================================================================
    //  RAIN
    // =========================================================================
    struct Raindrop
    {
        Vectors::Vector3 pos;
        float target_y;
        float drift_x, drift_z;
        float size;
        float splash_timer;
        bool  splashing;
        bool  active;
    };

    static std::vector<Raindrop> s_drops;
    static float s_rain_last_t = 0.f;

    static void SpawnDrop(Raindrop& d, const Vectors::Vector3& player)
    {
        const float R = 150.f;
        d.pos.x = player.x + randf(-R, R);
        d.pos.z = player.z + randf(-R, R);
        d.target_y = player.y - 3.f;
        d.pos.y = d.target_y + randf(8.f, 90.f);
        d.drift_x = 1.5f + randf(-0.5f, 0.5f);
        d.drift_z = 0.5f + randf(-0.3f, 0.3f);
        d.size = randf(0.5f, 0.9f);
        d.splash_timer = 0.f;
        d.splashing = false;
        d.active = true;
    }

    static void TickRain(ImDrawList* dl)
    {
        float now = (float)ImGui::GetTime();
        float dt = now - s_rain_last_t;
        if (dt > 0.1f) dt = 0.1f;
        s_rain_last_t = now;

        Vectors::Vector3 player = GetLocalPlayerPos();
        if (player.x == 0.f && player.y == 0.f && player.z == 0.f) return;

        int desired = std::clamp(Options::Weather::Intensity, 50, 3000);
        if ((int)s_drops.size() != desired)
        {
            int old = (int)s_drops.size();
            s_drops.resize(desired);
            for (int i = old; i < desired; ++i) s_drops[i].active = false;
        }

        const float R = 150.f;
        const float spd = Options::Weather::Speed * 25.f;

        for (auto& d : s_drops)
        {
            if (!d.active) { SpawnDrop(d, player); continue; }

            if (d.splashing)
            {
                d.splash_timer -= dt;
                if (d.splash_timer <= 0.f) { SpawnDrop(d, player); continue; }
            }
            else
            {
                d.pos.y -= spd * dt;
                d.pos.x += d.drift_x * dt + Options::Weather::Wind * dt;
                d.pos.z += d.drift_z * dt;
                if (d.pos.y <= d.target_y + 0.05f)
                {
                    d.pos.y = d.target_y + 0.05f;
                    d.splashing = true;
                    d.splash_timer = randf(0.08f, 0.18f);
                }
                float dx = d.pos.x - player.x, dz = d.pos.z - player.z;
                if (dx * dx + dz * dz > R * R) { SpawnDrop(d, player); continue; }
            }

            Vectors::Vector2 sc = WorldToScreen(d.pos);
            if (!OnScreen(sc)) continue;

            float ddx = d.pos.x - player.x, ddy = d.pos.y - player.y, ddz = d.pos.z - player.z;
            float dist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            float alpha = 1.f - dist / (R * 0.85f);
            if (alpha < 0.02f) continue;
            alpha = std::clamp(alpha, 0.f, 1.f);

            int a = (int)(alpha * 100.f);

            float cr = Options::Weather::Color[0] * 255.f;
            float cg = Options::Weather::Color[1] * 255.f;
            float cb = Options::Weather::Color[2] * 255.f;

            if (d.splashing)
            {
                float prog = std::clamp(1.f - d.splash_timer / 0.15f, 0.f, 1.f);
                int sa = (int)(alpha * 90.f * (1.f - prog));
                float r1 = std::clamp(2.f + prog * 6.f, 1.f, 8.f);
                dl->AddCircle(ImVec2(sc.x, sc.y), r1,
                    IM_COL32((int)(cr * 0.63f), (int)(cg * 0.82f), (int)cb, sa), 12, 0.9f);
            }
            else
            {
                Vectors::Vector3 top_pos = d.pos;
                top_pos.y += d.size * 1.0f * (spd / 25.f);
                Vectors::Vector2 sc_top = WorldToScreen(top_pos);
                if (!OnScreen(sc_top)) continue;

                float lx = sc_top.x - sc.x;
                float ly = sc_top.y - sc.y;
                float ll = sqrtf(lx * lx + ly * ly);
                if (ll < 1.f) ll = 1.f;
                if (ll > 12.f) { float s = 12.f / ll; lx *= s; ly *= s; }

                dl->AddLine(ImVec2(sc.x - 0.5f, sc.y),
                    ImVec2(sc.x - 0.5f + lx, sc.y + ly),
                    IM_COL32((int)(cr * 0.71f), (int)(cg * 0.86f), (int)cb, a / 4), Options::Weather::RainThickness + 0.7f);
                dl->AddLine(ImVec2(sc.x, sc.y),
                    ImVec2(sc.x + lx, sc.y + ly),
                    IM_COL32((int)(cr * 0.63f), (int)(cg * 0.82f), (int)cb, a), Options::Weather::RainThickness);
            }
        }
    }

    inline void Render(ImDrawList* dl)
    {
        if (!Options::Weather::Enabled)
            return;

        if (!dl)
            return;

        if (!Globals::Viewport::Valid)
            return;

        if (Options::Weather::Type == 0)
        {
            if (!s_drops.empty()) s_drops.clear();
            TickSnow(dl);
        }
        else
        {
            if (!s_flakes.empty()) s_flakes.clear();
            TickRain(dl);
        }
    }
}
