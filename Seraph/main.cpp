#ifdef _MSC_VER
#pragma warning (disable: 26812)    // [Static Analyzer] The enum type 'xxx' is unscoped. Prefer 'enum class' over 'enum' (Enum.3). ImGui uses unscoped enum flag bitmasks heavily.
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <thread>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include "features/noclip.h"
#include "features/orbit.h"
#include "features/arsenal_gunmods.h"
#include "features/desync.h"
#include "features/rampfling.h"
#include "features/voidhide.h"
#include "features/bunnyhop.h"
#include "features/ragebot.h"
#include "features/visibility.h"
#include "rbx/Caches/playercache.h"
#include "rbx/Caches/playerobjectscache.h"
#include "rbx/Caches/TPHandler.h"
#include "rbx/globals/globals.h"
#include "rbx/configs/configs.h"
#include "features/stealth.h"
#include "features/movement_extra.h"
#include "tray.h"
#include "overlay/loader.h"

bool IsGameRunning(const wchar_t* processName)
{
    // Match by process name (game-name independent). The Roblox client window
    // title is the GAME name (e.g. "Arsenal"), so a title-based FindWindow would
    // never match for non-generic games.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, processName) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
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
    OutputDebugStringA("[Seraph] main: START\n");
    HideConsoleWindow();

    // Stealth: relaunch self as a benign-named copy in %TEMP% before doing
    // anything else. This must run before attach so the "hidden" process is
    // the one that actually does the work. On clean exit we wipe our traces.
    std::atexit(Stealth::WipeTempTraces);
    Stealth::RelaunchAsRenamed();

    InitializeConfigPaths();

    // ── Loader UI ──────────────────────────────────────────────────────
    // Show the loader window for stealth/theme/font/config selection.
    // If AutoAttach is enabled or user clicks Inject, proceed to injection.
    if (!Options::Loader::AutoAttach)
    {
        bool injected = Loader::Run();
if (!injected)
    {
        OutputDebugStringA("[Seraph] main: loader returned false, exiting\n");
        return 0; // user closed the loader without injecting
    }
OutputDebugStringA("[Seraph] main: loader returned true, proceeding to attach\n");
    OutputDebugStringA("[Seraph] main: waiting for Roblox...\n");
    }

    while (true)
    {
        OutputDebugStringA("[Seraph] main: checking for Roblox...\n");
        while (!IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        OutputDebugStringA("[Seraph] main: Roblox found, attaching...\n");

        // Wait 1.5 seconds to let Roblox fully spin up before attaching
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        if (!Memory->attachToProcess("RobloxPlayerBeta.exe"))
        {
            OutputDebugStringA("[Seraph] main: attachToProcess FAILED\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            continue;
        }
        OutputDebugStringA("[Seraph] main: attachToProcess OK\n");

        if (Memory->getProcessId("RobloxPlayerBeta.exe") == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            continue;
        }
        OutputDebugStringA("[Seraph] main: got PID, launching threads...\n");

        InitializeConfigPaths();
        TryLoadAutoloadConfig();

        // Scan for hit sound files next to the executable
        if (Globals::HitSounds::FolderPath.empty())
        {
            Globals::HitSounds::FolderPath = Globals::executablePath + "\\hitsounds";
            Globals::HitSounds::ScanFolder();
        }

        g_ResolveCharacterFallback = &ResolveCharacterFallback;

        OutputDebugStringA("[Seraph] main: reading FakeDataModel...\n");
        uintptr_t base = Memory->getBaseAddress();
        auto fakeDataModel = Memory->read<uintptr_t>(base + Offsets::FakeDataModel::Pointer);
        char dbg[256];
        sprintf_s(dbg, "[Seraph] main: FakeDataModel pointer = 0x%llx\n", fakeDataModel);
        OutputDebugStringA(dbg);
        
        if (fakeDataModel == 0) {
            OutputDebugStringA("[Seraph] main: FakeDataModel is 0, trying VisualEngine path...\n");
            auto visualEngine = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
            sprintf_s(dbg, "[Seraph] main: VisualEngine = 0x%llx\n", visualEngine);
            OutputDebugStringA(dbg);
            if (visualEngine != 0) {
                fakeDataModel = Memory->read<uintptr_t>(visualEngine + Offsets::VisualEngine::FakeDataModel);
                sprintf_s(dbg, "[Seraph] main: FakeDataModel from VisualEngine = 0x%llx\n", fakeDataModel);
                OutputDebugStringA(dbg);
            }
        }
        
        if (fakeDataModel == 0) {
            OutputDebugStringA("[Seraph] main: trying TaskScheduler path...\n");
            auto taskScheduler = Memory->read<uintptr_t>(base + Offsets::TaskScheduler::Pointer);
            sprintf_s(dbg, "[Seraph] main: TaskScheduler = 0x%llx\n", taskScheduler);
            OutputDebugStringA(dbg);
            if (taskScheduler != 0) {
                auto renderJob = Memory->read<uintptr_t>(taskScheduler + 0x38); // RenderJob from TaskScheduler
                if (renderJob != 0) {
                    fakeDataModel = Memory->read<uintptr_t>(renderJob + Offsets::RenderJob::FakeDataModel);
                    sprintf_s(dbg, "[Seraph] main: FakeDataModel from RenderJob = 0x%llx\n", fakeDataModel);
                    OutputDebugStringA(dbg);
                }
            }
        }
        
        if (fakeDataModel == 0) {
            OutputDebugStringA("[Seraph] main: waiting for pointers to populate...\n");
            int waitCount = 0;
            while (fakeDataModel == 0 && IsGameRunning(L"RobloxPlayerBeta.exe") && waitCount < 60) {
                fakeDataModel = Memory->read<uintptr_t>(base + Offsets::FakeDataModel::Pointer);
                if (fakeDataModel == 0) {
                    auto ve = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
                    if (ve != 0) {
                        fakeDataModel = Memory->read<uintptr_t>(ve + Offsets::VisualEngine::FakeDataModel);
                    }
                }
                if (fakeDataModel == 0) {
                    auto ts = Memory->read<uintptr_t>(base + Offsets::TaskScheduler::Pointer);
                    if (ts != 0) {
                        auto rj = Memory->read<uintptr_t>(ts + 0x38);
                        if (rj != 0) {
                            fakeDataModel = Memory->read<uintptr_t>(rj + Offsets::RenderJob::FakeDataModel);
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                waitCount++;
            }
            sprintf_s(dbg, "[Seraph] main: after wait, FakeDataModel = 0x%llx\n", fakeDataModel);
            OutputDebugStringA(dbg);
        }
        
        if (fakeDataModel == 0) {
            OutputDebugStringA("[Seraph] main: Failed to get FakeDataModel, continuing...\n");
        }
        
        auto realDataModelPtr = Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel);
        sprintf_s(dbg, "[Seraph] main: RealDataModel = 0x%llx\n", realDataModelPtr);
        OutputDebugStringA(dbg);
        
        auto dataModel = RobloxInstance(realDataModelPtr);

        // Wait for Ugc or Roblox exit
        OutputDebugStringA("[Seraph] main: waiting for DataModel (Ugc/Game)...\n");
        int dmWait = 0;
        while (dataModel.Name() != "Ugc" && dataModel.Name() != "Game" && IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            if (dmWait % 5 == 0) {
                char dbg[256];
                sprintf_s(dbg, "[Seraph] main: dataModel.addr=0x%llx, Name()=\"%s\"\n", dataModel.address, dataModel.Name().c_str());
                OutputDebugStringA(dbg);
            }
            dmWait++;
            fakeDataModel = Memory->read<uintptr_t>(Offsets::FakeDataModel::Pointer);
            dataModel = RobloxInstance(Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        if (!IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            Memory->closeProcess();
            continue;
        }

        Globals::Roblox::DataModel = dataModel;

        OutputDebugStringA("[Seraph] main: reading VisualEngine...\n");
        auto visualEngine = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
        OutputDebugStringA("[Seraph] main: waiting for VisualEngine\n");
        int veWait = 0;
        while (visualEngine == 0 && IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            if (veWait % 5 == 0) OutputDebugStringA("[Seraph] main: still waiting for VisualEngine...\n");
            veWait++;
            visualEngine = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        if (!IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            Memory->closeProcess();
            continue;
        }

        Globals::Roblox::VisualEngine = visualEngine;
        OutputDebugStringA("[Seraph] main: got VisualEngine\n");

        Globals::Roblox::Workspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
        OutputDebugStringA("[Seraph] main: got Workspace\n");
        Globals::Roblox::Players = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
        OutputDebugStringA("[Seraph] main: got Players\n");
        Globals::Roblox::Camera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");
        OutputDebugStringA("[Seraph] main: got Camera\n");

        Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
        OutputDebugStringA("[Seraph] main: got LocalPlayer\n");

        Globals::Roblox::lastPlaceID = Memory->read<int>(Globals::Roblox::DataModel.address + Offsets::DataModel::PlaceId);
        Globals::Roblox::isPhantomForces = (Globals::Roblox::lastPlaceID == Globals::Roblox::PHANTOM_FORCES_ID);
        Globals::Roblox::isRivals = (Globals::Roblox::lastPlaceID == Globals::Roblox::RIVALS_ID);
        Globals::Roblox::isOverkill = (Globals::Roblox::lastPlaceID == Globals::Roblox::OVERKILL_ID);

        // Resolve a human-readable game name for the detected place.
        if (Globals::Roblox::isPhantomForces) Globals::Roblox::gameName = "Phantom Forces";
        else if (Globals::Roblox::isRivals) Globals::Roblox::gameName = "Rivals";
        else if (Globals::Roblox::isOverkill) Globals::Roblox::gameName = "Overkill";
        else Globals::Roblox::gameName = "Game #" + std::to_string(Globals::Roblox::lastPlaceID);

        // Debug: dump hierarchy when Overkill is detected (run after 45s delay for game to fully load)
        if (Globals::Roblox::isOverkill)
        {
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(45));
                if (!Globals::running) return;

                std::ofstream dbg("C:\\Users\\ncomp\\overkill_debug.txt");
                if (!dbg.is_open()) return;

                dbg << "=== Overkill Debug Dump ===" << std::endl;
                dbg << "PlaceId: " << Globals::Roblox::lastPlaceID << std::endl;

                // Dump ALL DataModel children (all services)
                dbg << "\n--- ALL DataModel children ---" << std::endl;
                auto dmChildren = Globals::Roblox::DataModel.GetChildren();
                dbg << "Count: " << dmChildren.size() << std::endl;
                for (auto& child : dmChildren)
                {
                    if (!child.address) continue;
                    dbg << "  Name=\"" << child.Name() << "\" Class=\"" << child.Class() << "\" Addr=0x" << std::hex << child.address << std::dec << std::endl;
                }

                // Dump ALL Players children with names
                dbg << "\n--- Players children ---" << std::endl;
                auto plChildren = Globals::Roblox::Players.GetChildren();
                dbg << "Count: " << plChildren.size() << std::endl;
                for (size_t i = 0; i < plChildren.size(); i++)
                {
                    auto& child = plChildren[i];
                    dbg << "  [" << i << "] Name=\"" << child.Name() << "\" Class=\"" << child.Class() << "\" Addr=0x" << std::hex << child.address << std::dec << std::endl;
                    dbg << "    ModelInstance(0x298)=0x" << std::hex << Memory->read<uintptr_t>(child.address + 0x298) << std::dec << std::endl;

                    // Dump Player's own children
                    auto playerKids = child.GetChildren();
                    dbg << "    Player.GetChildren() count=" << playerKids.size() << std::endl;
                    for (size_t j = 0; j < playerKids.size() && j < 10; j++)
                    {
                        dbg << "      [" << j << "] Name=\"" << playerKids[j].Name() << "\" Class=\"" << playerKids[j].Class() << "\" Addr=0x" << std::hex << playerKids[j].address << std::dec << std::endl;
                    }
                }

                // Recursively search ALL services for Models with Humanoids
                dbg << "\n--- Recursive search ALL services for Models with Humanoids ---" << std::endl;
                std::function<void(RobloxInstance&, const std::string&, int)> deepSearch = [&](RobloxInstance& inst, const std::string& path, int depth) {
                    if (depth > 8) return;
                    auto children = inst.GetChildren();
                    for (auto& child : children)
                    {
                        if (!child.address) continue;
                        std::string cls = child.Class();
                        std::string name = child.Name();
                        std::string childPath = path + "/" + name;

                        if (cls == "Model")
                        {
                            auto humanoid = child.FindFirstChildWhichIsA("Humanoid");
                            auto hrp = child.FindFirstChild("HumanoidRootPart");
                            if (humanoid.address || hrp.address)
                            {
                                dbg << "  FOUND Model \"" << name << "\" at " << childPath
                                    << " Addr=0x" << std::hex << child.address << std::dec
                                    << " Humanoid=" << (humanoid.address ? "YES" : "no")
                                    << " HRP=" << (hrp.address ? "YES" : "no") << std::endl;

                                if (humanoid.address)
                                    dbg << "    RigType=" << Memory->read<int>(humanoid.address + Offsets::Humanoid::RigType) << std::endl;

                                auto head = child.FindFirstChild("Head");
                                dbg << "    Head=0x" << std::hex << (head.address) << std::dec << std::endl;
                            }
                        }

                        // Recurse into ANY instance that has children, not just Folder/Model
                        auto grandChildren = child.GetChildren();
                        if (!grandChildren.empty())
                        {
                            deepSearch(child, childPath, depth + 1);
                        }
                    }
                };

                // Dump ALL direct Workspace children with class names
                dbg << "\n--- ALL Workspace direct children ---" << std::endl;
                {
                    auto wsKids = Globals::Roblox::Workspace.GetChildren();
                    dbg << "Count: " << wsKids.size() << std::endl;
                    for (auto& kid : wsKids)
                    {
                        if (!kid.address) continue;
                        auto kidKids = kid.GetChildren();
                        dbg << "  Name=\"" << kid.Name() << "\" Class=\"" << kid.Class()
                            << "\" Children=" << kidKids.size()
                            << " Addr=0x" << std::hex << kid.address << std::dec << std::endl;
                    }
                }

                for (auto& child : dmChildren)
                {
                    if (!child.address) continue;
                    std::string name = child.Name();
                    std::string cls = child.Class();
                    dbg << "\n[" << name << "] Class=" << cls << std::endl;
                    deepSearch(child, name, 0);
                }

                // Also scan Player object memory 0x100-0x400 for pointers that might be Character
                dbg << "\n--- LocalPlayer memory scan 0x100-0x400 ---" << std::endl;
                if (Globals::Roblox::LocalPlayer.address)
                {
                    for (uintptr_t off = 0x100; off <= 0x400; off += 0x8)
                    {
                        uintptr_t val = Memory->read<uintptr_t>(Globals::Roblox::LocalPlayer.address + off);
                        if (val == 0 || val < 0x10000 || val > 0x7FFFFFFFFFFF) continue;

                        // Try reading Name from this pointer
                        uintptr_t namePtr = Memory->read<uintptr_t>(val + 0x98);
                        if (namePtr == 0 || namePtr < 0x10000) continue;
                        std::string instName = Memory->readString(namePtr);
                        if (instName.empty()) continue;

                        // Try ClassName
                        std::string clsName = "";
                        uintptr_t classDesc = Memory->read<uintptr_t>(val + 0x18);
                        if (classDesc != 0 && classDesc > 0x10000)
                        {
                            uintptr_t classNamePtr = Memory->read<uintptr_t>(classDesc + 0x8);
                            if (classNamePtr != 0 && classNamePtr > 0x10000)
                                clsName = Memory->readString(classNamePtr);
                        }

                        dbg << "  offset=0x" << std::hex << off << " -> 0x" << val
                            << " class=\"" << clsName << "\" name=\"" << instName << "\"" << std::dec << std::endl;

                        // If it's a Model, check for Humanoid
                        if (clsName == "Model")
                        {
                            auto humanoid = RobloxInstance(val).FindFirstChildWhichIsA("Humanoid");
                            auto hrp = RobloxInstance(val).FindFirstChild("HumanoidRootPart");
                            dbg << "    Model check: Humanoid=" << (humanoid.address ? "YES" : "no")
                                << " HRP=" << (hrp.address ? "YES" : "no") << std::endl;
                        }
                    }
                }

                dbg.close();
            }).detach();
        }

        OutputDebugStringA("[Seraph] main: about to launch threads\n");
        // Enable global hack state and launch all threads
        Globals::running = true;
        OutputDebugStringA("[Seraph] main: launching threads\n");

        std::thread(InitTray).detach();
        OutputDebugStringA("[Seraph] main: InitTray launched\n");
        std::thread(ShowImgui).detach();
        OutputDebugStringA("[Seraph] main: ShowImgui launched\n");
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
        std::thread(Chams::CacheChamsLoop).detach();
        std::thread(NoclipLoop).detach();
        std::thread(OrbitLoop).detach();
        std::thread(ArsenalGunmodsLoop).detach();
        std::thread(DesyncLoop).detach();
        std::thread(RampFlingLoop).detach();
        std::thread(VoidHideLoop).detach();
        std::thread(BhopLoop).detach();
        std::thread(ClickTPLoop).detach();
        std::thread(HipHeightLoop).detach();
        std::thread(FreeCamLoop).detach();
        std::thread(StretchResLoop).detach();
    std::thread(RageKillLoop).detach();
    Visibility::StartOccluderThread();

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
            while (IsGameRunning(L"RobloxPlayerBeta.exe"))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        // Turn off features so threads exit cleanly
        Globals::running = false;
        ShutdownTray();

        // Wait for overlay to fully clean up (window destroyed, ImGui context freed, class unregistered)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!Globals::overlayDone.load() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!Globals::overlayDone.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }

        // Wait for all remaining threads to terminate
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Close the Roblox process handle and reset state
        Memory->closeProcess();

        // Clear all caches and reset global state
        Globals::Caches::CachedPlayers.clear();
        Globals::Caches::CachedPlayerObjects.clear();
        Globals::Caches::CharacterFallbackCache.clear();
        Globals::DynamicOffsets::PlayerTeam = 0;
        Globals::Roblox::isPhantomForces = false;
        Globals::Roblox::isRivals = false;
        Globals::Roblox::isOverkill = false;
        Globals::Roblox::gameName = "Unknown";
    }

    return 0;
}
