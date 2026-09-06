#pragma once
#include <cstdint>
#include <vector>

namespace DMA {

    struct Config {
        uint16_t vendorId = 0x1234;
        uint16_t deviceId = 0x5678;
        size_t windowSize = 0x1000000;
    };

    bool Initialize(const Config& cfg = {});
    void Shutdown();

    bool ReadPhysical(uint64_t physAddr, void* buffer, size_t size);
    bool WritePhysical(uint64_t physAddr, const void* buffer, size_t size);

    bool ReadVirtual(uint32_t pid, uint64_t virtAddr, void* buffer, size_t size);
    bool WriteVirtual(uint32_t pid, uint64_t virtAddr, const void* buffer, size_t size);

    uint64_t GetCr3ForPid(uint32_t pid);

} // namespace DMA