#ifdef _MSC_VER
#pragma warning (disable: 26812)    // [Static Analyzer] The enum type 'xxx' is unscoped. Prefer 'enum class' over 'enum' (Enum.3). ImGui uses unscoped enum flag bitmasks heavily.
#endif

#include <thread>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include "Memory/MemoryManager.h"
#include "overlay/renderer.h"
#include "features/misc.h"
#include "features/hitboxexpander.h"
#include "features/fly.h"
#include "features/speed.h"
#include "features/world.h"
#include "features/antiaim.h"
#include "features/tickrate.h"
#include "features/spin360.h"
#include "features/chams.h"
#include "rbx/Caches/playercache.h"
#include "rbx/Caches/playerobjectscache.h"
#include "rbx/Caches/TPHandler.h"
#include "rbx/globals/globals.h"
#include "rbx/configs/configs.h"

bool IsGameRunning(const wchar_t* windowTitle)
{
    HWND hwnd = FindWindowW(NULL, windowTitle);
    return hwnd != NULL;
}

std::string GetExecutableDir()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::filesystem::path exePath(path);
    return exePath.parent_path().string();
}

static void HideConsoleWindow()
{
    HWND console = GetConsoleWindow();
    if (console)
        ShowWindow(console, SW_HIDE);
}

int main()
{
    HideConsoleWindow();



    InitializeConfigPaths();

    while (true)
    {
        while (!IsGameRunning(L"Roblox"))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        // Wait 1.5 seconds to let Roblox fully spin up before attaching
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        if (!Memory->attachToProcess("RobloxPlayerBeta.exe"))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            continue;
        }

        if (Memory->getProcessId("RobloxPlayerBeta.exe") == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            continue;
        }

        InitializeConfigPaths();
        TryLoadAutoloadConfig();

        auto fakeDataModel = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::FakeDataModel::Pointer);
        auto dataModel = RobloxInstance(Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel));

        // Wait for Ugc or Roblox exit
        while (dataModel.Name() != "Ugc" && dataModel.Name() != "Game" && IsGameRunning(L"Roblox"))
        {
            fakeDataModel = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::FakeDataModel::Pointer);
            dataModel = RobloxInstance(Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        if (!IsGameRunning(L"Roblox"))
        {
            Memory->closeProcess();
            continue;
        }

        Globals::Roblox::DataModel = dataModel;

        auto visualEngine = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::VisualEngine::Pointer);

        while (visualEngine == 0 && IsGameRunning(L"Roblox"))
        {
            visualEngine = Memory->read<uintptr_t>(Memory->getBaseAddress() + Offsets::VisualEngine::Pointer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        if (!IsGameRunning(L"Roblox"))
        {
            Memory->closeProcess();
            continue;
        }

        Globals::Roblox::VisualEngine = visualEngine;

        Globals::Roblox::Workspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
        Globals::Roblox::Players = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
        Globals::Roblox::Camera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");

        Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));

        Globals::Roblox::lastPlaceID = Memory->read<int>(Globals::Roblox::DataModel.address + Offsets::DataModel::PlaceId);

        // Enable global hack state and launch all threads
        Globals::running = true;

        std::thread(ShowImgui).detach();
        std::thread(CachePlayers).detach();
        std::thread(CachePlayerObjects).detach();
        std::thread(TPHandler).detach();
        std::thread(MiscLoop).detach();
        std::thread(RunHitboxExpander).detach();
        std::thread(FlyLoop).detach();
        std::thread(SpeedLoop).detach();
        std::thread(WorldLoop).detach();
        std::thread(AntiAimLoop).detach();
        std::thread(TickRateLoop).detach();
        std::thread(Spin360Loop).detach();
        std::thread(ChamsLoop).detach();

        // Monitor process exits cleanly and without CPU cycles using synchronize handle
        HANDLE processHandle = OpenProcess(SYNCHRONIZE, FALSE, Memory->getProcessId());
        if (processHandle && processHandle != INVALID_HANDLE_VALUE)
        {
            WaitForSingleObject(processHandle, INFINITE);
            CloseHandle(processHandle);
        }
        else
        {
            // Fallback checking in case OpenProcess fails
            while (IsGameRunning(L"Roblox"))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        // Turn off features so threads exit cleanly
        Globals::running = false;

        // Wait 1.5 seconds for all threads to terminate safely
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        // Close the Roblox process handle and reset state
        Memory->closeProcess();

        // Clear active caches
        Globals::Caches::CachedPlayers.clear();
        Globals::Caches::CachedPlayerObjects.clear();
    }

    return 0;
}