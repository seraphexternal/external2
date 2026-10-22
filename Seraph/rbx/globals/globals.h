#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <algorithm>
#include <string>
#include <cctype>
#include <mutex>
#include <chrono>

#include "../../Memory/MemoryManager.h"
#include "../SDK/sdk.h"

struct RobloxPlayer
{
    uintptr_t address = 0;
    int RigType = 0;
    std::string Name = "";
    float Health = 0.0f;
    float MaxHealth = 0.0f;
    RobloxInstance Team = RobloxInstance(0);
    RobloxInstance Character = RobloxInstance(0);
    RobloxInstance Humanoid = RobloxInstance(0);
    RobloxInstance Head = RobloxInstance(0);
    RobloxInstance HumanoidRootPart = RobloxInstance(0);
    RobloxInstance Left_Arm = RobloxInstance(0);
    RobloxInstance Left_Leg = RobloxInstance(0);
    RobloxInstance Right_Arm = RobloxInstance(0);
    RobloxInstance Right_Leg = RobloxInstance(0);
    RobloxInstance Torso = RobloxInstance(0);
    RobloxInstance Upper_Torso = RobloxInstance(0);
    RobloxInstance Lower_Torso = RobloxInstance(0);
    RobloxInstance Right_Upper_Arm = RobloxInstance(0);
    RobloxInstance Right_Lower_Arm = RobloxInstance(0);
    RobloxInstance Right_Hand = RobloxInstance(0);
    RobloxInstance Left_Upper_Arm = RobloxInstance(0);
    RobloxInstance Left_Lower_Arm = RobloxInstance(0);
    RobloxInstance Left_Hand = RobloxInstance(0);
    RobloxInstance Right_Upper_Leg = RobloxInstance(0);
    RobloxInstance Right_Lower_Leg = RobloxInstance(0);
    RobloxInstance Right_Foot = RobloxInstance(0);
    RobloxInstance Left_Upper_Leg = RobloxInstance(0);
    RobloxInstance Left_Lower_Leg = RobloxInstance(0);
    RobloxInstance Left_Foot = RobloxInstance(0);
    int TeamColor = 0;
    int TeamBrickColor = 0;
    std::string TeamName = "";
    std::string ToolName = "";
    Vectors::Vector3 Velocity = {0.f, 0.f, 0.f};

    // ── Rivals progression (ESP) ──
    // Rivals does not expose a rank name. It exposes an ELO number in
    // CustomLeaderstats/"Current ELO", and the tier is derived from it. ELO is
    // unranked at -1 and is otherwise always populated.
    std::int64_t Level = -1;
    std::int64_t Elo = -1;
    bool LevelResolved = false;
    bool RankResolved = false;

    // Icon filename stem resolved once when the progression is read, so the
    // render loop never does string work per player per frame.
    std::string RankStem;

    // Rivals: the game tags YOUR teammates' characters with a "TeammateLabel"
    // BillboardGui parented to a body part (the Head). Team()/TeamColor both
    // read as unassigned for every player in Rivals, so this name-tag is the
    // only client-visible teammate-vs-enemy signal the game exposes.
    bool HasTeammateLabel = false;

    bool UseAnchor = false;
    Vectors::Vector3 AnchorPos = {0.f, 0.f, 0.f};

    // Sound-based position for culled enemies (footstep triangulation)
    Vectors::Vector3 SoundPos = {0.f, 0.f, 0.f};
    float SoundPosTime = 0.f;
};

// ELO -> tier name. Thresholds are the same ladder the reference ESP used, in
// the same order, so the two agree on what a given player is.
//
// Note this ladder contains a Diamond tier, which is not in assets/rank/, and
// only the "3" division of each tier has an icon. Both fall back to text.
inline const char* RankFromElo(std::int64_t elo)
{
    if (elo < 0) return "Unranked";
    if (elo >= 7855) return "ArchNemesis";
    if (elo >= 3600) return "Nemesis";
    if (elo >= 3400) return "Onyx 3";
    if (elo >= 3200) return "Onyx 2";
    if (elo >= 3000) return "Onyx 1";
    if (elo >= 2800) return "Diamond 3";
    if (elo >= 2600) return "Diamond 2";
    if (elo >= 2400) return "Diamond 1";
    if (elo >= 2200) return "Platinum 3";
    if (elo >= 2000) return "Platinum 2";
    if (elo >= 1800) return "Platinum 1";
    if (elo >= 1600) return "Gold 3";
    if (elo >= 1400) return "Gold 2";
    if (elo >= 1200) return "Gold 1";
    if (elo >= 1000) return "Silver 3";
    if (elo >= 800) return "Silver 2";
    if (elo >= 600) return "Silver 1";
    if (elo >= 400) return "Bronze 3";
    if (elo >= 200) return "Bronze 2";
    return "Bronze 1";
}

