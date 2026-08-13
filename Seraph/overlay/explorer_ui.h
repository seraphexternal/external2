// ============================================================================
// ExplorerUI -- turn the read-only Roblox Explorer into a real-time
// property editor. Tree on the left (search, click to select), properties
// on the right (read/write through the Memory-> interface).
// ----------------------------------------------------------------------------
// Performance:
//  * The tree is a CachedInstance snapshot that refreshes every ~1.5 s AND
//    only when no search keystroke has arrived for ~0.35 s. The render loop
//    reads straight out of the snapshot -- zero Memory-> calls per frame --
//    so opening this window no longer stalls the overlay on big Instance
//    hierarchies.
//  * The search box does NOT trigger a re-walk. Instead, the cached
//    lowercase name is checked at render time, so typing in the search box
//    filters the tree instantly with no full-tree refresh.
//  * Right-pane string / scalar editors lazy-load their value on selection
//    change (so adding WalkSpeed once is one read, not one per frame).
//  * Name/Class for the selected instance are also cached; only re-read
//    when the selection address actually changes.
// ----------------------------------------------------------------------------
// Concurrency / safety notes:
//  * Memory->writeString cannot reallocate Roblox ManagedStrings in the
//    remote process. SafeWritePropertyString silently no-ops when the new
//    value would exceed the existing capacity, so the user-visible error
//    is intentionally just "no commit". Keep new texture / sound IDs at
//    or below the original length to avoid silent no-ops.
//  * Roblox's VM never fires PropertyChanged signals on external memory
//    writes; LocalScripts that listen for Name/Texture changes will
//    silently miss updates. Scripts that cached instances by old Name
//    still see the old name until they re-query the Instance tree.
// ============================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "../rbx/SDK/sdk.h"
#include "../rbx/offsets.h"
#include "../Memory/MemoryManager.h"

namespace ExplorerUI
{
    // =========================================================================
    // STATE
    // =========================================================================
    inline uintptr_t SelectedAddress    = 0;
    inline char      SearchBuf[128]     = "";
    inline bool      ForceRefreshNext   = true;

    // Snapshot of the Roblox Instance tree. Rebuilt every ~1.5 s (or on
    // explicit Refresh button). Render reads straight out of this vector
    // with no Memory-> calls, which is what was killing FPS in the older
    // implementation.
    struct CachedInstance
    {
        uintptr_t address = 0;
        std::string name;          // resolved at refresh-time, kept across frames
        std::string className;     // resolved at refresh-time
        std::string nameLower;     // pre-lowercased so search-filter is just a find()
        int  depth        = 0;
        int  parentIdx    = -1;
        std::vector<int> childIndices;       // indices of immediate children in TreeCache
        bool visibleInFilter = true;         // computed lazily by RebuildVisibility
        bool selfMatches     = false;        // name contains CurrentFilterLower
    };

    inline std::vector<CachedInstance> TreeCache;
    inline std::string LastSearchFilter = "@@__sentinel__@@";
    inline double      LastTreeRefreshTime   = -1.0;
    inline constexpr double kTreeRefreshSeconds = 1.5;     // tree-walk cadence
    inline constexpr int    kMaxTreeDepth       = 24;
    inline constexpr size_t kMaxCachedNodes     = 20000;

    // Current search filter (lowercased). Updated every frame from SearchBuf
    // so the search-as-you-type path doesn't need to touch Memory.
    inline std::string CurrentFilterLower;

    // Right-pane transient hint shown for a few seconds when the previous
    // selection went stale (instance despawned), so the panel does not
    // silently reset to "Select an instance".
    inline bool RightPaneHintActive = false;
    inline std::string RightPaneHintText;
    inline double RightPaneHintExpiry = -1.0;
    inline constexpr double kRightPaneHintSeconds = 2.0;

    // Per-string-property editor state. Each renderer declares one of
    // these, hands it to RenderStringProperty(...) and reuses it across
    // frames; the editor detects a selection change and refills the
    // buffer from the live memory read so the user can't keep typing
    // into stale text.
    struct StringEditor
    {
        char     buf[256];
        uintptr_t lastInst;
        bool     lastCommitOk;
        bool     justCommitted;
        StringEditor()
            : buf{0}
            , lastInst(0xFFFFFFFFFFFFFFFFull)
            , lastCommitOk(true)
            , justCommitted(false)
        {}
    };
    static constexpr uintptr_t kNeverSeen = 0xFFFFFFFFFFFFFFFFull;

    inline std::string ToLowerCopy(const std::string& s)
    {
        std::string out;
        out.resize(s.size());
        std::transform(s.begin(), s.end(), out.begin(), [](unsigned char c){ return (char)::tolower(c); });
        return out;
    }

