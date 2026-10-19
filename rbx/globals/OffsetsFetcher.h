#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <windows.h>
#include <wininet.h>
#include <thread>
#pragma comment(lib, "wininet.lib")

#include "../offsets.h"            // Offsets::SetOffset, Offsets::ClientVersion
#include "../configs/json.hpp"            // nlohmann::json
#include "../../features/obfuscate.h"
#include "../../rbx/globals/options.h"
#include "RobloxVersion.h"
#include "RobloxClient.h"          // live version, launch, client upkeep

using json = nlohmann::json;

namespace OffsetsFetcher {

    // Public state (mirrors original fetcher surface)
    inline std::string lastUrl;
    inline std::string lastError;
    inline std::string fetchedClientVersion;
    inline std::size_t overridesApplied = 0;     // offsets actually written
    inline std::size_t overridesSkipped = 0;     // already set / unknown
    inline bool loaded = false;
    inline std::string lastFetchStatus = "(uninitialized)";

    namespace detail {

        inline std::wstring ToWide(const std::string& s)
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            std::wstring w; w.resize(n > 0 ? n : 0);
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }

        // Simple HTTPS GET via WinINet (InternetOpenUrl handles full URLs and
        // system proxy/TLS automatically -- avoids the WinHttpCrackUrl path that
        // was rejecting the URLs). Logs the underlying Win32 error on failure.
        inline std::string HttpGet(const std::string& urlUtf8)
        {
            std::wstring url = ToWide(urlUtf8);
            std::string result;

            HINTERNET hInet = InternetOpen("Mozilla/5.0 (Windows NT 10.0; Win64; x64)", INTERNET_OPEN_TYPE_PRECONFIG,
                NULL, NULL, 0);
            if (!hInet) {
                lastError = "InternetOpen failed (err=" + std::to_string(GetLastError()) + ")";
                return {};
            }

            // Bounded waits: an unreachable host must not stall startup, and the
            // fetch now runs before the attach loop rather than in the dark.
            DWORD timeoutMs = 8000;
            InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
            InternetSetOption(hInet, INTERNET_OPTION_SEND_TIMEOUT,    &timeoutMs, sizeof(timeoutMs));
            InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

            HINTERNET hUrl = InternetOpenUrl(hInet, urlUtf8.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
            if (!hUrl) {
                lastError = "InternetOpenUrl failed (err=" + std::to_string(GetLastError()) + ")";
                InternetCloseHandle(hInet);
                return {};
            }

            char buffer[4096];
            DWORD dwRead = 0;
            while (InternetReadFile(hUrl, buffer, sizeof(buffer), &dwRead) && dwRead > 0)
                result.append(buffer, dwRead);

            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInet);
            if (result.empty())
                lastError = "InternetReadFile returned empty (err=" + std::to_string(GetLastError()) + ")";
            return result;
        }

