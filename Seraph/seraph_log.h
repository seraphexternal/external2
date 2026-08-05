#pragma once
#include <cstdio>
#include <chrono>
#include <string>
#include <windows.h>

// File + debugger trace helper. Appends a timestamped line to
// C:\Users\ncomp\seraph_log.txt (fixed path regardless of stealth rename)
// and also emits it via OutputDebugStringA.
inline void SeraphLog(const std::string& msg)
{
    FILE* f = nullptr;
    fopen_s(&f, "C:\\Users\\ncomp\\seraph_log.txt", "a");
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
    OutputDebugStringA(msg.c_str());
}