    // =========================================================================
    // TREE SNAPSHOT REFRESH -- iterative DFS, depth-bounded, parentIdx-wired
    // so render is depth-flat. Refresh happens out of the hot path so it
    // can cost tens of ms without the user noticing.
    //
    // CALLER CONTRACT: this function rebuilds the cache with default
    // visibleInFilter=true on every node. Any active search filter is
    // NOT preserved -- the caller must call RebuildVisibilityFromCurrentFilter()
    // after this returns if CurrentFilterLower is non-empty.
    // =========================================================================
    inline void RefreshTreeCache()
    {
        TreeCache.clear();
        TreeCache.reserve(8192);

        if (!Globals::Roblox::DataModel.address) return;

        // Iterative DFS, address-only on the stack so we never blow the
        // C++ stack on a 50k-instance tree.
        struct Frame { uintptr_t address; int parentIdx; int depth; };
        std::vector<Frame> stack;
        const auto seed = Globals::Roblox::DataModel.GetChildren();
        for (const auto& c : seed)
        {
            if (c.address) stack.push_back({c.address, -1, 0});
        }

        while (!stack.empty() && TreeCache.size() < kMaxCachedNodes)
        {
            Frame f = stack.back();
            stack.pop_back();

            CachedInstance ci;
            ci.address   = f.address;
            ci.depth     = f.depth;
            ci.parentIdx = f.parentIdx;

            RobloxInstance ri(f.address);
            std::string n = ri.Name();
            if (n.empty()) ci.name = "(unnamed)";
            else            ci.name = std::move(n);
            ci.nameLower = ToLowerCopy(ci.name);
            ci.className = ri.Class();

            int myIdx = (int)TreeCache.size();
            TreeCache.push_back(std::move(ci));

            if (f.depth >= kMaxTreeDepth) continue;

            const auto children = ri.GetChildren();
            // Push children in reverse so the FIRST sibling pops FIRST
            // and ends up displayed before later siblings.
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                if (it->address)
                    stack.push_back({it->address, myIdx, f.depth + 1});
            }
        }

        // Wire parent -> child index lists from parentIdx. One linear pass.
        for (size_t i = 0; i < TreeCache.size(); ++i)
        {
            int p = TreeCache[i].parentIdx;
            if (p >= 0 && (size_t)p < TreeCache.size())
                TreeCache[p].childIndices.push_back((int)i);
        }

