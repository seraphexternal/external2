#pragma once
#include <cstdint>
#include <windows.h>

namespace BYOVD {

    bool Open(const wchar_t* devicePath = L"\\\\.\\byovd");
    void Close();
    bool IsOpen();

    bool KRead(uint64_t addr, void* buffer, size_t size);
    bool KWrite(uint64_t addr, const void* buffer, size_t size);
    bool KMemcpy(uint64_t dst, uint64_t src, size_t size);

    void WipeKernelCallbacks();

    bool KernelReadProcessMemory(HANDLE pid, uint64_t addr, void* buffer, size_t size);
    bool KernelWriteProcessMemory(HANDLE pid, uint64_t addr, const void* buffer, size_t size);

    bool HidePhysicalPages(uint64_t va, size_t size);

} // namespace BYOVD