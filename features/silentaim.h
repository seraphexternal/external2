#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <string>
#include <unordered_map>
#include <vector>
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/KeyBind.h"
#include "visibility.h"
#include "playerfilter.h"

// Defined later in aimbot.h, which includes this header first.
inline float ApplySmoothnessCurve(float smoothness, int curveType);

// Defined later in aimbot.h.
inline void MouseSendInput(const Vectors::Vector2& targetPos, const POINT& currentPos, float sensitivity);

// ─────────────────────────────────────────────────────────────────────────────
// Raycast silent aim.
//
// Fixes over the previous version, each of which caused misses or shots that
// landed on the wrong body part:
//
//  1. The camera rotation field (Offsets::Camera::Rotation) is an inline
//     Matrix3x3 on the Camera instance, NOT a pointer to one. It used to be
//     dereferenced as a pointer, so the aim matrix was written to whatever the
//     current rotation matrix reinterpreted to as an address and the camera was
//     never actually turned. Every other writer in the project
//     (CameraRotation(), PFSilentAim(), ragebot.h, aimview.h) writes straight
//     to Camera.address + Rotation; this does too now.
//  2. Target selection no longer consults Options::Aimbot::ClosestPart before
//     the chosen bone. ClosestPart is derived from the main aimbot's HitboxMode
//     and is overwritten every frame by ApplyWeaponProfile(), so with a weapon
//     profile set to "Closest Part" the selected hit part was silently discarded
//     and the shot snapped to whichever part sat nearest the cursor - usually
//     the torso. Hit mode now lives in its own SilentAimHitMode option.
//  3. The hit-part enums are resolved explicitly instead of being fed into a
//     switch built for a different list:
//       - raycast UI   { Head, UpperTorso, LowerTorso, HumanoidRootPart } (4)
//       - aimbot UI    { Head, Torso, L Arm, R Arm, L Leg, R Leg,
//                        Lower Torso, Upper Torso } (8)
//       - profile UI   { Head, Torso, Upper Torso, Lower Torso } (4)
//     They previously shared one switch, so "UpperTorso" resolved to the
//     HumanoidRootPart and "LowerTorso" to the Left Arm, and profile bones 2/3
//     resolved to the arms for the main aimbot as well.
//  4. SilentAimToggled is now actually written. It was read but never assigned
//     anywhere, so the dedicated raycast bind could never arm, and AimingType
//     3 was unreachable from the UI - the weapon-profile branch was the only
//     live path.
//  5. Visibility is tested against the point being aimed at, not the whole
//     body. IsPlayerOccluded() passes when any limb is visible, which let the
//     code pick a head that was behind cover and bury the shot in a wall.
//  6. Prediction uses projectile speed and time of flight (with optional drop)
//     instead of a raw velocity multiplier, and reads the target part's own
//     velocity rather than the smoothed HumanoidRootPart average.
//  7. Target acquisition is measured from the cursor rather than the centre of
//     the screen, and sticky targeting with a switch delay stops the lock
//     flipping between enemies in the middle of a burst.
//  8. Smoothness interpolates with a quaternion slerp. Lerping matrix elements
//     individually shears the basis and the camera stops pointing at the target.
// ─────────────────────────────────────────────────────────────────────────────
namespace SilentAim
{
    // Matches the raycast "Target Part" combo order exactly:
    // { "Head", "UpperTorso", "LowerTorso", "HumanoidRootPart" }
    enum class HitPart : int
    {
        Head = 0,
        UpperTorso = 1,
        LowerTorso = 2,
        HumanoidRootPart = 3
    };

    enum class HitMode : int
    {
        Fixed = 0,    // always the selected part
        Closest = 1,  // part nearest the cursor
        Adaptive = 2  // selected part, stepping down the body when covered
    };

    inline bool     s_Active = false;
    inline uintptr_t s_TargetAddress = 0;
    inline Vectors::Vector3 s_TargetPosition = { 0.f, 0.f, 0.f };
    inline int      s_TargetBoneIndex = 0;
    inline RobloxInstance s_TargetPart = RobloxInstance(0);

