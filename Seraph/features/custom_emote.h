#pragma once

// CustomEmote — plays a real Roblox animation (.rbxm / .rbxmx KeyframeSequence)
// on the overlay model.
//
// Drop .rbxm emote files into an "Emotes" folder next to the executable (or the
// repo's Emotes\ folder); they are listed in the Model Changer menu and, when
// selected, sampled each frame and applied to the model's six rig joints with
// the renderer's local-rotation path (SetJointLocalQuat), so limbs stay attached
// as the parent rotates.

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <Windows.h>

#include "rbxm_parser.h"
#include "preview3d/PreviewRenderer.h"

namespace CustomEmote
{
    struct FileEntry
    {
        std::string display; // path relative to the Emotes root, no extension
        std::string full;    // absolute path
        bool        rbxmx = false;
    };

    struct State
    {
        RBXM::Parser parser;
        std::vector<FileEntry> files;
        bool  scanned = false;
        bool  loaded = false;
        bool  playing = false;
        bool  loop = true;
        float speed = 1.0f;
        float time = 0.0f;
        int   index = -1;
        std::vector<std::pair<std::string, RBXM::Quat>> sample;
    };

    inline State& Get()
    {
        static State s;
        return s;
    }

    inline std::string ExeDir()
    {
        char buf[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::string s(buf);
        const size_t pos = s.find_last_of('\\');
        return (pos == std::string::npos) ? std::string() : s.substr(0, pos + 1);
    }

    // Resolve the Emotes folder the same way the Model Changer finds Models\.
    inline std::string EmotesRoot()
    {
        const std::string dir = ExeDir();
        const char* candidates[] = { "", "..\\..\\Seraph\\", "..\\..\\..\\Seraph\\", "..\\..\\..\\..\\Seraph\\" };
        for (const char* c : candidates)
        {
            const std::string p = dir + c + "Emotes\\";
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                return p;
        }
        return dir + "Emotes\\";
    }

    inline void ScanDir(const std::string& root, const std::string& rel,
                        std::vector<FileEntry>& out, int depth)
    {
        if (depth > 4) return;
        WIN32_FIND_DATAA fd;
        const std::string pattern = root + rel + "*";
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        do
        {
            if (fd.cFileName[0] == '.') continue;
            const std::string childRel = rel + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                ScanDir(root, childRel + "\\", out, depth + 1);
                continue;
            }

            std::string name = fd.cFileName;
            std::string lower = name;
            for (char& c : lower) c = (char)tolower((unsigned char)c);

            bool rbxm = false, rbxmx = false;
            if (lower.size() > 5 && lower.substr(lower.size() - 5) == ".rbxm") rbxm = true;
            if (lower.size() > 6 && lower.substr(lower.size() - 6) == ".rbxmx") rbxmx = true;
            if (!rbxm && !rbxmx) continue;

            FileEntry e;
            const size_t dot = childRel.find_last_of('.');
            e.display = (dot == std::string::npos) ? childRel : childRel.substr(0, dot);
            e.full = root + childRel;
            e.rbxmx = rbxmx;
            out.push_back(std::move(e));
        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }

    inline const std::vector<FileEntry>& Files()
    {
        State& s = Get();
        if (!s.scanned)
        {
            s.scanned = true;
            ScanDir(EmotesRoot(), "", s.files, 0);
            std::sort(s.files.begin(), s.files.end(),
                      [](const FileEntry& a, const FileEntry& b) { return a.display < b.display; });
        }
        return s.files;
    }

    inline void Rescan()
    {
        State& s = Get();
        s.files.clear();
        s.scanned = false;
        Files();
    }

    // Load (or reload) the selected file into the parser.
    inline bool Load(int index)
    {
        State& s = Get();
        const auto& list = Files();
        if (index < 0 || index >= (int)list.size())
        {
            s.loaded = false;
            s.index = -1;
            return false;
        }
        s.index = index;
        s.time = 0.0f;
        s.loaded = s.parser.LoadFromFile(list[index].full);
        s.playing = s.loaded;
        return s.loaded;
    }

    inline void Reset()
    {
        State& s = Get();
        s.loaded = false;
        s.playing = false;
        s.index = -1;
        s.time = 0.0f;
    }

    // Map a Roblox pose/joint name to one of the model's six rig joints.
    inline const char* MapJoint(const std::string& poseName)
    {
        std::string s = poseName;
        for (char& c : s) c = (char)tolower((unsigned char)c);

        auto has = [&](const char* needle) { return s.find(needle) != std::string::npos; };

        if (has("head") || s == "neck") return "Head";
        if (has("torso") || has("root") || has("waist")) return "Torso";

        const bool left  = has("left");
        const bool right = has("right");
        if (has("arm") || has("shoulder") || has("hand"))
            return left ? "Left Arm" : (right ? "Right Arm" : "");
        if (has("leg") || has("hip") || has("foot") || has("thigh") || has("knee"))
            return left ? "Left Leg" : (right ? "Right Leg" : "");
        return "";
    }

    // Drive the renderer for one frame. Returns false if nothing is playing.
    inline bool Update(Cheat::Core::PreviewRenderer& r, float dt)
    {
        State& s = Get();
        if (!s.loaded) return false;
        const auto& seqs = s.parser.Sequences();
        if (seqs.empty()) return false;
        const int si = (s.index >= 0 && s.index < (int)seqs.size()) ? s.index : 0;
        const RBXM::Sequence& seq = seqs[(size_t)si];
        if (seq.tracks.empty()) return false;

        const float len = (seq.length > 0.01f) ? seq.length : 1.0f;
        if (s.playing)
        {
            s.time += dt * s.speed;
            if (s.time >= len)
            {
                if (s.loop) s.time = fmodf(s.time, len);
                else        s.time = len;
            }
        }

        r.ClearPose();
        seq.Sample(s.time, s.sample);
        for (const auto& p : s.sample)
        {
            const char* joint = MapJoint(p.first);
            if (joint && *joint)
                r.SetJointLocalQuat(joint, p.second.x, p.second.y, p.second.z, p.second.w);
        }
        return true;
    }

} // namespace CustomEmote
