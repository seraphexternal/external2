#pragma once
#include <windows.h>
#include <string>
#include <filesystem>
#include <vector>
#include <shellapi.h>
#include <tlhelp32.h>
#include "../rbx/globals/options.h"

// Convert a UTF-8/ANSI char buffer to a wide string (used for process names, etc.)
inline std::wstring ToWide(const char* s)
{
    if (!s || !s[0]) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);
    return out;
}

// -----------------------------------------------------------------------------
// Stealth + trace-cleaning helpers.
//
// HideProcess: external cheats cannot rename a running process in memory, so the
// standard technique is to copy our own executable to a benign-named file in
// %TEMP% and re-launch from there, then exit. The spawned copy is the "hidden"
// process (and is removed by WipeTempTraces on exit). The window class/title are
// also spoofed to something non-descript.
//
// WipeTempTraces: deletes our own temp artifacts. Anything under
// Options::Misc::ExclusionPath is preserved (the "Silent Exclusion" folder).
// -----------------------------------------------------------------------------

namespace Stealth
{
    inline bool g_Relaunched = false;

    // Returns the benign temp path we would spawn from, e.g.
    // C:\Users\...\AppData\Local\Temp\RuntimeBroker.exe
    inline std::wstring GetRelaunchPath()
    {
        wchar_t tmp[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, tmp);
        std::wstring name = L"RuntimeBroker";
        if (Options::Misc::ProcessName[0])
        {
            name = ToWide(Options::Misc::ProcessName);
            // strip a trailing .exe if the user typed one
            if (name.size() >= 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0)
                name.resize(name.size() - 4);
        }
        return std::wstring(tmp) + name + L".exe";
    }

    // Terminate any stale instance of ours still running from the relaunch path
    // (a previous copy that was killed instead of exiting cleanly). We match on
    // the exact full image path so system RuntimeBroker processes are untouched.
    inline void KillStaleRelaunch()
    {
        std::wstring dest = GetRelaunchPath();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe = { sizeof(pe) };
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (!h) continue;
            wchar_t path[MAX_PATH] = { 0 };
            DWORD len = MAX_PATH;
            BOOL got = QueryFullProcessImageNameW(h, 0, path, &len);
            if (got && _wcsicmp(path, dest.c_str()) == 0)
                TerminateProcess(h, 1);
            CloseHandle(h);
        }
        CloseHandle(snap);
    }

    // Copy current exe to the relaunch path and spawn it, then exit this process.
    inline void RelaunchAsRenamed()
    {
        if (!Options::Misc::HideProcess)
            return;
        if (g_Relaunched)
            return;

        wchar_t selfPath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, selfPath, MAX_PATH);

        std::wstring dest = GetRelaunchPath();

        // Already running from the renamed temp copy.
        if (_wcsicmp(selfPath, dest.c_str()) == 0)
        {
            g_Relaunched = true;
            return;
        }

        // Kill any stale temp copy still running from a previous session, then
        // always overwrite the file so a leftover copy never blocks relaunch.
        KillStaleRelaunch();

        if (!CopyFileW(selfPath, dest.c_str(), FALSE))
            return; // copy failed -> just run as-is (still functional)

        // Re-launch with a clean command line so Task Manager's "Command Line"
        // column shows only the disguised image name (no original path leaks).
        std::wstring cmdLineName = dest.substr(dest.find_last_of(L"\\/") + 1);
        wchar_t cmdLineBuf[MAX_PATH] = { 0 };
        wcsncpy_s(cmdLineBuf, cmdLineName.c_str(), _TRUNCATE);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        if (CreateProcessW(dest.c_str(), cmdLineBuf, nullptr, nullptr,
            FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            ExitProcess(0); // terminate the original Seraph.exe instance
        }
        // If spawn failed, fall through and run normally.
    }

    // Remove our renamed temp copy (called on clean exit).
    inline void RemoveRelaunchCopy()
    {
        if (!g_Relaunched)
            return;
        std::error_code ec;
        std::filesystem::remove(GetRelaunchPath(), ec);
    }

    // Delete trace files we created in %TEMP% and LocalAppData, skipping the
    // user's exclusion folder. Conservative: only removes files/dirs that look
    // like our artifacts by name prefix to avoid touching unrelated data.
    inline void WipeTempTraces()
    {
        const wchar_t* prefixes[] = { L"Seraph", L"sourcestackz" };
        auto tryWipeDir = [&](const std::wstring& root)
        {
            std::error_code ec;
            if (!std::filesystem::exists(root, ec)) return;
            std::wstring excl = ToWide(Options::Misc::ExclusionPath);
            for (const auto& ent : std::filesystem::directory_iterator(root, ec))
            {
                std::wstring p = ent.path().wstring();
                if (!excl.empty() && p.find(excl) == 0)
                    continue; // excluded
                bool match = false;
                for (const auto* pre : prefixes)
                {
                    std::wstring fname = ent.path().filename().wstring();
                    if (fname.rfind(pre, 0) == 0) { match = true; break; }
                }
                if (!match) continue;
                if (ent.is_directory(ec))
                    std::filesystem::remove_all(ent.path(), ec);
                else
                    std::filesystem::remove(ent.path(), ec);
            }
        };

        wchar_t tmp[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, tmp);
        tryWipeDir(std::wstring(tmp));

        wchar_t lad[MAX_PATH] = { 0 };
        SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, lad);
        tryWipeDir(std::wstring(lad));

        RemoveRelaunchCopy();
    }
}