// Maps a tier name onto the icon file stem in assets/rank/. Returns "" when no
// art exists, which is the signal to draw the name as text instead.
//
// Two wrinkles handled here:
//   - Only division 3 has art, but a Bronze 2 player should still get the bronze
//     icon rather than the words "Bronze 2", so any division falls back to its
//     tier's division-3 stem.
//   - "Nemesis" is the one name that does not equal its filename; it is
//     NemesisRank.png.
//
// The ladder contains a Diamond tier, which was never supplied. It returns "" so
// the ESP falls back to text until art for it exists.
inline std::string RankIconStem(const char* tier)
{
    if (!tier) return {};

    if (::_stricmp(tier, "Nemesis") == 0)
        return "nemesisrank";
    if (::_stricmp(tier, "ArchNemesis") == 0)
        return "archnemesis";

    // Lowercase and strip spaces so "Gold 2" and "gold2" agree.
    std::string key;
    for (const char* p = tier; *p; ++p)
        if (*p != ' ')
            key.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(*p))));

    if (key == "unranked")
        return "unranked";

    // Drop the division suffix before matching the tier. Only division 3 has
    // art and every division shares it, so "gold2" resolves to gold3.png exactly
    // like "gold1" and "gold3" do. Matching the bare tier name is not enough:
    // the key still carries its digit at this point.
    if (!key.empty() && key.back() >= '0' && key.back() <= '9')
        key.pop_back();

    static const char* tiers[] = { "bronze", "silver", "gold", "platinum", "onyx", "diamond" };
    for (const char* t : tiers)
        if (key == t)
            return std::string(t) + "3";

    return {};
}

// True when the player's equipped tool is a katana (Rivals). Matches the tool
// name case-insensitively so "Katana", "Katanas", etc. all count.
inline bool IsHoldingKatana(const RobloxPlayer& player)
{
    if (player.ToolName.empty())
        return false;

    std::string hay = player.ToolName;
    for (auto& ch : hay)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    // Check for various katana names used in Rivals and other games
    return hay.find("katana") != std::string::npos ||
           hay.find("sword") != std::string::npos ||
           hay.find("blade") != std::string::npos;
}

namespace Globals
{
    namespace Roblox
    {
        inline RobloxInstance DataModel(0);
        inline uintptr_t VisualEngine;
        inline RobloxInstance Workspace(0);
        inline RobloxInstance Players(0);
        inline RobloxInstance Camera(0);
        inline RobloxInstance LocalPlayer(0);
        inline RobloxInstance LocalPlayerTeam(0);
        inline int LocalPlayerTeamColor = 0;
        inline int LocalPlayerTeamBrickColor = 0;
        inline std::string LocalPlayerTeamName = "";
        inline __int64 lastPlaceID;
        inline std::string gameName = "Unknown";
        inline bool isPhantomForces = false;
        inline constexpr __int64 PHANTOM_FORCES_ID = 292439477;
		inline bool isRivals = false;
		inline constexpr __int64 RIVALS_ID = 117398147513099; // RIVALS (Nosniy Games) match place
		inline constexpr __int64 RIVALS_LOBBY_ID = 17625359962; // RIVALS lobby / root place
		inline constexpr __int64 RIVALS_HUB_ID = 3806411531; // RIVALS hub / tutorial place
		inline bool IsRivalsPlace(__int64 id) { return id == RIVALS_ID || id == RIVALS_LOBBY_ID || id == RIVALS_HUB_ID; }
		inline bool isOverkill = false;
		inline constexpr __int64 OVERKILL_ID = 124842176624983;

		inline bool isMM2 = false;
		inline constexpr __int64 MM2_ID = 142823291; // Murder Mystery 2
		inline bool isBladeBall = false;
		inline constexpr __int64 BLADEBALL_ID = 13772394625; // Blade Ball
		inline bool isBloxStrike = false;
		inline constexpr __int64 BLOX_STRIKE_ID = 114234929420007; // Blox Strike
		inline bool IsBloxStrikePlace(__int64 id) { return id == BLOX_STRIKE_ID; }
    }

    namespace BladeBall
    {
        inline bool isActive = false;     // true while an auto-parry is firing
        inline std::string status = "idle";
    }
    namespace Viewport
    {
        inline Matrixes::Matrix4 ViewMatrix;
        inline Vectors::Vector2 Dimensions;
        inline POINT ScreenPos;
        inline HWND RobloxHWND = nullptr;
        inline bool Valid = false;

        // The game's render surface lives in the "WINDOWSCLIENT" top-level
        // window owned by the Roblox process. FindWindowA(NULL, "Roblox") used
        // to be the primary lookup, but the title is not unique or stable and
        // it happily matched a transient 120x0 window, which then got cached
        // for the whole session. Every WorldToScreen call divides by
        // Dimensions, so a 120x0 client rect collapsed the entire ESP into a
        // 120x0 box in the top-left corner of the screen.
        //
        // Resolve by class *and* owning pid instead, and keep looking until a
        // window with a real client rect shows up, re-resolving whenever the
        // cached handle stops being usable (game relaunches get a new hwnd).
        inline bool ClientRectUsable(HWND hwnd, RECT& out)
        {
            if (!hwnd || !IsWindow(hwnd)) return false;
            if (!IsWindowVisible(hwnd)) return false;
            if (IsIconic(hwnd)) return false;
            RECT rc{};
            if (!GetClientRect(hwnd, &rc)) return false;
            // A real game client is at least a couple hundred pixels each way;
            // anything smaller is a helper/transient window.
            if (rc.right < 200 || rc.bottom < 200) return false;
            out = rc;
            return true;
        }