        // Drop the selection if the previously-selected instance no
        // longer exists in the freshly-refreshed tree, and surface a
        // transient hint so the user knows the right pane reset.
        if (SelectedAddress != 0)
        {
            bool stillThere = false;
            for (const auto& c : TreeCache)
            {
                if (c.address == SelectedAddress) { stillThere = true; break; }
            }
            if (!stillThere)
            {
                SelectedAddress = 0;
                RightPaneHintActive    = true;
                RightPaneHintText      = "Selection went stale (instance disappeared). Pick another node.";
                RightPaneHintExpiry    = ImGui::GetTime() + kRightPaneHintSeconds;
            }
        }

    }

    // Recompute visibleInFilter / selfMatches for every cached node using
    // the current search filter. Iterative post-order over each root so
    // a node is visible iff it matches OR any descendant matches.
    inline void RebuildVisibilityFromCurrentFilter()
    {
        const std::string& filt = CurrentFilterLower;
        const bool active = !filt.empty();

        for (size_t i = 0; i < TreeCache.size(); ++i)
        {
            CachedInstance& c = TreeCache[i];
            c.selfMatches = active && (c.nameLower.find(filt) != std::string::npos);
            c.visibleInFilter = !active || c.selfMatches; // provisional; post-order upgrades parents
        }
        if (!active) return;

        for (size_t rootI = 0; rootI < TreeCache.size(); ++rootI)
        {
            if (TreeCache[rootI].parentIdx != -1) continue;
            std::vector<std::pair<int, char>> post;
            post.push_back({(int)rootI, 0});
            while (!post.empty())
            {
                int idx = post.back().first;
                char phase = post.back().second;
                post.pop_back();
                if (phase == 1)
                {
                    bool anyChild = false;
                    for (int ci : TreeCache[idx].childIndices)
                        if (TreeCache[ci].visibleInFilter) anyChild = true;
                    TreeCache[idx].visibleInFilter = TreeCache[idx].selfMatches || anyChild;
                }
                else
                {
                    post.push_back({idx, 1});           // revisit after kids
                    for (int ci : TreeCache[idx].childIndices)
                        post.push_back({ci, 0});       // kids before us
                }
            }
        }
    }

    inline void MaybeApplySearchFilter()
    {
        if (LastSearchFilter != SearchBuf)
        {
            LastSearchFilter   = SearchBuf;
            CurrentFilterLower = ToLowerCopy(SearchBuf);
            // Tree-Cache filter dependency: first paint of new tree OR new
            // filter -> recompute visibility from cached lowercased name.
            RebuildVisibilityFromCurrentFilter();
        }
    }

    inline void MaybeRefreshTree()
    {
        // Tick the right-pane hint expiry here (not only inside the
        // right-pane render) so it can expire while the user has the
        // tree pane focused. Runs even when the target driver is
        // disconnected so the hint does not stay latched forever.
        if (RightPaneHintActive && ImGui::GetTime() >= RightPaneHintExpiry)
            RightPaneHintActive = false;

        // If the target process disconnected, do not call into the
        // Memory layer (every RobloxInstance::Name/Class/GetChildren
        // would dereference a stale driver pointer). The cached tree
        // still renders correctly from local memory.
        if (Memory == nullptr) return;

        const double now = ImGui::GetTime();

        const bool firstRun      = TreeCache.empty();
        const bool timerExpired  = firstRun || ((now - LastTreeRefreshTime) > kTreeRefreshSeconds);
        const bool forcePressed  = ForceRefreshNext;        // The render-time filter in MaybeApplySearchFilter already
        // handles search-as-you-type cheaply against the cached
        // lowercase names, so a filter keystroke does NOT trigger a
        // tree walk. Only the periodic timer (or manual Refresh) does.
        if (forcePressed || timerExpired)
        {
            RefreshTreeCache();
            LastTreeRefreshTime = ImGui::GetTime();
            ForceRefreshNext     = false;
            // Preserve any active search filter across the refresh so
            // the user does not see every Instance flash visible every
            // ~1.5 s while their filter is set.
            if (!CurrentFilterLower.empty())
                RebuildVisibilityFromCurrentFilter();
        }
    }

    inline void RenderCachedNode(int idx)
    {
        const CachedInstance& c = TreeCache[idx];
        if (!c.visibleInFilter) return;

        const std::string label = c.name + " [" + c.className + "]";
        const ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            (c.address == SelectedAddress ? ImGuiTreeNodeFlags_Selected : 0);

        const bool opened = ImGui::TreeNodeEx((void*)c.address, flags, "%s", label.c_str());
        if (ImGui::IsItemClicked())
            SelectedAddress = c.address;
        if (opened)
        {
            for (int ci : c.childIndices)
                RenderCachedNode(ci);
            ImGui::TreePop();
        }
    }

    // =========================================================================
    // PROPERTY EDITORS (right pane)
    // =========================================================================
    inline std::string SafeReadPropertyString(uintptr_t inst, uintptr_t off)
    {
        if (inst == 0) return std::string();
        const uintptr_t strPtr = Memory->read<uintptr_t>(inst + off);
        if (strPtr == 0) return std::string();
        return Memory->readString(strPtr);
    }

    inline bool SafeWritePropertyString(uintptr_t inst, uintptr_t off, const std::string& value)
    {
        if (inst == 0) return false;
        const uintptr_t strPtr = Memory->read<uintptr_t>(inst + off);
        if (strPtr == 0) return false;
        return Memory->writeString(strPtr, value);
    }

    inline std::string SafeReadStringAt(uintptr_t strAddr)
    {
        if (strAddr == 0) return std::string();
        return Memory->readString(strAddr);
    }

    inline bool SafeWriteStringAt(uintptr_t strAddr, const std::string& value)
    {
        if (strAddr == 0) return false;
        return Memory->writeString(strAddr, value);
    }

    inline uintptr_t PrimitiveOf(uintptr_t inst)
    {
        if (inst == 0) return 0;
        return Memory->read<uintptr_t>(inst + Offsets::BasePart::Primitive);
    }

    inline void RenderStringProperty(const char* label, StringEditor& ed, uintptr_t inst, uintptr_t off)
    {
        if (ed.lastInst != inst)
        {
            ed.lastInst = inst;
            const std::string cur = SafeReadPropertyString(inst, off);
            strncpy_s(ed.buf, cur.c_str(), _TRUNCATE);
        }
        ImGui::InputText(label, ed.buf, sizeof(ed.buf));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            // writeString silently fails when the new value would exceed
            // the existing Roblox ManagedString capacity. Surface the
            // miss so the user knows their edit didn't commit instead of
            // wondering why the texture in-game didn't change.
            ed.lastCommitOk = SafeWritePropertyString(inst, off, ed.buf);
            ed.justCommitted = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enter to commit. Must fit original Roblox string capacity.");
        if (ed.justCommitted)
        {
            ImGui::SameLine();
            if (ed.lastCommitOk)
                ImGui::TextColored(ImVec4(0.35f, 1.f, 0.45f, 1.f), "  committed");
            else
                ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "  too long for Roblox string capacity");
            ed.justCommitted = false;
        }
    }

    // Variant for strings stored inline at a fixed address (e.g. Instance
    // name lives inside its NameContainer, so the caller passes the resolved
    // string address rather than an (instance, offset) pointer field).
    inline void RenderStringPropertyAt(const char* label, StringEditor& ed, uintptr_t strAddr)
    {
        if (ed.lastInst != strAddr)
        {
            ed.lastInst = strAddr;
            const std::string cur = SafeReadStringAt(strAddr);
            strncpy_s(ed.buf, cur.c_str(), _TRUNCATE);
        }
        ImGui::InputText(label, ed.buf, sizeof(ed.buf));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            ed.lastCommitOk = SafeWriteStringAt(strAddr, ed.buf);
            ed.justCommitted = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enter to commit. Must fit original Roblox string capacity.");
        if (ed.justCommitted)
        {
            ImGui::SameLine();
            if (ed.lastCommitOk)
                ImGui::TextColored(ImVec4(0.35f, 1.f, 0.45f, 1.f), "  committed");
            else
                ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "  too long for Roblox string capacity");
            ed.justCommitted = false;
        }
    }

    inline void RenderBasePartProperties(uintptr_t inst, const char* cls)
    {
        if (inst == 0) return;

        // Color3 (3 floats at Offsets::BasePart::Color3 = 0x148)
        static float color[3] = { 0.55f, 0.55f, 0.55f };
        static uintptr_t colorLast = kNeverSeen;
        if (colorLast != inst)
        {
            colorLast = inst;
            color[0] = Memory->read<float>(inst + Offsets::BasePart::Color3 + 0x0);
            color[1] = Memory->read<float>(inst + Offsets::BasePart::Color3 + 0x4);
            color[2] = Memory->read<float>(inst + Offsets::BasePart::Color3 + 0x8);
        }
        if (ImGui::ColorEdit3((std::string("Color##") + cls).c_str(), color))
        {
            Memory->write<float>(inst + Offsets::BasePart::Color3 + 0x0, color[0]);
            Memory->write<float>(inst + Offsets::BasePart::Color3 + 0x4, color[1]);
            Memory->write<float>(inst + Offsets::BasePart::Color3 + 0x8, color[2]);
        }

        // Transparency
        static float transparency = 0.f;
        static uintptr_t transLast = kNeverSeen;
        if (transLast != inst)
        {
            transLast = inst;
            transparency = Memory->read<float>(inst + Offsets::BasePart::Transparency);
        }
        if (ImGui::SliderFloat((std::string("Transparency##") + cls).c_str(), &transparency, 0.f, 1.f))
            Memory->write<float>(inst + Offsets::BasePart::Transparency, transparency);

        // Reflectance
        static float reflectance = 0.f;
        static uintptr_t reflLast = kNeverSeen;
        if (reflLast != inst)
        {
            reflLast = inst;
            reflectance = Memory->read<float>(inst + Offsets::BasePart::Reflectance);
        }
        if (ImGui::SliderFloat((std::string("Reflectance##") + cls).c_str(), &reflectance, 0.f, 1.f))
            Memory->write<float>(inst + Offsets::BasePart::Reflectance, reflectance);

        const uintptr_t primitive = PrimitiveOf(inst);
        if (primitive == 0)
        {
            ImGui::TextDisabled("Primitive pointer invalid.");
            return;
        }

        // Position
        static Vectors::Vector3 pos = {0.f, 0.f, 0.f};
        static uintptr_t posLast = kNeverSeen;
        if (posLast != inst)
        {
            posLast = inst;
            pos = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Position);
        }
        if (ImGui::DragFloat3((std::string("Position##") + cls).c_str(), &pos.x, 0.25f))
            Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Position, pos);

        // Size
        static Vectors::Vector3 size = {4.f, 1.f, 2.f};
        static uintptr_t sizeLast = kNeverSeen;
        if (sizeLast != inst)
        {
            sizeLast = inst;
            size = Memory->read<Vectors::Vector3>(primitive + Offsets::Primitive::Size);
        }
        if (ImGui::DragFloat3((std::string("Size##") + cls).c_str(), &size.x, 0.25f))
            Memory->write<Vectors::Vector3>(primitive + Offsets::Primitive::Size, size);

        // Material
        static int material = 0;
        static uintptr_t matLast = kNeverSeen;
        if (matLast != inst)
        {
            matLast = inst;
            material = Memory->read<int>(primitive + Offsets::Primitive::Material);
        }
        if (ImGui::InputInt((std::string("Material##") + cls).c_str(), &material))
            Memory->write<int>(primitive + Offsets::Primitive::Material, material);

        // Primitive flags (uint8 at primitive+0x1b6, bitmask).
        static uint8_t flags = 0;
        static uintptr_t flagLast = kNeverSeen;
        if (flagLast != inst)
        {
            flagLast = inst;
            flags = Memory->read<uint8_t>(primitive + Offsets::Primitive::Flags);
        }
        bool anchored   = (flags & Offsets::PrimitiveFlags::Anchored)  != 0;
        bool canCollide = (flags & Offsets::PrimitiveFlags::CanCollide) != 0;
        bool canTouch   = (flags & Offsets::PrimitiveFlags::CanTouch)   != 0;
        bool canQuery   = (flags & Offsets::PrimitiveFlags::CanQuery)   != 0;
        bool changed = false;
        if (ImGui::Checkbox((std::string("Anchored##")   + cls).c_str(), &anchored))   { if (anchored)   flags |= Offsets::PrimitiveFlags::Anchored;   else flags &= ~Offsets::PrimitiveFlags::Anchored;   changed = true; }
        if (ImGui::Checkbox((std::string("CanCollide##") + cls).c_str(), &canCollide)) { if (canCollide) flags |= Offsets::PrimitiveFlags::CanCollide; else flags &= ~Offsets::PrimitiveFlags::CanCollide; changed = true; }
        if (ImGui::Checkbox((std::string("CanTouch##")   + cls).c_str(), &canTouch))   { if (canTouch)   flags |= Offsets::PrimitiveFlags::CanTouch;   else flags &= ~Offsets::PrimitiveFlags::CanTouch;   changed = true; }
        if (ImGui::Checkbox((std::string("CanQuery##")   + cls).c_str(), &canQuery))   { if (canQuery)   flags |= Offsets::PrimitiveFlags::CanQuery;   else flags &= ~Offsets::PrimitiveFlags::CanQuery;   changed = true; }
        if (changed) Memory->write<uint8_t>(primitive + Offsets::Primitive::Flags, flags);
    }

    inline void RenderTextureLikeProperties(uintptr_t inst, const char* cls, uintptr_t textureOff)
    {
        static StringEditor ed;
        RenderStringProperty((std::string("Texture##") + cls).c_str(), ed, inst, textureOff);
    }

    inline void RenderHumanoidProperties(uintptr_t inst)
    {
        static float ws = 16.f;
        static uintptr_t wsLast = kNeverSeen;
        if (wsLast != inst)
        {
            wsLast = inst;
            ws = Memory->read<float>(inst + Offsets::Humanoid::Walkspeed);
        }
        if (ImGui::DragFloat("WalkSpeed##humanoid", &ws, 0.5f, 0.f, 500.f, "%.1f st/s"))
        {
            Memory->write<float>(inst + Offsets::Humanoid::Walkspeed, ws);
            // Mirror WalkspeedCheck so anticheat-based games accept the new value.
            Memory->write<float>(inst + Offsets::Humanoid::WalkspeedCheck, ws);
        }

        static float jp = 50.f;
        static uintptr_t jpLast = kNeverSeen;
        if (jpLast != inst)
        {
            jpLast = inst;
            jp = Memory->read<float>(inst + Offsets::Humanoid::JumpPower);
        }
        if (ImGui::DragFloat("JumpPower##humanoid", &jp, 1.f, 0.f, 500.f, "%.1f"))
            Memory->write<float>(inst + Offsets::Humanoid::JumpPower, jp);

        static float health = 100.f;
        static uintptr_t hlLast = kNeverSeen;
        if (hlLast != inst)
        {
            hlLast = inst;
            health = Memory->read<float>(inst + Offsets::Humanoid::Health);
        }
        if (ImGui::DragFloat("Health##humanoid", &health, 1.f, 0.f, 100000.f, "%.0f"))
            Memory->write<float>(inst + Offsets::Humanoid::Health, health);

        static float maxHealth = 100.f;
        static uintptr_t mxLast = kNeverSeen;
        if (mxLast != inst)
        {
            mxLast = inst;
            maxHealth = Memory->read<float>(inst + Offsets::Humanoid::MaxHealth);
        }
        if (ImGui::DragFloat("MaxHealth##humanoid", &maxHealth, 1.f, 0.f, 100000.f, "%.0f"))
            Memory->write<float>(inst + Offsets::Humanoid::MaxHealth, maxHealth);
    }

    inline void RenderSoundProperties(uintptr_t inst)
    {
        static StringEditor sid;
        RenderStringProperty("SoundId##sound", sid, inst, Offsets::Sound::SoundId);

        static float vol = 0.5f;
        static uintptr_t vLast = kNeverSeen;
        if (vLast != inst) { vLast = inst; vol = Memory->read<float>(inst + Offsets::Sound::Volume); }
        if (ImGui::SliderFloat("Volume##sound", &vol, 0.f, 5.f))
            Memory->write<float>(inst + Offsets::Sound::Volume, vol);

        static float pbs = 1.f;
        static uintptr_t pLast = kNeverSeen;
        if (pLast != inst) { pLast = inst; pbs = Memory->read<float>(inst + Offsets::Sound::PlaybackSpeed); }
        if (ImGui::SliderFloat("PlaybackSpeed##sound", &pbs, 0.1f, 5.f))
            Memory->write<float>(inst + Offsets::Sound::PlaybackSpeed, pbs);

        static bool looped = false;
        static uintptr_t lpLast = kNeverSeen;
        if (lpLast != inst) { lpLast = inst; looped = Memory->read<bool>(inst + Offsets::Sound::Looped); }
        if (ImGui::Checkbox("Looped##sound", &looped))
            Memory->write<bool>(inst + Offsets::Sound::Looped, looped);
    }

    inline void RenderSkyProperties(uintptr_t inst)
    {
        ImGui::TextDisabled("Skybox textures (rbxassetid://... strings)");
        static StringEditor bk, dn, ft, lf, rt, up, sun, moon;
        RenderStringProperty("Skybox Back##sky",  bk,  inst, Offsets::Sky::SkyboxBk);
        RenderStringProperty("Skybox Down##sky",  dn,  inst, Offsets::Sky::SkyboxDn);
        RenderStringProperty("Skybox Front##sky", ft,  inst, Offsets::Sky::SkyboxFt);
        RenderStringProperty("Skybox Left##sky",  lf,  inst, Offsets::Sky::SkyboxLf);
        RenderStringProperty("Skybox Right##sky", rt,  inst, Offsets::Sky::SkyboxRt);
        RenderStringProperty("Skybox Up##sky",    up,  inst, Offsets::Sky::SkyboxUp);
        RenderStringProperty("Sun Texture##sky",  sun, inst, Offsets::Sky::SunTextureId);
        RenderStringProperty("Moon Texture##sky", moon,inst, Offsets::Sky::MoonTextureId);
    }

    inline void RenderLightProperties(uintptr_t /*inst*/, const std::string& cls)
    {
        // Light offsets aren't in the dumped Offsets:: namespace for the
        // current Roblox build. Don't write blind.
        (void)cls;
        ImGui::TextColored(ImVec4(1.f, 0.85f, 0.30f, 1.f),
            "Light (%s) editor disabled: offsets not in Offsets:: namespace.", cls.c_str());
        ImGui::TextWrapped("Only Texture / BasePart / Humanoid / Sound / GUI properties are editable right now (others need verified offsets before they can safely write).");
    }

    inline void RenderGuiObjectProperties(uintptr_t inst, const char* cls)
    {
        static float color[3] = { 1.f, 1.f, 1.f };
        static uintptr_t cLast = kNeverSeen;
        if (cLast != inst) {
            cLast = inst;
            color[0] = Memory->read<float>(inst + Offsets::GuiObject::BackgroundColor3);
            color[1] = Memory->read<float>(inst + Offsets::GuiObject::BackgroundColor3 + 4);
            color[2] = Memory->read<float>(inst + Offsets::GuiObject::BackgroundColor3 + 8);
        }
        if (ImGui::ColorEdit3((std::string("BackgroundColor3##") + cls).c_str(), color)) {
            Memory->write<float>(inst + Offsets::GuiObject::BackgroundColor3,     color[0]);
            Memory->write<float>(inst + Offsets::GuiObject::BackgroundColor3 + 4, color[1]);
            Memory->write<float>(inst + Offsets::GuiObject::BackgroundColor3 + 8, color[2]);
        }

        static float tr = 0.f;
        static uintptr_t tLast = kNeverSeen;
        if (tLast != inst) { tLast = inst; tr = Memory->read<float>(inst + Offsets::GuiObject::BackgroundTransparency); }
        if (ImGui::SliderFloat((std::string("BackgroundTransparency##") + cls).c_str(), &tr, 0.f, 1.f))
            Memory->write<float>(inst + Offsets::GuiObject::BackgroundTransparency, tr);

        static bool vis = true;
        static uintptr_t vLast = kNeverSeen;
        if (vLast != inst) { vLast = inst; vis = Memory->read<bool>(inst + Offsets::GuiObject::Visible); }
        if (ImGui::Checkbox((std::string("Visible##") + cls).c_str(), &vis))
            Memory->write<bool>(inst + Offsets::GuiObject::Visible, vis);
    }

    inline void RenderBeamProperties(uintptr_t inst)
    {
        static StringEditor ed;
        RenderStringProperty("Texture##beam", ed, inst, Offsets::Beam::Texture);

        static float br = 1.f;
        static uintptr_t bL = kNeverSeen;
        if (bL != inst) { bL = inst; br = Memory->read<float>(inst + Offsets::Beam::Brightness); }
        if (ImGui::SliderFloat("Brightness##beam", &br, 0.f, 10.f))
            Memory->write<float>(inst + Offsets::Beam::Brightness, br);

        static float le = 0.f;
        static uintptr_t leL = kNeverSeen;
        if (leL != inst) { leL = inst; le = Memory->read<float>(inst + Offsets::Beam::LightEmission); }
        if (ImGui::SliderFloat("LightEmission##beam", &le, 0.f, 5.f))
            Memory->write<float>(inst + Offsets::Beam::LightEmission, le);

        static float ts = 1.f;
        static uintptr_t tsL = kNeverSeen;
        if (tsL != inst) { tsL = inst; ts = Memory->read<float>(inst + Offsets::Beam::TextureSpeed); }
        if (ImGui::DragFloat("TextureSpeed##beam", &ts, 0.05f, -10.f, 10.f))
            Memory->write<float>(inst + Offsets::Beam::TextureSpeed, ts);

        static float tl = 10.f;
        static uintptr_t tlL = kNeverSeen;
        if (tlL != inst) { tlL = inst; tl = Memory->read<float>(inst + Offsets::Beam::TextureLength); }
        if (ImGui::DragFloat("TextureLength##beam", &tl, 0.1f, 0.f, 100.f))
            Memory->write<float>(inst + Offsets::Beam::TextureLength, tl);
    }

    inline void RenderToolProperties(uintptr_t inst)
    {
        static StringEditor ed;
        RenderStringProperty("TextureId##tool", ed, inst, Offsets::Tool::TextureId);
    }

    inline void RenderParticleEmitterProperties(uintptr_t inst)
    {
        static StringEditor ed;
        RenderStringProperty("Texture##pemitter", ed, inst, Offsets::ParticleEmitter::Texture);

        static float rate = 20.f;
        static uintptr_t rL = kNeverSeen;
        if (rL != inst) { rL = inst; rate = Memory->read<float>(inst + Offsets::ParticleEmitter::Rate); }
        if (ImGui::DragFloat("Rate##pemitter", &rate, 1.f, 0.f, 5000.f))
            Memory->write<float>(inst + Offsets::ParticleEmitter::Rate, rate);

        static float speed = 5.f;
        static uintptr_t sL = kNeverSeen;
        if (sL != inst) { sL = inst; speed = Memory->read<float>(inst + Offsets::ParticleEmitter::Speed); }
        if (ImGui::DragFloat("Speed##pemitter", &speed, 0.1f, -50.f, 50.f))
            Memory->write<float>(inst + Offsets::ParticleEmitter::Speed, speed);

        static float lt = 5.f;
        static uintptr_t lL = kNeverSeen;
        if (lL != inst) { lL = inst; lt = Memory->read<float>(inst + Offsets::ParticleEmitter::Lifetime); }
        if (ImGui::DragFloat("Lifetime##pemitter", &lt, 0.1f, 0.f, 60.f))
            Memory->write<float>(inst + Offsets::ParticleEmitter::Lifetime, lt);

        static float br = 1.f;
        static uintptr_t bL = kNeverSeen;
        if (bL != inst) { bL = inst; br = Memory->read<float>(inst + Offsets::ParticleEmitter::Brightness); }
        if (ImGui::SliderFloat("Brightness##pemitter", &br, 0.f, 10.f))
            Memory->write<float>(inst + Offsets::ParticleEmitter::Brightness, br);

        static float le = 0.f;
        static uintptr_t leL = kNeverSeen;
        if (leL != inst) { leL = inst; le = Memory->read<float>(inst + Offsets::ParticleEmitter::LightEmission); }
        if (ImGui::SliderFloat("LightEmission##pemitter", &le, 0.f, 5.f))
            Memory->write<float>(inst + Offsets::ParticleEmitter::LightEmission, le);
    }

    // =========================================================================
    // SELECTED-INSTANCE IDENTITY CACHE -- so the right pane header
    // (Name, Class) does not re-read Roblox memory every frame.
    // =========================================================================
    inline std::string SelectedNameCache  = "(none)";
    inline std::string SelectedClassCache = "(none)";
    inline uintptr_t   LastIdentityInst   = 0;

    inline void RenderInstanceProperties(uintptr_t inst)
    {
        // If the target process is gone (Memory driver disconnected) do
        // not call into the property editors -- every one of them does
        // a Memory->read which would crash the overlay.
        if (!Memory)
        {
            ImGui::TextDisabled("Target process disconnected.");
            return;
        }

        if (RightPaneHintActive)
        {
            ImGui::TextColored(ImVec4(1.f, 0.85f, 0.30f, 1.f), "%s", RightPaneHintText.c_str());
            ImGui::Separator();
            if (ImGui::GetTime() >= RightPaneHintExpiry)
                RightPaneHintActive = false;
        }

        if (inst == 0)
        {
            ImGui::TextDisabled("Select an instance in the tree to edit its properties.");
            return;
        }
        if (inst != LastIdentityInst)
        {
            LastIdentityInst = inst;
            RobloxInstance ri(inst);
            std::string n = ri.Name();
            SelectedNameCache  = n.empty() ? "(unnamed)" : n;
            SelectedClassCache = ri.Class();
        }

        ImGui::TextColored(main_color, "%s", SelectedNameCache.c_str());
        ImGui::TextDisabled("Class: %s   Address: 0x%llX", SelectedClassCache.c_str(), (unsigned long long)inst);
        ImGui::Separator();

        const std::string cls = SelectedClassCache;
        if (cls == "Texture")
            RenderTextureLikeProperties(inst, cls.c_str(), Offsets::Textures::Texture_Texture);
        else if (cls == "Decal")
            RenderTextureLikeProperties(inst, cls.c_str(), Offsets::Textures::Decal_Texture);
        else if (cls == "MeshPart")
        {
            RenderBasePartProperties(inst, cls.c_str());
            static StringEditor meshTex;
            RenderStringProperty("Texture##meshpart", meshTex, inst, Offsets::MeshPart::Texture);
            static StringEditor meshId;
            RenderStringProperty("MeshId##meshpart",  meshId, inst, Offsets::MeshPart::MeshId);
        }
        else if (cls == "ParticleEmitter")
            RenderParticleEmitterProperties(inst);
        else if (cls == "Tool")
            RenderToolProperties(inst);
        else if (cls == "Beam")
            RenderBeamProperties(inst);
        else if (cls == "Sky")
            RenderSkyProperties(inst);
        else if (cls == "Sound")
            RenderSoundProperties(inst);
        else if (cls == "Humanoid")
            RenderHumanoidProperties(inst);
        else if (cls == "PointLight" || cls == "SpotLight" || cls == "SurfaceLight")
            RenderLightProperties(inst, cls);
        else if (cls == "Part" || cls == "SpawnLocation" || cls == "WedgePart"
              || cls == "CornerWedgePart" || cls == "CylinderPart" || cls == "Ball"
              || cls == "TrussPart"  || cls == "Seat"  || cls == "VehicleSeat"
              || cls == "UnionOperation")
            RenderBasePartProperties(inst, cls.c_str());
        else if (cls == "Frame" || cls == "ImageLabel" || cls == "ImageButton"
              || cls == "TextLabel" || cls == "TextButton" || cls == "TextBox"
              || cls == "ViewportFrame" || cls == "ScrollingFrame")
            RenderGuiObjectProperties(inst, cls.c_str());

        ImGui::Separator();
        static StringEditor nameEditor;
        RenderStringPropertyAt("Name##rename", nameEditor, RobloxInstance(inst).NameAddress());
    }

    // =========================================================================
    // WINDOW
    // =========================================================================
    inline void RenderWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Roblox Explorer", &Options::Misc::ExplorerEnabled))
        {
            ImGui::End();
            return;
        }

        // Order matters: refresh first so the cache exists, then apply
        // the current search filter to it.
        MaybeRefreshTree();
        MaybeApplySearchFilter();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float treeWidth = avail.x * 0.42f;

        // -------- LEFT: searchable tree (cache-only) --------
        ImGui::BeginChild("##explorer_tree", ImVec2(treeWidth, avail.y), true);
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.06f, 0.06f, 1.f));
            ImGui::InputTextWithHint("##explorer_search", "Search...", SearchBuf, sizeof(SearchBuf));
            ImGui::PopStyleColor(1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh##explorer")) ForceRefreshNext = true;
            ImGui::Separator();

            if (TreeCache.empty())
            {
                ImGui::TextDisabled("Tree not loaded yet.");
            }
            else
            {
                ImGui::TextDisabled("%zu instances cached  -  refreshes every %.1fs",
                    TreeCache.size(), kTreeRefreshSeconds);
                ImGui::Separator();
                for (size_t i = 0; i < TreeCache.size(); ++i)
                {
                    if (TreeCache[i].parentIdx == -1 && TreeCache[i].visibleInFilter)
                        RenderCachedNode((int)i);
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -------- RIGHT: properties (live read for the selected one) --------
        ImGui::BeginChild("##explorer_props", ImVec2(0, avail.y), true);
        RenderInstanceProperties(SelectedAddress);
        ImGui::EndChild();

        ImGui::End();
    }
}
