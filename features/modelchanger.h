#pragma once

// Mode Changer
// ------------
// Draws one of the models from the Models folder over the LOCAL player's
// character. The real character keeps running its own movement and animation;
// this is a pure screen-space overlay that follows the character's position and,
// optionally, turns with the character's look direction.
//
// The model is rendered by a dedicated Cheat::Core::PreviewRenderer (the same
// hardware pipeline the 3D preview uses), so it uses the real .glb geometry and
// embedded textures, with the exact per-model orientation the preview applies.
// The render target is cleared transparent and blitted straight into the
// character's screen box, so nothing here touches the game or the world.

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <array>
#include <Windows.h>

#include "preview3d/ModelLoader.h"
#include "preview3d/PreviewRenderer.h"
#include "emote_system.h"
#include "player_mirror.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "../overlay/utils/W2S.h"
#include "../overlay/imgui/imgui.h"
#include "../overlay/imgui/imgui_impl_dx11.h"

namespace ModelChanger
{
    // Set each frame by RenderModeChanger; the character-hider only removes the
    // player's body once the replacement model has actually loaded, so a failed
    // load never leaves you invisible with nothing to show.
    inline bool g_modelReady = false;

    struct Entry
    {
        std::string display;  // shown in the menu (path without the extension)
        std::string baseRel;  // path relative to the Models root, without extension
        bool hasGlb = false;
        bool hasObj = false;
    };

