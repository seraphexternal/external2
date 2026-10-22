#pragma once

// PlayerMirror — poses the model from the local character's REAL parts.
//
// Each frame we read the world CFrames of the character's root / torso / head /
// arms / legs, turn each into an orientation relative to the root part, subtract
// a one-time rest snapshot (taken while standing still), and hand the result to
// the renderer as an ABSOLUTE semantic-space joint orientation. The renderer
// composes those down the model's own hierarchy, so the model copies whatever
// the player is actually doing — walking, jumping, sitting, and any emote
// (including the RBXM ones forced onto the character elsewhere).
//
// It works on any rig because it only ever compares the character against
// itself and re-expresses the delta in the model's semantic axes; the model's
// own bind pose is the neutral.

#include <cmath>
#include <cstring>
#include <string>
#include <initializer_list>

#include "../rbx/globals/globals.h"
#include "../rbx/offsets.h"
#include "../rbx/SDK/SDK.h"
#include "preview3d/PreviewRenderer.h"

namespace PlayerMirror
{
    // ── tiny 3x3 helpers (column-vector convention) ─────────────────────────
    struct M3 { float m[9]; };

    inline M3 M3FromCFrame(const sCFrame& c)
    {
        M3 r;
        r.m[0] = c.r00; r.m[1] = c.r01; r.m[2] = c.r02;
        r.m[3] = c.r10; r.m[4] = c.r11; r.m[5] = c.r12;
        r.m[6] = c.r20; r.m[7] = c.r21; r.m[8] = c.r22;
        return r;
    }
    inline M3 M3Mul(const M3& a, const M3& b)
    {
        M3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                float s = 0.0f;
                for (int k = 0; k < 3; ++k) s += a.m[i * 3 + k] * b.m[k * 3 + j];
                r.m[i * 3 + j] = s;
            }
        return r;
    }
    inline M3 M3T(const M3& a)
    {
        M3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[j * 3 + i];
        return r;
    }
    // Quaternion (x,y,z,w) of a column-vector rotation matrix.
    inline void QuatFromM3(const M3& m, float q[4])
    {
        const float tr = m.m[0] + m.m[4] + m.m[8];
        float x, y, z, w;
        if (tr > 0.0f) {
            float s = sqrtf(tr + 1.0f) * 2.0f;
            w = 0.25f * s; x = (m.m[7] - m.m[5]) / s; y = (m.m[2] - m.m[6]) / s; z = (m.m[3] - m.m[1]) / s;
        } else if (m.m[0] > m.m[4] && m.m[0] > m.m[8]) {
            float s = sqrtf(1.0f + m.m[0] - m.m[4] - m.m[8]) * 2.0f;
            w = (m.m[7] - m.m[5]) / s; x = 0.25f * s; y = (m.m[1] + m.m[3]) / s; z = (m.m[2] + m.m[6]) / s;
        } else if (m.m[4] > m.m[8]) {
            float s = sqrtf(1.0f + m.m[4] - m.m[0] - m.m[8]) * 2.0f;
            w = (m.m[2] - m.m[6]) / s; x = (m.m[1] + m.m[3]) / s; y = 0.25f * s; z = (m.m[5] + m.m[7]) / s;
        } else {
            float s = sqrtf(1.0f + m.m[8] - m.m[0] - m.m[4]) * 2.0f;
            w = (m.m[3] - m.m[1]) / s; x = (m.m[2] + m.m[6]) / s; y = (m.m[5] + m.m[7]) / s; z = 0.25f * s;
        }
        q[0] = x; q[1] = y; q[2] = z; q[3] = w;
    }

    enum Part { kRoot = 0, kTorso, kHead, kLArm, kRArm, kLLeg, kRLeg, kCount };

    // Model joint slot driven by each character part (kRoot only sets the frame).
    inline const char* JointForPart(int part)
    {
        switch (part) {
            case kTorso: return "Torso";
            case kHead:  return "Head";
            case kLArm:  return "Left Arm";
            case kRArm:  return "Right Arm";
            case kLLeg:  return "Left Leg";
            case kRLeg:  return "Right Leg";
        }
        return "";
    }

    struct Rig
    {
        uintptr_t    character = 0;
        RobloxInstance part[kCount];
        float        rest[kCount][9]{};
        bool         restCaptured = false;
        float        retryClock = 0.0f;
        bool         valid = false;
    };

    inline Rig& GetRig()
    {
        static Rig rig;
        return rig;
    }

    inline RobloxInstance FindPart(const RobloxInstance& ch,
                                   std::initializer_list<const char*> names)
    {
        for (const char* n : names) {
            auto p = ch.FindFirstChild(n);
            if (p.address) return p;
        }
        return RobloxInstance(0);
    }

    inline void Resolve(Rig& rig, const RobloxInstance& ch)
    {
        rig.part[kRoot]  = FindPart(ch, { "HumanoidRootPart", "UpperTorso", "Torso" });
        rig.part[kTorso] = FindPart(ch, { "UpperTorso", "Torso", "LowerTorso" });
        rig.part[kHead]  = FindPart(ch, { "Head" });
        rig.part[kLArm]  = FindPart(ch, { "LeftUpperArm", "Left Arm", "LeftArm" });
        rig.part[kRArm]  = FindPart(ch, { "RightUpperArm", "Right Arm", "RightArm" });
        rig.part[kLLeg]  = FindPart(ch, { "LeftUpperLeg", "Left Leg", "LeftLeg" });
        rig.part[kRLeg]  = FindPart(ch, { "RightUpperLeg", "Right Leg", "RightLeg" });

        rig.valid = rig.part[kRoot].address != 0 && rig.part[kTorso].address != 0;
        rig.restCaptured = false;
    }

    inline float HorizontalSpeed(const RobloxInstance& root)
    {
        const uintptr_t prim = Memory->read<uintptr_t>(root.address + Offsets::BasePart::Primitive);
        if (!prim) return 0.0f;
        const Vectors::Vector3 v =
            Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
        return sqrtf(v.x * v.x + v.z * v.z);
    }

    // Force a fresh rest snapshot (e.g. when the user picks a new model).
    inline void ResetRest()
    {
        GetRig().restCaptured = false;
    }

    inline void Update(Cheat::Core::PreviewRenderer& r, float dt)
    {
        if (!Globals::Roblox::LocalPlayer.address) return;
        const RobloxInstance ch = Globals::Roblox::LocalPlayer.Character();
        if (!ch.address) return;

        Rig& rig = GetRig();
        if (rig.character != ch.address) {
            rig.character = ch.address;
            Resolve(rig, ch);
        }
        if (!rig.valid) {
            rig.retryClock += dt;
            if (rig.retryClock >= 1.0f) { rig.retryClock = 0.0f; Resolve(rig, ch); }
            return;
        }
        if (!r.HasSkin()) return;

        // World orientation of every part.
        M3 world[kCount];
        for (int i = 0; i < kCount; ++i)
            world[i] = M3FromCFrame(rig.part[i].CFrame());
        const M3 rootT = M3T(world[kRoot]);

        // Snapshot the rest pose only while (nearly) still, so the model's bind
        // matches a neutral player pose rather than whatever we caught mid-stride.
        if (!rig.restCaptured) {
            if (HorizontalSpeed(rig.part[kRoot]) > 1.5f) return;
            for (int i = 0; i < kCount; ++i) {
                const M3 rel = M3Mul(rootT, world[i]);   // part orientation in the root frame
                memcpy(rig.rest[i], rel.m, sizeof(rel.m));
            }
            rig.restCaptured = true;
        }

        r.ClearWorldDeltas();

        for (int i = 0; i < kCount; ++i) {
            if (i == kRoot) continue;
            const M3 restM = *reinterpret_cast<const M3*>(rig.rest[i]);
            const M3 rel   = M3Mul(rootT, world[i]);
            M3 d = M3Mul(M3T(restM), rel);   // absolute change in the root frame

            // Re-express in the semantic frame: semantic X is the player's LEFT
            // (-X of the root), so the X row/column of the rotation flips sign.
            d.m[1] = -d.m[1]; d.m[2] = -d.m[2];
            d.m[3] = -d.m[3]; d.m[6] = -d.m[6];

            float q[4];
            QuatFromM3(d, q);
            r.SetJointDeltaSemantic(JointForPart(i), q[0], q[1], q[2], q[3]);
        }
    }
}