        inline HWND ResolveGameWindow()
        {
            const DWORD pid = Memory ? (DWORD)Memory->getProcessId() : 0;

            struct Ctx { DWORD pid; HWND best; long area; bool bestIsClient; };
            Ctx ctx{ pid, nullptr, 0, false };

            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lParam);

                DWORD wpid = 0;
                GetWindowThreadProcessId(hwnd, &wpid);
                if (c->pid != 0 && wpid != c->pid) return TRUE;

                RECT rc{};
                if (!ClientRectUsable(hwnd, rc)) return TRUE;

                char cls[64] = { 0 };
                GetClassNameA(hwnd, cls, sizeof(cls));
                const bool isClient = (strcmp(cls, "WINDOWSCLIENT") == 0);
                const long area = (rc.right - rc.left) * (rc.bottom - rc.top);

                // Prefer the game's own client class; otherwise the largest
                // usable window the process owns.
                if (!c->best ||
                    (isClient && !c->bestIsClient) ||
                    (isClient == c->bestIsClient && area > c->area))
                {
                    c->best = hwnd;
                    c->area = area;
                    c->bestIsClient = isClient;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx));

            return ctx.best;
        }

        inline void Update()
        {
            RECT clientRect{};
            if (!ClientRectUsable(RobloxHWND, clientRect))
            {
                // Cached handle is gone, minimised, or degenerate - re-resolve.
                RobloxHWND = ResolveGameWindow();
                if (!ClientRectUsable(RobloxHWND, clientRect))
                {
                    Valid = false;
                    return;
                }
            }

            Dimensions = { (float)clientRect.right, (float)clientRect.bottom };

            ScreenPos = { 0, 0 };
            ClientToScreen(RobloxHWND, &ScreenPos);

            if (Roblox::VisualEngine != 0)
            {
                ViewMatrix = Memory->read<Matrixes::Matrix4>(Roblox::VisualEngine + Offsets::VisualEngine::ViewMatrix);
                Valid = true;
            }
            else
            {
                Valid = false;
            }
        }
    }
namespace FrameCache
{
    inline Vectors::Vector3 CameraPos{};
    inline sCFrame CameraCFrame{};
    inline Vectors::Vector3 LocalHRPPos{};
    inline bool CameraValid = false;
    inline bool LocalHRPValid = false;
    inline int LastFrame = -1;