    // Set once the per-frame aim has been resolved, and consumed by
    // CommitDelivery() at the very end of RunAimCore. Splitting resolve from
    // write is what keeps the aimbot's own ResetViewport()/CameraRotation()
    // calls - which run after this file's entry point - from overwriting the
    // silent aim before the shot is cast.
    inline bool s_DeliveryReady = false;
    inline Vectors::Vector2 s_DeliverScreen = { 0.f, 0.f };
    inline Matrixes::Matrix3x3 s_DeliverRotation
    {
        1.f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f
    };

    // Camera::Viewport is a pair of int16s. Named separately from the
    // ViewportOffset16 in aimbot.h, which this header is included before.
    struct ViewportOffset16
    {
        int16_t x;
        int16_t y;
    };

    // ── Part resolution ────────────────────────────────────────────────────
    // Rivals is R6, so there is no UpperTorso/LowerTorso part. Both collapse
    // onto Torso rather than returning a null instance, which keeps the combo
    // usable instead of silently falling back to the head.

    inline RobloxInstance ResolveHead(const RobloxPlayer& player)
    {
        if (player.Head.address)
            return player.Head;
        if (player.Character.address)
            return player.Character.FindFirstChild("Head");
        return RobloxInstance(0);
    }

    inline RobloxInstance ResolveUpperTorso(const RobloxPlayer& player)
    {
        if (player.RigType == 1)
        {
            if (player.Upper_Torso.address)
                return player.Upper_Torso;
            if (player.Character.address)
            {
                auto part = player.Character.FindFirstChild("UpperTorso");
                if (part.address) return part;
            }
        }
        if (player.Torso.address)
            return player.Torso;
        if (player.Character.address)
            return player.Character.FindFirstChild("Torso");
        return RobloxInstance(0);
    }

    inline RobloxInstance ResolveLowerTorso(const RobloxPlayer& player)
    {
        if (player.RigType == 1)
        {
            if (player.Lower_Torso.address)
                return player.Lower_Torso;
            if (player.Character.address)
            {
                auto part = player.Character.FindFirstChild("LowerTorso");
                if (part.address) return part;
            }
        }
        if (player.Torso.address)
            return player.Torso;
        if (player.Character.address)
            return player.Character.FindFirstChild("Torso");
        return RobloxInstance(0);
    }

    inline RobloxInstance ResolveRootPart(const RobloxPlayer& player)
    {
        if (player.HumanoidRootPart.address)
            return player.HumanoidRootPart;
        if (player.Character.address)
            return player.Character.FindFirstChild("HumanoidRootPart");
        return RobloxInstance(0);
    }

    // Raycast combo, 4 entries: { Head, UpperTorso, LowerTorso, HumanoidRootPart }
    inline RobloxInstance ResolveHitPart(const RobloxPlayer& player, HitPart part)
    {
        switch (part)
        {
            case HitPart::Head:             return ResolveHead(player);
            case HitPart::UpperTorso:       return ResolveUpperTorso(player);
            case HitPart::LowerTorso:       return ResolveLowerTorso(player);
            case HitPart::HumanoidRootPart: return ResolveRootPart(player);
        }
        return ResolveHead(player);
    }

    // Aimbot combo, 8 entries:
    // { Head, Torso, Left Arm, Right Arm, Left Leg, Right Leg,
    //   Lower Torso, Upper Torso }
    // Weapon profiles feed this one, so it has to resolve in this exact order.
    inline RobloxInstance ResolveAimbotBone(const RobloxPlayer& player, int bone)
    {
        switch (bone)
        {
            case 0: return ResolveHead(player);
            case 1: return ResolveRootPart(player);
            case 2:
                if (player.RigType == 0)
                    return player.Left_Arm.address ? player.Left_Arm : RobloxInstance(0);
                return player.Left_Hand.address ? player.Left_Hand : RobloxInstance(0);
            case 3:
                if (player.RigType == 0)
                    return player.Right_Arm.address ? player.Right_Arm : RobloxInstance(0);
                return player.Right_Hand.address ? player.Right_Hand : RobloxInstance(0);
            case 4:
                if (player.RigType == 0)
                    return player.Left_Leg.address ? player.Left_Leg : RobloxInstance(0);
                return player.Left_Foot.address ? player.Left_Foot : RobloxInstance(0);
            case 5:
                if (player.RigType == 0)
                    return player.Right_Leg.address ? player.Right_Leg : RobloxInstance(0);
                return player.Right_Foot.address ? player.Right_Foot : RobloxInstance(0);
            case 6: return ResolveLowerTorso(player); // Lower Torso
            case 7: return ResolveUpperTorso(player); // Upper Torso
            default: return ResolveHead(player);
        }
    }

