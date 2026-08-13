// MapParser — shape-classifying ray-trace system for wallchecks.
//
// Why this exists: the existing Visibility::IsPointVisible is a coarse bounding-
// sphere check that only reads distance+radius from a small per-frame scan. The
// triggerbot / aimbot / visible-bones ESP features need accurate ray-vs-shape
// intersections against the entire static map (Parts, MeshParts, WedgeParts,
// CornerWedgeParts, TrussParts, plus SpecialMesh-based shapes).
//
// Ported from the user's reference c_map_parser (Roblox-style). We use the
// Seraph Vectors::Vector3 / RobloxInstance / sCFrame / Memory abstraction.
//
// Threading: a background std::thread (WorkerLoop) runs scan_internal() into a
// local vector, then moves it under unique_lock onto m_cached_parts. All read
// sites (IsVisible etc.) take std::shared_lock — concurrent queries don't
// block each other.
//
// Fail-safe: if the cache is empty (no scan has completed yet — cold join,
// scan in progress, or scan failed) IsVisible returns true so the player isn't
// starved of kills during the first ~5 s of attach.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../rbx/offsets.h"
#include "../rbx/math/math.h"

namespace MapParser
{
    enum class Shape : uint8_t { Box, Sphere, Cylinder, Wedge, CornerWedge, Truss, Mesh };

    struct ParsedPart
    {
        Vectors::Vector3 position{};
        Vectors::Vector3 size{};
        // Rotation rows (object-to-world). e0 = column 0 / x-axis basis,
        // e1 = column 1 / y-axis basis, e2 = column 2 / z-axis basis.
        // We multiply world vectors by these as rows to transform to local
        // object space, mirroring the user's source convention.
        Vectors::Vector3 e0{ 1.f, 0.f, 0.f };
        Vectors::Vector3 e1{ 0.f, 1.f, 0.f };
        Vectors::Vector3 e2{ 0.f, 0.f, 1.f };
        Shape shape = Shape::Box;
        float radius = 0.f;
        float radius_sq = 0.f;
    };

    inline std::vector<ParsedPart> m_cached_parts;
    inline std::shared_mutex m_mutex;

    // Background-scan state. Scan cadence is conservative (~5s) because full
    // walk+classify is expensive on workspaces with thousands of parts.
    inline std::atomic<uint64_t> m_last_scan_ms{ 0 };
    inline std::atomic<int> m_scan_interval_ms{ 5000 };
    inline std::atomic<bool> m_force_scan{ false };

    // ---------- helpers ----------

    // Multiplies the columns of a 3x3 [e0;e1;e2] by the components of v to
    // produce a transformed vector. Equivalent to dot(row_i, v) for each row i.
    inline Vectors::Vector3 TransformLocal(
        const Vectors::Vector3& v,
        const ParsedPart& p)
    {
        return Vectors::Vector3{
            v.x * p.e0.x + v.y * p.e1.x + v.z * p.e2.x,
            v.x * p.e0.y + v.y * p.e1.y + v.z * p.e2.y,
            v.x * p.e0.z + v.y * p.e1.z + v.z * p.e2.z,
        };
    }

    inline bool ContainsIgnore(const std::string& text, const std::string& token)
    {
        return text.find(token) != std::string::npos;
    }

    // ---------- classification ----------

