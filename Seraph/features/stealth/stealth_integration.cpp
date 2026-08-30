#include "stealth_integration.h"
#include "stealth_core.h"
#include "byovd_client.h"
#include "dma_controller.h"
#include <filesystem>

namespace StealthIntegration {

    static HANDLE g_cheatModule = nullptr;

    void InitializeStealthLayer() {
        MemoryStealth::Initialize();
    }

    bool LoadCheatModule(const wchar_t* dllPath) {
        std::vector<BYTE> dllBytes = MemoryStealth::LoadDllFromDisk(dllPath);
        if (dllBytes.empty()) return false;

        g_cheatModule = MemoryStealth::ManualMapDll(dllBytes);
        if (!g_cheatModule) return false;

        MemoryStealth::HideModuleFromPeb(g_cheatModule);
        return true;
    }

    void EnableBYOVD(const wchar_t* devicePath) {
        BYOVD::Open(devicePath);
        if (BYOVD::IsOpen()) {
            BYOVD::WipeKernelCallbacks();
        }
    }

    void EnableDMA() {
        DMA::Initialize();
    }

} // namespace StealthIntegration