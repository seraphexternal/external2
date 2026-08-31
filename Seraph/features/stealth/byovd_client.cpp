#include "byovd_client.h"
#include "../../driver/byovd_protocol.h"
#include <vector>
#include <cstring>

static HANDLE g_drv = INVALID_HANDLE_VALUE;

namespace BYOVD {

    bool Open(const wchar_t* devicePath) {
        if (g_drv != INVALID_HANDLE_VALUE) Close();
        g_drv = CreateFileW(devicePath ? devicePath : BYOVD_DEVICE_NAME,
                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        return g_drv != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (g_drv != INVALID_HANDLE_VALUE) CloseHandle(g_drv);
        g_drv = INVALID_HANDLE_VALUE;
    }

    bool IsOpen() {
        return g_drv != INVALID_HANDLE_VALUE;
    }

    // The cheat calls these with a target PID + virtual address. The driver
    // resolves the EPROCESS and copies across the boundary at ring 0, so the
    // cheat never issues NtReadVirtualMemory/NtWriteVirtualMemory itself.
    bool KernelReadProcessMemory(HANDLE pid, uint64_t addr, void* buffer, size_t size) {
        if (g_drv == INVALID_HANDLE_VALUE || !buffer || size == 0) return false;
        std::vector<uint8_t> inout(sizeof(BYOVD_READ_PROC_REQUEST) + size);
        BYOVD_READ_PROC_REQUEST* req = (BYOVD_READ_PROC_REQUEST*)inout.data();
        req->pid = (uint32_t)(ULONG_PTR)pid;
        req->address = addr;
        req->size = size;
        DWORD returned = 0;
        if (!DeviceIoControl(g_drv, IOCTL_BYOVD_READ_PROC, inout.data(),
                             (DWORD)inout.size(), inout.data(), (DWORD)inout.size(),
                             &returned, nullptr))
            return false;
        if (returned < sizeof(BYOVD_READ_PROC_REQUEST) + size) return false;
        memcpy(buffer, inout.data() + sizeof(BYOVD_READ_PROC_REQUEST), size);
        return true;
    }

    bool KernelWriteProcessMemory(HANDLE pid, uint64_t addr, const void* buffer, size_t size) {
        if (g_drv == INVALID_HANDLE_VALUE || !buffer || size == 0) return false;
        std::vector<uint8_t> inout(sizeof(BYOVD_WRITE_PROC_REQUEST) + size);
        BYOVD_WRITE_PROC_REQUEST* req = (BYOVD_WRITE_PROC_REQUEST*)inout.data();
        req->pid = (uint32_t)(ULONG_PTR)pid;
        req->address = addr;
        req->size = size;
        memcpy(req->data, buffer, size);
        DWORD returned = 0;
        return DeviceIoControl(g_drv, IOCTL_BYOVD_WRITE_PROC, inout.data(),
                               (DWORD)inout.size(), inout.data(), (DWORD)inout.size(),
                               &returned, nullptr) != 0;
    }

    // Physical-address primitives (unused in the external cheat; DMA lane).
    bool KRead(uint64_t addr, void* buffer, size_t size) {
        (void)addr; (void)buffer; (void)size; return false;
    }

    bool KWrite(uint64_t addr, const void* buffer, size_t size) {
        (void)addr; (void)buffer; (void)size; return false;
    }

    bool KMemcpy(uint64_t dst, uint64_t src, size_t size) {
        (void)dst; (void)src; (void)size; return false;
    }

    void WipeKernelCallbacks() {
        // Requires a kernel driver cursor into PspLoadImageNotifyRoutine /
        // PspCreateThreadNotifyRoutine / PspCreateProcessNotifyRoutineEx.
        // Pattern scan in ntoskrnl.exe — per-build offsets. Not wired.
    }

    bool HidePhysicalPages(uint64_t va, size_t size) {
        (void)va; (void)size; return false;
    }

} // namespace BYOVD
