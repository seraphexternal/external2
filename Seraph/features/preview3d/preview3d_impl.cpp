#include "preview3d.h"

#include "../../features/obfuscate.h"
#include "../../rbx/globals/options.h"
#include "../../rbx/globals/globals.h"
#include "../../overlay/imgui/imgui_impl_dx11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>

#include "ModelLoader.h"
#include "PreviewRenderer.h"

#include "../../rbx/configs/json.hpp"
#include "stb_image.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Minimal stand-in for the donor project's color constants (used as the model
// render-target clear color). Transparent so only the model shows over the player.
namespace colors { inline ImVec4 child_fill{ 0.f, 0.f, 0.f, 0.f }; }

namespace Preview3D
{
    static Cheat::Core::PreviewRenderer g_Renderer;
    static bool   g_InitDone = false;
    static Model  g_Current = Model::None;
    static bool   g_ModelLoaded = false;
    static float  g_LastTime = 0.f;

    // 1x1 white BMP (BGR + pad) used when a model has no texture file.
    static const unsigned char k_WhiteBMP[] = {
        'B','M', 58,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 1,0,0,0, 1,0,0,0, 1,0, 24,0, 0,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        255,255,255,0
    };

    static std::string ExeDirA()
    {
        char buf[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::string s(buf);
        auto pos = s.find_last_of('\\');
        return (pos == std::string::npos) ? "" : s.substr(0, pos + 1);
    }

    static std::string FindObj(const char* rel)
    {
        std::string dir = ExeDirA();
        const char* candidates[] = {
            "",
            "..\\..\\Seraph\\",
            "..\\..\\..\\Seraph\\",
        };
        for (const char* c : candidates)
        {
            std::string p = dir + c + std::string("Models\\") + rel;
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                return p;
        }
        if (Globals::originalExeDir.empty())
        {
            char envBuf[MAX_PATH] = {};
            DWORD sz = GetEnvironmentVariableA(SX("SERAPH_ORIGDIR").c_str(), envBuf, MAX_PATH);
            if (sz > 0 && sz < MAX_PATH)
                Globals::originalExeDir = envBuf;
        }
        if (!Globals::originalExeDir.empty())
        {
            std::string p = Globals::originalExeDir + "Models\\" + rel;
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                return p;
        }
        return dir + std::string("..\\..\\Seraph\\Models\\") + rel;
    }

    static std::string FirstTextureInDir(const std::string& objPath)
    {
        std::string dir = objPath.substr(0, objPath.find_last_of('\\') + 1);
        auto search = [](const std::string& d) -> std::string {
            WIN32_FIND_DATAA fd;
            std::string pat = d + "*.*";
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

    static void LoadDefaultTexture()
    {
        g_Renderer.LoadTextureFromMemory(k_WhiteBMP, sizeof(k_WhiteBMP));
    }

    // ── Built-in procedural humanoid (used when no .obj asset is present) ──
    static void AddBox(Cheat::Core::LoadedModel& out,
                       float cx, float cy, float cz, float sx, float sy, float sz)
    {
        using V = Cheat::Core::ModelVertex;
        const float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
        // Register each box as a distinct body part so the R6 skeleton heuristic
        // (torso/arms/legs/head detection) can build humanoid bones from the
        // procedural fallback figure.
        Cheat::Core::ModelPartAABB part;
        part.min[0] = cx - hx; part.min[1] = cy - hy; part.min[2] = cz - hz;
        part.max[0] = cx + hx; part.max[1] = cy + hy; part.max[2] = cz + hz;
        part.valid = true;
        out.body_parts.push_back(part);
        float C[8][3] = {
            { cx - hx, cy - hy, cz - hz }, { cx + hx, cy - hy, cz - hz },
            { cx + hx, cy + hy, cz - hz }, { cx - hx, cy + hy, cz - hz },
            { cx - hx, cy - hy, cz + hz }, { cx + hx, cy - hy, cz + hz },
            { cx + hx, cy + hy, cz + hz }, { cx - hx, cy + hy, cz + hz },
        };
        int F[6][4] = {
            { 0,3,2,1 }, { 4,5,6,7 }, { 0,1,5,4 },
            { 3,7,6,2 }, { 1,2,6,5 }, { 0,4,7,3 },
        };
        float N[6][3] = {
            { 0,0,-1 }, { 0,0,1 }, { 0,-1,0 },
            { 0,1,0 },  { 1,0,0 }, { -1,0,0 },
        };
        for (int f = 0; f < 6; ++f)
        {
            int idx[6] = { F[f][0], F[f][1], F[f][2], F[f][0], F[f][2], F[f][3] };
            for (int t = 0; t < 6; ++t)
            {
                int ci = idx[t];
                V v{};
                v.x = C[ci][0]; v.y = C[ci][1]; v.z = C[ci][2];
                v.nx = N[f][0]; v.ny = N[f][1]; v.nz = N[f][2];
                v.u = (C[ci][0] - (cx - hx)) / (sx > 1e-5f ? sx : 1.0f);
                v.v = (C[ci][1] - (cy - hy)) / (sy > 1e-5f ? sy : 1.0f);
                out.vertices.push_back(v);
            }
        }
    }

    static bool BuildHumanoid(Cheat::Core::LoadedModel& out)
    {
        // Standing figure, origin at feet (y = 0), ~1.8 tall, centered on x/z.
        AddBox(out, 0.00f, 1.62f, 0.00f, 0.26f, 0.28f, 0.26f); // head
        AddBox(out, 0.00f, 1.15f, 0.00f, 0.50f, 0.60f, 0.28f); // torso
        AddBox(out, 0.00f, 0.82f, 0.00f, 0.46f, 0.22f, 0.26f); // pelvis
        AddBox(out, -0.36f, 1.20f, 0.00f, 0.16f, 0.36f, 0.16f); // L upper arm
        AddBox(out,  0.36f, 1.20f, 0.00f, 0.16f, 0.36f, 0.16f); // R upper arm
        AddBox(out, -0.36f, 0.82f, 0.00f, 0.14f, 0.34f, 0.14f); // L lower arm
        AddBox(out,  0.36f, 0.82f, 0.00f, 0.14f, 0.34f, 0.14f); // R lower arm
        AddBox(out, -0.14f, 0.55f, 0.00f, 0.18f, 0.50f, 0.18f); // L upper leg
        AddBox(out,  0.14f, 0.55f, 0.00f, 0.18f, 0.50f, 0.18f); // R upper leg
        AddBox(out, -0.14f, 0.12f, 0.00f, 0.15f, 0.42f, 0.15f); // L lower leg
        AddBox(out,  0.14f, 0.12f, 0.00f, 0.15f, 0.42f, 0.15f); // R lower leg

        if (out.vertices.empty()) return false;

        float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
        float mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const auto& v : out.vertices)
        {
            mn[0] = (std::min)(mn[0], v.x); mx[0] = (std::max)(mx[0], v.x);
            mn[1] = (std::min)(mn[1], v.y); mx[1] = (std::max)(mx[1], v.y);
            mn[2] = (std::min)(mn[2], v.z); mx[2] = (std::max)(mx[2], v.z);
        }
        out.scale = 1.0f;
        out.center[0] = (mn[0] + mx[0]) * 0.5f;
        out.center[1] = (mn[1] + mx[1]) * 0.5f;
        out.center[2] = (mn[2] + mx[2]) * 0.5f;
        for (int i = 0; i < 3; ++i)
        {
            out.raw_min[i] = mn[i]; out.raw_max[i] = mx[i];
            out.body_min[i] = mn[i]; out.body_max[i] = mx[i];
            out.box_min[i] = mn[i]; out.box_max[i] = mx[i];
        }
        return true;
    }

    static void DebugLog(const char* fmt, ...)
    {
        char tmpPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        strcat_s(tmpPath, "p3d_diag.log");
        FILE* f = nullptr;
        fopen_s(&f, tmpPath, "a");
        if (!f) return;
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }

    static void EnsureModelLoaded()
    {
        if (g_ModelLoaded)
            return;

        // None = show 2D Roblox avatar, skip 3D model entirely
        if (g_Current == Model::None) {
            g_ModelLoaded = true;
            return;
        }

        static bool msgShown = false;
        if (!msgShown) {
            char dbg[512];
            snprintf(dbg, sizeof(dbg), "EnsureModelLoaded called. model=%d exeDir=%s", (int)g_Current, ExeDirA().c_str());
            OutputDebugStringA(dbg);
            msgShown = true;
        }

        DebugLog("=== EnsureModelLoaded begin, model=%d, exeDir=%s", (int)g_Current, ExeDirA().c_str());

        const char* glbRel = nullptr;
        const char* objRel = nullptr;
        switch (g_Current)
        {
            case Model::Roblox:
                glbRel = "Roblox\\RobloxR15Rigged.glb";
                objRel = "Roblox\\RobloxR15Rigged.obj"; break;
            case Model::TungTung:
                glbRel = "TungTung\\tung_tung_sahur.glb";
                objRel = "TungTung\\tung_tung_sahur.obj"; break;
            case Model::Mario:
                glbRel = "Mario\\Mario.glb";
                objRel = "Mario\\Mario.obj"; break;
            default: glbRel = nullptr; objRel = nullptr; break;
        }

        // Per-model orientation + skeleton up axis. This MUST run before each
        // LoadModel call (GLB and OBJ), because the skeleton is built inside
        // LoadModel from m_SkelUpAxis; configuring it afterwards is a no-op for
        // the already-constructed bones. Frozen test: XMMatrixRotationX maps
        // raw -Z to screen-up at pitch +90, and raw +Z to screen-up at -90.
        auto applyModelConfig = [&]() {
            switch (g_Current)
            {
                case Model::TungTung:
                    g_Renderer.SetModelYawOffset(3.14159265f); // OBJ faces away; flip 180° to face viewer
                    g_Renderer.SetModelPitchOffset(3.14159265f / 2.0f);
                    g_Renderer.SetSkeletonUpAxis(-2); // Z-up, head at -Z
                    break;
                case Model::Mario:
                    g_Renderer.SetModelYawOffset(0.0f);
                    g_Renderer.SetModelPitchOffset(-3.14159265f / 2.0f);
                    g_Renderer.SetSkeletonUpAxis(2); // Z-up, head at +Z
                    break;
                default:
                    g_Renderer.SetModelYawOffset(0.0f);
                    g_Renderer.SetModelPitchOffset(0.0f);
                    g_Renderer.SetSkeletonUpAxis(1); // Y-up humanoid
                    break;
            }
        };

        // Try GLB first (Sketchfab default format, embedded textures)
        if (glbRel)
        {
            std::string glb = FindObj(glbRel);
            DebugLog("GLB path: '%s' (empty=%d)", glb.c_str(), (int)glb.empty());
            if (!glb.empty())
            {
                Cheat::Core::LoadedModel model;
                Cheat::Core::GLBTexture tex;
                DebugLog("Calling LoadGLB...");
                bool glbOk = Cheat::Core::LoadGLB(glb, model, &tex);
                DebugLog("LoadGLB returned %d, verts=%zu", (int)glbOk, model.vertices.size());
                if (glbOk)
                {
                    // Apply per-model orientation + skeleton up axis BEFORE
                    // LoadModel, because the skeleton is built inside LoadModel
                    // using m_SkelUpAxis. Setting it afterwards has no effect on
                    // the already-built bones.
                    applyModelConfig();

                    DebugLog("Calling g_Renderer.LoadModel...");
                    bool renderOk = g_Renderer.LoadModel(model);
                    DebugLog("g_Renderer.LoadModel returned %d", (int)renderOk);
                    if (renderOk)
                    {
                        if (!model.submeshes.empty())
                        {
                            // Multi-material GLB: ApplyLoadedModel already built a
                            // per-submesh SRV for each mesh, so keep the single
                            // texture path clear (it would only reuse image[0]).
                            DebugLog("Multi-texture submeshes loaded (%zu)", model.submeshes.size());
                        }
                        else if (!tex.data.empty())
                        {
                            DebugLog("Loading embedded texture, size=%zu", tex.data.size());
                            g_Renderer.LoadTextureFromMemory(tex.data.data(), tex.data.size());
                        }
                        else
                        {
                            DebugLog("No embedded texture, using default");
                            LoadDefaultTexture();
                        }
                        g_ModelLoaded = true;
                        DebugLog("GLB loaded successfully!");
                        return;
                    }
                }
                DebugLog("GLB path failed, falling through to OBJ");
            }
        }

        // Fall back to OBJ (skip for models whose OBJ is just a tiny placeholder
        // box; the humanoid fallback below produces a much better preview).
        if (objRel && g_Current == Model::TungTung)
        {
            std::string obj = FindObj(objRel);
            DebugLog("OBJ path: '%s' (empty=%d)", obj.c_str(), (int)obj.empty());
            if (!obj.empty())
            {
                applyModelConfig();
                if (g_Renderer.LoadModel(obj))
                {
                    std::string tex = FirstTextureInDir(obj);
                    if (!tex.empty())
                        g_Renderer.LoadTexture(tex);
                    else
                        LoadDefaultTexture();
                    g_ModelLoaded = true;
                    DebugLog("OBJ loaded successfully!");
                    return;
                }
            }
        }

        DebugLog("Both GLB and OBJ failed, using humanoid fallback");
        // Fallback: built-in procedural humanoid so the preview always shows a
        // rotating figure even when no .obj asset is deployed on disk.
        Cheat::Core::LoadedModel human;
        applyModelConfig();
        if (BuildHumanoid(human) && g_Renderer.LoadModel(human))
        {
            LoadDefaultTexture();
            g_ModelLoaded = true;
            DebugLog("Humanoid fallback loaded");
            return;
        }

        g_ModelLoaded = false;
        DebugLog("All model loading failed!");
    }

    void SetModel(Model m)
    {
        if (m != g_Current)
        {
            g_Current = m;
            g_ModelLoaded = false;
        }
    }

    void Update()
    {
        if (!Options::Preview3D::Enabled)
            return;

        {
            static int dbgOnce = 0;
            if (dbgOnce++ < 3) DebugLog("Update() called, model=%d enabled=%d", (int)Options::Preview3D::Model, (int)Options::Preview3D::Enabled);
        }

        SetModel((Model)Options::Preview3D::Model);

        ID3D11Device* dev = ImGui_ImplDX11_GetDevice();
        ID3D11DeviceContext* ctx = ImGui_ImplDX11_GetDeviceContext();
        if (!dev || !ctx)
            return;

        if (!g_InitDone)
        {
            if (!g_Renderer.Initialize(dev, ctx, 600, 900))
                return;
            LoadDefaultTexture();
            g_InitDone = true;
        }

        EnsureModelLoaded();

        // Only push SetAutoSpin on change, otherwise it would reset the
        // manual-input spin pause (NotifyManualInput) every frame.
        static bool s_LastAuto = false;
        if (Options::Preview3D::AutoSpin != s_LastAuto)
        {
            g_Renderer.SetAutoSpin(Options::Preview3D::AutoSpin);
            s_LastAuto = Options::Preview3D::AutoSpin;
        }

        float now = (float)ImGui::GetTime();
        float dt = (g_LastTime > 0.f) ? (now - g_LastTime) : 0.016f;
        if (dt < 0.f) dt = 0.f;
        if (dt > 0.1f) dt = 0.1f;
        g_LastTime = now;

        if (g_ModelLoaded)
            g_Renderer.Update(dt);
    }

    void DrawPreview(ImDrawList* dl, const ImVec2& min, const ImVec2& max)
    {
        if (!Options::Preview3D::Enabled)
            return;
        // Sync g_Current from the authoritative selection BEFORE the None check.
        // Update()/SetModel() are only reached through this function, so without
        // this, switching away from the default None model leaves g_Current stuck
        // at None and the 3D model never loads or renders.
        SetModel((Model)Options::Preview3D::Model);
        // None = show 2D Roblox avatar; don't draw 3D model
        if (g_Current == Model::None)
            return;
        Update();
        if (!g_Renderer.IsReady())
            return;
        dl->AddImage((ImTextureID)g_Renderer.GetTextureID(), min, max);
        // Skeleton overlay for all 3D models. Humanoid models (Roblox/Mario)
        // use the R6 limb heuristic; non-humanoid models (TungTung) fall back
        // to a generic bounding-box-proportional silhouette.
        // Gated by the Visuals-tab ESP Skeleton so it matches the in-world
        // skeleton's colour and visibility.
        if (Options::ESP::Skeleton)
        {
            std::vector<float> segs;
            if (g_Renderer.GetProjectedR6Skeleton(segs))
            {
                const float w = max.x - min.x;
                const float h = max.y - min.y;
                const ImU32 skelCol = IM_COL32(
                    static_cast<int>(Options::ESP::SkeletonColor[0] * 255.f),
                    static_cast<int>(Options::ESP::SkeletonColor[1] * 255.f),
                    static_cast<int>(Options::ESP::SkeletonColor[2] * 255.f),
                    255);
                const float thick = (std::clamp)(Options::ESP::SkeletonThickness, 1.0f, 10.0f);
                for (size_t i = 0; i + 3 < segs.size(); i += 4)
                {
                    dl->AddLine(ImVec2(min.x + segs[i] * w, min.y + segs[i + 1] * h),
                                ImVec2(min.x + segs[i + 2] * w, min.y + segs[i + 3] * h), skelCol, thick);
                }
            }
        }
    }

    void AddRotation(float dyaw, float dpitch)
    {
        g_Renderer.AddRotationDelta(dyaw, dpitch);
    }

    bool GetProjectedBounds(float& u0, float& v0, float& u1, float& v1)
    {
        return g_Renderer.GetProjectedUVBounds(u0, v0, u1, v1);
    }

    bool GetProjectedSkeleton(std::vector<float>& uv_segs)
    {
        return g_Renderer.GetProjectedR6Skeleton(uv_segs);
    }

    bool GetProjectedHead(float& u, float& v)
    {
        return g_Renderer.GetProjectedHead(u, v);
    }

    bool GetProjectedModelBox(std::vector<float>& uv_corners)
    {
        return g_Renderer.GetProjectedModelBox(uv_corners);
    }

    void AddZoom(float delta)
    {
        g_Renderer.AddZoom(delta);
    }

    void NotifyManual()
    {
        g_Renderer.NotifyManualInput();
    }

    void ResetView()
    {
        g_Renderer.ResetView();
    }

    void Shutdown()
    {
        if (g_InitDone)
            g_Renderer.Shutdown();
        g_InitDone = false;
        g_ModelLoaded = false;
        g_Current = Model::None;
    }
}

// Compile the donor PreviewRenderer / ModelLoader method bodies. ModelLoader.inl
// self-wraps its contents in namespace Cheat::Core, so it is included at global
// scope. The remaining .inl files define Cheat::Core::PreviewRenderer members and
// call Cheat::Core::LoadOBJ (visible via unqualified lookup from inside the
// namespace), so they must be expanded inside namespace Cheat::Core.
// PreviewShaders.inl (which defines s_VS / s_PS / CompileShader) must come before
// PreviewInit.inl.
#include "ModelLoader.inl"
namespace Cheat::Core {
#include "PreviewShaders.inl"
#include "PreviewInit.inl"
#include "PreviewModel.inl"
#include "PreviewUpdate.inl"
#include "PreviewProject.inl"
}
