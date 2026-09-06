#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Injector {

struct InjectResult {
    bool success = false;
    DWORD pid = 0;
    uintptr_t dllBase = 0;
    std::string error;
};

InjectResult InjectDLL(int targetPid, const std::vector<BYTE>& dllBytes);
InjectResult InjectDLLByName(const wchar_t* processName, const std::vector<BYTE>& dllBytes);
std::vector<BYTE> LoadDllFromDisk(const wchar_t* path);

int FindRobloxPID();
bool IsRobloxRunning();

typedef void(*LogCallback)(const std::string&);
void SetLogCallback(LogCallback cb);

} // namespace Injector