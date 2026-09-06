#pragma once
#include <windows.h>
#include <vector>

namespace MemoryStealth {

    void Initialize();
    HANDLE ManualMapDll(const std::vector<BYTE>& dllBytes);
    void HideModuleFromPeb(HANDLE moduleBase);
    void UnhookCriticalSyscalls();
    void MakeRegionRx(PVOID addr, SIZE_T size);
    std::vector<BYTE> LoadDllFromDisk(const wchar_t* path);

} // namespace Stealth