    inline void Update()
    {
        // Use GetTickCount for frame detection (no ImGui dependency in globals.h)
        int frame = static_cast<int>(GetTickCount() / 16); // ~60fps tick buckets
        if (frame != LastFrame)
        {
            // New frame: drop every cached read so all features observe a single
            // consistent snapshot, and so nothing cached can go stale.
            Memory->InvalidateReadCache();
            if (Roblox::Camera.address)
            {
                // Position is needed twice below — read it once.
                CameraPos = Memory->read<Vectors::Vector3>(Roblox::Camera.address + Offsets::Camera::Position);
                Matrixes::Matrix3x3 rot = Memory->read<Matrixes::Matrix3x3>(Roblox::Camera.address + Offsets::Camera::Rotation);
                const Vectors::Vector3& pos = CameraPos;
                // Construct sCFrame manually (aggregate struct)
CameraCFrame = sCFrame{};
CameraCFrame.r00 = rot.r00; CameraCFrame.r01 = rot.r01; CameraCFrame.r02 = rot.r02;
CameraCFrame.r10 = rot.r10; CameraCFrame.r11 = rot.r11; CameraCFrame.r12 = rot.r12;
CameraCFrame.r20 = rot.r20; CameraCFrame.r21 = rot.r21; CameraCFrame.r22 = rot.r22;
CameraCFrame.x = pos.x; CameraCFrame.y = pos.y; CameraCFrame.z = pos.z;
                CameraValid = true;
            }
            else
            {
                CameraValid = false;
            }

            if (Roblox::LocalPlayer.address)
            {
                auto localChar = Roblox::LocalPlayer.Character();
                // Resolving the local rig costs up to 3 FindFirstChild child
                // scans (~100 driver reads) and this ran every frame; the parts
                // only change on respawn, so re-resolve when the character
                // address changes instead.
                static uintptr_t s_fcCharAddr = 0;
                static RobloxInstance s_fcHRP;
                static std::chrono::steady_clock::time_point s_fcRescanAt = std::chrono::steady_clock::now();
                if (localChar.address != s_fcCharAddr ||
                    std::chrono::steady_clock::now() - s_fcRescanAt > std::chrono::seconds(2))
                {
                    s_fcCharAddr = localChar.address;
                    s_fcRescanAt = std::chrono::steady_clock::now();
                    s_fcHRP = localChar.FindFirstChild("HumanoidRootPart");
                    if (!s_fcHRP.address) s_fcHRP = localChar.FindFirstChild("Torso");
                    if (!s_fcHRP.address) s_fcHRP = localChar.FindFirstChild("UpperTorso");
                }
                if (s_fcHRP.address)
                {
                    LocalHRPPos = s_fcHRP.Position();
                    LocalHRPValid = true;
                }
                else
                {
                    LocalHRPValid = false;
                }
            }
            else
            {
                LocalHRPValid = false;
            }

            LastFrame = frame;
        }
    }
}
    namespace DynamicOffsets
    {
        inline uintptr_t PlayerTeam = 0;
    }

    namespace Caches
    {
        inline std::vector<RobloxInstance> CachedPlayers;
        inline std::vector<RobloxPlayer> CachedPlayerObjects;
        inline std::mutex CachedPlayerObjectsMutex;

        // Locked copy of the player list.  CachePlayerObjects() replaces
        // CachedPlayerObjects wholesale (`= std::move(tempList)`) on its own
        // thread, so any thread that merely *reads* the list must not iterate the
        // live vector - doing so walked the freed heap and crashed.  Use this in
        // range-for loops: for (const auto& p : SnapshotCachedPlayerObjects()).
        inline std::vector<RobloxPlayer> SnapshotCachedPlayerObjects()
        {
            std::lock_guard<std::mutex> lock(CachedPlayerObjectsMutex);
            return CachedPlayerObjects;
        }

        // One deep copy of the player list per render frame, shared by every
        // draw/aim pass.  Each RobloxPlayer owns 4 std::strings, so the old
        // pattern of taking a fresh locked copy at each call site (ESP, arrows,
        // radar, aimbot x3, triggerbot x2, player list) cost ~10 vector copies
        // per frame - hundreds of heap allocations before any drawing happened.
        //
        // The render thread bumps the sequence once per frame; callers on other
        // threads still get a correct (their own) copy, and the 250ms fallback
        // refresh guarantees the list can never go stale even if the bump is
        // missed on some code path.
        inline std::atomic<uint64_t> PlayersFrameSeq{ 0 };
        inline void NextPlayersFrame() { PlayersFrameSeq.fetch_add(1, std::memory_order_relaxed); }

        inline const std::vector<RobloxPlayer>& FramePlayers()
        {
            static thread_local std::vector<RobloxPlayer> cache;
            static thread_local uint64_t cacheSeq = UINT64_MAX;
            static thread_local std::chrono::steady_clock::time_point lastRefresh{};

            const uint64_t seq = PlayersFrameSeq.load(std::memory_order_relaxed);
            if (cacheSeq != seq ||
                std::chrono::steady_clock::now() - lastRefresh > std::chrono::milliseconds(250))
            {
                std::lock_guard<std::mutex> lock(CachedPlayerObjectsMutex);
                cache = CachedPlayerObjects;
                cacheSeq = seq;
                lastRefresh = std::chrono::steady_clock::now();
            }
            return cache;
        }
        inline bool forceRefresh = false;
        inline bool playerCacheNeedsRefresh = false;
        inline bool playerObjectsCacheNeedsRefresh = false;
        inline std::unordered_map<uintptr_t, RobloxInstance> CharacterFallbackCache;
    }
    inline std::string executablePath;
    inline std::string originalExeDir;
    inline std::string configsPath;

    // Returns the directory containing the actual executable, trimmed of any
    // trailing backslash. This prefers the original exe location because the
    // process is optionally relaunched as a renamed copy in %TEMP% (Hide Process),
    // in which case GetModuleFileName/executablePath would point at the temp copy
    // and not at the folder the user placed files (hitsounds, models, configs) in.
    inline std::string ResolveExeDir()
    {
        std::string dir;
        if (!originalExeDir.empty())
            dir = originalExeDir;
        else
            dir = executablePath;

        while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/'))
            dir.pop_back();
        return dir;
    }

    inline uintptr_t ResolveTeamOffset()
    {
        if (DynamicOffsets::PlayerTeam != 0)
            return DynamicOffsets::PlayerTeam;

        if (Globals::Roblox::DataModel.address == 0 || Globals::Roblox::Players.address == 0)
            return 0;

        auto teamsService = Globals::Roblox::DataModel.FindFirstChild("Teams");
        if (teamsService.address == 0)
            return 0;

        auto teams = teamsService.GetChildren();
        if (teams.empty())
            return 0;

        std::vector<uintptr_t> teamAddresses;
        for (const auto& t : teams)
            teamAddresses.push_back(t.address);

        auto players = Globals::Roblox::Players.GetChildren();
        for (const auto& player : players)
        {
            if (player.address == 0) continue;

            // Scan player struct for a pointer to a team instance
            for (uintptr_t offset = 0x100; offset <= 0x400; offset += 8)
            {
                uintptr_t ptr = Memory->read<uintptr_t>(player.address + offset);
                for (uintptr_t tAddr : teamAddresses)
                {
                    if (ptr == tAddr)
                    {
                        DynamicOffsets::PlayerTeam = offset;
                        return offset;
                    }
                }
            }
        }

        return 0;
    }

    inline bool running = true;
    inline std::atomic<bool> overlayDone = true;
    inline std::atomic<bool> overlayShouldShutdown = false;
    // Set by the FreeCam loop while it owns the camera; Third Person checks this
    // so the two features never fight over the Camera object.
    inline std::atomic<bool> freecamOwnsCamera = false;

    namespace HitSounds
    {
        inline std::vector<std::string> Files;
        inline std::string FolderPath;

        inline void ScanFolder()
        {
            Files.clear();
            if (FolderPath.empty()) return;

            WIN32_FIND_DATAA findData;
            std::string searchPath = FolderPath + "\\*";
            HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
            if (hFind == INVALID_HANDLE_VALUE) return;

            do
            {
                std::string name = findData.cFileName;
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

                if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".wav" || lower.substr(lower.size() - 4) == ".mp3"))
                {
                    Files.push_back(name);
                }
            } while (FindNextFileA(hFind, &findData));

            FindClose(hFind);

            std::sort(Files.begin(), Files.end());
        }
    }
}

