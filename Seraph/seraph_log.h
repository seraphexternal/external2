#pragma once
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <string>
#include <windows.h>
#include "rbx/globals/options.h"

// Debug trace helper. Always emits via OutputDebugStringA (visible only to a
// debugger); the file log is written only when the loader's "Debug Logging"
// toggle is enabled. Logs go to %LOCALAPPDATA%\Seraph\seraph_log.txt so the
// path is portable across machines/users.
inline std::string GetSeraphLogPath()
{
    char* env = nullptr;
    size_t sz = 0;
    std::string base = ".";
    if (_dupenv_s(&env, &sz, "LOCALAPPDATA") == 0 && env)
    {
        base = env;
        free(env);
    }
    std::string dir = base + "\\Seraph";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\seraph_log.txt";
}

inline void SeraphLog(const std::string& msg)
{
    OutputDebugStringA(msg.c_str());
    if (!Options::Misc::DebugLog)
        return;
    FILE* f = nullptr;
    fopen_s(&f, GetSeraphLogPath().c_str(), "a");
    if (f)
    {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        tm t{};
        localtime_s(&t, &tt);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", &t);
        fprintf(f, "[%s] %s\n", ts, msg.c_str());
        fclose(f);
    }
}