    inline RobloxInstance ClosestPartToCursor(const RobloxPlayer& player, const POINT& cursor)
    {
        const RobloxInstance parts[] =
        {
            player.Head, player.HumanoidRootPart, player.Upper_Torso, player.Lower_Torso,
            player.Torso, player.Left_Arm, player.Right_Arm, player.Left_Leg,
            player.Right_Leg, player.Left_Hand, player.Right_Hand, player.Left_Foot,
            player.Right_Foot, player.Left_Upper_Arm, player.Left_Lower_Arm,
            player.Right_Upper_Arm, player.Right_Lower_Arm, player.Left_Upper_Leg,
            player.Left_Lower_Leg, player.Right_Upper_Leg, player.Right_Lower_Leg
        };

        float bestDist = FLT_MAX;
        RobloxInstance best = RobloxInstance(0);

        for (const auto& part : parts)
        {
            if (!part.address) continue;

            const Vectors::Vector2 sp = WorldToScreen(part.Position());
            if (sp.x < 0.f || sp.y < 0.f) continue;

            const float dx = sp.x - static_cast<float>(cursor.x);
            const float dy = sp.y - static_cast<float>(cursor.y);
            const float d = dx * dx + dy * dy;

            if (d < bestDist)
            {
                bestDist = d;
                best = part;
            }
        }

        return best;
    }

    // Preference chain for Adaptive mode: keep the selected part when it has
    // line of sight, otherwise walk down the body so the shot still connects
    // instead of burying itself in cover.
    inline int BuildFallbackChain(const RobloxPlayer& player, RobloxInstance primary,
        RobloxInstance* out, int maxCount)
    {
        const RobloxInstance order[] =
        {
            primary,
            ResolveHead(player),
            ResolveUpperTorso(player),
            ResolveLowerTorso(player),
            ResolveRootPart(player)
        };

        int count = 0;
        for (const auto& inst : order)
        {
            if (count >= maxCount) break;
            if (!inst.address) continue;

            bool duplicate = false;
            for (int i = 0; i < count; ++i)
                if (out[i].address == inst.address) { duplicate = true; break; }

            if (!duplicate) out[count++] = inst;
        }

        return count;
    }

    // ── Effective settings ─────────────────────────────────────────────────
    // Weapon profiles overwrite the matching Options::Aimbot fields each frame,
    // so profile-specific hit-part selection is read through here rather than
    // from the profile struct directly.

    inline bool IsProfileActive()
    {
        return Options::WeaponProfiles::ActiveProfile >= 0
            && Options::WeaponProfiles::ActiveProfile < static_cast<int>(Options::WeaponProfiles::Profiles.size());
    }

    inline bool IsSilentAimEnabled()
    {
        if (IsProfileActive())
        {
            const auto& prof = Options::WeaponProfiles::Profiles[Options::WeaponProfiles::ActiveProfile];
            return prof.SilentAim && prof.Enabled;
        }
        return Options::Aimbot::SilentAimEnabled;
    }

    inline HitMode GetEffectiveHitMode()
    {
        if (IsProfileActive())
        {
            const auto& prof = Options::WeaponProfiles::Profiles[Options::WeaponProfiles::ActiveProfile];
            int mode = prof.SilentAimHitMode;
            if (mode < 0 || mode > 2) mode = 0;
            return static_cast<HitMode>(mode);
        }
        int mode = Options::Aimbot::SilentAimHitMode;
        if (mode < 0 || mode > 2) mode = 0;
        return static_cast<HitMode>(mode);
    }

    // The part the user actually asked for, independent of hit mode.
    inline RobloxInstance PrimaryPart(const RobloxPlayer& player)
    {
        if (IsProfileActive())
        {
            // ApplyWeaponProfile() maps the profile's 4-entry bone onto the
            // aimbot's 8-entry enum before storing it here.
            return ResolveAimbotBone(player, Options::Aimbot::TargetBone);
        }

        int bone = Options::Aimbot::SilentAimTargetBone;
        if (bone < 0) bone = 0;
        if (bone > 3) bone = 3;

        RobloxInstance part = ResolveHitPart(player, static_cast<HitPart>(bone));
        if (!part.address)
            part = ResolveHead(player);
        return part;
    }