// Uncached team check; use IsTeammate() (below), which memoizes per frame.
inline bool IsTeammateRaw(const RobloxPlayer& player);

// A player's team signal cannot change within one frame, but every consumer
// asks for itself: ESP twice per player, plus aimbot, triggerbot, chams,
// ragebot and silent aim. The Blox Strike branch of the check answers by
// walking the character's children and reading a class per child, so repeating
// it per consumer was tens of thousands of driver reads/sec on its own.
// Results are memoized per player for the current render frame, per thread.
inline bool IsTeammate(const RobloxPlayer& player)
{
    if (Globals::Roblox::LocalPlayer.address == 0 || player.address == 0)
        return false;

    if (player.address == Globals::Roblox::LocalPlayer.address)
        return true;

    static thread_local uint64_t memoSeq = UINT64_MAX;
    static thread_local std::unordered_map<uintptr_t, bool> memo;
    const uint64_t seq = Globals::Caches::PlayersFrameSeq.load(std::memory_order_relaxed);
    if (memoSeq != seq)
    {
        memo.clear();
        memoSeq = seq;
    }

    auto it = memo.find(player.address);
    if (it != memo.end())
        return it->second;

    const bool result = IsTeammateRaw(player);
    memo.emplace(player.address, result);
    return result;
}

