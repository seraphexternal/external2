#pragma once
// Roblox client upkeep.
//
// Seraph is an external reader, so it can only work against offsets that match
// the Roblox build the user actually has. This header owns the three things
// needed to keep that true without a manual step:
//
//   * the LIVE client version, straight from offsets.imtheo.lol/roblox/version;
//   * launching Roblox when it is not already running;
//   * bringing an out-of-date install up to date by running the Roblox
//     installer -- a copy already on disk if there is one, otherwise the
//     per-version installer fetched from Roblox's own CDN.
//
// Everything here is best-effort. Nothing throws out, and no failure may stop
// the cheat from loading: at worst the user is left on the version they had.

#include <Windows.h>
#include <wininet.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cctype>

#include "RobloxVersion.h"
#include "options.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

namespace RobloxClient
{
    namespace fs = std::filesystem;

    namespace detail
    {
        inline std::wstring ToWide(const std::string& s)
        {
            if (s.empty()) return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            std::wstring w; w.resize(n > 0 ? n : 0);
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }

        inline std::string Trim(const std::string& s)
        {
            std::size_t a = 0, b = s.size();
            while (a < b && (unsigned char)s[a] <= ' ') ++a;
            while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
            return s.substr(a, b - a);
        }

        inline std::string Env(const char* name)
        {
            char buf[32768] = { 0 };
            const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
            if (n == 0 || n >= sizeof(buf)) return {};
            return std::string(buf, n);
        }

