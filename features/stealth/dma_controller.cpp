#include "dma_controller.h"
#include <windows.h>

namespace DMA {

    static uint64_t g_bar0 = 0, g_bar2 = 0;
    static HANDLE g_device = INVALID_HANDLE_VALUE;
    static Config g_cfg;

    bool Initialize(const Config& cfg) {
        g_cfg = cfg;
        g_device = CreateFileW(L"\\\\.\\DmaDevice", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        return g_device != INVALID_HANDLE_VALUE;
    }

    void Shutdown() {
        if (g_device != INVALID_HANDLE_VALUE) CloseHandle(g_device);
    }

    bool ReadPhysical(uint64_t physAddr, void* buffer, size_t size) {
        return false;
    }

    bool WritePhysical(uint64_t physAddr, const void* buffer, size_t size) {
        return false;
    }

    bool ReadVirtual(uint32_t pid, uint64_t virtAddr, void* buffer, size_t size) {
        uint64_t cr3 = GetCr3ForPid(pid);
        if (!cr3) return false;
        return false;
    }

    bool WriteVirtual(uint32_t pid, uint64_t virtAddr, const void* buffer, size_t size) {
        return false;
    }

    uint64_t GetCr3ForPid(uint32_t pid) {
        return 0;
    }

} // namespace DMA