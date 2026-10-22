#pragma once
// Runtime Roblox client version detection.
//
// This is deliberately INDEPENDENT of Offsets::ClientVersion (the version the
// bundled offsets were dumped from). It reports what the user actually has
// installed / is running, so UI code can flag a version mismatch before the
// offsets silently go stale.

#include <Windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <processthreadsapi.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cctype>
#include <cwctype>

#include "../offsets.h"

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")

namespace RobloxVersion
{
    enum Status
    {
        Unknown   = 0, // nothing detected (no client installed)
        Matched   = 1, // live == Offsets::ClientVersion
        Mismatch  = 2  // live != Offsets::ClientVersion
    };

    inline std::string cached;
    inline int cachedStatus = Status::Unknown;
    inline bool cachedCheckDone = false;

    inline std::wstring WideLower(std::wstring in)
    {
        std::transform(in.begin(), in.end(), in.begin(),
            [](wchar_t c) { return (wchar_t)std::towlower(c); });
        return in;
    }

    // Pull a "version-xxxxxxxxxxxxxx" token out of a client path.
    inline std::string ExtractVersionToken(const std::wstring& path)
    {
        const std::wstring lower = WideLower(path);
        const size_t at = lower.find(L"version-");
        if (at == std::wstring::npos)
            return std::string();
        size_t end = at + 8; // past "version-"
        while (end < path.size() && path[end] != L'\\' && path[end] != L'/')
            end++;
        std::wstring tok = path.substr(at, end - at);
        std::string out(tok.begin(), tok.end());
        // A bare "version-" (no hash) is meaningless.
        if (tok.size() <= 8)
            return std::string();
        return out;
    }

    // 1. Exact answer: version folder of the RUNNING RobloxPlayerBeta.exe.
    inline bool DetectRunning(std::string& out)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return false;

        bool found = false;
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") != 0 &&
                    _wcsicmp(pe.szExeFile, L"RobloxPlayer.exe") != 0)
                    continue;

                HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (!proc)
                    continue;

                wchar_t path[MAX_PATH] = { 0 };
                DWORD len = MAX_PATH;
                if (QueryFullProcessImageNameW(proc, 0, path, &len))
                {
                    out = ExtractVersionToken(path);
                    if (!out.empty())
                        found = true;
                }
                CloseHandle(proc);
                if (found)
                    break;
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    // 2. Installed answer: the version the launcher is pinned to, stored in
    //    %LOCALAPPDATA%\Roblox\GlobalSettings.clientSettingsContent as
    //    { ... "version": "version-xxxxxxxxxxxxxxxx", ... }.
    inline bool DetectSettingsFile(std::string& out)
    {
        wchar_t localAppData[MAX_PATH] = { 0 };
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) != S_OK)
            return false;

        std::filesystem::path file =
            (std::filesystem::path(localAppData) / L"Roblox" / L"GlobalSettings.clientSettingsContent");

        std::error_code ec;
        if (!std::filesystem::exists(file, ec))
            return false;

        std::ifstream f(file, std::ios::binary);
        if (!f.is_open())
            return false;

        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.empty())
            return false;

        const std::string key = "\"version\":\"";
        const size_t at = content.find(key);
        if (at == std::string::npos)
            return false;

        const size_t start = at + key.size();
        const size_t end = content.find('"', start);
        if (end == std::string::npos || end <= start)
            return false;

        const std::string ver = content.substr(start, end - start);
        if (ver.rfind("version-", 0) != 0)
            return false;
        if (ver.size() <= 8)
            return false;

        out = ver;
        return true;
    }

    // 3. Fallback: newest installed version dirtree (the folder name IS the
    //    version token). Prefers directories that actually contain the player.
    inline bool DetectVersionsDir(std::string& out)
    {
        wchar_t localAppData[MAX_PATH] = { 0 };
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) != S_OK)
            return false;

        std::filesystem::path versionsDir =
            (std::filesystem::path(localAppData) / L"Roblox" / L"Versions");

        std::error_code ec;
        if (!std::filesystem::exists(versionsDir, ec))
            return false;

        std::filesystem::path best;
        auto bestTime = std::filesystem::file_time_type();
        bool haveBest = false;

        for (const auto& entry : std::filesystem::directory_iterator(versionsDir, ec))
        {
            if (!entry.is_directory(ec))
                continue;
            const std::string tok = ExtractVersionToken(entry.path().wstring());
            if (tok.empty() || tok.size() <= 8)
                continue;
            const std::filesystem::path player = entry.path() / L"RobloxPlayerBeta.exe";
            if (!std::filesystem::exists(player, ec))
                continue;
            auto ft = std::filesystem::last_write_time(player, ec);
            if (!haveBest || ft > bestTime)
            {
                best = player;
                bestTime = ft;
                haveBest = true;
            }
        }

        if (!haveBest)
            return false;

        out = ExtractVersionToken(best.wstring());
        return !out.empty();
    }

    inline void EnsureCached()
    {
        if (cachedCheckDone)
            return;

        std::string det;
        if (DetectRunning(det) || DetectSettingsFile(det) || DetectVersionsDir(det))
        {
            cached = det;
            cachedStatus = (det == Offsets::ClientVersion) ? Status::Matched : Status::Mismatch;
        }
        else
        {
            cached.clear();
            cachedStatus = Status::Unknown;
        }
        cachedCheckDone = true;
    }

    // Actual client version string ("version-xxxxxxxxxxxx") or "" if none found.
    inline const std::string& GetClientVersion()
    {
        EnsureCached();
        return cached;
    }

    // 0 = unknown, 1 = matches bundled offsets, 2 = mismatch (outdated offsets).
    inline int GetStatus()
    {
        EnsureCached();
        return cachedStatus;
    }

    inline bool IsEnabled()
    {
        return GetStatus() != Status::Unknown;
    }

    inline void Invalidate()
    {
        cachedCheckDone = false;
        cached.clear();
        cachedStatus = Status::Unknown;
    }
}