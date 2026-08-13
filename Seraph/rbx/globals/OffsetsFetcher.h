#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <windows.h>
#include <wininet.h>
#include <thread>
#pragma comment(lib, "wininet.lib")

#include "../offsets.h"            // Offsets::SetOffset, Offsets::ClientVersion
#include "../configs/json.hpp"            // nlohmann::json

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

            HINTERNET hInet = InternetOpen("Seraph/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                NULL, NULL, 0);
            if (!hInet) {
                lastError = "InternetOpen failed (err=" + std::to_string(GetLastError()) + ")";
                return {};
            }

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
            // Best-effort diagnostic log so the user can see what the fetch did.
            std::ofstream f("C:\\Users\\ncomp\\AppData\\Local\\Temp\\seraph_offsets.log", std::ios::out | std::ios::app);
            if (f) f << msg << "\n";
        }
    }

    // Fetch a dump (nested JSON) from `url`.
    //   force=false -> only fill offsets that are still zero (never clobber a dumped value)
    //   force=true  -> overwrite every matching offset
    //   versionGated=true -> force is only applied when the fetched version differs
    //                        from the bundled one (i.e. Roblox likely updated)
    inline bool FetchAndApply(const std::string& url, bool force, bool versionGated = false)
    {
        lastUrl = url;
        std::string body = detail::HttpGet(url);
        if (body.empty()) {
            lastFetchStatus = "skip: fetch failed (" + lastError + ")";
            loaded = false;
            detail::Log("[OffsetsFetcher] " + lastFetchStatus + " url=" + url);
            return false;
        }

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

    // Fill any offsets the dump left at zero, using the bundled client version.
    inline void FetchBundledFill()
    {
        std::string ver = Offsets::ClientVersion;
        if (ver.empty()) { lastFetchStatus = "skip: no bundled ClientVersion"; return; }
        std::string url = "https://offsets.imtheo.lol/" + ver + "/offsets.json";
        FetchAndApply(url, false);
    }

    // Override with the live/latest offsets -- only when they differ from the
    // bundled version (Roblox likely updated). Survives client updates.
    inline void FetchLatestOverride()
    {
        std::string url = "https://offsets.imtheo.loll/offsets.json";
        url = "https://offsets.imtheo.lol/offsets.json";
        FetchAndApply(url, true, true);
    }

    inline void FetchAll()
    {
        FetchBundledFill();
        FetchLatestOverride();
    }

    inline void Reset()
    {
        loaded = false;
        overridesApplied = 0;
        overridesSkipped = 0;
        lastFetchStatus = "(reset)";
    }

    // Auto-trigger: when this header is compiled into the binary (main.cpp),
    // kick off a non-blocking fetch on a background thread at load time.
    // Best-effort only: a failure here must never prevent the cheat from loading.
    namespace {
        struct AutoReg {
            AutoReg() {
                try {
                    std::thread([]() { OffsetsFetcher::FetchAll(); }).detach();
                } catch (...) {
                    // background fetch is optional; ignore spawn failures
                }
            }
        } g_autoReg;
    }
}