    // Hold / Toggle / Always On for the dedicated raycast bind. This is what
    // left SilentAimToggled permanently false.
    inline bool UpdateToggleState()
    {
        const int key = Options::Aimbot::SilentAimKey;
        const int mode = Options::Aimbot::SilentAimToggleType;

        static bool wasDown = false;

        if (mode == 2) // Always On
        {
            Options::Aimbot::SilentAimToggled = true;
            return true;
        }

        if (key == 0)
        {
            Options::Aimbot::SilentAimToggled = false;
            return false;
        }

        const bool down = KeyBind::IsPressed(key);

        if (mode == 1) // Toggle
        {
            if (down && !wasDown)
                Options::Aimbot::SilentAimToggled = !Options::Aimbot::SilentAimToggled;

            wasDown = down;
            return Options::Aimbot::SilentAimToggled;
        }

        // Hold
        wasDown = down;
        Options::Aimbot::SilentAimToggled = down;
        return down;
    }

    inline bool IsSilentAimActive()
    {
        if (!IsSilentAimEnabled())
            return false;

        // A weapon profile's Silent Aim flag is the arming switch in that mode.
        if (IsProfileActive())
            return true;

        return UpdateToggleState();
    }

    inline float GetEffectiveSilentAimFOV()
    {
        if (IsProfileActive())
            return Options::Aimbot::FOV;
        return Options::Aimbot::SilentAimFOV;
    }

    inline float GetEffectiveSilentAimSmoothness()
    {
        if (IsProfileActive())
            return Options::Aimbot::Smoothness;
        return Options::Aimbot::SilentAimSmoothness;
    }

    inline bool GetEffectiveSilentAimPrediction()
    {
        if (IsProfileActive())
            return Options::Aimbot::Prediction;
        return Options::Aimbot::SilentAimPrediction;
    }

    inline float GetEffectiveSilentAimPredictionX()
    {
        if (IsProfileActive())
            return Options::Aimbot::PredictionX;
        return Options::Aimbot::SilentAimPredictionX;
    }

    inline float GetEffectiveSilentAimPredictionY()
    {
        if (IsProfileActive())
            return Options::Aimbot::PredictionY;
        return Options::Aimbot::SilentAimPredictionY;
    }

    inline bool GetEffectiveSilentAimTeamCheck()
    {
        if (IsProfileActive())
            return Options::Aimbot::TeamCheck;
        return Options::Aimbot::SilentAimTeamCheck;
    }

    inline bool GetEffectiveSticky()
    {
        if (IsProfileActive())
            return Options::Aimbot::StickyAim;
        return Options::Aimbot::SilentAimSticky;
    }

    inline float GetEffectiveSwitchDelay()
    {
        if (IsProfileActive())
            return Options::Aimbot::TargetSwitchDelay;
        return Options::Aimbot::SilentAimSwitchDelay;
    }

    // 0 = camera rotation only, 1 = mouse spoof only, 2 = both
    inline int GetEffectiveSilentAimMethod()
    {
        if (IsProfileActive())
        {
            const auto& prof = Options::WeaponProfiles::Profiles[Options::WeaponProfiles::ActiveProfile];
            return prof.SilentAimMode == 1 ? 2 : 0;
        }
        return Options::Aimbot::SilentAimMethod;
    }

    // ── Target geometry ────────────────────────────────────────────────────

    // The target part's own velocity beats the HumanoidRootPart average, which
    // smooths out the head bob and strafing movement that actually throws off
    // a shot.
    inline Vectors::Vector3 PartVelocity(const RobloxInstance& part)
    {
        if (!part.address)
            return { 0.f, 0.f, 0.f };

        const uintptr_t prim = Memory->read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
        if (!prim)
            return { 0.f, 0.f, 0.f };

        return Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
    }

    // Roblox workspace gravity default. Only used when drop compensation is on.
    constexpr float kGravity = 196.2f;