        // Minimal HTTPS GET. WinINet rather than WinHTTP because the rest of the
        // project already links it and it picks up the system proxy and TLS
        // configuration for free. Fails closed on a non-200 so a proxy error page
        // is never mistaken for a payload.
        inline bool HttpGet(const std::wstring& url, std::string& out, DWORD timeoutMs = 12000)
        {
            out.clear();
            HINTERNET hInet = InternetOpenW(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
            if (!hInet) return false;

            if (timeoutMs)
            {
                InternetSetOptionW(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
                InternetSetOptionW(hInet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
                InternetSetOptionW(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
            }

            HINTERNET hUrl = InternetOpenUrlW(hInet, url.c_str(), nullptr, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
            if (!hUrl) { InternetCloseHandle(hInet); return false; }

            DWORD status = 0, sz = sizeof(status);
            const bool ok = HttpQueryInfoW(hUrl,
                HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &sz, nullptr) && status == 200;

            if (ok)
            {
                char buf[16384]; DWORD read = 0;
                while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
                    out.append(buf, read);
            }

            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInet);
            return ok && !out.empty();
        }
    }  // namespace detail

    // ── live client version ──────────────────────────────────────────────
    // The version offsets.imtheo.lol is currently dumping for. Fetched lazily
    // and cached for the life of the process: the answer cannot change faster
    // than a Roblox release, and the wait would otherwise be paid twice.
    inline const std::string& GetLiveVersion()
    {
        static const std::string version = []() -> std::string {
            std::string body;
            if (detail::HttpGet(L"https://offsets.imtheo.lol/roblox/version", body, 8000))
            {
                const std::string v = detail::Trim(body);
                if (v.rfind("version-", 0) == 0 && v.size() > 8)
                    return v;
            }
            return {};
        }();
        return version;
    }

    // ── process / install discovery ──────────────────────────────────────

    inline bool IsRobloxRunning()
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;

        bool found = false;
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") == 0 ||
                    _wcsicmp(pe.szExeFile, L"RobloxPlayer.exe") == 0)
                {
                    found = true;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    // Newest installed player binary. The Versions folder keeps every build the
    // user has ever run, so "the client" is whichever one Roblox most recently
    // wrote, not merely the first directory that happens to contain a player.
    inline bool FindPlayerExe(std::wstring& out)
    {
        const std::string lad = detail::Env("LOCALAPPDATA");
        if (lad.empty()) return false;

        const fs::path versions = fs::path(detail::ToWide(lad)) / L"Roblox" / L"Versions";
        std::error_code ec;
        if (!fs::is_directory(versions, ec)) return false;

        fs::path best;
        fs::file_time_type bestTime{};
        for (const auto& entry : fs::directory_iterator(versions, ec))
        {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;

            const fs::path player = entry.path() / L"RobloxPlayerBeta.exe";
            if (!fs::is_regular_file(player, ec)) continue;

            const auto ft = fs::last_write_time(player, ec);
            if (best.empty() || ft > bestTime)
            {
                best     = player;
                bestTime = ft;
            }
        }

        if (best.empty()) return false;
        out = best.wstring();
        return true;
    }

    // ── launching ────────────────────────────────────────────────────────

    inline bool LaunchRoblox()
    {
        std::wstring exe;
        if (!FindPlayerExe(exe)) return false;

        std::wstring cmd = L"\"" + exe + L"\"";
        STARTUPINFOW        si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }

        // Direct launch refused: fall back to the protocol the website itself
        // uses to open the app on a machine whose layout we misread.
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"roblox://", nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(r) > 32;
    }

    // Start Roblox when it is not up yet. Never starts a second copy: the client
    // holds a single-instance mutex, but a redundant spawn would still leave the
    // user staring at nothing while the first copy finishes loading.
    inline bool EnsureRunning()
    {
        if (IsRobloxRunning()) return true;

        const bool ok = LaunchRoblox();
        OutputDebugStringA(ok ? "[RobloxClient] Roblox not running, launched it\n"
                              : "[RobloxClient] Roblox not running and could not be launched\n");
        return ok;
    }

    // ── updating an out-of-date install ──────────────────────────────────

    // Candidate installer locations, best first. A package that ships one next
    // to the cheat wins; the rest are the places a user would already have
    // downloaded it to, so a normal install never needs the download below.
    inline std::string FindInstaller()
    {
        std::vector<fs::path> cands;

        wchar_t mod[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(nullptr, mod, MAX_PATH))
            cands.push_back(fs::path(mod).parent_path() / L"RobloxPlayerInstaller.exe");

        char tmp[MAX_PATH] = { 0 };
        if (GetTempPathA(MAX_PATH, tmp))
            cands.push_back(fs::path(tmp) / L"RobloxPlayerInstaller.exe");

        const std::string profile = detail::Env("USERPROFILE");
        if (!profile.empty())
            cands.push_back(fs::path(detail::ToWide(profile)) / L"Downloads" / L"RobloxPlayerInstaller.exe");

        const std::string lad = detail::Env("LOCALAPPDATA");
        if (!lad.empty())
            cands.push_back(fs::path(detail::ToWide(lad)) / L"Roblox" / L"RobloxPlayerInstaller.exe");

        std::error_code ec;
        for (const auto& c : cands)
            if (fs::is_regular_file(c, ec)) return c.string();
        return {};
    }

    // Fetch the exact installer for `liveVersion` from Roblox's own CDN and
    // drop it in %TEMP%. Used only when the machine has no installer at all, so
    // an ordinary install never pays for the download.
    inline std::string DownloadInstaller(const std::string& liveVersion)
    {
        if (liveVersion.empty()) return {};

        char tmp[MAX_PATH] = { 0 };
        if (GetTempPathA(MAX_PATH, tmp) == 0) return {};

        const std::string url = "https://setup.rbxcdn.com/" + liveVersion + "-RobloxPlayerInstaller.exe";
        std::string body;
        if (!detail::HttpGet(detail::ToWide(url), body, 60000) || body.size() < 4096)
        {
            OutputDebugStringA("[RobloxClient] installer download failed\n");
            return {};
        }

        const std::string dest = std::string(tmp) + "RobloxPlayerInstaller.exe";
        std::ofstream f(dest, std::ios::binary | std::ios::trunc);
        if (!f) return {};
        f.write(body.data(), (std::streamsize)body.size());
        f.close();
        return dest;
    }

    // Run the installer and wait for it, so the version read back afterwards is
    // the new one. Bounded on purpose: an installer that sits on a prompt must
    // not hold the cheat hostage forever.
    inline bool RunInstaller(const std::string& installerPath, DWORD timeoutMs = 240000)
    {
        const std::wstring w   = detail::ToWide(installerPath);
        const std::wstring dir = fs::path(w).parent_path().wstring();

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask       = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb      = L"open";
        sei.lpFile      = w.c_str();
        sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
        sei.nShow       = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei)) return false;

        if (sei.hProcess)
        {
            WaitForSingleObject(sei.hProcess, timeoutMs);
            CloseHandle(sei.hProcess);
        }
        return true;
    }

    // ── one-shot startup upkeep ──────────────────────────────────────────

    // Bring the installed client up to the live version if it is behind, and
    // describe what happened for the debug log. Call once, before the attach
    // loop; it never throws and never blocks on anything unbounded.
    //
    // Skipped entirely while Roblox is running: the client cannot be replaced
    // underneath a live session, and a user mid-game wants the cheat, not an
    // installer.
    inline std::string EnsureClientLatest()
    {
        if (!Options::Misc::AutoUpdateRoblox) return "roblox update: disabled";
        if (IsRobloxRunning())                return "roblox update: client already running";

        RobloxVersion::Invalidate();
        const std::string installed = RobloxVersion::GetClientVersion();
        const std::string live      = GetLiveVersion();

        if (live.empty())
            return "roblox update: live version unavailable";
        if (!installed.empty() && installed == live)
            return "roblox update: already latest (" + live + ")";

        std::string installer = FindInstaller();
        if (installer.empty())
            installer = DownloadInstaller(live);
        if (installer.empty())
            return "roblox update: no installer available for " + live;

        const bool ran = RunInstaller(installer);

        RobloxVersion::Invalidate();
        const std::string after = RobloxVersion::GetClientVersion();
        return std::string("roblox update: ") + (ran ? "installer ran, now " : "installer failed, still ") +
               (after.empty() ? std::string("<unknown>") : after);
    }
}  // namespace RobloxClient
