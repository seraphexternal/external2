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
#include "../features/visibility.h"
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

    // Enhanced 3D hit effect particles
    struct ImpactParticle
    {
        Vectors::Vector3 worldPos;
        Vectors::Vector3 velocity;
        ImU32 color;
        float size;
        float timeLeft;
        float maxTime;
        float rotation;
        float rotationSpeed;
        bool isSpark;
    };

    // 3D damage number floating up
    struct DamageNumber
    {
        Vectors::Vector3 worldPos;
        float damage;
        float timeLeft;
        float maxTime;
        float yOffset;
        ImU32 color;
    };

    // Hit splatter / blood effect
    struct HitSplatter
    {
        Vectors::Vector3 worldPos;
        Vectors::Vector3 normal;
        float timeLeft;
        float maxTime;
        float size;
    };

    // Ghost chams left at hit position
    struct GhostChams
    {
        uintptr_t playerAddress;
        std::vector<ImVec2> hull;
        ImU32 fillColor;
        ImU32 outlineColor;
        float timeLeft;
        float maxTime;
    };

    struct BulletTracer
    {
        Vectors::Vector3 start;
        Vectors::Vector3 end;
        float timeLeft = 0.0f;
        float maxTime = 1.0f;
    };

    struct Footstep
    {
        Vectors::Vector3 position;
        float timeLeft = 0.0f;
    };

    inline std::unordered_map<uintptr_t, float> lastHealth;
    inline std::unordered_map<uintptr_t, float> hitFlashTimer;
    inline std::vector<HitNotification> notifications;
    inline std::vector<HitEffect> effects;
    inline std::vector<ImpactParticle> impactParticles;
    inline std::vector<DamageNumber> damageNumbers;
    inline std::vector<HitSplatter> hitSplatters;
    inline std::vector<GhostChams> ghostChams;
    inline std::vector<BulletTracer> bulletTracers;
    inline std::vector<Footstep> footsteps;
    inline std::unordered_map<uintptr_t, Vectors::Vector3> lastPlayerPos;

    // Builds a minimal RIFF/WAV in memory and plays it asynchronously.
    // freq   = tone frequency in Hz
    // durationMs = milliseconds
    // vol    = 0.0 – 1.0 amplitude
    // fadeMs = milliseconds of linear fade-out at the end
    inline void PlayTone(int freq, int durationMs, float vol = 0.9f, int fadeMs = 20)
    {
        const int sampleRate = 44100;
        const int numSamples = sampleRate * durationMs / 1000;
        const int fadeSmps   = sampleRate * fadeMs   / 1000;

        // WAV header = 44 bytes, then 16-bit PCM data
        const int dataBytes  = numSamples * sizeof(int16_t);
        const int totalBytes = 44 + dataBytes;

        // Use a persistent buffer so the pointer stays valid during async playback.
        // We keep up to 3 rotating slots so rapid successive calls don't clobber
        // a still-playing buffer.
        static std::vector<uint8_t> slots[3];
        static int slotIdx = 0;
        slotIdx = (slotIdx + 1) % 3;
        std::vector<uint8_t>& buf = slots[slotIdx];
        buf.resize(totalBytes);

        // --- RIFF header ---
        uint8_t* p = buf.data();
        auto write4 = [&](const char* tag)       { memcpy(p, tag, 4); p += 4; };
        auto writeU32 = [&](uint32_t v)           { memcpy(p, &v, 4); p += 4; };
        auto writeU16 = [&](uint16_t v)           { memcpy(p, &v, 2); p += 2; };

        write4("RIFF");
        writeU32(totalBytes - 8);   // ChunkSize
        write4("WAVE");
        write4("fmt ");
        writeU32(16);               // Subchunk1Size  (PCM)
        writeU16(1);                // AudioFormat     1 = PCM
        writeU16(1);                // NumChannels     mono
        writeU32(sampleRate);       // SampleRate
        writeU32(sampleRate * 2);   // ByteRate
        writeU16(2);                // BlockAlign
        writeU16(16);               // BitsPerSample
        write4("data");
        writeU32(dataBytes);

        // --- PCM samples ---
        int16_t* samples = reinterpret_cast<int16_t*>(p);
        const float twoPi = 6.28318530f;
        const float peak  = 32767.0f * ClampFloat(vol, 0.f, 1.f);

        for (int i = 0; i < numSamples; ++i)
        {
            float t = static_cast<float>(i) / sampleRate;
            float s = sinf(twoPi * freq * t);

            // Linear fade-out at the end to avoid click
            if (i >= numSamples - fadeSmps)
            {
                float frac = static_cast<float>(numSamples - i) / fadeSmps;
                s *= frac;
            }

            samples[i] = static_cast<int16_t>(s * peak);
        }

        PlaySound(reinterpret_cast<LPCTSTR>(buf.data()), nullptr,
                  SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }

    // Plays two tones sequentially by encoding them back-to-back in one buffer.
    inline void PlayTwoTone(int freq1, int freq2, int dur1Ms, int dur2Ms,
                             float vol = 0.9f, int fadeMs = 15)
    {
        const int sampleRate = 44100;
        const int s1 = sampleRate * dur1Ms / 1000;
        const int s2 = sampleRate * dur2Ms / 1000;
        const int numSamples = s1 + s2;
        const int fadeSmps   = sampleRate * fadeMs / 1000;
        const int dataBytes  = numSamples * sizeof(int16_t);
        const int totalBytes = 44 + dataBytes;

        static std::vector<uint8_t> slots[3];
        static int slotIdx = 0;
        slotIdx = (slotIdx + 1) % 3;
        std::vector<uint8_t>& buf = slots[slotIdx];
        buf.resize(totalBytes);

        uint8_t* p = buf.data();
        auto write4   = [&](const char* tag) { memcpy(p, tag, 4); p += 4; };
        auto writeU32 = [&](uint32_t v)      { memcpy(p, &v, 4); p += 4; };
        auto writeU16 = [&](uint16_t v)      { memcpy(p, &v, 2); p += 2; };

        write4("RIFF");  writeU32(totalBytes - 8);
        write4("WAVE");  write4("fmt ");  writeU32(16);
        writeU16(1);  writeU16(1);  writeU32(sampleRate);
        writeU32(sampleRate * 2);  writeU16(2);  writeU16(16);
        write4("data");  writeU32(dataBytes);

        int16_t* samples = reinterpret_cast<int16_t*>(p);
        const float twoPi = 6.28318530f;
        const float peak  = 32767.0f * ClampFloat(vol, 0.f, 1.f);

        for (int i = 0; i < numSamples; ++i)
        {
            int freq = (i < s1) ? freq1 : freq2;
            float t  = static_cast<float>(i < s1 ? i : i - s1) / sampleRate;
            float s  = sinf(twoPi * freq * t);

            if (i >= numSamples - fadeSmps)
            {
                float frac = static_cast<float>(numSamples - i) / fadeSmps;
                s *= frac;
            }
            samples[i] = static_cast<int16_t>(s * peak);
        }

        PlaySound(reinterpret_cast<LPCTSTR>(buf.data()), nullptr,
                  SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }

    // Debounce so multiple hit-detection paths (health delta + shot confirmation)
    // or fast auto-fire don't stack hitsounds into an unpleasant machine-gun roar.
    inline float& LastHitSoundTime()
    {
        static float t = -1.0f;
        return t;
    }

    inline void PlayHitSound()
    {
        if (!Options::Combat::HitSounds)
            return;

        float now = ImGui::GetTime();
        if (LastHitSoundTime() >= 0.f && (now - LastHitSoundTime()) < 0.045f)
            return;
        LastHitSoundTime() = now;

        // If custom file is set, use it
        if (Options::Combat::HitSoundFile[0] != '\0')
        {
            std::string path = Options::Combat::HitSoundFile;
            PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            return;
        }

        // Auto-use first file from hitsounds folder if available
        auto& files = Globals::HitSounds::Files;
        if (!files.empty())
        {
            std::string path = Globals::HitSounds::FolderPath + "\\" + files[0];
            PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            return;
        }

        // Built-in presets fallback
        switch (Options::Combat::HitSoundType)
        {
            case 1: PlayTone(800, 120, 0.85f, 10); break;      // bell
            case 2: PlayTone(150, 200, 1.0f, 20); break;       // bass
            case 3: PlayTwoTone(200, 400, 80, 80, 0.9f, 15); break; // hvhpissy
            case 4: PlayTwoTone(600, 800, 50, 50, 0.9f, 15); break; // hvhks
            case 5: PlayTwoTone(400, 600, 100, 50, 0.9f, 15); break; // hvhtag
            case 6: PlayTone(1800, 45, 0.85f, 10); break;      // neverlose
            case 7: PlayTwoTone(1200, 600, 50, 50); break;     // rust
            case 8: PlayTwoTone(1000, 3000, 30, 30); break;    // quake
            case 9: PlayTone(1200, 80, 0.8f, 10); break;       // cod
            case 10: PlayTwoTone(400, 800, 40, 40); break;     // bubble
            case 11: PlayTone(1000, 80, 0.8f, 10); break;      // minecraft
            case 12: PlayTwoTone(2000, 1000, 40, 60); break;   // fatality
            default: PlayTone(1800, 45, 0.85f, 10); break;     // click (default)
        }
    }

    inline float ReadLiveHealth(const RobloxPlayer& player)
    {
        if (player.Humanoid.address)
            return Memory->read<float>(player.Humanoid.address + Offsets::Humanoid::Health);
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

        if (Options::Combat::BulletTracers)
        {
            Vectors::Vector3 startPos{ 0.f, 0.f, 0.f };
            bool hasStart = false;

            if (Globals::Roblox::Camera.address)
            {
                startPos = Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);
                hasStart = true;
            }
            else if (Globals::Roblox::LocalPlayer.address)
            {
                auto localChar = Globals::Roblox::LocalPlayer.Character();
                auto localHrp = localChar.FindFirstChild("HumanoidRootPart");
                if (localHrp.address)
                {
                    startPos = localHrp.Position();
                    hasStart = true;
                }
            }

            auto targetPart = player.Head.address ? player.Head : player.HumanoidRootPart;
            if (hasStart && targetPart.address)
            {
                BulletTracer tracer;
                tracer.start = startPos;
                tracer.end = targetPart.Position();
                tracer.timeLeft = Options::Combat::BulletTracerDuration;
                tracer.maxTime = Options::Combat::BulletTracerDuration;
                bulletTracers.push_back(tracer);
                if (bulletTracers.size() > 50)
                    bulletTracers.erase(bulletTracers.begin());
            }
        }
    }

    // Called every aimbot frame when a target is locked and LMB is held.
    // Spawns a tracer unless a wall blocks the line of sight (so tracers don't
    // draw through geometry). ignoreModel is the target's character model,
    // excluded from the occlusion test.
    inline void RegisterShot(const Vectors::Vector3& startPos, const Vectors::Vector3& endPos, uintptr_t ignoreModel = 0)
    {
        if (!Options::Combat::BulletTracers)
            return;

        if (!Visibility::IsPointVisibleForced(endPos, ignoreModel))
            return;

        BulletTracer tracer;
        tracer.start   = startPos;
        tracer.end     = endPos;
        tracer.timeLeft = Options::Combat::BulletTracerDuration;
        tracer.maxTime  = Options::Combat::BulletTracerDuration;
        bulletTracers.push_back(tracer);
        if (bulletTracers.size() > 50)
            bulletTracers.erase(bulletTracers.begin());
    }

    // Geometry-based hit confirmation. Works on games (e.g. Overkill/Chickynoid)
    // that do NOT replicate Humanoid.Health to the client, so the health-delta
    // path never fires. Given the world-space point a shot resolved to, we check
    // whether any cached enemy body part is within a small radius of it.
    inline void RegisterShotHit(const Vectors::Vector3& hitPoint)
    {
        if (!Options::Combat::HitSounds && !Options::Combat::HitEffects &&
            !Options::Combat::HitNotifications && !Options::Combat::HitChams)
            return;

        if (Globals::Caches::CachedPlayerObjects.empty())
            return;

        RobloxPlayer best;
        float bestDist = 1e9f;

        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (player.address == 0)
                continue;
            if (player.address == Globals::Roblox::LocalPlayer.address)
                continue;
            if (Globals::Roblox::isOverkill && Globals::Roblox::LocalPlayer.address &&
                !player.Name.empty() && player.Name == Globals::Roblox::LocalPlayer.Name())
                continue;

            Vectors::Vector3 pts[3];
            int n = 0;
            if (player.Head.address)            pts[n++] = player.Head.Position();
            if (player.Upper_Torso.address)     pts[n++] = player.Upper_Torso.Position();
            else if (player.Torso.address)      pts[n++] = player.Torso.Position();
            if (player.HumanoidRootPart.address) pts[n++] = player.HumanoidRootPart.Position();

            for (int i = 0; i < n; i++)
            {
                float d = pts[i].Distance(hitPoint);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = player;
                }
            }
        }

        if (best.address && bestDist < 2.0f)
        {
            // Without health replication (Chickynoid) we can't read a true hit,
            // so require a clear line of sight to the enemy: if a wall is between
            // you and them, the shot can't have connected.
            if (!Visibility::IsPointVisibleForced(best.Head.address ? best.Head.Position() : hitPoint, best.Character.address))
                return;

            hitFlashTimer[best.address] = Options::Combat::HitChamsDuration;
            PlayHitSound();

            if (Options::Combat::HitNotifications)
            {
                HitNotification note;
                note.name = best.Name;
                note.damage = 0.f;
                note.timeLeft = 2.5f;
                notifications.push_back(note);
                if (notifications.size() > 8)
                    notifications.erase(notifications.begin());
            }

            if (Options::Combat::HitEffects)
            {
                auto headPos = WorldToScreen(best.Head.Position());
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
    }

    inline void Update()
    {
        const float dt = ImGui::GetIO().DeltaTime;

        // Keep wall occluders fresh so bullet tracers and hitsounds can respect
        // line of sight even when ESP visibility is disabled.
        Visibility::RefreshOccludersIfNeeded();

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

        for (auto& tracer : bulletTracers)
            tracer.timeLeft -= dt;
        bulletTracers.erase(
            std::remove_if(bulletTracers.begin(), bulletTracers.end(),
                [](const BulletTracer& t) { return t.timeLeft <= 0.0f; }),
            bulletTracers.end());

        for (auto& fs : footsteps)
            fs.timeLeft -= dt;
        footsteps.erase(
            std::remove_if(footsteps.begin(), footsteps.end(),
                [](const Footstep& f) { return f.timeLeft <= 0.0f; }),
            footsteps.end());

        // Update impact particles
        for (auto& p : impactParticles)
        {
            p.worldPos = p.worldPos + p.velocity * dt;
            p.velocity.y -= dt * 9.81f * 2.0f; // gravity
            p.rotation += p.rotationSpeed * dt;
            p.timeLeft -= dt;
        }
        impactParticles.erase(
            std::remove_if(impactParticles.begin(), impactParticles.end(),
                [](const ImpactParticle& p) { return p.timeLeft <= 0.0f; }),
            impactParticles.end());

        // Update damage numbers
        for (auto& d : damageNumbers)
        {
            d.yOffset += dt * 40.0f; // float upward
            d.timeLeft -= dt;
        }
        damageNumbers.erase(
            std::remove_if(damageNumbers.begin(), damageNumbers.end(),
                [](const DamageNumber& d) { return d.timeLeft <= 0.0f; }),
            damageNumbers.end());

        // Update hit splatters
        for (auto& s : hitSplatters)
        {
            s.timeLeft -= dt;
        }
        hitSplatters.erase(
            std::remove_if(hitSplatters.begin(), hitSplatters.end(),
                [](const HitSplatter& s) { return s.timeLeft <= 0.0f; }),
            hitSplatters.end());

        // Update ghost chams
        for (auto& g : ghostChams)
        {
            g.timeLeft -= dt;
        }
        ghostChams.erase(
            std::remove_if(ghostChams.begin(), ghostChams.end(),
                [](const GhostChams& g) { return g.timeLeft <= 0.0f; }),
            ghostChams.end());

        // Update bullet tracers
        for (auto& tracer : bulletTracers)
            tracer.timeLeft -= dt;
        bulletTracers.erase(
            std::remove_if(bulletTracers.begin(), bulletTracers.end(),
                [](const BulletTracer& t) { return t.timeLeft <= 0.0f; }),
            bulletTracers.end());

        // Update footsteps
        for (auto& fs : footsteps)
            fs.timeLeft -= dt;
        footsteps.erase(
            std::remove_if(footsteps.begin(), footsteps.end(),
                [](const Footstep& f) { return f.timeLeft <= 0.0f; }),
            footsteps.end());

        std::unordered_map<uintptr_t, bool> seen;
        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (player.address == 0)
                continue;

            if (player.address == Globals::Roblox::LocalPlayer.address)
                continue;

            if (Globals::Roblox::isOverkill && Globals::Roblox::LocalPlayer.address &&
                !player.Name.empty() &&
                player.Name == Globals::Roblox::LocalPlayer.Name())
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

            if (Options::SoundVisualizer::Enabled && player.HumanoidRootPart.address)
            {
                Vectors::Vector3 currPos = player.HumanoidRootPart.Position();
                auto posIt = lastPlayerPos.find(player.address);
                if (posIt != lastPlayerPos.end())
                {
                    float dist = currPos.Distance(posIt->second);
                    float speed = dist / dt;
                    if (speed > 8.0f && dist > 0.5f)
                    {
                        Footstep fs;
                        fs.position = currPos;
                        fs.timeLeft = Options::SoundVisualizer::Duration;
                        footsteps.push_back(fs);
                        if (footsteps.size() > (size_t)Options::SoundVisualizer::MaxSteps)
                            footsteps.erase(footsteps.begin());
                    }
                }
                lastPlayerPos[player.address] = currPos;
            }

        for (auto it = lastHealth.begin(); it != lastHealth.end();)
        {
            if (seen.find(it->first) == seen.end())
                it = lastHealth.erase(it);
            else
                ++it;
        }

        for (auto it = lastPlayerPos.begin(); it != lastPlayerPos.end();)
        {
            if (seen.find(it->first) == seen.end())
                it = lastPlayerPos.erase(it);
            else
                ++it;
        }
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

            ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
            const int style = Options::Combat::HitmarkerStyle;
            const float hmSize = Options::Combat::HitmarkerSize;
            const float hmThick = Options::Combat::HitmarkerThickness;

            for (const auto& effect : effects)
            {
                const float t = effect.timeLeft / MaxFloat(effect.maxTime, 0.01f);
                const int alpha = static_cast<int>(220.0f * t);
                const ImU32 col = (effectColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);

                // Expanding hitmarker on the enemy (or fixed marker at crosshair).
                ImVec2 pos = Options::Combat::HitmarkerOnCrosshair ? center : effect.position;

                if (Options::Combat::HitmarkerOnCrosshair)
                {
                    float s = hmSize;
                    float a = hmThick;
                    if (style == 1) // circle
                    {
                        drawList->AddCircle(pos, s, col, 24, a);
                    }
                    else if (style == 2) // dot
                    {
                        drawList->AddCircleFilled(pos, s * 0.4f, col, 16);
                    }
                    else // cross (4 spokes)
                    {
                        drawList->AddLine(ImVec2(pos.x - s, pos.y - s), ImVec2(pos.x - s * 0.3f, pos.y - s * 0.3f), col, a);
                        drawList->AddLine(ImVec2(pos.x + s, pos.y - s), ImVec2(pos.x + s * 0.3f, pos.y - s * 0.3f), col, a);
                        drawList->AddLine(ImVec2(pos.x - s, pos.y + s), ImVec2(pos.x - s * 0.3f, pos.y + s * 0.3f), col, a);
                        drawList->AddLine(ImVec2(pos.x + s, pos.y + s), ImVec2(pos.x + s * 0.3f, pos.y + s * 0.3f), col, a);
                    }
                }
                else
                {
                    drawList->AddCircle(effect.position, effect.radius, col, 32, 2.0f);
                    drawList->AddCircleFilled(effect.position, effect.radius * 0.35f, (effectColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha / 2) << 24), 16);
                }
            }
        }

        if (Options::Combat::BulletTracers && !bulletTracers.empty())
        {
            const ImU32 tracerColor = IM_COL32(
                static_cast<int>(Options::Combat::BulletTracerColor[0] * 255.f),
                static_cast<int>(Options::Combat::BulletTracerColor[1] * 255.f),
                static_cast<int>(Options::Combat::BulletTracerColor[2] * 255.f),
                255);

            for (const auto& tracer : bulletTracers)
            {
                const float t = tracer.timeLeft / MaxFloat(tracer.maxTime, 0.01f);
                const int alpha = static_cast<int>(220.0f * t);
                if (alpha <= 0) continue;

                auto startSS = WorldToScreen(tracer.start);
                auto endSS   = WorldToScreen(tracer.end);

                if (startSS.x == -1.f && startSS.y == -1.f) continue;
                if (endSS.x   == -1.f && endSS.y   == -1.f) continue;

                const ImU32 col = (tracerColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
                const float thickness = Options::Combat::BulletTracerThickness;
                const ImVec2 p1(startSS.x, startSS.y);
                const ImVec2 p2(endSS.x, endSS.y);

                switch (Options::Combat::BulletTracerStyle)
                {
                case 1: // Glow/beam
                {
                    const ImU32 glowCol = (tracerColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha / 3) << 24);
                    drawList->AddLine(p1, p2, glowCol, thickness * 4.0f);
                    drawList->AddLine(p1, p2, col, thickness);
                    break;
                }
                case 2: // Dashed
                {
                    const float dashLen = 12.0f;
                    const float gapLen = 8.0f;
                    ImVec2 dir(p2.x - p1.x, p2.y - p1.y);
                    const float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
                    if (len > 0.f)
                    {
                        dir.x /= len; dir.y /= len;
                        float drawn = 0.f;
                        while (drawn < len)
                        {
                            float segEnd = drawn + dashLen;
                            if (segEnd > len) segEnd = len;
                            ImVec2 a(p1.x + dir.x * drawn, p1.y + dir.y * drawn);
                            ImVec2 b(p1.x + dir.x * segEnd, p1.y + dir.y * segEnd);
                            drawList->AddLine(a, b, col, thickness);
                            drawn = segEnd + gapLen;
                        }
                    }
                    break;
                }
                case 3: // Pulse
                {
                    const float pulseAlpha = (0.5f + 0.5f * sinf(t * 30.0f));
                    const int pulseA = static_cast<int>(alpha * pulseAlpha);
                    const ImU32 pulseCol = (tracerColor & 0x00FFFFFF) | (static_cast<ImU32>(pulseA) << 24);
                    drawList->AddLine(p1, p2, pulseCol, thickness);
                    break;
                }
                default: // 0 - Solid
                    drawList->AddLine(p1, p2, col, thickness);
                    break;
}
        }

        // Render 3D impact particles
        if (!impactParticles.empty())
        {
            for (const auto& p : impactParticles)
            {
                auto screenPos = WorldToScreen(p.worldPos);
                if (screenPos.x < 0 || screenPos.y < 0) continue;

                float t = p.timeLeft / p.maxTime;
                int alpha = static_cast<int>(255.0f * t);
                ImU32 col = (p.color & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);

                float size = p.size * (0.5f + 0.5f * t);
                if (p.isSpark)
                {
                    // Draw spark as small line
                    float len = p.size * 4.0f;
                    Vectors::Vector3 end3D = p.worldPos + p.velocity.Normalize() * len * 0.1f;
                    auto endScreen = WorldToScreen(end3D);
                    if (endScreen.x >= 0 && endScreen.y >= 0)
                    {
                        drawList->AddLine(ImVec2(screenPos.x, screenPos.y), ImVec2(endScreen.x, endScreen.y), col, 1.5f);
                    }
                }
                else
                {
                    drawList->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), size, col, 12);
                }
            }
        }

        // Render 3D damage numbers
        if (!damageNumbers.empty())
        {
            ImFont* font = ImGui::GetFont();
            for (const auto& d : damageNumbers)
            {
                auto screenPos = WorldToScreen({ d.worldPos.x, d.worldPos.y + d.yOffset, d.worldPos.z });
                if (screenPos.x < 0 || screenPos.y < 0) continue;

                float t = d.timeLeft / d.maxTime;
                int alpha = static_cast<int>(255.0f * t);
                ImU32 col = (d.color & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
                ImU32 outlineCol = IM_COL32(0, 0, 0, alpha);

                char buf[32];
                snprintf(buf, sizeof(buf), "%.0f", d.damage);
                const float fontSize = 16.0f + (1.0f - t) * 8.0f;
                ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
                ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y * 0.5f);

                // Outline
                drawList->AddText(font, fontSize, ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, alpha), buf);
                drawList->AddText(font, fontSize, ImVec2(textPos.x - 1, textPos.y - 1), IM_COL32(0, 0, 0, alpha), buf);
                drawList->AddText(font, fontSize, ImVec2(textPos.x + 1, textPos.y - 1), IM_COL32(0, 0, 0, alpha), buf);
                drawList->AddText(font, fontSize, ImVec2(textPos.x - 1, textPos.y + 1), IM_COL32(0, 0, 0, alpha), buf);
                drawList->AddText(font, fontSize, textPos, col, buf);
            }
        }

        // Render hit splatters (3D blood/hit decals)
        if (!hitSplatters.empty())
        {
            for (const auto& s : hitSplatters)
            {
                auto screenPos = WorldToScreen(s.worldPos);
                if (screenPos.x < 0 || screenPos.y < 0) continue;

                float t = s.timeLeft / s.maxTime;
                float size = s.size * (1.0f - t * 0.5f);
                int alpha = static_cast<int>(200.0f * t);
                ImU32 col = IM_COL32(255, 50, 50, alpha);

                // Draw splatter as multiple small circles
                int numDrops = 6;
                for (int i = 0; i < numDrops; ++i)
                {
                    float angle = (float)i / numDrops * 6.28318f;
                    float dist = size * (0.5f + 0.5f * (1.0f - t));
                    float dx = cosf(angle) * dist;
                    float dy = sinf(angle) * dist;
                    drawList->AddCircleFilled(ImVec2(screenPos.x + dx, screenPos.y + dy), size * 0.3f, col, 8);
                }
            }
        }

        // Render ghost chams (copies of player at hit moment)
        if (!ghostChams.empty())
        {
            for (const auto& g : ghostChams)
            {
                if (g.hull.size() < 3) continue;

                float t = g.timeLeft / g.maxTime;
                int fillAlpha = static_cast<int>(180.0f * g.timeLeft / g.maxTime);
                int outlineAlpha = static_cast<int>(220.0f * g.timeLeft / g.maxTime);

                ImU32 fillColor = (g.fillColor & 0x00FFFFFF) | (static_cast<ImU32>(fillAlpha) << 24);
                ImU32 outlineColor = (g.outlineColor & 0x00FFFFFF) | (static_cast<ImU32>(outlineAlpha) << 24);

                // Draw ghost as wireframe with fading fill
                drawList->AddConvexPolyFilled(g.hull.data(), static_cast<int>(g.hull.size()), fillColor);
                drawList->AddPolyline(g.hull.data(), static_cast<int>(g.hull.size()), outlineColor, true, 2.0f);
            }
        }

        if (Options::SoundVisualizer::Enabled && !footsteps.empty())
        {
            const ImU32 svColor = IM_COL32(
                static_cast<int>(Options::SoundVisualizer::Color[0] * 255.f),
                static_cast<int>(Options::SoundVisualizer::Color[1] * 255.f),
                static_cast<int>(Options::SoundVisualizer::Color[2] * 255.f),
                180);

            for (const auto& fs : footsteps)
            {
                auto fsScreen = WorldToScreen(fs.position);
                if (fsScreen.x == -1.f && fsScreen.y == -1.f)
                    continue;

                float t = fs.timeLeft / Options::SoundVisualizer::Duration;
                float radius = Options::SoundVisualizer::Radius * (1.0f - t);
                int alpha = static_cast<int>(180 * t);
                ImU32 col = (svColor & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);

                drawList->AddCircle(ImVec2(fsScreen.x, fsScreen.y), radius, col, 16, 1.5f);
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
}