    inline std::string ExeDirectory()
    {
        char buf[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::string s(buf);
        const size_t pos = s.find_last_of('\\');
        return (pos == std::string::npos) ? std::string() : s.substr(0, pos + 1);
    }

    // Mirrors Preview3D's asset resolution so the same Models folder is found
    // whether the tool runs from build\Release or from the repo layout.
    inline std::string ModelsRoot()
    {
        const std::string dir = ExeDirectory();
        const char* candidates[] = { "", "..\\..\\Seraph\\", "..\\..\\..\\Seraph\\" };
        for (const char* c : candidates)
        {
            const std::string p = dir + c + "Models\\";
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                return p;
        }
        if (Globals::originalExeDir.empty())
        {
            char envBuf[MAX_PATH] = {};
            DWORD sz = GetEnvironmentVariableA("SERAPH_ORIGDIR", envBuf, MAX_PATH);
            if (sz > 0 && sz < MAX_PATH)
                Globals::originalExeDir = envBuf;
        }
        if (!Globals::originalExeDir.empty())
        {
            const std::string p = Globals::originalExeDir + "Models\\";
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                return p;
        }
        return dir + "..\\..\\Seraph\\Models\\";
    }

    inline void ScanDir(const std::string& root, const std::string& rel,
                        std::vector<Entry>& out, int depth)
    {
        if (depth > 3) return;

        WIN32_FIND_DATAA fd;
        const std::string pattern = root + rel + "*";
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        do
        {
            if (fd.cFileName[0] == '.') continue; // skip "." and ".."

            const std::string childRel = rel + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                ScanDir(root, childRel + "\\", out, depth + 1);
                continue;
            }

            const std::string name = fd.cFileName;
            if (name.size() <= 4) continue;
            std::string ext = name.substr(name.size() - 4);
            for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
            const bool isGlb = (ext == ".glb");
            const bool isObj = (ext == ".obj");
            if (!isGlb && !isObj) continue;

            const std::string base = childRel.substr(0, childRel.size() - 4);

            auto it = std::find_if(out.begin(), out.end(),
                [&](const Entry& e) { return e.baseRel == base; });
            if (it == out.end())
            {
                Entry e;
                e.baseRel = base;
                e.display = base;
                e.hasGlb = isGlb;
                e.hasObj = isObj;
                out.push_back(std::move(e));
            }
            else
            {
                it->hasGlb = it->hasGlb || isGlb;
                it->hasObj = it->hasObj || isObj;
            }
        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }

    // Discovered once, sorted by path so the saved index stays stable as long as
    // the file set does not change.
    inline const std::vector<Entry>& GetModelList()
    {
        static std::vector<Entry> s_list;
        static bool s_scanned = false;
        if (!s_scanned)
        {
            s_scanned = true;
            ScanDir(ModelsRoot(), "", s_list, 0);
            std::sort(s_list.begin(), s_list.end(),
                [](const Entry& a, const Entry& b) { return a.baseRel < b.baseRel; });
        }
        return s_list;
    }

    struct Orientation
    {
        float yaw;    // radians; aligns the model's default facing
        float pitch;  // radians; corrects the model's up axis
        int   upAxis; // skeleton up axis (only affects the preview skeleton)
    };

    // Same convention the 3D preview uses, keyed off the model's folder name so
    // bundled Sketchfab models stay upright. Unknown/drop-in models are assumed
    // Y-up; the Yaw/Pitch offsets in the menu correct anything else.
    inline Orientation OrientationFor(const std::string& base)
    {
        std::string s = base;
        for (char& c : s) c = (char)tolower((unsigned char)c);
        if (s.find("tungtung") != std::string::npos)
            return { 3.14159265f, 3.14159265f / 2.0f, -2 }; // Z-up, head at -Z
        if (s.find("mario") != std::string::npos)
            return { 178.0f * (3.14159265f / 180.0f), -3.14159265f / 2.0f, 2 }; // Z-up, head at +Z, yaw 178
        return { 0.0f, 0.0f, 1 }; // Y-up humanoid
    }

    inline std::string FirstTextureInDir(const std::string& file)
    {
        const std::string dir = file.substr(0, file.find_last_of('\\') + 1);
        auto search = [](const std::string& d) -> std::string {
            WIN32_FIND_DATAA fd;
            const std::string pat = d + "*.*";
            HANDLE h = FindFirstFileA(pat.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return "";
            std::string best;
            do {
                std::string name = fd.cFileName;
                std::string lower = name;
                for (auto& c : lower) c = (char)::tolower(c);
                if (lower.find(".png") != std::string::npos ||
                    lower.find(".jpg") != std::string::npos ||
                    lower.find(".jpeg") != std::string::npos ||
                    lower.find(".bmp") != std::string::npos ||
                    lower.find(".tga") != std::string::npos)
                {
                    best = d + name;
                    break;
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
            return best;
        };
        // Prefer a "textures" subfolder (common for rigged character exports).
        std::string sub = search(dir + "textures\\");
        if (!sub.empty()) return sub;
        return search(dir);
    }

    // 1x1 white BMP (BGR + pad) used when a model has no texture file.
    inline const unsigned char* WhiteBmpBytes(size_t& outSize)
    {
        static const unsigned char k_WhiteBMP[] = {
            'B','M', 58,0,0,0, 0,0,0,0, 54,0,0,0,
            40,0,0,0, 1,0,0,0, 1,0,0,0, 1,0, 24,0, 0,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
            255,255,255,0
        };
        outSize = sizeof(k_WhiteBMP);
        return k_WhiteBMP;
    }

    inline void LoadDefaultTexture(Cheat::Core::PreviewRenderer& r)
    {
        size_t n = 0;
        const unsigned char* bytes = WhiteBmpBytes(n);
        r.LoadTextureFromMemory(bytes, n);
    }

    // Loads a discovered model into the given renderer, GLB first (real geometry
    // + embedded texture), OBJ as a fallback. Returns false if neither loads.
    inline bool LoadSelected(Cheat::Core::PreviewRenderer& r, const Entry& e)
    {
        const Orientation o = OrientationFor(e.baseRel);
        r.SetModelYawOffset(o.yaw);
        r.SetModelPitchOffset(o.pitch);
        r.SetSkeletonUpAxis(o.upAxis);

        const std::string root = ModelsRoot();

        if (e.hasGlb)
        {
            Cheat::Core::LoadedModel model;
            Cheat::Core::GLBTexture tex;
            if (Cheat::Core::LoadGLB(root + e.baseRel + ".glb", model, &tex) && r.LoadModel(model))
            {
                // Multi-material GLB: ApplyLoadedModel already built a per-submesh
                // SRV for each mesh; only fall back to the single texture otherwise.
                if (!model.submeshes.empty())
                {
                }
                else if (!tex.data.empty())
                {
                    r.LoadTextureFromMemory(tex.data.data(), tex.data.size());
                }
                else
                {
                    LoadDefaultTexture(r);
                }
                return true;
            }
        }

        if (e.hasObj)
        {
            Cheat::Core::LoadedModel model;
            if (Cheat::Core::LoadOBJ(root + e.baseRel + ".obj", model) && r.LoadModel(model))
            {
                const std::string t = FirstTextureInDir(root + e.baseRel + ".obj");
                if (!t.empty()) r.LoadTexture(t);
                else LoadDefaultTexture(r);
                return true;
            }
        }

        return false;
    }

    // Character hiding — mirrors the Headless feature: target specific named parts
    // (Head, Torso/UpperTorso/LowerTorso, Left/Right Arm/Leg) instead of walking
    // the whole hierarchy. Runs every frame so it works immediately.
    inline void UpdateCharacterHidden()
    {
        const bool want = Options::ModelChanger::Enabled && Options::ModelChanger::HideCharacter;

        if (!want) return;

        if (!Globals::Roblox::LocalPlayer.address) return;
        auto character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address) return;

        // Parts to hide (R15 + R6 names)
        const char* partNames[] = {
            "Head",
            "UpperTorso", "LowerTorso", "Torso",
            "LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
            "RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
            "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
            "RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg",
            "HumanoidRootPart"
        };

        // 21 FindFirstChild calls per frame is ~1700 driver reads/frame (each
        // one re-reads the whole child list plus a name per child). The parts
        // only change on respawn, so resolve once per character address and
        // re-apply the transparency writes (which the game can reset) each
        // frame from the cached list.
        static uintptr_t s_hiddenCharAddr = 0;
        static std::vector<RobloxInstance> s_hiddenParts;
        static ULONGLONG s_hiddenRescanTick = 0;
        if (character.address != s_hiddenCharAddr || GetTickCount64() - s_hiddenRescanTick > 2000)
        {
            s_hiddenCharAddr = character.address;
            s_hiddenRescanTick = GetTickCount64();
            s_hiddenParts.clear();
            for (const char* name : partNames)
            {
                auto part = character.FindFirstChild(name);
                if (part.address)
                    s_hiddenParts.push_back(part);
            }
        }

        for (const auto& part : s_hiddenParts)
            Memory->write<float>(part.address + Offsets::BasePart::Transparency, 1.0f);
    }

    inline void RenderModeChanger(ImDrawList* dl)
    {
        UpdateCharacterHidden();

        if (!Options::ModelChanger::Enabled || !dl || !Globals::Viewport::Valid)
            return;

        ID3D11Device* dev = ImGui_ImplDX11_GetDevice();
        ID3D11DeviceContext* ctx = ImGui_ImplDX11_GetDeviceContext();
        if (!dev || !ctx)
            return;

        static Cheat::Core::PreviewRenderer s_renderer;
        static bool s_inited = false;
        static std::string s_loadedKey;
        static bool s_loadFailed = false;

        if (!s_inited)
        {
            if (!s_renderer.Initialize(dev, ctx, 600, 900))
                return;
            s_renderer.SetAutoSpin(false);
            // Transparent background so only the model shows over the character.
            s_renderer.SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            s_inited = true;
        }

        const auto& list = GetModelList();
        const int idx = Options::ModelChanger::Model;
        if (idx < 0 || idx >= (int)list.size())
            return;
        const Entry& e = list[idx];

        if (s_loadedKey != e.baseRel)
        {
            s_loadedKey = e.baseRel;
            s_loadFailed = !LoadSelected(s_renderer, e);
            PlayerMirror::ResetRest();
        }
        g_modelReady = !s_loadFailed && s_renderer.IsReady();
        if (!g_modelReady)
            return;

        if (!Globals::Roblox::LocalPlayer.address)
            return;
        const auto character = Globals::Roblox::LocalPlayer.Character();
        if (!character.address)
            return;

        auto hrp = character.FindFirstChild("HumanoidRootPart");
        if (!hrp.address) hrp = character.FindFirstChild("UpperTorso");
        if (!hrp.address) hrp = character.FindFirstChild("Torso");
        if (!hrp.address)
            return;

        const Vectors::Vector3 p = hrp.Position();
        if (p.x == 0.f && p.y == 0.f && p.z == 0.f)
            return;

        // Full-body screen box from the root part, using the same anchors the ESP
        // applies on its "no limbs" fallback path: 2.2 studs below the root for the
        // feet, 3.0 above for the top of the head.
        const Vectors::Vector3 feet = { p.x, p.y - 2.2f, p.z };
        const Vectors::Vector3 top = { p.x, p.y + 3.0f, p.z };
        const Vectors::Vector2 bSc = WorldToScreen(feet);
        const Vectors::Vector2 tSc = WorldToScreen(top);
        if (bSc.x < 0.f || bSc.y < 0.f || tSc.x < 0.f || tSc.y < 0.f)
            return;

        const float boxTop = tSc.y;
        const float boxBottom = bSc.y;
        const float boxH = boxBottom - boxTop;
        if (boxH <= 1.0f)
            return;

        const float cx = (tSc.x + bSc.x) * 0.5f;
        const float cy = (boxTop + boxBottom) * 0.5f;

        const float scaleMul = Options::ModelChanger::Scale > 0.05f ? Options::ModelChanger::Scale : 1.0f;
        const float modelH = boxH * scaleMul;
        const float modelW = modelH * (600.0f / 900.0f);
        const ImVec2 rectMin(cx - modelW * 0.5f, cy - modelH * 0.5f);
        const ImVec2 rectMax(cx + modelW * 0.5f, cy + modelH * 0.5f);

        // Turn the model with the character relative to the camera. The look
        // vectors are flattened to the ground plane; the difference of their yaws
        // is how far the character has rotated away from the camera's forward.
        // The renderer's camera sits on -Z looking toward +Z, and the per-model
        // yaw offset already makes the model face the viewer at view yaw 0, so the
        // absolute view yaw that points the model along the character is pi - d.
        const Orientation o = OrientationFor(e.baseRel);
        const float userYaw = Options::ModelChanger::YawOffset * (3.14159265f / 180.0f);
        const float userPitch = Options::ModelChanger::PitchOffset * (3.14159265f / 180.0f);

        float viewYaw = userYaw;
        if (Options::ModelChanger::FollowFacing && Globals::Roblox::Camera.address)
        {
            const auto pf = hrp.CFrame().GetLookVector();
            const auto cf = Globals::Roblox::Camera.CFrame().GetLookVector();
            const float dPhi = atan2f(pf.x, pf.z) - atan2f(cf.x, cf.z);
            viewYaw += 3.14159265f - dPhi;
        }

        s_renderer.SetModelYawOffset(o.yaw);
        s_renderer.SetModelPitchOffset(o.pitch);
        s_renderer.SetViewYaw(viewYaw);
        s_renderer.SetViewPitch(userPitch);
        s_renderer.SetOrthographic(Options::ModelChanger::UseOrthographic);

        // Use actual frame delta time instead of fixed 1/60 for smooth animation
        static float s_lastFrameTime = 0.0f;
        const float now = (float)ImGui::GetTime();
        float dt = (s_lastFrameTime > 0.0f) ? (now - s_lastFrameTime) : (1.0f / 60.0f);
        if (dt < 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;
        s_lastFrameTime = now;

        // Pose the skeleton before rendering (no-op for models with no rig).
        // Priority: mirror the player's live pose, else play a chosen emote.
        static int s_lastEmote = -2;
        if (Options::ModelChanger::Animate)
        {
            // Player mirror: the model copies the character's real parts, so it
            // follows walking/jumping and any emote the player performs.
            if (s_lastEmote != -2) s_lastEmote = -2;
            EmoteSystem::EmoteManager::Get().Enabled = false;
            PlayerMirror::Update(s_renderer, dt);
        }
        else if (Options::ModelChanger::EmoteEnabled)
        {
            auto& em = EmoteSystem::EmoteManager::Get();
            em.Initialize();
            em.Enabled = true;
            em.SetSpeed(Options::ModelChanger::EmoteSpeed);
            // Play() resets the clock, so only call it when the selection changes.
            if (Options::ModelChanger::Emote != s_lastEmote)
            {
                s_lastEmote = Options::ModelChanger::Emote;
                if (s_lastEmote >= 0) em.Play(s_lastEmote);
                else em.Stop();
            }
            em.Apply(s_renderer, dt);
        }
        else if (!s_renderer.HasSkin() || s_lastEmote != -2)
        {
            s_lastEmote = -2;
            EmoteSystem::EmoteManager::Get().Enabled = false;
            s_renderer.ClearPose();
        }

        s_renderer.Update(dt);

        dl->AddImage((ImTextureID)s_renderer.GetTextureID(), rectMin, rectMax);
    }
}