inline bool IsTeammateRaw(const RobloxPlayer& player)
{
    if (Globals::Roblox::LocalPlayer.address == 0 || player.address == 0)
        return false;

    if (player.address == Globals::Roblox::LocalPlayer.address)
        return true;

    // Blox Strike: team is indicated by a Highlight on the character model.
    // Green fill (R<0.5, G>0.5) = CT, Red/orange fill (R>0.5, G<0.5) = T.
    // Local player is T (per user), so teammates have red Highlight.
    if (Globals::Roblox::isBloxStrike)
    {
        const auto charModel = player.Character;
        if (charModel.address)
        {
            const auto kids = charModel.GetChildren();
            for (auto& k : kids)
            {
                if (k.Class() == "Highlight" && k.address)
                {
                    const float r = Memory->read<float>(k.address + 0xC8); // FillColor.R
                    const float g = Memory->read<float>(k.address + 0xCC); // FillColor.G
                    // Red/orange = teammate (T), Green = enemy (CT)
                    if (r > 0.5f && g < 0.5f)
                        return true; // Terrorist teammate
                    if (r < 0.5f && g > 0.5f)
                        return false; // CT enemy
                    // If color is ambiguous, fall through
                }
            }
        }
        // No Highlight or ambiguous color: can't determine, treat as enemy
        return false;
    }

    // Rivals-specific: Team object is stripped for non-local players,
    // so fall back to TeamColor (from player's TeamColor offset) and
    // TeamBrickColor (cached from same offset for Rivals).
    if (Globals::Roblox::isRivals)
    {
        // Authoritative signal: Rivals hangs a "TeammateLabel" BillboardGui on
        // the Head of players on YOUR team (confirmed by probe: only the one
        // teammate in a 2v2 carried it). Cached per player during rebuild.
        // Enemies never have it, so its presence is the whole answer.
        if (player.HasTeammateLabel)
            return true;

        auto localTeam = Globals::Roblox::LocalPlayerTeam;
        auto playerTeam = player.Team;
        
        if (localTeam.address != 0 && playerTeam.address != 0)
        {
            if (localTeam.address == playerTeam.address)
                return true;
                
            if (!Globals::Roblox::LocalPlayerTeamName.empty() && Globals::Roblox::LocalPlayerTeamName == player.TeamName)
                return true;
        }

        // Fallback: compare TeamColor (player's TeamColor offset)
        // In Rivals, TeamColor=1 (White) is the default/unassigned color.
        // Only use TeamColor for team check if it's a real team color.
        auto localTeamColor = Globals::Roblox::LocalPlayerTeamColor & 0xFFFF;
        auto playerTeamColor = player.TeamColor & 0xFFFF;
        if (localTeamColor != 0 && localTeamColor != 1 && localTeamColor != 194 && localTeamColor != 0xFFFF &&
            localTeamColor == playerTeamColor)
            return true;

        // Fallback: compare TeamBrickColor (cached from player's TeamColor offset for Rivals)
        if (Globals::Roblox::LocalPlayerTeamBrickColor != 0 && Globals::Roblox::LocalPlayerTeamBrickColor != 1 && Globals::Roblox::LocalPlayerTeamBrickColor != 194 && Globals::Roblox::LocalPlayerTeamBrickColor != -1 &&
            Globals::Roblox::LocalPlayerTeamBrickColor == player.TeamBrickColor)
            return true;

        // If local player has default TeamColor (1=White), assume FFA mode - no teammates
        if (Globals::Roblox::LocalPlayerTeamColor == 1 || Globals::Roblox::LocalPlayerTeamColor == 194 || Globals::Roblox::LocalPlayerTeamColor == 0)
            return false;

        return false;
    }

    // Prioritize Player.TeamColor as it's not encrypted and very reliable in games like Arsenal.
    // Mask with 0xFFFF because BrickColor is a 16-bit integer, preventing garbage in upper bits.
    auto localPlayerColor = Globals::Roblox::LocalPlayerTeamColor & 0xFFFF;
    auto playerPlayerColor = player.TeamColor & 0xFFFF;
    
    if (localPlayerColor == playerPlayerColor && localPlayerColor != 194 && localPlayerColor != 0 && localPlayerColor != 0xFFFF)
    {
        return true;
    }

    // Rivals teammates are joined by TeamColor (not Team()), and Rivals also
    // carries LocalPlayerTeamBrickColor through the lossless custom cache even
    // when the live Team() member is stripped for the non-local player.
    // Fall back to BrickColor when game-local Team members came back empty so
    // teamcheck keeps working in Rivals (the default "no-op" only happens when
    // ALL of Team, TeamName, and BrickColor are missing).
    // If TeamColor didn't match (or was neutral), try Team object comparison.
    // Note: Team pointers can be encrypted in some games, causing false negative matches.
    auto localTeam = Globals::Roblox::LocalPlayerTeam;
    auto playerTeam = player.Team;

    if (localTeam.address != 0 && playerTeam.address != 0)
    {
        // If team color is neutral, treat as FFA (enemies)
        if (Globals::Roblox::LocalPlayerTeamBrickColor == 194 || Globals::Roblox::LocalPlayerTeamBrickColor == 0 || Globals::Roblox::LocalPlayerTeamBrickColor == -1)
            return false;

        if (localTeam.address == playerTeam.address)
            return true;
            
        if (!Globals::Roblox::LocalPlayerTeamName.empty() && Globals::Roblox::LocalPlayerTeamName == player.TeamName)
            return true;
            
        if (Globals::Roblox::LocalPlayerTeamBrickColor != 0 && Globals::Roblox::LocalPlayerTeamBrickColor != 194 && Globals::Roblox::LocalPlayerTeamBrickColor != -1 && 
            Globals::Roblox::LocalPlayerTeamBrickColor == player.TeamBrickColor)
            return true;
            
        // If pointers were valid but didn't match anything, they are enemies.
        return false;
    }
    else if (localTeam.address != 0 || playerTeam.address != 0)
    {
        return false; // One has a team, the other doesn't
    }

    return false;
}

#include <algorithm>
#include <string>

inline RobloxInstance FindPlayerByName(const std::string& name)
{
    if (name.empty()) return RobloxInstance(0);
    
    auto playersService = Globals::Roblox::Players;
    if (!playersService.address) return RobloxInstance(0);
    
    std::string searchName = name;
    std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);
    
    for (auto& child : playersService.GetChildren())
    {
        std::string childName = child.Name();
        std::string lowerChildName = childName;
        std::transform(lowerChildName.begin(), lowerChildName.end(), lowerChildName.begin(), ::tolower);
        if (lowerChildName.find(searchName) != std::string::npos)
        {
            return child;
        }
    }
    return RobloxInstance(0);
}