    inline Vectors::Vector3 ApplyPrediction(const RobloxInstance& part,
        const RobloxPlayer& player, const Vectors::Vector3& rawPos,
        const Vectors::Vector3& origin)
    {
        if (!GetEffectiveSilentAimPrediction())
            return rawPos;

        Vectors::Vector3 velocity = PartVelocity(part);
        if (velocity.x == 0.f && velocity.y == 0.f && velocity.z == 0.f)
            velocity = player.Velocity;

        const Vectors::Vector3 toTarget = rawPos - origin;
        const float distance = toTarget.Magnitude();
        if (distance < 0.001f)
            return rawPos;

        const float speed = Options::Aimbot::SilentAimProjectileSpeed;
        Vectors::Vector3 result = rawPos;

        if (speed > 1.f)
        {
            // Time of flight from the actual flight distance.
            const float flightTime = distance / speed;

            result.x += velocity.x * flightTime;
            result.y += velocity.y * flightTime;
            result.z += velocity.z * flightTime;

            if (Options::Aimbot::SilentAimDropCompensation)
                result.y -= kGravity * 0.5f * flightTime * flightTime;
        }
        else
        {
            // Legacy model: the sliders are raw multipliers on velocity.
            result.x += velocity.x * GetEffectiveSilentAimPredictionX();
            result.y += velocity.y * GetEffectiveSilentAimPredictionY();
            result.z += velocity.z * GetEffectiveSilentAimPredictionX();
        }

        return result;
    }

    // The part the shot should actually leave from. Rivals spawns bullets at
    // the weapon tip, so aiming the camera at the head from the camera origin
    // leaves the bullet travelling parallel to that line and passing beside
    // the target at close range - which reads as "hitting the body".
    // Blending toward the muzzle origin makes the two converge.
    inline RobloxInstance LocalMuzzle()
    {
        if (!Globals::Roblox::LocalPlayer.address)
            return RobloxInstance(0);

        const RobloxInstance character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address)
            return RobloxInstance(0);

        RobloxInstance hand = character.FindFirstChild("RightHand");
        if (!hand.address)
            hand = character.FindFirstChild("Right Arm");
        if (!hand.address)
            return RobloxInstance(0);

        for (auto& child : hand.GetChildren())
        {
            if (!child.address) continue;

            const std::string cls = child.Class();
            if (cls != "Part" && cls != "MeshPart")
                continue;

            std::string lower = child.Name();
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("muzzle") != std::string::npos ||
                lower.find("barrel") != std::string::npos ||
                lower.find("tip") != std::string::npos)
                return child;
        }

        return hand;
    }

    inline Vectors::Vector3 ResolveOrigin(const Vectors::Vector3& cameraPos)
    {
        const float comp = std::clamp(Options::Aimbot::SilentAimMuzzleComp, 0.f, 1.f);
        if (comp <= 0.f)
            return cameraPos;

        const RobloxInstance muzzle = LocalMuzzle();
        if (!muzzle.address)
            return cameraPos;

        return cameraPos + (muzzle.Position() - cameraPos) * comp;
    }

    // ── Part selection ─────────────────────────────────────────────────────

