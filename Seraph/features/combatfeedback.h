#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "../overlay/imgui/imgui.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/utils/W2S.h"

namespace CombatFeedback
{
    inline float ClampFloat(float value, float minVal, float maxVal)
    {
        if (value < minVal) return minVal;
        if (value > maxVal) return maxVal;
        return value;
    }

    inline float MaxFloat(float a, float b)
    {
        return a > b ? a : b;
    }

    struct HitNotification
    {
        std::string name;
        float damage = 0.0f;
        float timeLeft = 0.0f;
    };

    struct HitEffect
    {
        ImVec2 position;
        float radius = 8.0f;
        float timeLeft = 0.0f;
        float maxTime = 0.6f;
    };

    inline std::unordered_map<uintptr_t, float> lastHealth;
    inline std::unordered_map<uintptr_t, float> hitFlashTimer;
    inline std::vector<HitNotification> notifications;
    inline std::vector<HitEffect> effects;

    inline void PlayHitSound()
    {
        if (!Options::Combat::HitSounds)
            return;

        switch (Options::Combat::HitSoundType)
        {
        case 1:
            PlaySound(TEXT("SystemAsterisk"), nullptr, SND_ALIAS | SND_ASYNC);
            break;
        case 2:
            PlaySound(TEXT("SystemExclamation"), nullptr, SND_ALIAS | SND_ASYNC);
            break;
        default:
            PlaySound(TEXT("SystemHand"), nullptr, SND_ALIAS | SND_ASYNC);
            break;
        }
    }

    inline float ReadLiveHealth(const RobloxPlayer& player)
    {
        return player.Health;
    }

    inline void RegisterHit(const RobloxPlayer& player, float damage)
    {
        hitFlashTimer[player.address] = Options::Combat::HitChamsDuration;
        PlayHitSound();

        if (Options::Combat::HitNotifications)
        {
            HitNotification note;
            note.name = player.Name;
            note.damage = damage;
            note.timeLeft = 2.5f;
            notifications.push_back(note);
            if (notifications.size() > 8)
                notifications.erase(notifications.begin());
        }

        if (Options::Combat::HitEffects)
        {
            auto headPos = WorldToScreen(player.Head.Position());
            if (headPos.x != -1.f && headPos.y != -1.f)
            {
                HitEffect effect;
                effect.position = ImVec2(headPos.x, headPos.y);
                effect.timeLeft = Options::Combat::HitEffectDuration;
                effect.maxTime = Options::Combat::HitEffectDuration;
                effects.push_back(effect);
                if (effects.size() > 24)
                    effects.erase(effects.begin());
            }
        }
    }

    inline void Update()
    {
        const float dt = ImGui::GetIO().DeltaTime;

        for (auto it = hitFlashTimer.begin(); it != hitFlashTimer.end();)
        {
            it->second -= dt;
            if (it->second <= 0.0f)
                it = hitFlashTimer.erase(it);
            else
                ++it;
        }

        for (auto& note : notifications)
            note.timeLeft -= dt;
        notifications.erase(
            std::remove_if(notifications.begin(), notifications.end(),
                [](const HitNotification& n) { return n.timeLeft <= 0.0f; }),
            notifications.end());

        for (auto& effect : effects)
        {
            effect.timeLeft -= dt;
            effect.radius += dt * 90.0f;
        }
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [](const HitEffect& e) { return e.timeLeft <= 0.0f; }),
            effects.end());

        if (Globals::Caches::CachedPlayerObjects.empty())
            return;

        std::unordered_map<uintptr_t, bool> seen;
        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (player.address == 0 || player.address == Globals::Roblox::LocalPlayer.address)
                continue;

            seen[player.address] = true;

            if (player.Health <= 0.f)
            {
                lastHealth[player.address] = 0.f;
                continue;
            }

            const float currentHealth = ReadLiveHealth(player);
            const auto it = lastHealth.find(player.address);

            if (it != lastHealth.end())
            {
                const float delta = it->second - currentHealth;
                if (delta >= Options::Combat::MinDamage && currentHealth >= 0.0f)
                    RegisterHit(player, delta);
            }

            lastHealth[player.address] = currentHealth;
        }

        for (auto it = lastHealth.begin(); it != lastHealth.end();)
        {
            if (seen.find(it->first) == seen.end())
                it = lastHealth.erase(it);
            else
                ++it;
        }
    }

    inline bool IsHitFlashing(uintptr_t playerAddress)
    {
        const auto it = hitFlashTimer.find(playerAddress);
        return it != hitFlashTimer.end() && it->second > 0.0f;
    }

    inline float HitFlashAlpha(uintptr_t playerAddress)
    {
        const auto it = hitFlashTimer.find(playerAddress);
        if (it == hitFlashTimer.end())
            return 0.0f;
        return ClampFloat(it->second / MaxFloat(Options::Combat::HitChamsDuration, 0.01f), 0.0f, 1.0f);
    }

    inline void Render(ImDrawList* drawList)
    {
        if (!drawList)
            return;

        if (Options::Combat::HitEffects)
        {
            const ImU32 effectColor = IM_COL32(
                static_cast<int>(Options::Combat::HitEffectColor[0] * 255.f),
                static_cast<int>(Options::Combat::HitEffectColor[1] * 255.f),
                static_cast<int>(Options::Combat::HitEffectColor[2] * 255.f),
                255);

            for (const auto& effect : effects)
            {
                const float t = effect.timeLeft / MaxFloat(effect.maxTime, 0.01f);
                const int alpha = static_cast<int>(220.0f * t);
                drawList->AddCircle(effect.position, effect.radius, (effectColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24), 32, 2.0f);
                drawList->AddCircleFilled(effect.position, effect.radius * 0.35f, (effectColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha / 2) << 24), 16);
            }
        }

        if (Options::Combat::HitNotifications && !notifications.empty())
        {
            ImGuiIO& io = ImGui::GetIO();
            float y = 60.0f;
            for (const auto& note : notifications)
            {
                char buffer[128];
                snprintf(buffer, sizeof(buffer), "Hit %s (-%.0f HP)", note.name.c_str(), note.damage);

                const ImVec2 textSize = ImGui::CalcTextSize(buffer);
                const ImVec2 pos(io.DisplaySize.x - textSize.x - 20.0f, y);
                const int alpha = static_cast<int>(255.0f * ClampFloat(note.timeLeft / 2.5f, 0.0f, 1.0f));

                drawList->AddRectFilled(
                    ImVec2(pos.x - 8.0f, pos.y - 4.0f),
                    ImVec2(pos.x + textSize.x + 8.0f, pos.y + textSize.y + 4.0f),
                    IM_COL32(8, 8, 8, alpha), 4.0f);
                drawList->AddRect(
                    ImVec2(pos.x - 8.0f, pos.y - 4.0f),
                    ImVec2(pos.x + textSize.x + 8.0f, pos.y + textSize.y + 4.0f),
                    IM_COL32(27, 27, 27, alpha), 4.0f);
                drawList->AddText(pos, IM_COL32(255, 255, 255, alpha), buffer);
                y += textSize.y + 10.0f;
            }
        }
    }
}