    inline Shape ClassifyPart(
        const RobloxInstance& part,
        const Vectors::Vector3& size,
        const std::string& lowerName)
    {
        const std::string cls = part.Class();

        if (cls == "WedgePart")     return Shape::Wedge;
        if (cls == "CornerWedgePart") return Shape::CornerWedge;
        if (cls == "TrussPart")     return Shape::Truss;

        // SpecialMesh.MeshType hint. Roblox SpecialMesh@0x1b0 holds MeshType
        // (0=Head, 1=Torso, 2=Wedge, 3=Sphere, 4=Cylinder, 5=FileMesh, 6=Brick).
        try
        {
            RobloxInstance mesh = part.FindFirstChildWhichIsA("SpecialMesh");
            if (mesh.address)
            {
                int meshType = Memory->read<int>(mesh.address + 0x1b0);
                if (meshType == 3) return Shape::Sphere;
                if (meshType == 4) return Shape::Cylinder;
                if (meshType == 2) return Shape::Wedge;
            }
        }
        catch (...) { /* fall through to dimension/name heuristics */ }

        const float diff_xy = std::fabs(size.x - size.y);
        const float diff_xz = std::fabs(size.x - size.z);
        const float diff_yz = std::fabs(size.y - size.z);
        const float tolerance = 0.02f;
        const bool isPerfectCube = (diff_xy < tolerance && diff_xz < tolerance);
        const bool isCylX = (diff_yz < tolerance);
        const bool isCylY = (diff_xz < tolerance);
        const bool isCylZ = (diff_xy < tolerance);
        const bool isAnyCyl = isCylX || isCylY || isCylZ;

        const bool hasRoundHint = (
            ContainsIgnore(lowerName, "sphere") ||
            ContainsIgnore(lowerName, "ball") ||
            ContainsIgnore(lowerName, "orb") ||
            ContainsIgnore(lowerName, "head") ||
            ContainsIgnore(lowerName, "round") ||
            ContainsIgnore(lowerName, "bullet") ||
            ContainsIgnore(lowerName, "circle") ||
            ContainsIgnore(lowerName, "pill") ||
            ContainsIgnore(lowerName, "cap") ||
            ContainsIgnore(lowerName, "dome")
        );

        const bool hasCylHint = (
            ContainsIgnore(lowerName, "cylinder") ||
            ContainsIgnore(lowerName, "pipe") ||
            ContainsIgnore(lowerName, "tube") ||
            ContainsIgnore(lowerName, "pole") ||
            ContainsIgnore(lowerName, "column") ||
            ContainsIgnore(lowerName, "wire") ||
            ContainsIgnore(lowerName, "cable") ||
            ContainsIgnore(lowerName, "barrel") ||
            ContainsIgnore(lowerName, "disc") ||
            ContainsIgnore(lowerName, "coin")
        );

        if (hasRoundHint && isPerfectCube) return Shape::Sphere;
        if (hasCylHint && isAnyCyl) return Shape::Cylinder;
        if (cls == "MeshPart")
        {
            if (isPerfectCube) return Shape::Sphere;
            if (isAnyCyl) return Shape::Cylinder;
            return Shape::Mesh;
        }
        // Native Part family.
        if (isPerfectCube) return (size.x < 10.f) ? Shape::Sphere : Shape::Box;
        if (isAnyCyl) return Shape::Cylinder;
        return Shape::Box;
    }

    // ---------- scan ----------

