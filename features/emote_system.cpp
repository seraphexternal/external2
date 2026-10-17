#include "emote_system.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Built-in emote poses.
//
// Poses are authored against a semantic frame, independent of how any given
// model is oriented in its own file:
//   UP    = from the feet toward the head,
//   SIDE  = from the right limbs toward the left limbs,
//   DEPTH = UP x SIDE (the forward/back axis).
// A limb rotation about DEPTH swings it in the lateral/vertical plane (raise
// or lower), about SIDE swings it forward/back, and about UP twists it. The
// renderer maps these onto the loaded model's actual axes (PreviewRenderer::
// MapEmoteAxis), so the same pose works on a Y-up humanoid and a Z-up plush.
// ---------------------------------------------------------------------------

namespace EmoteSystem
{

EmoteManager& EmoteManager::Get()
{
    static EmoteManager instance;
    return instance;
}

void EmoteManager::Initialize()
{
    if (!m_Emotes.empty())
        return;

    // name, duration (s), loop by default
    m_Emotes = {
        { "Wave",   2.2f, true  },
        { "Cheer",  2.4f, true  },
        { "Dance",  4.0f, true  },
        { "Sit",    3.0f, true  },
        { "Point",  1.6f, false },
        { "Salute", 1.8f, false },
        { "Laugh",  2.4f, true  },
        { "Idle",   3.0f, true  },
    };
}

const char* EmoteManager::Name(int index) const
{
    if (index < 0 || index >= (int)m_Emotes.size()) return "";
    return m_Emotes[index].name.c_str();
}

float EmoteManager::Duration(int index) const
{
    if (index < 0 || index >= (int)m_Emotes.size()) return 1.0f;
    return m_Emotes[index].duration;
}

void EmoteManager::Play(int index)
{
    if (index < 0 || index >= (int)m_Emotes.size()) { Stop(); return; }
    m_Current = index;
    m_Time = 0.0f;
    m_Loop = m_Emotes[index].loopDefault;
}

void EmoteManager::Stop()
{
    m_Current = -1;
    m_Time = 0.0f;
}

float EmoteManager::Progress() const
{
    if (m_Current < 0) return 0.0f;
    const float d = Duration(m_Current);
    return d > 0.0f ? (m_Time / d) : 0.0f;
}

void EmoteManager::Apply(Cheat::Core::PreviewRenderer& renderer, float dt)
{
    if (!Enabled || m_Current < 0) {
        renderer.ClearPose();
        return;
    }

    m_Time += dt * m_Speed;

    const float dur = Duration(m_Current);
    if (m_Loop) {
        if (dur > 0.0f) m_Time = std::fmod(m_Time, dur);
    } else if (m_Time >= dur) {
        m_Time = dur;
        // one-shot: keep the final pose until the user stops/plays another
    }

    ApplyPose(renderer, m_Current, m_Time);
}

bool EmoteManager::ApplyAt(Cheat::Core::PreviewRenderer& renderer, int index, float t) const
{
    if (index < 0 || index >= (int)m_Emotes.size()) return false;
    ApplyPose(renderer, index, t);
    return true;
}

void EmoteManager::ApplyPose(Cheat::Core::PreviewRenderer& r, int index, float t) const
{
    r.ClearPose();
    if (index < 0 || index >= (int)m_Emotes.size()) return;

    const std::string& name = m_Emotes[index].name;
    auto S = [](float x) { return std::sin(x); };
    auto C = [](float x) { return std::cos(x); };

    if (name == "Wave") {
        // Right arm raised and waving (swing between "out" and "up").
        r.RotateJointZ("Right Arm", 2.35f + 0.38f * S(t * 7.0f));
        r.AddJointRotationAxis("Right Arm", 1, 0, 0, -0.15f);
    }
    else if (name == "Cheer") {
        const float b = 2.55f + 0.18f * S(t * 8.0f);
        r.RotateJointZ("Right Arm", b);
        r.RotateJointZ("Left Arm", -b);
        // small vertical bounce via the head/torso
        r.RotateJointX("Torso", 0.05f * S(t * 8.0f));
    }
    else if (name == "Dance") {
        const float a = S(t * 5.0f);
        const float b = S(t * 5.0f + 1.5708f);
        r.RotateJointZ("Right Arm", 1.5f + 0.95f * a);
        r.RotateJointZ("Left Arm", -1.5f + 0.95f * b);
        r.RotateJointZ("Right Leg", 0.30f * b);
        r.RotateJointZ("Left Leg", -0.30f * a);
        r.RotateJointY("Torso", 0.40f * S(t * 2.5f));
    }
    else if (name == "Sit") {
        r.RotateJointX("Left Leg", -1.45f);
        r.RotateJointX("Right Leg", -1.45f);
        r.RotateJointX("Left Arm", 0.35f);
        r.RotateJointX("Right Arm", 0.35f);
    }
    else if (name == "Point") {
        // Right arm forward (along -Z), lifted a touch to the side.
        r.RotateJointX("Right Arm", -1.45f);
        r.AddJointRotationAxis("Right Arm", 0, 0, 1, 0.45f);
    }
    else if (name == "Salute") {
        r.RotateJointZ("Right Arm", 2.60f);
        r.AddJointRotationAxis("Right Arm", 1, 0, 0, -0.65f);
        r.AddJointRotationAxis("Right Arm", 0, 1, 0, 0.25f);
    }
    else if (name == "Laugh") {
        const float a = S(t * 9.0f);
        r.RotateJointX("Torso", 0.10f * a);
        r.RotateJointX("Head", 0.16f * a);
        r.RotateJointZ("Right Arm", 0.60f + 0.18f * a);
        r.RotateJointZ("Left Arm", -0.60f - 0.18f * a);
    }
    else if (name == "Idle") {
        const float a = S(t * 1.6f);
        r.RotateJointY("Torso", 0.05f * a);
        r.RotateJointZ("Right Arm", 0.10f * a);
        r.RotateJointZ("Left Arm", -0.10f * a);
        r.RotateJointX("Head", 0.04f * C(t * 1.1f));
    }
    else {
        // Unknown emote: gentle idle sway.
        r.RotateJointY("Torso", 0.05f * S(t * 1.5f));
    }
}

} // namespace EmoteSystem
