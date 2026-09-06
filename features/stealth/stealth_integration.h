#pragma once

namespace StealthIntegration {

    void InitializeStealthLayer();
    bool LoadCheatModule(const wchar_t* dllPath);
    void EnableBYOVD(const wchar_t* devicePath = L"\\\\.\\byovd");
    void EnableDMA();

} // namespace StealthIntegration