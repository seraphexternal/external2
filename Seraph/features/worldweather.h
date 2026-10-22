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

// World-space precipitation, drawn as a screen-space overlay.
//
// The particles live in real 3D world space around the player, are projected
// per frame, and are drawn back-to-front so near particles correctly occlude
// far ones. That ordering is what separates this from a flat sheet of stickers:
// without it, a foreground snowflake gets punched through by one behind it.
//
// Three things carry the realism, and each was wrong in the original port:
//
//   1. Colour. Rain is desaturated cool grey, snow is near-white, and each is
//      tinted by DISTANCE toward the sky haze. The old code multiplied each
//      channel by a different factor (cr*0.71, cg*0.86, cb), which is not
//      "dimmer" - it is a hue shift, and it forced everything blue no matter
//      what colour you picked. All shading here scales the three channels
//      uniformly, so hue is preserved.
//
//   2. Rain streaks follow the real velocity vector. The old streak offset the
//      second point by +Y only, so the streak stayed vertical even in strong
//      wind, which is the one thing you always notice when rain looks fake.
//      Here both ends of the streak are projected from world space along the
//      actual velocity, so it slants with the wind and foreshortens correctly.
//
//   3. Snowflakes tumble. A real crystal is a flat hexagonal plate that rotates
//      and flips, so it periodically presents edge-on and looks narrow. The old
//      shape was a rigid 2D star. Here the arm radius is scaled per-axis by a
//      tumble phase, which reads as a plate turning in 3D.
//
// Note on ground height: there is no downward-raycast primitive in this SDK, so
// the landing plane stays approximate (the player's foot level). It is refreshed
// every tick rather than frozen at spawn, which keeps rain landing sensibly when
// the player moves up or down a slope.
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
        if (!localChar.address)
            return { 0, 0, 0 };

        auto hrp = localChar.FindFirstChild("HumanoidRootPart");
        if (!hrp.address)
            hrp = localChar.FindFirstChild("Torso");
        if (!hrp.address)
            return { 0, 0, 0 };

        return hrp.Position();
    }

    static bool OnScreen(Vectors::Vector2 sc)
    {
        // WorldToScreen returns (-1,-1) for anything behind the near plane, and
        // this also rejects that.
        const auto& dims = Globals::Viewport::Dimensions;
        if (dims.x <= 0.f || dims.y <= 0.f)
            return false;
        if (sc.x < 0.f || sc.y < 0.f)
            return false;
        if (sc.x > dims.x || sc.y > dims.y)
            return false;
        return true;
    }

    // Scale brightness uniformly and blend toward the haze colour with distance.
    // Returns a packed colour. Uniform scaling is deliberate: scaling channels
    // by different amounts is a hue shift, not a brightness change.
    static ImU32 Shade(const float rgb[3], float brightness, float dist, float alpha01)
    {
        float r = rgb[0] * brightness;
        float g = rgb[1] * brightness;
        float b = rgb[2] * brightness;

        if (Options::World::Weather::Haze)
        {
            // Haze weight ramps in over the far half of the spawn radius, so
            // near particles keep full colour and distance reads correctly.
            float haze = std::clamp((dist - 55.f) / 125.f, 0.f, 0.72f);

            r += (Options::World::Weather::HazeColor[0] - r) * haze;
            g += (Options::World::Weather::HazeColor[1] - g) * haze;
            b += (Options::World::Weather::HazeColor[2] - b) * haze;
        }

        const int a = static_cast<int>(std::clamp(alpha01, 0.f, 1.f) * 255.f);
        return IM_COL32(
            static_cast<int>(std::clamp(r, 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(g, 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(b, 0.f, 1.f) * 255.f),
            a);
    }

    // =========================================================================
    //  SNOW
    // =========================================================================
    struct Snowflake
    {
        Vectors::Vector3 pos;
        float target_y = 0.f;
        float drift_x = 0.f, drift_z = 0.f;
        float spin = 0.f, spin_vel = 0.f;
        float tumble = 0.f, tumble_vel = 0.f;
        float sway_amp = 0.f, sway_freq = 0.f, sway_phase = 0.f;
        float size = 0.f;
        float land_timer = 0.f;

        // Per-frame projection scratch, so the depth sort does not re-project.
        float screen_x = 0.f, screen_y = 0.f, screen_r = 0.f;
        float alpha = 0.f, squash = 1.f;

        bool  landed = false;
        bool  active = false;
    };

    static std::vector<Snowflake> s_flakes;
    static float s_snow_last_t = 0.f;

    static void SpawnFlake(Snowflake& f, const Vectors::Vector3& player, float groundY)
    {
        const float R = 180.f;
        f.pos.x = player.x + randf(-R, R);
        f.pos.z = player.z + randf(-R, R);
        f.target_y = groundY;
        f.pos.y = groundY + randf(6.f, 80.f);

        f.drift_x = randf(-0.4f, 0.4f);
        f.drift_z = randf(-0.4f, 0.4f);

        f.spin = randf(0.f, 6.2832f);
        f.spin_vel = randf(0.4f, 1.6f) * (randf(0.f, 1.f) > 0.5f ? 1.f : -1.f);

        // Tumble is a separate, slower axis from spin. Real crystals rotate
        // about their normal (spin) while also flipping (tumble), and the two
        // rates are not correlated.
        f.tumble = randf(0.f, 6.2832f);
        f.tumble_vel = randf(0.25f, 1.1f) * (randf(0.f, 1.f) > 0.5f ? 1.f : -1.f);

        f.sway_amp = randf(0.25f, 0.9f);
        f.sway_freq = randf(0.4f, 1.3f);
        f.sway_phase = randf(0.f, 6.2832f);

        f.size = randf(0.14f, 0.40f);
        f.land_timer = 0.f;
        f.landed = false;
        f.active = true;
    }

    // A hexagonal plate seen at a shallow angle. The arm radius is scaled
    // per-axis, so at the extremes of the tumble phase the crystal presents
    // nearly edge-on and collapses to a sliver - which is what sells the depth.
    static void DrawSnowflakeShape(ImDrawList* dl, float cx, float cy,
        float r, float angle, float squash, ImU32 col, ImU32 col_core)
    {
        const float PI3 = 3.14159265f / 3.f;
        const float sx = r * squash;
        const float sy = r;

        for (int i = 0; i < 6; ++i)
        {
            const float a = angle + i * PI3;
            const float ca = cosf(a), sa = sinf(a);

            dl->AddLine(ImVec2(cx, cy), ImVec2(cx + ca * sx, cy + sa * sy), col, 1.0f);

            // Branches at 55% out along the arm, angled ±60 degrees.
            const float bl = r * 0.42f * squash;
            const float bx = cx + ca * sx * 0.55f;
            const float by_ = cy + sa * sy * 0.55f;

            for (int b = -1; b <= 1; b += 2)
            {
                const float ba = a + b * PI3;
                dl->AddLine(ImVec2(bx, by_),
                    ImVec2(bx + cosf(ba) * bl, by_ + sinf(ba) * bl),
                    col, 0.8f);
            }
        }

        dl->AddCircleFilled(ImVec2(cx, cy), r * 0.20f, col_core, 6);
    }

    // One projected, sortable draw item.
    struct DrawItem
    {
        float dist = 0.f;
        int index = 0;
    };

    static void TickSnow(ImDrawList* dl)
    {
        const float now = static_cast<float>(ImGui::GetTime());
        float dt = now - s_snow_last_t;
        if (dt > 0.1f) dt = 0.1f;
        s_snow_last_t = now;

        const Vectors::Vector3 player = GetLocalPlayerPos();
        if (player.x == 0.f && player.y == 0.f && player.z == 0.f)
            return;

        // Approximate foot level; see the note at the top of this file.
        const float groundY = player.y - 3.f;

        const int desired = static_cast<int>(std::clamp(Options::World::Weather::Intensity, 50.f, 1200.f));
        if (static_cast<int>(s_flakes.size()) != desired)
        {
            const int old = static_cast<int>(s_flakes.size());
            s_flakes.resize(desired);
            for (int i = old; i < desired; ++i)
                s_flakes[i].active = false;
        }

        const float R = 180.f;
        const float spd = Options::World::Weather::Speed * 15.f;
        const float wind = Options::World::Weather::Wind;

        static std::vector<DrawItem> order;
        order.clear();
        order.reserve(s_flakes.size());

        // Pass 1: simulate and project.
        for (int i = 0; i < static_cast<int>(s_flakes.size()); ++i)
        {
            Snowflake& f = s_flakes[i];

            if (!f.active) { SpawnFlake(f, player, groundY); continue; }

            if (f.landed)
            {
                f.land_timer -= dt;
                if (f.land_timer <= 0.f) { SpawnFlake(f, player, groundY); continue; }
            }
            else
            {
                // Refresh the landing plane as the player moves, so a slope or
                // staircase does not leave rain hanging in the air.
                f.target_y = groundY;

                // Snow flutters rather than falling straight: a slow sinusoidal
                // sway on top of the constant drift.
                const float sway = f.sway_amp * sinf(now * f.sway_freq + f.sway_phase);

                f.pos.y -= spd * dt;
                f.pos.x += (f.drift_x + wind + sway) * dt;
                f.pos.z += f.drift_z * dt;
                f.spin += f.spin_vel * dt;
                f.tumble += f.tumble_vel * dt;

                if (f.pos.y <= f.target_y + 0.05f)
                {
                    f.pos.y = f.target_y + 0.05f;
                    f.landed = true;
                    f.land_timer = randf(0.5f, 2.0f);
                }

                const float dx = f.pos.x - player.x;
                const float dz = f.pos.z - player.z;
                if (dx * dx + dz * dz > R * R) { SpawnFlake(f, player, groundY); continue; }
            }

            const Vectors::Vector2 sc = WorldToScreen(f.pos);
            if (!OnScreen(sc)) continue;

            const float ddx = f.pos.x - player.x;
            const float ddy = f.pos.y - player.y;
            const float ddz = f.pos.z - player.z;
            const float dist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);

            float alpha = 1.f - dist / (R * 0.85f);
            if (alpha < 0.02f) continue;
            alpha = std::clamp(alpha, 0.f, 1.f);

            float screen_r = Options::World::Weather::SnowSize * f.size * 110.f / (dist + 0.5f);
            screen_r = std::clamp(screen_r, 1.f, 13.f);

            // Edge-on squash. |sin| so the crystal passes through edge-on twice
            // per tumble cycle instead of only once.
            const float squash = 0.18f + 0.82f * fabsf(sinf(f.tumble));

            order.push_back({ dist, i });

            // Cache per-frame draw data on the flake itself so the sort pass does
            // not have to re-project.
            f.screen_x = sc.x;
            f.screen_y = sc.y;
            f.screen_r = screen_r;
            f.alpha = alpha;
            f.squash = squash;
        }

        // Pass 2: draw far to near so nearer flakes overlap correctly.
        std::sort(order.begin(), order.end(), [](const DrawItem& a, const DrawItem& b)
            { return a.dist > b.dist; });

        const float* snowCol = Options::World::Weather::SnowColor;

        for (const auto& item : order)
        {
            const Snowflake& f = s_flakes[item.index];

            if (f.landed)
            {
                const float fade = std::clamp(f.land_timer / 0.6f, 0.f, 1.f);
                const float a = f.alpha * fade;
                if (a < 0.02f) continue;

                // A landed crystal flattens into a wide, dim disc. Keep the same
                // hue, just darker - no per-channel skew.
                const float rx = f.screen_r * 2.2f;
                float ry = f.screen_r * 0.3f;
                if (ry < 0.5f) ry = 0.5f;
                const float avg_r = (rx + ry) * 0.5f;

                dl->AddCircleFilled(ImVec2(f.screen_x, f.screen_y), avg_r + 1.5f,
                    Shade(snowCol, 0.72f, item.dist, a * 0.20f), 10);
                dl->AddCircleFilled(ImVec2(f.screen_x, f.screen_y), avg_r,
                    Shade(snowCol, 0.95f, item.dist, a * 0.86f), 10);
                continue;
            }

            // Arms slightly dimmer than the core, but the SAME hue.
            const ImU32 arm = Shade(snowCol, 0.82f, item.dist, f.alpha * 0.86f);
            const ImU32 core = Shade(snowCol, 1.0f, item.dist, f.alpha);

            if (f.screen_r > 2.5f)
            {
                DrawSnowflakeShape(dl, f.screen_x, f.screen_y, f.screen_r,
                    f.spin, f.squash, arm, core);
            }
            else
            {
                // Too small for arms to read; a dot with a bright centre is
                // cheaper and indistinguishable at that size.
                dl->AddCircleFilled(ImVec2(f.screen_x, f.screen_y), f.screen_r, arm, 6);
                dl->AddCircleFilled(ImVec2(f.screen_x, f.screen_y), f.screen_r * 0.3f, core, 4);
            }
        }
    }

    // =========================================================================
    //  RAIN
    // =========================================================================
    struct Raindrop
    {
        Vectors::Vector3 pos;
        Vectors::Vector3 vel;
        float target_y = 0.f;
        float size = 0.f;
        float splash_timer = 0.f;
        float screen_x = 0.f, screen_y = 0.f;
        float tail_x = 0.f, tail_y = 0.f;
        float alpha = 0.f;
        bool  splashing = false;
        bool  active = false;
    };

    static std::vector<Raindrop> s_drops;
    static float s_rain_last_t = 0.f;

    static void SpawnDrop(Raindrop& d, const Vectors::Vector3& player, float groundY)
    {
        const float R = 150.f;
        d.pos.x = player.x + randf(-R, R);
        d.pos.z = player.z + randf(-R, R);
        d.target_y = groundY;
        d.pos.y = groundY + randf(8.f, 90.f);

        const float spd = Options::World::Weather::Speed * 25.f;
        const float wind = Options::World::Weather::Wind;

        // Rain falls much faster than it drifts sideways. The horizontal share
        // is roughly 10-20 degrees off vertical in real rain, which is what the
        // streak angle ends up showing.
        d.vel.y = -spd;
        d.vel.x = wind + randf(-0.4f, 0.4f);
        d.vel.z = randf(-0.5f, 0.7f);

        d.size = randf(0.5f, 0.9f);
        d.splash_timer = 0.f;
        d.splashing = false;
        d.active = true;
    }

    static void TickRain(ImDrawList* dl)
    {
        const float now = static_cast<float>(ImGui::GetTime());
        float dt = now - s_rain_last_t;
        if (dt > 0.1f) dt = 0.1f;
        s_rain_last_t = now;

        const Vectors::Vector3 player = GetLocalPlayerPos();
        if (player.x == 0.f && player.y == 0.f && player.z == 0.f)
            return;

        const float groundY = player.y - 3.f;

        const int desired = static_cast<int>(std::clamp(Options::World::Weather::Intensity, 50.f, 1500.f));
        if (static_cast<int>(s_drops.size()) != desired)
        {
            const int old = static_cast<int>(s_drops.size());
            s_drops.resize(desired);
            for (int i = old; i < desired; ++i)
                s_drops[i].active = false;
        }

        const float R = 150.f;
        const float shutter = Options::World::Weather::RainStreak;

        static std::vector<DrawItem> order;
        order.clear();
        order.reserve(s_drops.size());

        for (int i = 0; i < static_cast<int>(s_drops.size()); ++i)
        {
            Raindrop& d = s_drops[i];

            if (!d.active) { SpawnDrop(d, player, groundY); continue; }

            if (d.splashing)
            {
                d.splash_timer -= dt;
                if (d.splash_timer <= 0.f) { SpawnDrop(d, player, groundY); continue; }
            }
            else
            {
                d.target_y = groundY;

                d.pos.x += d.vel.x * dt;
                d.pos.y += d.vel.y * dt;
                d.pos.z += d.vel.z * dt;

                if (d.pos.y <= d.target_y + 0.05f)
                {
                    d.pos.y = d.target_y + 0.05f;
                    d.splashing = true;
                    d.splash_timer = randf(0.08f, 0.18f);
                }

                const float dx = d.pos.x - player.x;
                const float dz = d.pos.z - player.z;
                if (dx * dx + dz * dz > R * R) { SpawnDrop(d, player, groundY); continue; }
            }

            const Vectors::Vector2 sc = WorldToScreen(d.pos);
            if (!OnScreen(sc)) continue;

            const float ddx = d.pos.x - player.x;
            const float ddy = d.pos.y - player.y;
            const float ddz = d.pos.z - player.z;
            const float dist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);

            float alpha = 1.f - dist / (R * 0.85f);
            if (alpha < 0.02f) continue;
            alpha = std::clamp(alpha, 0.f, 1.f);

            d.screen_x = sc.x;
            d.screen_y = sc.y;
            d.alpha = alpha;

            if (!d.splashing)
            {
                // Streak tail sits BEHIND the drop along the velocity vector, by
                // speed * shutter. Projecting both world points is what makes the
                // streak slant with the wind and foreshorten with distance -
                // offsetting in +Y alone cannot do either.
                Vectors::Vector3 tail;
                tail.x = d.pos.x - d.vel.x * shutter;
                tail.y = d.pos.y - d.vel.y * shutter;
                tail.z = d.pos.z - d.vel.z * shutter;

                const Vectors::Vector2 sc_tail = WorldToScreen(tail);

                float lx = sc_tail.x - sc.x;
                float ly = sc_tail.y - sc.y;
                const float ll = sqrtf(lx * lx + ly * ly);

                // A drop almost overhead projects a very short streak and one at
                // the horizon a very long one. Cap the screen length so neither
                // turns into a spike or vanishes.
                if (ll > 26.f)
                {
                    const float s = 26.f / ll;
                    lx *= s;
                    ly *= s;
                }
                if (ll < 1.5f)
                {
                    const float s = 1.5f / (ll > 0.0001f ? ll : 0.0001f);
                    lx *= s;
                    ly *= s;
                }

                d.tail_x = lx;
                d.tail_y = ly;
            }

            order.push_back({ dist, i });
        }

        std::sort(order.begin(), order.end(), [](const DrawItem& a, const DrawItem& b)
            { return a.dist > b.dist; });

        const float* rainCol = Options::World::Weather::RainColor;
        const float* splashCol = Options::World::Weather::SplashColor;
        const float thickness = Options::World::Weather::RainThickness;

        for (const auto& item : order)
        {
            const Raindrop& d = s_drops[item.index];

            if (d.splashing)
            {
                const float prog = std::clamp(1.f - d.splash_timer / 0.15f, 0.f, 1.f);
                const float sa = d.alpha * 0.55f * (1.f - prog);
                if (sa < 0.02f) continue;

                // Ring expands and fades. This ImGui build has no AddEllipse, so the ripple is
                // a plain circle - at these radii the difference is not visible.
                const float r1 = std::clamp(2.f + prog * 6.f, 1.f, 8.f);

                dl->AddCircle(ImVec2(d.screen_x, d.screen_y), r1,
                    Shade(splashCol, 1.f, item.dist, sa), 12, 0.9f);
                continue;
            }

            // Two passes give the streak a soft edge and a brighter core, which
            // reads as a cylinder of water rather than a flat line. Both use the
            // same hue - only brightness and alpha differ.
            float width = thickness;
            if (width < 0.5f) width = 0.5f;

            dl->AddLine(
                ImVec2(d.screen_x - width * 0.5f, d.screen_y),
                ImVec2(d.screen_x - width * 0.5f + d.tail_x, d.screen_y + d.tail_y),
                Shade(rainCol, 1.0f, item.dist, d.alpha * 0.22f), width + 0.7f);

            dl->AddLine(
                ImVec2(d.screen_x, d.screen_y),
                ImVec2(d.screen_x + d.tail_x, d.screen_y + d.tail_y),
                Shade(rainCol, 1.12f, item.dist, d.alpha * 0.72f), width);
        }
    }

    inline void Render(ImDrawList* dl)
    {
        if (!dl || !Globals::Viewport::Valid)
            return;

        // Not enabled: drop the particle buffers so re-enabling does not resume
        // with a stale field of drops in odd places.
        if (!Options::World::Weather::Enabled)
        {
            s_flakes.clear();
            s_drops.clear();
            return;
        }

        if (Options::World::Weather::Type == 0)
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