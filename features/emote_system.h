#pragma once

// Emote system for the Model Changer.
//
// Drives the model's skeleton through a set of procedural emote poses. The
// poses are expressed as model-space rotations about each joint's pivot, which
// the renderer turns into skinning matrices (see PreviewRenderer).

#include <string>
#include <vector>

#include "preview3d/PreviewRenderer.h"

namespace EmoteSystem
{

class EmoteManager
{
public:
    static EmoteManager& Get();

    // Register the built-in emote list. Safe to call more than once.
    void Initialize();

    int Count() const { return (int)m_Emotes.size(); }
    const char* Name(int index) const;
    float Duration(int index) const;

    void Play(int index);
    void Stop();
    bool IsPlaying() const { return m_Current >= 0; }
    int  CurrentIndex() const { return m_Current; }
    const char* CurrentName() const { return IsPlaying() ? Name(m_Current) : ""; }
    float Progress() const;

    void  SetLoop(bool loop) { m_Loop = loop; }
    bool  GetLoop() const { return m_Loop; }
    void  SetSpeed(float s) { m_Speed = (s > 0.05f && s < 10.0f) ? s : 1.0f; }
    float GetSpeed() const { return m_Speed; }

    // Advance the current emote by dt and write the resulting pose into the
    // renderer. Call once per frame, before renderer.Update().
    void Apply(Cheat::Core::PreviewRenderer& renderer, float dt);

    // Apply the pose of a given emote at time t (seconds) without changing the
    // playing state. Used for menu previews. Returns false if index is bad.
    bool ApplyAt(Cheat::Core::PreviewRenderer& renderer, int index, float t) const;

    bool Enabled = true;

private:
    EmoteManager() = default;

    struct EmoteDef {
        std::string name;
        float duration;
        bool  loopDefault;
    };

    std::vector<EmoteDef> m_Emotes;
    int   m_Current = -1;
    float m_Time = 0.0f;
    float m_Speed = 1.0f;
    bool  m_Loop = true;

    void ApplyPose(Cheat::Core::PreviewRenderer& r, int index, float t) const;
};

inline EmoteManager& Mgr() { return EmoteManager::Get(); }

} // namespace EmoteSystem