        inline void Log(const std::string& msg)
        {
            // Best-effort diagnostic log, only written when Debug Logging is
            // enabled. Uses a runtime %TEMP% path (no hardcoded home dir / no
            // identifying filename) so it leaves no username trace by default.
            if (!Options::Misc::DebugLog)
                return;
            char tmp[MAX_PATH] = { 0 };
            if (GetTempPathA(MAX_PATH, tmp) == 0)
                return;
            std::ofstream f(std::string(tmp) + SX("offs_diag.log"), std::ios::out | std::ios::app);
            if (f) f << msg << "\n";
        }
    }

    // Apply a dump (nested JSON) that has already been fetched.
    //   force=false -> only fill offsets that are still zero (never clobber a dumped value)
    //   force=true  -> overwrite every matching offset
    //   versionGated=true -> force is only applied when the fetched version differs
    //                        from the bundled one (i.e. Roblox likely updated)
    inline bool ApplyJson(const std::string& body, const std::string& url,
                          bool force, bool versionGated = false)
    {
        lastUrl = url;

        try {
            json j = json::parse(body);
            std::string fetchedVer = j.value("Roblox Version", std::string());
            fetchedClientVersion = fetchedVer;

            bool effectiveForce = force;
            if (versionGated)
                effectiveForce = (fetchedVer != Offsets::ClientVersion);

            if (!j.contains("Offsets") || !j["Offsets"].is_object()) {
                lastFetchStatus = "skip: no Offsets object in response";
                loaded = false;
                detail::Log("[OffsetsFetcher] " + lastFetchStatus + " url=" + url);
                return false;
            }

            const auto& offs = j["Offsets"];
            std::size_t applied = 0, skipped = 0;
            for (auto it = offs.begin(); it != offs.end(); ++it)
            {
                const std::string& ns = it.key();
                const auto& nsObj = it.value();
                if (!nsObj.is_object()) continue;
                for (auto jt = nsObj.begin(); jt != nsObj.end(); ++jt)
                {
                    const std::string& name = jt.key();
                    if (!jt.value().is_number()) continue;
                    uintptr_t v = (uintptr_t)jt.value().get<long long>();
                    if (Offsets::SetOffset(ns, name, v, effectiveForce)) ++applied; else ++skipped;
                }
            }

            overridesApplied = applied;
            overridesSkipped = skipped;
            loaded = applied > 0;
            lastFetchStatus = std::string(effectiveForce ? "override" : "fill") + ": wrote " + std::to_string(applied) +
                " / skipped " + std::to_string(skipped) + " (fetched " + fetchedVer +
                " bundled " + Offsets::ClientVersion + ")";
            detail::Log("[OffsetsFetcher] " + lastFetchStatus);
            return loaded;
        }
        catch (...) {
            lastFetchStatus = "skip: JSON parse error";
            loaded = false;
            detail::Log("[OffsetsFetcher] " + lastFetchStatus + " url=" + url);
            return false;
        }
    }

    // Fetch a dump from `url` and apply it (see ApplyJson for the flags).
    inline bool FetchAndApply(const std::string& url, bool force, bool versionGated = false)
    {
        std::string body = detail::HttpGet(url);
        if (body.empty()) {
            lastFetchStatus = "skip: fetch failed (" + lastError + ")";
            loaded = false;
            detail::Log("[OffsetsFetcher] " + lastFetchStatus + " url=" + url);
            return false;
        }
        return ApplyJson(body, url, force, versionGated);
    }

    // ── on-disk cache ────────────────────────────────────────────────────
    //
    // A dump that was right for a build stays right for that build, so the last
    // good one is kept next to the configs. It is applied before the network is
    // touched, which is what lets the cheat work with no connection at all --
    // and what stops a fetch outage from leaving offsets that are simply wrong
    // for the client that is running.
    namespace cache
    {
        inline std::string Dir()
        {
            char base[32768] = { 0 };
            if (GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base)) == 0)
                return {};
            const std::string root = std::string(base) + "\\Seraph";
            const std::string dir  = root + "\\offsets";
            CreateDirectoryA(root.c_str(), nullptr);
            CreateDirectoryA(dir.c_str(), nullptr);
            return dir;
        }

        inline std::string JsonPath() { const std::string d = Dir(); return d.empty() ? std::string() : d + "\\offsets.json"; }
        inline std::string VerPath()  { const std::string d = Dir(); return d.empty() ? std::string() : d + "\\offsets.version"; }

        inline std::string ReadAll(const std::string& path)
        {
            if (path.empty()) return {};
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        inline void WriteAll(const std::string& path, const std::string& data)
        {
            if (path.empty()) return;
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (f) f.write(data.data(), (std::streamsize)data.size());
        }

        inline void Save(const std::string& body, const std::string& version)
        {
            if (body.empty() || version.empty()) return;
            WriteAll(JsonPath(), body);
            WriteAll(VerPath(), version);
        }

        inline std::string CachedVersion()
        {
            std::string v = ReadAll(VerPath());
            std::size_t a = 0, b = v.size();
            while (a < b && (unsigned char)v[a] <= ' ') ++a;
            while (b > a && (unsigned char)v[b - 1] <= ' ') --b;
            return v.substr(a, b - a);
        }

        // Only apply the cache when it belongs to the build the user is on.
        // Anything else would write another version's offsets over the bundled
        // ones and quietly corrupt every read the cheat makes.
        inline bool Load()
        {
            const std::string body = ReadAll(JsonPath());
            const std::string ver  = CachedVersion();
            if (body.empty() || ver.empty()) return false;

            const std::string live = RobloxVersion::GetClientVersion();
            if (!live.empty() && ver != live) {
                detail::Log("[OffsetsFetcher] cache skipped: cached " + ver + " live " + live);
                return false;
            }
            if (live.empty() && ver != Offsets::ClientVersion) {
                detail::Log("[OffsetsFetcher] cache skipped: cached " + ver + " (no live client)");
                return false;
            }
            return ApplyJson(body, "(cache) " + ver, true, false);
        }
    }

    // Fill any offsets the dump left at zero, using the bundled client version.
    inline void FetchBundledFill()
    {
        std::string ver = Offsets::ClientVersion;
        if (ver.empty()) { lastFetchStatus = "skip: no bundled ClientVersion"; return; }
        std::string url = "https://offsets.imtheo.lol/" + ver + "/offsets.json";
        FetchAndApply(url, false);
    }

    // Override with the dump for the version the user is actually running or
    // has installed. RobloxVersion detects the live client independently of
    // the bundled offsets, and the per-version endpoint serves the exact dump
    // for it -- this takes priority over both the bundled and latest dumps.
    //
    // On success the bundled ClientVersion is repointed at the dump just
    // applied. It is no longer what the offsets belong to, and leaving it stale
    // would make every version check report a mismatch the cheat has already
    // resolved.
    inline bool FetchLiveVersionOverride()
    {
        const std::string live = RobloxVersion::GetClientVersion();
        if (live.empty()) {
            lastFetchStatus = "skip: no live Roblox version detected";
            detail::Log("[OffsetsFetcher] " + lastFetchStatus);
            return false;
        }

        const std::string url = "https://offsets.imtheo.lol/" + live + "/offsets.json";
        std::string body = detail::HttpGet(url);
        if (body.empty()) {
            lastFetchStatus = "skip: fetch failed (" + lastError + ")";
            loaded = false;
            detail::Log("[OffsetsFetcher] " + lastFetchStatus + " url=" + url);
            return false;
        }

        if (!ApplyJson(body, url, true, false) || fetchedClientVersion.empty())
            return false;

        Offsets::ClientVersion = fetchedClientVersion;
        RobloxVersion::Invalidate();
        cache::Save(body, fetchedClientVersion);
        return true;
    }

    // Override with the live/latest offsets -- only when they differ from the
    // bundled version (Roblox likely updated). Survives client updates, and is
    // the last resort when no per-version dump could be had at all.
    inline void FetchLatestOverride()
    {
        const std::string url = "https://offsets.imtheo.lol/offsets.json";
        std::string body = detail::HttpGet(url);
        if (body.empty()) {
            lastFetchStatus = "skip: fetch failed (" + lastError + ")";
            detail::Log("[OffsetsFetcher] " + lastFetchStatus + " url=" + url);
            return;
        }
        if (ApplyJson(body, url, true, true) && !fetchedClientVersion.empty())
            cache::Save(body, fetchedClientVersion);
    }

    inline void FetchAll()
    {
        cache::Load();
        FetchBundledFill();
        FetchLiveVersionOverride();
        // The latest dump force-overwrites every offset, so it must never run
        // after a successful per-version fetch -- it would clobber the exact
        // values with whatever version the dumper last ran against. Only fall
        // back to it when no live per-version dump could be fetched.
        if (!loaded)
            FetchLatestOverride();
    }

    inline void Reset()
    {
        loaded = false;
        overridesApplied = 0;
        overridesSkipped = 0;
        lastFetchStatus = "(reset)";
    }

    // Ordered one-shot startup path. Call once from main(), before the attach
    // loop: the client has to be up to date and running before its version can
    // be read, and that version has to be known before the offsets are fetched
    // -- otherwise the cheat applies a dump for a build that is about to be
    // replaced under it.
    //
    // Best-effort throughout. A failure at any step leaves the bundled offsets
    // in place and must never stop the cheat from loading.
    inline void Bootstrap()
    {
        try {
            if (Options::Misc::AutoUpdateRoblox) {
                const std::string status = RobloxClient::EnsureClientLatest();
                OutputDebugStringA(("[OffsetsFetcher] " + status + "\n").c_str());
                detail::Log("[OffsetsFetcher] " + status);
            }

            if (Options::Misc::AutoLaunchRoblox)
                RobloxClient::EnsureRunning();

            if (Options::Misc::AutoUpdateOffsets) {
                // Off the render thread. The attach sequence waits on Roblox for
                // at least a second, which is longer than the fetch takes.
                std::thread([]() { OffsetsFetcher::FetchAll(); }).detach();
            }
        } catch (...) {
            // upkeep is optional; ignore anything it throws
        }
    }
}