inline RobloxInstance FindCharacterInWorkspace(const std::string& playerName)
{
    auto workspace = Globals::Roblox::Workspace;
    if (!workspace.address) return RobloxInstance(0);

    auto children = workspace.GetChildren();
    for (auto& child : children)
    {
        if (!child.address) continue;
        std::string childClass = child.Class();

        auto checkModel = [&](RobloxInstance& model) -> RobloxInstance
        {
            if (!model.address) return RobloxInstance(0);
            std::string modelName = model.Name();
            if (modelName != playerName) return RobloxInstance(0);
            auto humanoid = model.FindFirstChildWhichIsA("Humanoid");
            if (!humanoid.address) return RobloxInstance(0);
            auto hrp = model.FindFirstChild("HumanoidRootPart");
            if (!hrp.address) return RobloxInstance(0);
            return model;
        };

        if (childClass == "Model")
        {
            auto result = checkModel(child);
            if (result.address) return result;
        }

        if (childClass == "Folder" || childClass == "Model")
        {
            auto grandchildren = child.GetChildren();
            for (auto& gc : grandchildren)
            {
                if (!gc.address) continue;
                std::string gcClass = gc.Class();

                if (gcClass == "Model")
                {
                    auto result = checkModel(gc);
                    if (result.address) return result;
                }

                if (gcClass == "Folder" || gcClass == "Model")
                {
                    auto ggchildren = gc.GetChildren();
                    for (auto& ggc : ggchildren)
                    {
                        if (!ggc.address) continue;
                        if (ggc.Class() == "Model")
                        {
                            auto result = checkModel(ggc);
                            if (result.address) return result;
                        }
                    }
                }
            }
        }
    }
    return RobloxInstance(0);
}

struct FoundCharacter
{
    RobloxInstance model;
    RobloxInstance humanoid;
    RobloxInstance hrp;
};

inline std::vector<FoundCharacter> FindAllCharactersInWorkspace()
{
    std::vector<FoundCharacter> results;
    auto workspace = Globals::Roblox::Workspace;
    if (!workspace.address) return results;

    // Iterative walk with an explicit stack. This used to be a std::function
    // lambda that, per node, called GetChildren() TWICE - once for the loop and
    // again (line 841) purely to test for emptiness - and paid std::function
    // indirect-call overhead at every level. The whole Workspace is walked
    // here on a 500ms cadence, so the duplicate read of every child array was
    // tens of thousands of unnecessary syscalls per pass.
    std::vector<RobloxInstance> stack;
    stack.push_back(workspace);

    while (!stack.empty())
    {
        RobloxInstance inst = stack.back();
        stack.pop_back();
        if (!inst.address) continue;

        std::vector<RobloxInstance> children = inst.GetChildren();
        for (auto& child : children)
        {
            if (!child.address) continue;

            const std::string cls = child.Class();

            if (cls == "Model")
            {
                // Search the child list the walk already fetched instead of
                // re-reading the same child array twice more per Model.
                const auto kids = child.GetChildren();
                auto humanoid = child.FindFirstChildWhichIsAIn(kids, "Humanoid");
                auto hrp = child.FindFirstChildIn(kids, "HumanoidRootPart");
                // A Humanoid is NOT required here: some games (Blox Strike) drive
                // their characters with a custom controller and ship a rig with no
                // Humanoid at all, only a HumanoidRootPart. The callers handle a
                // null Humanoid (they fall back to the root part for position and
                // to a default health), so match on the root part instead.
                if (hrp.address)
                    results.push_back({ child, humanoid, hrp });
            }

            // Recurse via the stack. Only containers can hold a character rig;
            // descending into every Part's children is what made this walk the
            // expensive part of the player cache.
            if (cls == "Model" || cls == "Folder" || cls == "Workspace" ||
                cls == "Configuration" || cls == "Actor")
                stack.push_back(child);
        }
    }

    return results;
}

inline void PopulateCharacterFallbacks()
{
    auto& cache = Globals::Caches::CharacterFallbackCache;
    cache.clear();

    auto players = Globals::Roblox::Players.GetChildren();
    if (players.empty()) return;

    auto characters = FindAllCharactersInWorkspace();
    if (characters.empty()) return;

    std::vector<uintptr_t> playerAddrs;
    for (auto& p : players)
        playerAddrs.push_back(p.address);

    for (auto& ch : characters)
    {
        // Strategy 1: Scan character Model's children for an ObjectValue referencing a Player
        auto chChildren = ch.model.GetChildren();
        for (auto& child : chChildren)
        {
            if (!child.address) continue;
            std::string cls = child.Class();
            if (cls == "ObjectValue" || cls == "ValueBase")
            {
                uintptr_t val = Memory->read<uintptr_t>(child.address + 0x78);
                for (size_t i = 0; i < playerAddrs.size(); i++)
                {
                    if (val == playerAddrs[i] && !cache.count(playerAddrs[i]))
                    {
                        cache[playerAddrs[i]] = ch.model;
                        goto next_character;
                    }
                }
            }
        }

        // Strategy 2: Scan Humanoid memory for Player pointers
        for (uintptr_t off = 0x80; off <= 0x300; off += 0x8)
        {
            uintptr_t val = Memory->read<uintptr_t>(ch.humanoid.address + off);
            for (size_t i = 0; i < playerAddrs.size(); i++)
            {
                if (val == playerAddrs[i] && !cache.count(playerAddrs[i]))
                {
                    cache[playerAddrs[i]] = ch.model;
                    goto next_character;
                }
            }
        }

        // Strategy 3: Scan character Model's own memory for Player pointers
        for (uintptr_t off = 0x80; off <= 0x300; off += 0x8)
        {
            uintptr_t val = Memory->read<uintptr_t>(ch.model.address + off);
            for (size_t i = 0; i < playerAddrs.size(); i++)
            {
                if (val == playerAddrs[i] && !cache.count(playerAddrs[i]))
                {
                    cache[playerAddrs[i]] = ch.model;
                    goto next_character;
                }
            }
        }

    next_character:;
    }
}