    inline void ScanInternal(
        const RobloxInstance& root,
        std::vector<ParsedPart>& outParts,
        std::vector<RobloxInstance>& workStack)
    {
        workStack.clear();
        workStack.reserve(4096);
        workStack.push_back(root);

        while (!workStack.empty())
        {
            RobloxInstance current = workStack.back();
            workStack.pop_back();
            if (!current.address) continue;

            std::string cls;
            try { cls = current.Class(); } catch (...) { continue; }

            // Skip service containers and non-map services.
            if (cls == "Players" || cls == "Debris" || cls == "Lighting" ||
                cls == "Teams" || cls == "SoundService" || cls == "StarterGui" ||
                cls == "StarterPack" || cls == "StarterPlayer" ||
                cls == "ReplicatedStorage" || cls == "ReplicatedFirst" ||
                cls == "ServerScriptService" || cls == "ServerStorage" ||
                cls == "Chat" || cls == "TestService" || cls == "PolicyService" ||
                cls == "LocalizationService" || cls == "HttpService")
            {
                continue;
            }

            // Skip World / Terrain (Terrain uses a voxel grid, not part primitives)
            if (cls == "Terrain" || cls == "WorldRoot") continue;

            // Skip player Models entirely — their body parts are not static map
            // geometry, and we don't want to ray-cast-against our own limbs.
            if (cls == "Model")
            {
                try
                {
                    if (current.FindFirstChildWhichIsA("Humanoid").address != 0)
                        continue;
                }
                catch (...) { /* fall through if class-read fails */ }
            }

            if (cls == "Part" || cls == "MeshPart" || cls == "CornerWedgePart" ||
                cls == "WedgePart" || cls == "TrussPart")
            {
                try
                {
                    const uintptr_t primitive =
                        Memory->read<uintptr_t>(current.address + Offsets::BasePart::Primitive);
                    if (!primitive) { /* descend anyway */ }
                    else
                    {
                        const uint8_t flags =
                            Memory->read<uint8_t>(primitive + Offsets::Primitive::Flags);
                        if ((flags & Offsets::PrimitiveFlags::CanCollide) != 0)
                        {
                            const Vectors::Vector3 pos =
                                Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Position);
                            const Vectors::Vector3 sz =
                                Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Size);

                            // Skip fully-transparent ghost parts — they don't occlude,
                            // mirroring the legacy ESP occlusion transparency cutoff.
                            const float transparency =
                                Memory->read<float>(current.address + Offsets::BasePart::Transparency);

                            // Skip tiny ornaments (trims, studs) to keep the cache bounded.
                            const float maxSide =
                                std::fmax(sz.x, std::fmax(sz.y, sz.z));
                            if (maxSide >= 0.5f && transparency < 0.92f)
                            {
                                const sCFrame rc =
                                    Memory->read<sCFrame>(primitive + Offsets::Primitive::Rotation);

                                std::string lowerName;
                                try { lowerName = current.Name(); }
                                catch (...) { lowerName.clear(); }
                                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                               [](unsigned char c) { return std::tolower(c); });

                                ParsedPart pp{};
                                pp.position = pos;
                                pp.size = sz;
                                pp.e0 = { rc.r00, rc.r10, rc.r20 };  // right (x-axis) basis
                                pp.e1 = { rc.r01, rc.r11, rc.r21 };  // up (y-axis) basis
                                pp.e2 = { rc.r02, rc.r12, rc.r22 };  // back (z-axis) basis
                                pp.shape = ClassifyPart(current, sz, lowerName);

                                // Bounding sphere radius (max-side * sqrt(3)/2) — broad-phase
                                // test is tight enough to be safe for box-shaped parts too.
                                const float r = maxSide * 0.8660254f;
                                pp.radius = r;
                                pp.radius_sq = r * r;

                                outParts.push_back(pp);
                            }
                        }
                    }
                }
                catch (...) { /* skip on read failure */ }
            }

            try
            {
                auto children = current.GetChildren();
                for (const auto& child : children)
                    workStack.push_back(child);
            }
            catch (...) { /* bail on this branch */ }
        }
    }

    inline void ScanOnce()
    {
        if (!Globals::Roblox::Workspace.address) return;

        std::vector<ParsedPart> temp;
        temp.reserve(65536);
        std::vector<RobloxInstance> stack;
        ScanInternal(Globals::Roblox::Workspace, temp, stack);

        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cached_parts = std::move(temp);
        }
        m_last_scan_ms.store(GetTickCount64());
    }

    inline void ForceScan()
    {
        m_force_scan.store(true);
    }

    inline void ClearCache()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cached_parts.clear();
        m_last_scan_ms.store(0);
    }

    // True once at least one background scan has completed and published parts.
    // Callers use this to fall back to the legacy occluder scan during warmup.
    inline bool IsCacheReady()
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return !m_cached_parts.empty();
    }

    // Background worker. Detached once in main.cpp after VisualEngine resolves.
    // Sleeps in 500ms slices twice between full scans so a force_scan request
    // is acknowledged within ~1s rather than having to wait the full interval.
    inline void WorkerLoop()
    {
        while (Globals::running)
        {
            if (m_force_scan.exchange(false))
            {
                ScanOnce();
            }
            else
            {
                const uint64_t now = GetTickCount64();
                if (now - m_last_scan_ms.load() >=
                    static_cast<uint64_t>(m_scan_interval_ms.load()))
                {
                    ScanOnce();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    // ---------- ray-vs-shape ----------

    inline bool BroadPhaseReject(
        const ParsedPart& part,
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        float maxDist)
    {
        const Vectors::Vector3 toCenter = {
            part.position.x - origin.x,
            part.position.y - origin.y,
            part.position.z - origin.z
        };
        const float projection =
            toCenter.x * dir.x + toCenter.y * dir.y + toCenter.z * dir.z;
        if (projection < -1.f || projection > maxDist + 1.f) return true;

        const float dsq =
            toCenter.x * toCenter.x +
            toCenter.y * toCenter.y +
            toCenter.z * toCenter.z - projection * projection;
        return dsq > part.radius_sq;
    }

    inline bool RayVsOBB(
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        const ParsedPart& part,
        float maxDist)
    {
        const Vectors::Vector3 lo = {
            origin.x - part.position.x,
            origin.y - part.position.y,
            origin.z - part.position.z
        };
        const Vectors::Vector3 ro = TransformLocal(lo, part);
        const Vectors::Vector3 rd = TransformLocal(dir, part);

        const Vectors::Vector3 hs = {
            part.size.x * 0.5f, part.size.y * 0.5f, part.size.z * 0.5f
        };

        // Avoid div-by-zero by collapsing parallel axes to a "no hit" entry.
        const float invX = (std::fabs(rd.x) > 1e-7f) ? (1.f / rd.x) : 1e30f;
        const float invY = (std::fabs(rd.y) > 1e-7f) ? (1.f / rd.y) : 1e30f;
        const float invZ = (std::fabs(rd.z) > 1e-7f) ? (1.f / rd.z) : 1e30f;

        const float t1 = (-hs.x - ro.x) * invX;
        const float t2 = ( hs.x - ro.x) * invX;
        const float t3 = (-hs.y - ro.y) * invY;
        const float t4 = ( hs.y - ro.y) * invY;
        const float t5 = (-hs.z - ro.z) * invZ;
        const float t6 = ( hs.z - ro.z) * invZ;

        const float tmin = std::fmax(
            std::fmin(t1, t2),
            std::fmax(std::fmin(t3, t4), std::fmin(t5, t6)));
        const float tmax = std::fmin(
            std::fmax(t1, t2),
            std::fmin(std::fmax(t3, t4), std::fmax(t5, t6)));

        if (tmax < 0.f || tmin > tmax) return false;
        return tmin <= maxDist;
    }

    inline bool RayVsSphere(
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        const ParsedPart& part,
        float maxDist)
    {
        const float r = part.size.x * 0.5f;
        const float r_sq = r * r;
        const Vectors::Vector3 oc = {
            origin.x - part.position.x,
            origin.y - part.position.y,
            origin.z - part.position.z
        };
        const float b = oc.x * dir.x + oc.y * dir.y + oc.z * dir.z;
        const float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - r_sq;
        const float disc = b * b - c;
        if (disc < 0.f) return false;

        const float sd = std::sqrt(disc);
        float t = -b - sd;
        if (t < 0.f) t = -b + sd;
        return t >= 0.f && t <= maxDist;
    }

    inline bool RayVsCylinder(
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        const ParsedPart& part,
        float maxDist)
    {
        const Vectors::Vector3 lo = {
            origin.x - part.position.x,
            origin.y - part.position.y,
            origin.z - part.position.z
        };
        const Vectors::Vector3 ro = TransformLocal(lo, part);
        const Vectors::Vector3 rd = TransformLocal(dir, part);

        float r, hHalf;
        int axis = 1; // 0=X, 1=Y, 2=Z
        const float tol = 0.02f;

        if (std::fabs(part.size.x - part.size.z) < tol)
        {
            r = part.size.x * 0.5f; hHalf = part.size.y * 0.5f; axis = 1;
        }
        else if (std::fabs(part.size.x - part.size.y) < tol)
        {
            r = part.size.x * 0.5f; hHalf = part.size.z * 0.5f; axis = 2;
        }
        else
        {
            r = part.size.y * 0.5f; hHalf = part.size.x * 0.5f; axis = 0;
        }
        const float r_sq = r * r;

        float a, b_coef, cval, ro_axis, rd_axis;
        if (axis == 1)
        {
            a = rd.x * rd.x + rd.z * rd.z;
            b_coef = 2.f * (ro.x * rd.x + ro.z * rd.z);
            cval = ro.x * ro.x + ro.z * ro.z - r_sq;
            ro_axis = ro.y; rd_axis = rd.y;
        }
        else if (axis == 2)
        {
            a = rd.x * rd.x + rd.y * rd.y;
            b_coef = 2.f * (ro.x * rd.x + ro.y * rd.y);
            cval = ro.x * ro.x + ro.y * ro.y - r_sq;
            ro_axis = ro.z; rd_axis = rd.z;
        }
        else
        {
            a = rd.y * rd.y + rd.z * rd.z;
            b_coef = 2.f * (ro.y * rd.y + ro.z * rd.z);
            cval = ro.y * ro.y + ro.z * ro.z - r_sq;
            ro_axis = ro.x; rd_axis = rd.x;
        }

        // Infinite cylinder test.
        if (std::fabs(a) > 1e-4f)
        {
            const float disc = b_coef * b_coef - 4.f * a * cval;
            if (disc >= 0.f)
            {
                const float sd = std::sqrt(disc);
                const float t0 = (-b_coef - sd) / (2.f * a);
                const float t1 = (-b_coef + sd) / (2.f * a);
                for (float t : { t0, t1 })
                {
                    if (t >= 0.f && t <= maxDist)
                    {
                        const float p = ro_axis + t * rd_axis;
                        if (std::fabs(p) <= hHalf) return true;
                    }
                }
            }
        }
        // Caps.
        if (std::fabs(rd_axis) > 1e-4f)
        {
            const float tTop = (hHalf - ro_axis) / rd_axis;
            if (tTop >= 0.f && tTop <= maxDist)
            {
                const Vectors::Vector3 p = {
                    ro.x + rd.x * tTop,
                    ro.y + rd.y * tTop,
                    ro.z + rd.z * tTop
                };
                const float dsq =
                    (axis == 1) ? (p.x * p.x + p.z * p.z) :
                    (axis == 2) ? (p.x * p.x + p.y * p.y) :
                                  (p.y * p.y + p.z * p.z);
                if (dsq <= r_sq) return true;
            }

            const float tBot = (-hHalf - ro_axis) / rd_axis;
            if (tBot >= 0.f && tBot <= maxDist)
            {
                const Vectors::Vector3 p = {
                    ro.x + rd.x * tBot,
                    ro.y + rd.y * tBot,
                    ro.z + rd.z * tBot
                };
                const float dsq =
                    (axis == 1) ? (p.x * p.x + p.z * p.z) :
                    (axis == 2) ? (p.x * p.x + p.y * p.y) :
                                  (p.y * p.y + p.z * p.z);
                if (dsq <= r_sq) return true;
            }
        }
        return false;
    }

    inline bool RayVsWedge(
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        const ParsedPart& part,
        float maxDist)
    {
        const Vectors::Vector3 lo = {
            origin.x - part.position.x,
            origin.y - part.position.y,
            origin.z - part.position.z
        };
        const Vectors::Vector3 ro = TransformLocal(lo, part);
        const Vectors::Vector3 rd = TransformLocal(dir, part);

        const Vectors::Vector3 hs = {
            part.size.x * 0.5f, part.size.y * 0.5f, part.size.z * 0.5f
        };
        float tmin = -1e30f, tmax = 1e30f;

        auto clip = [&](float dist, float d)
        {
            if (std::fabs(d) < 1e-6f) { if (dist > 0.f) tmax = -1.f; return; }
            const float t = -dist / d;
            if (d > 0.f) tmin = std::fmax(tmin, t);
            else        tmax = std::fmin(tmax, t);
        };

        clip( ro.x - hs.x,  rd.x);    // x <= +hs.x
        clip(-ro.x - hs.x, -rd.x);    // x >= -hs.x
        clip(-ro.y - hs.y, -rd.y);    // y >= -hs.y
        clip( ro.z - hs.z,  rd.z);    // z <= +hs.z
        // Sloping plane: y * size.z - z * size.y = 0  ⇒  normal = (0, sz, -sy)
        clip(ro.y * part.size.z + ro.z * (-part.size.y),
             rd.y * part.size.z + rd.z * (-part.size.y));

        if (tmax < 0.f || tmin > tmax) return false;
        return tmin <= maxDist;
    }

    inline bool RayVsCornerWedge(
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        const ParsedPart& part,
        float maxDist)
    {
        const Vectors::Vector3 lo = {
            origin.x - part.position.x,
            origin.y - part.position.y,
            origin.z - part.position.z
        };
        const Vectors::Vector3 ro = TransformLocal(lo, part);
        const Vectors::Vector3 rd = TransformLocal(dir, part);

        const Vectors::Vector3 hs = {
            part.size.x * 0.5f, part.size.y * 0.5f, part.size.z * 0.5f
        };
        float tmin = -1e30f, tmax = 1e30f;

        auto clip = [&](const Vectors::Vector3& n, float d_origin)
        {
            const float d = rd.x * n.x + rd.y * n.y + rd.z * n.z;
            const float o = ro.x * n.x + ro.y * n.y + ro.z * n.z - d_origin;
            if (std::fabs(d) < 1e-6f) { if (o > 0.f) tmax = -1.f; return; }
            const float t = -o / d;
            if (d > 0.f) tmin = std::fmax(tmin, t);
            else        tmax = std::fmin(tmax, t);
        };

        // Bottom  (y = -hs.y,  n=(0,-1,0))
        clip({ 0.f, -1.f, 0.f }, -hs.y);
        // Back    (z = +hs.z,  n=(0, 0, 1))
        clip({ 0.f,  0.f, 1.f },  hs.z);
        // Left    (x = -hs.x,  n=(-1,0,0))
        clip({ -1.f, 0.f, 0.f }, -hs.x);
        // Slope 1 (front-bottom edge → top-back-left corner): n = (0, -sz, +sy).
        {
            Vectors::Vector3 n = { 0.f, -part.size.z, part.size.y };
            const float mag = n.Magnitude();
            if (mag > 0.f)
            {
                n.x /= mag; n.y /= mag; n.z /= mag;
                const float d_origin = -hs.x * n.x + -hs.y * n.y + -hs.z * n.z;
                clip(n, d_origin);
            }
        }
        // Slope 2 (right-bottom edge → top-back-left corner): n = (-sy, -sx, 0).
        {
            Vectors::Vector3 n = { -part.size.y, -part.size.x, 0.f };
            const float mag = n.Magnitude();
            if (mag > 0.f)
            {
                n.x /= mag; n.y /= mag; n.z /= mag;
                const float d_origin =  hs.x * n.x + -hs.y * n.y +  hs.z * n.z;
                clip(n, d_origin);
            }
        }

        if (tmax < 0.f || tmin > tmax) return false;
        return tmin <= maxDist;
    }

    inline bool IntersectsPart(
        const ParsedPart& part,
        const Vectors::Vector3& origin,
        const Vectors::Vector3& dir,
        float maxDist)
    {
        if (BroadPhaseReject(part, origin, dir, maxDist)) return false;

        switch (part.shape)
        {
        case Shape::Sphere:      return RayVsSphere(origin, dir, part, maxDist);
        case Shape::Cylinder:    return RayVsCylinder(origin, dir, part, maxDist);
        case Shape::Wedge:       return RayVsWedge(origin, dir, part, maxDist);
        case Shape::CornerWedge: return RayVsCornerWedge(origin, dir, part, maxDist);
        case Shape::Box:
        case Shape::Truss:
        case Shape::Mesh:
        default:                 return RayVsOBB(origin, dir, part, maxDist);
        }
    }

    // Returns true if no static-map part blocks the (start → end) segment.
    // ignoreModelAddress is reserved for future per-character exclusion; the
    // current scan already skips entire player Models so we don't bounce off
    // friendly limbs.
    inline bool IsVisible(
        const Vectors::Vector3& start,
        const Vectors::Vector3& end,
        uintptr_t /*ignoreModelAddress*/ = 0)
    {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float dz = end.z - start.z;
        const float maxDist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (maxDist < 0.001f) return true;

        const Vectors::Vector3 dir = {
            dx / maxDist, dy / maxDist, dz / maxDist
        };

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_cached_parts.empty()) return true; // Fail-safe: cheat isn't useless while scan warms up.

        for (const auto& part : m_cached_parts)
        {
            if (IntersectsPart(part, start, dir, maxDist))
                return false;
        }
        return true;
    }
}