// Per-frame memo for the line-of-sight tests. IsPointVisibleForced runs a full
    // IsMapBlocking raycast with no caching of its own, and SelectPart can be
    // reached twice for the same player in one frame (once on the lock path,
    // once during acquisition), so without this the same part was raycast
    // repeatedly.
    inline std::unordered_map<uintptr_t, bool> s_VisibilityMemo;

    inline void BeginFrame()
    {
        s_VisibilityMemo.clear();
        s_DeliveryReady = false;
    }

    inline bool PartVisibleCached(const RobloxInstance& part, uintptr_t ignoreModel)
    {
        if (!part.address)
            return false;

        const auto found = s_VisibilityMemo.find(part.address);
        if (found != s_VisibilityMemo.end())
            return found->second;

        const bool visible = Visibility::IsPointVisibleForced(part.Position(), ignoreModel);
        s_VisibilityMemo.emplace(part.address, visible);
        return visible;
    }

    inline RobloxInstance SelectPart(const RobloxPlayer& player, const POINT& cursor)
    {
        const HitMode mode = GetEffectiveHitMode();

        // Line-of-sight gating is opt-in and OFF by default.
        //
        // IsMapBlocking was never tuned for Rivals geometry. When it reported
        // every candidate as covered, SelectPart returned a null part for all of
        // them, so target acquisition produced nothing and silent aim stopped
        // hitting entirely. Defaulting this on was a regression; the selection
        // below now runs purely on distance/FOV unless this is switched on.
        const bool requireVisible =
            Options::Aimbot::SilentAimRequireVisible && Options::Aimbot::WallCheck;

        const uintptr_t ignore = player.Character.address;

        if (mode == HitMode::Closest)
        {
            const RobloxInstance nearest = ClosestPartToCursor(player, cursor);
            if (nearest.address)
            {
                if (!requireVisible || PartVisibleCached(nearest, ignore))
                    return nearest;
            }
            return PrimaryPart(player);
        }

        const RobloxInstance primary = PrimaryPart(player);
        if (!primary.address)
            return RobloxInstance(0);

        if (!requireVisible)
            return primary;

        if (mode == HitMode::Adaptive)
        {
            RobloxInstance chain[5];
            const int count = BuildFallbackChain(player, primary, chain, 5);

            for (int i = 0; i < count; ++i)
            {
                if (PartVisibleCached(chain[i], ignore))
                    return chain[i];
            }

            // Nothing in the chain is clear; keep the requested part rather than
            // dropping the target entirely.
            return primary;
        }

        return PartVisibleCached(primary, ignore) ? primary : RobloxInstance(0);
    }

    // ── Target acquisition ─────────────────────────────────────────────────

    inline bool IsValidCandidate(const RobloxPlayer& player, uintptr_t localAddress)
    {
        if (!player.address || player.address == localAddress)
            return false;

        if (player.Health <= 0.f)
            return false;

        if (!player.Name.empty() && !PlayerFilter::AimbotAllowed(player.Name))
            return false;

        if (GetEffectiveSilentAimTeamCheck() && IsTeammate(player))
            return false;

        if (Globals::Roblox::isRivals && Options::Rivals::AntiKatana && IsHoldingKatana(player))
            return false;

        if (Options::Aimbot::DownedCheck && player.Health <= 5.0f)
            return false;

        if (Options::Aimbot::IgnoreJump && player.HumanoidRootPart.address)
        {
            const uintptr_t prim = Memory->read<uintptr_t>(
                player.HumanoidRootPart.address + Offsets::BasePart::Primitive);
            if (prim)
            {
                // Y component of AssemblyLinearVelocity.
                const float yVel = Memory->read<float>(
                    prim + Offsets::Primitive::AssemblyLinearVelocity + sizeof(float));

                if (yVel > Options::Aimbot::JumpThreshold ||
                    yVel < -Options::Aimbot::JumpThreshold)
                    return false;
            }
        }

        return true;
    }

    inline void UpdateTarget(const POINT& cursor, const Vectors::Vector3& localPos, bool haveLocal)
    {
        s_Active = false;
        s_TargetAddress = 0;
        s_TargetPart = RobloxInstance(0);

        static uintptr_t lockedTarget = 0;
        static std::chrono::steady_clock::time_point lastSwitch;

        const uintptr_t localAddress = Globals::Roblox::LocalPlayer.address;
        const float fovLimit = GetEffectiveSilentAimFOV();
        const float range = Options::Aimbot::Range;

        // ── Reuse the current lock while it stays legal ────────────────────
        if (lockedTarget != 0)
        {
            for (const auto& player : Globals::Caches::CachedPlayerObjects)
            {
                if (player.address != lockedTarget)
                    continue;

                // SelectPart applies the wall check against the chosen part.
                bool keep = IsValidCandidate(player, localAddress);

                RobloxInstance part = RobloxInstance(0);
                if (keep)
                {
                    part = SelectPart(player, cursor);
                    if (!part.address)
                        keep = false;
                }

                if (keep && haveLocal)
                {
                    const float dist = (part.Position() - localPos).Magnitude();
                    if (dist > range)
                        keep = false;
                }

                if (keep)
                {
                    const Vectors::Vector2 sp = WorldToScreen(part.Position());
                    if (sp.x < 0.f || sp.y < 0.f)
                        keep = false;
                    else
                    {
                        // Sticky locks get slack so a target drifting near the
                        // FOV edge is not dropped mid-burst.
                        const float slack = GetEffectiveSticky() ? fovLimit * 1.35f : fovLimit;
                        const float dx = sp.x - static_cast<float>(cursor.x);
                        const float dy = sp.y - static_cast<float>(cursor.y);

                        if (sqrtf(dx * dx + dy * dy) > slack)
                            keep = false;
                        else
                        {
                            s_Active = true;
                            s_TargetAddress = player.address;
                            s_TargetPart = part;
                            s_TargetPosition = part.Position();
                            s_TargetBoneIndex = IsProfileActive()
                                ? Options::Aimbot::TargetBone
                                : Options::Aimbot::SilentAimTargetBone;
                            return;
                        }
                    }
                }

                if (!keep)
                    lockedTarget = 0;

                break;
            }
        }

        // ── Acquire ───────────────────────────────────────────────────────
        uintptr_t bestTarget = 0;
        RobloxInstance bestPart = RobloxInstance(0);
        Vectors::Vector3 bestPos = { 0.f, 0.f, 0.f };
        float bestScore = FLT_MAX;

        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (!IsValidCandidate(player, localAddress))
                continue;

            const RobloxInstance part = SelectPart(player, cursor);
            if (!part.address)
                continue;

            const Vectors::Vector3 worldPos = part.Position();

            if (haveLocal && (worldPos - localPos).Magnitude() > range)
                continue;

            const Vectors::Vector2 sp = WorldToScreen(worldPos);
            if (sp.x < 0.f || sp.y < 0.f)
                continue;

            const float dx = sp.x - static_cast<float>(cursor.x);
            const float dy = sp.y - static_cast<float>(cursor.y);
            float score = sqrtf(dx * dx + dy * dy);

            if (score > fovLimit)
                continue;

            // Crosshair proximity wins; a low-health target gets a nudge so
            // finishes resolve in the player's favour.
            if (player.Health > 0.f && player.Health < 25.f)
                score -= 1.0f;

            if (score < bestScore)
            {
                bestScore = score;
                bestTarget = player.address;
                bestPart = part;
                bestPos = worldPos;
            }
        }

        if (bestTarget == 0)
            return;

        // ── Switch delay ──────────────────────────────────────────────────
        const float delay = GetEffectiveSwitchDelay();
        const auto now = std::chrono::steady_clock::now();
        const float sinceMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSwitch).count());

        if (GetEffectiveSticky() && delay > 0.f && sinceMs < delay)
            return;

        lastSwitch = now;

        s_Active = true;
        s_TargetAddress = bestTarget;
        s_TargetPart = bestPart;
        s_TargetPosition = bestPos;
        s_TargetBoneIndex = IsProfileActive()
            ? Options::Aimbot::TargetBone
            : Options::Aimbot::SilentAimTargetBone;
        lockedTarget = bestTarget;
    }

    // ── Camera write ──────────────────────────────────────────────────────

    inline Matrixes::Matrix3x3 ReadCameraRotation()
    {
        if (!Globals::Roblox::Camera.address)
            return Matrixes::Matrix3x3{ 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };

        return Memory->read<Matrixes::Matrix3x3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Rotation);
    }

    inline void WriteCameraRotation(const Matrixes::Matrix3x3& rot)
    {
        Memory->write<Matrixes::Matrix3x3>(
            Globals::Roblox::Camera.address + Offsets::Camera::Rotation, rot);
    }

    // Resolves the final aim point for this frame and stages it for
    // CommitDelivery(). Deliberately performs no camera or viewport writes.
    inline void PrepareDelivery()
    {
        s_DeliveryReady = false;

        if (!s_Active || !Globals::Roblox::Camera.address || !Globals::Viewport::Valid)
            return;

        const Vectors::Vector3 cameraPos =
            Memory->read<Vectors::Vector3>(Globals::Roblox::Camera.address + Offsets::Camera::Position);

        const Vectors::Vector3 origin = ResolveOrigin(cameraPos);

        // Re-resolve from the cached part so prediction uses this frame's
        // position rather than the position seen at acquisition time.
        Vectors::Vector3 targetPos = s_TargetPosition;

        if (s_TargetPart.address)
        {
            const Vectors::Vector3 live = s_TargetPart.Position();
            if (live.x != 0.f || live.y != 0.f || live.z != 0.f)
                targetPos = live;
        }

        for (const auto& player : Globals::Caches::CachedPlayerObjects)
        {
            if (player.address == s_TargetAddress)
            {
                targetPos = ApplyPrediction(s_TargetPart, player, targetPos, origin);
                break;
            }
        }

        const Vectors::Vector3 delta = targetPos - origin;
        const float distance = delta.Magnitude();
        if (distance < 0.001f)
            return;

        const Vectors::Vector3 direction = delta / distance;

        const sCFrame lookAtCFrame = LookAt(cameraPos, cameraPos + direction);
        const Vectors::Vector3 rightVec = lookAtCFrame.GetRightVector();
        const Vectors::Vector3 upVec = lookAtCFrame.GetUpVector();
        const Vectors::Vector3 lookVec = lookAtCFrame.GetLookVector();

        Matrixes::Matrix3x3 rotationMatrix
        {
            rightVec.x, upVec.x, lookVec.x,
            rightVec.y, upVec.y, lookVec.y,
            rightVec.z, upVec.z, lookVec.z
        };

        const float smoothness = GetEffectiveSilentAimSmoothness();
        if (smoothness > 0.f)
        {
            // Slerp the orientation. Lerping matrix elements individually
            // shears the basis and the camera drifts off the target.
            const Vectors::Vector4 currentQuat = Vectors::Vector4::FromMatrix(ReadCameraRotation());
            const Vectors::Vector4 targetQuat = Vectors::Vector4::FromMatrix(rotationMatrix);

            const float t = ApplySmoothnessCurve(smoothness, Options::Aimbot::SmoothnessCurve);
            rotationMatrix = Vectors::Vector4::Slerp(currentQuat, targetQuat, t).ToMatrix();
        }

        // Project the predicted point so the viewport offset lands the shot on
        // the same position the rotation was built from.
        const Vectors::Vector2 screenPos = WorldToScreen(targetPos);
        if (screenPos.x < 0.f || screenPos.y < 0.f)
            return;

        s_DeliverScreen = screenPos;
        s_DeliverRotation = rotationMatrix;
        s_DeliveryReady = true;
    }

    // Writes the staged aim. Called last in RunAimCore so the aimbot's own
    // camera/viewport writes earlier in the frame cannot win.
    inline void CommitDelivery()
    {
        if (!s_DeliveryReady)
            return;

        const int delivery = Options::Aimbot::SilentAimDelivery;

        // 0 = viewport, 1 = camera rotation, 2 = both
        if (delivery == 0 || delivery == 2)
        {
            const Vectors::Vector2 dims = Memory->read<Vectors::Vector2>(
                Globals::Roblox::VisualEngine + Offsets::VisualEngine::Dimensions);

            ViewportOffset16 offset
            {
                static_cast<int16_t>(2 * (dims.x - s_DeliverScreen.x)),
                static_cast<int16_t>(2 * (dims.y - s_DeliverScreen.y))
            };

            Memory->write<ViewportOffset16>(
                Globals::Roblox::Camera.address + Offsets::Camera::Viewport, offset);
        }

        if (delivery == 1 || delivery == 2)
            WriteCameraRotation(s_DeliverRotation);

        if (GetEffectiveSilentAimMethod() == 1 || GetEffectiveSilentAimMethod() == 2)
        {
            if (Options::Aimbot::SilentAimRealCursor2)
            {
                POINT cur{};
                GetCursorPos(&cur);
                MouseSendInput(s_DeliverScreen, cur, 1.0f);
            }
        }

        s_DeliveryReady = false;
    }

    // ── Entry point ────────────────────────────────────────────────────────

    inline void RunSilentAim()
    {
        BeginFrame();

        if (!Globals::Roblox::LocalPlayer.address || !Globals::Viewport::Valid)
        {
            s_Active = false;
            return;
        }

        if (!IsSilentAimActive())
        {
            s_Active = false;
            return;
        }

        // Fire gate: keep the aim alive briefly after the trigger releases so
        // the last shot of a burst still leaves on target.
        static auto lastFire = std::chrono::steady_clock::now();
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
            lastFire = std::chrono::steady_clock::now();

        if (Options::Aimbot::SilentAimFireOnly)
        {
            const float sinceMs = static_cast<float>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - lastFire).count());

            if (sinceMs > Options::Aimbot::SilentAimHoldMs)
            {
                s_Active = false;
                return;
            }
        }

        POINT cursor{};
        GetCursorPos(&cursor);

        const RobloxInstance localHRP =
            Globals::Roblox::LocalPlayer.Character().FindFirstChild("HumanoidRootPart");

        const Vectors::Vector3 localPos =
            localHRP.address ? localHRP.Position() : Vectors::Vector3{ 0.f, 0.f, 0.f };

        UpdateTarget(cursor, localPos, localHRP.address != 0);

        if (s_Active)
            PrepareDelivery();
    }
}