inline RobloxInstance ResolveCharacterFallback(uintptr_t playerAddress)
{
    auto& cache = Globals::Caches::CharacterFallbackCache;
    auto it = cache.find(playerAddress);
    if (it != cache.end()) return it->second;
    return RobloxInstance(0);
}

// Looks for a Tool/HopperBin instance among the given instance's children (one
// level deep, plus a shallow recursion into folders) and returns its name.
inline bool IsWeaponLikeName(const std::string& name)
{
    if (name.empty())
        return false;
    std::string lower = name;
    for (auto& ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    static const char* kws[] = {
        "katana", "sword", "blade", "knife", "dagger",
        "rifle", "sniper", "bow", "uzi", "handgun", "pistol",
        "shotgun", "revolver", "minigun", "rpg", "crossbow",
        "pepper", "smg", "scar", "carbine", "ak-", "deagle",
        "vector", "mp5", "mp7", "p90", "fal", "g3", "m4a1",
        "m4", "ak47", "akm", "lmg", "desert", "grappling",
        "hammer", "axe", "bat", "tonfa", "stun", "gun",
        "trident", "lance", "staff", "reaper", "cutter"
    };
    for (auto kw : kws)
        if (lower.find(kw) != std::string::npos)
            return true;
    return false;
}

inline std::string FindToolName(const RobloxInstance& inst, int depth)
{
    if (inst.address == 0 || depth > 7)
        return "";

    // Fetch the child list once and derive each child's class once: the old
    // copy called GetChildren() twice and Class() up to twice per child, and
    // this walks every player's character/backpack on every cache rebuild.
    auto children = inst.GetChildren();

    for (auto& child : children)
    {
        const std::string cls = child.Class();
        if (cls == "Tool" || cls == "HopperBin")
            return child.Name();
    }

    // Recurse to find weapon-like containers (weapons in Rivals are Models,
    // often nested deeper inside Backpack folders/slots). Descend every child
    // class (not just folders/models) since the search is rooted at the
    // Character/Backpack subtrees, which stay small; the depth cap bounds it.
    // Pass order is unchanged: any direct Tool still wins over weapon-like
    // models and over anything found in a nested folder.
    for (auto& child : children)
    {
        const std::string cls = child.Class();
        if (cls == "Tool" || cls == "HopperBin")
            continue;
        if (cls == "Model" && IsWeaponLikeName(child.Name()))
            return child.Name();
        // Only containers can hold a Tool deeper down. Descending into every
        // body part's children (and their children) was the bulk of this
        // walk's reads, and a Part never contains a weapon.
        if (cls != "Folder" && cls != "Model" && cls != "Backpack" &&
            cls != "Accessory" && cls != "Actor" && cls != "Configuration")
            continue;
        auto inner = FindToolName(child, depth + 1);
        if (!inner.empty())
            return inner;
    }

    return "";
}

// Returns the local player's currently equipped tool name.
// Empty string if local player has no tool. Scans the live character (a couple
// levels deep) first, then falls back to the player's Backpack (Rivals keeps
// weapons there), and finally checks the cached player objects.
inline std::string GetLocalPlayerWeapon()
{
    if (Globals::Roblox::LocalPlayer.address == 0)
        return "";

    std::string found;

    // 1) Direct live scan of the local character for an equipped Tool.
    auto lChar = Globals::Roblox::LocalPlayer.Character();
    if (lChar.address)
        found = FindToolName(lChar, 0);

    // 2) Fall back to the player's Backpack (weapons live here in Rivals).
    if (found.empty())
    {
        auto backpack = Globals::Roblox::LocalPlayer.FindFirstChild("Backpack");
        if (backpack.address)
            found = FindToolName(backpack, 0);
    }

    // 3) Last resort: the cached player objects.
    if (found.empty())
    {
        for (const auto& player : Globals::Caches::FramePlayers())
        {
            if (player.address == Globals::Roblox::LocalPlayer.address)
            {
                found = player.ToolName;
                break;
            }
        }
    }

    return found;
}

// Case-insensitive substring match: returns true if 'hay' contains 'needle'.
inline bool CaseInsensitiveFind(const std::string& hay, const char* needle)
{
    if (!needle[0])
        return false;
    std::string lower = hay;
    for (auto& ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower.find(needle) != std::string::npos;
}

// True when any non-local cached player is currently holding a katana.
// Used to fully suppress firing (anti-katana): in Rivals the katana deflects
// bullets back at the shooter, so firing while an enemy katana is out is a
// self-kill. Iterates the same cached objects the ESP/aim systems use.
inline bool AnyRivalsKatanaUser()
{
    for (const auto& player : Globals::Caches::FramePlayers())
    {
        if (!player.address || player.address == Globals::Roblox::LocalPlayer.address)
            continue;
        if (IsHoldingKatana(player))
            return true;
    }
    return false;
}
