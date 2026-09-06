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
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include "Memory/MemoryManager.h"
#include "rbx/globals/OffsetsFetcher.h"
#include "overlay/renderer.h"
#include "features/misc.h"
#include "features/hitboxexpander.h"
#include "features/fly.h"
#include "features/autoclicker.h"
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
#include "features/map_parser.h"
#include "features/ragebot.h"
#include "features/visibility.h"
#include "rbx/Caches/playercache.h"
#include "rbx/Caches/playerobjectscache.h"
#include "rbx/Caches/TPHandler.h"
#include "rbx/globals/globals.h"
#include "rbx/configs/configs.h"
#include "features/stealth.h"
#include "features/stealth/stealth_integration.h"
#include "features/stealth/byovd_client.h"
#include "features/movement_extra.h"
#include "features/rewind.h"
#include "features/thirdperson.h"
#include "tray.h"
#include "overlay/loader.h"
#include "features/rivals_skinchanger.h"
#include "features/mm2.h"
#include "features/bladeball.h"
#include "seraph_log.h"

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

// ── Crash reporting ───────────────────────────────────────────────────────
// Registers an unhandled-exception filter at the top of main(). On a crash it
// writes %LOCALAPPDATA%\Seraph\crash_log.txt with the exception code, the
// faulting module and its offset (so an RVA can be resolved against the PDB),
// and attempts a full minidump at crash.dmp. The filter survives the stealth
// relaunch because main() runs again in the renamed child.
static const char* CrashExceptionName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    default: return "UNKNOWN";
    }
}

static LONG CALLBACK SeraphCrashFilter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord)
    {
        char logPath[MAX_PATH] = {};
        char appData[MAX_PATH] = {};
        const char* name = CrashExceptionName(ep->ExceptionRecord->ExceptionCode);

        // Resolve the module containing the faulting instruction, if any.
        HMODULE mod = nullptr;
        std::string moduleName = "?";
        ULONGLONG moduleBase = 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(ep->ExceptionRecord->ExceptionAddress), &mod) && mod)
        {
            char modPath[MAX_PATH] = {};
            if (GetModuleFileNameA(mod, modPath, MAX_PATH))
            {
                const char* slash = strrchr(modPath, '\\');
                moduleName = slash ? slash + 1 : modPath;
            }
            moduleBase = reinterpret_cast<ULONGLONG>(mod);
        }

        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
        {
            snprintf(logPath, sizeof(logPath), "%s\\Seraph\\crash_log.txt", appData);
            CreateDirectoryA((std::string(appData) + "\\Seraph").c_str(), nullptr);

            DWORD tid = GetCurrentThreadId();
            DWORD pid = GetCurrentProcessId();
            ULONGLONG faultOffset = ep->ExceptionRecord->ExceptionAddress ?
                reinterpret_cast<ULONGLONG>(ep->ExceptionRecord->ExceptionAddress) - moduleBase : 0;
            DWORD flt = ep->ExceptionRecord->NumberParameters > 0 ?
                static_cast<DWORD>(ep->ExceptionRecord->ExceptionInformation[0]) : 0;

            std::ofstream f(logPath, std::ios::app);
            if (f)
            {
                f << "[" << pid << ":" << tid << "] EXCEPTION 0x"
                  << std::hex << ep->ExceptionRecord->ExceptionCode << std::dec
                  << " (" << name << ") flt=" << flt
                  << " pc=0x" << std::hex << reinterpret_cast<ULONGLONG>(ep->ExceptionRecord->ExceptionAddress)
                  << " module=" << moduleName << " base=0x" << std::hex << moduleBase
                  << " rva=0x" << std::hex << faultOffset << std::dec
                  << "\n";
                f.close();
            }

            // Best-effort minidump for full stack analysis.
            std::string dmpPath = std::string(appData) + "\\Seraph\\crash.dmp";
            HANDLE hFile = CreateFileA(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                MINIDUMP_EXCEPTION_INFORMATION mei = {};
                mei.ThreadId = tid;
                mei.ExceptionPointers = ep;
                mei.ClientPointers = TRUE;
                MiniDumpWriteDump(GetCurrentProcess(), pid, hFile,
                    MiniDumpNormal, &mei, nullptr, nullptr);
                CloseHandle(hFile);
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main()
{
    SetUnhandledExceptionFilter(SeraphCrashFilter);
    OutputDebugStringA("[S] main: START\n");
    SeraphLog("[S] main: START");
    HideConsoleWindow();

    // Memory stealth: capture clean ntdll, unhook syscalls — must run FIRST
    StealthIntegration::InitializeStealthLayer();

    // Kernel memory primitive: if seraph_drv.sys is loaded (by SeraphLoader),
    // open \\.\byovd so MemoryManager routes all reads/writes through ring 0
    // (MmCopyVirtualMemory). If the driver isn't present this fails quietly and
    // the cheat falls back to the direct-syscall path (Luck_*.asm).
    BYOVD::Open();

    // Stealth: relaunch self as a benign-named copy in %TEMP% before doing
    // anything else. This must run before attach so the "hidden" process is
    // the one that actually does the work. On clean exit we wipe our traces.
    std::atexit(Stealth::WipeTempTraces);
    Stealth::WipeTempTraces(); // also clear leftovers from any prior (crashed) session
    {
        char origDir[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, origDir, MAX_PATH) != 0) {
            auto pos = std::string(origDir).find_last_of('\\');
            if (pos != std::string::npos)
                Globals::originalExeDir = std::string(origDir, pos + 1);
        }
        // Only set the env var if we haven't already been relaunched (i.e. we're
        // still in the original exe directory, NOT in %TEMP%).  The child
        // inherits the parent's environment, so if we're already in temp we
        // must NOT overwrite the correct value the parent set.
        char tempPath[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, tempPath);
        bool inTemp = (_strnicmp(Globals::originalExeDir.c_str(), tempPath, strlen(tempPath)) == 0);
        if (!inTemp && !Globals::originalExeDir.empty()) {
            // Parent (original exe dir): propagate to child via env var.
            SetEnvironmentVariableA(SX("SERAPH_ORIGDIR").c_str(), Globals::originalExeDir.c_str());
        } else if (inTemp) {
            // Child (relaunched into %TEMP%): override GetModuleFileNameA
            // result with the original dir the parent set via the env var.
            char envBuf[MAX_PATH] = {};
            DWORD sz = GetEnvironmentVariableA(SX("SERAPH_ORIGDIR").c_str(), envBuf, MAX_PATH);
            if (sz > 0 && sz < MAX_PATH)
                Globals::originalExeDir = envBuf;
        }
    }
    Stealth::RelaunchAsRenamed();

    InitializeConfigPaths();

    // The loader UI (stealth/theme/font/config selection) is only shown on the
    // very first launch. Reattaches afterwards are fully automatic: when Roblox
    // closes and reopens, the overlay reappears without any user interaction.
    bool firstLaunch = true;
    int loopIter = 0;
    while (true)
    {
        loopIter++;
        SeraphLog("[S] main: loop iteration " + std::to_string(loopIter));
        // â”€â”€ Loader UI â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (firstLaunch && !Options::Loader::AutoAttach)
        {
            SeraphLog("[S] main: showing loader (first launch)");
            bool injected = Loader::Run();
            SeraphLog(std::string("[S] main: loader returned ") + (injected ? "true" : "false"));
            if (!injected)
            {
                OutputDebugStringA("[S] main: loader returned false, exiting\n");
                return 0; // user closed the loader without injecting
            }
            OutputDebugStringA("[S] main: loader returned true, proceeding to attach\n");
        }
        else
        {
            SeraphLog("[S] main: skipping loader (auto reattach)");
        }
        firstLaunch = false;
        OutputDebugStringA("[S] main: checking for Roblox...\n");
        while (!IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        OutputDebugStringA("[S] main: Roblox found, attaching...\n");
        SeraphLog("[S] main: Roblox found, attaching...");

        // Wait 1.5 seconds to let Roblox fully spin up before attaching
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        if (!Memory->attachToProcess("RobloxPlayerBeta.exe"))
        {
            OutputDebugStringA("[S] main: attachToProcess FAILED\n");
            SeraphLog("[S] main: attachToProcess FAILED");
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            continue;
        }
        OutputDebugStringA("[S] main: attachToProcess OK\n");
        SeraphLog("[S] main: attachToProcess OK, PID=" + std::to_string(Memory->getProcessId()));

        if (Memory->getProcessId("RobloxPlayerBeta.exe") == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            continue;
        }
        OutputDebugStringA("[S] main: got PID, launching threads...\n");

        InitializeConfigPaths();
        TryLoadAutoloadConfig();

        // Scan for hit sound files next to the executable. Prefer the original
        // exe directory (the exe is relaunched as a renamed copy in %TEMP% when
        // Hide Process is on, so the current module path would point there).
        if (Globals::HitSounds::FolderPath.empty())
        {
            Globals::HitSounds::FolderPath = Globals::ResolveExeDir() + "\\hitsounds";
            Globals::HitSounds::ScanFolder();
        }

        g_ResolveCharacterFallback = &ResolveCharacterFallback;

        uintptr_t base = Memory->getBaseAddress();
        auto fakeDataModel = Memory->read<uintptr_t>(base + Offsets::FakeDataModel::Pointer);
        SeraphLog("[S] main: base=0x" + std::to_string(base) + " FakeDataModel@0x" +
            std::to_string(Offsets::FakeDataModel::Pointer) + " = 0x" + std::to_string(fakeDataModel));
        
        if (fakeDataModel == 0) {
            auto visualEngine = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
            SeraphLog("[S] main: VisualEngine@0x" + std::to_string(Offsets::VisualEngine::Pointer) +
                " = 0x" + std::to_string(visualEngine));
            if (visualEngine != 0) {
                fakeDataModel = Memory->read<uintptr_t>(visualEngine + Offsets::VisualEngine::FakeDataModel);
                SeraphLog("[S] main: FakeDataModel from VisualEngine = 0x" + std::to_string(fakeDataModel));
            }
        }
        
        if (fakeDataModel == 0) {
            auto taskScheduler = Memory->read<uintptr_t>(base + Offsets::TaskScheduler::Pointer);
            SeraphLog("[S] main: TaskScheduler@0x" + std::to_string(Offsets::TaskScheduler::Pointer) +
                " = 0x" + std::to_string(taskScheduler));
            if (taskScheduler != 0) {
                auto renderJob = Memory->read<uintptr_t>(taskScheduler + 0x38); // RenderJob from TaskScheduler
                if (renderJob != 0) {
                    fakeDataModel = Memory->read<uintptr_t>(renderJob + Offsets::RenderJob::FakeDataModel);
                    SeraphLog("[S] main: FakeDataModel from RenderJob = 0x" + std::to_string(fakeDataModel));
                }
            }
        }
        
        if (fakeDataModel == 0) {
            SeraphLog("[S] main: waiting for pointers to populate...");
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
                if (waitCount % 5 == 0)
                {
                    char dbg[256];
                    sprintf_s(dbg, "[S] main: pointer wait %d, FakeDataModel = 0x%llx\n", waitCount, fakeDataModel);
                    SeraphLog(dbg);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                waitCount++;
            }
            SeraphLog("[S] main: after wait, FakeDataModel = 0x" + std::to_string(fakeDataModel));
        }
        
        if (fakeDataModel == 0) {
            SeraphLog("[S] main: Failed to get FakeDataModel");
        }
        
        auto realDataModelPtr = Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel);
        {
            char dbg[256];
            sprintf_s(dbg, "[S] main: RealDataModel = 0x%llx\n", realDataModelPtr);
            OutputDebugStringA(dbg);
        }
        
        auto dataModel = RobloxInstance(realDataModelPtr);

        // Wait for Ugc or Roblox exit
        OutputDebugStringA("[S] main: waiting for DataModel (Ugc/Game)...\n");
        SeraphLog("[S] main: waiting for DataModel (Ugc/Game)... addr=0x" + std::to_string(dataModel.address));
        int dmWait = 0;
        while (dataModel.Name() != "Ugc" && dataModel.Name() != "Game" && IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            if (dmWait % 5 == 0) {
                char dbg[256];
                sprintf_s(dbg, "[S] main: dataModel.addr=0x%llx, Name()=\"%s\"\n", dataModel.address, dataModel.Name().c_str());
                OutputDebugStringA(dbg);
                SeraphLog(std::string("[S] main: DataModel wait ") + std::to_string(dmWait) +
                    " addr=0x" + std::to_string(dataModel.address) + " Name()=\"" + dataModel.Name() + "\"");
            }
            dmWait++;
            fakeDataModel = Memory->read<uintptr_t>(base + Offsets::FakeDataModel::Pointer);
            dataModel = RobloxInstance(Memory->read<uintptr_t>(fakeDataModel + Offsets::FakeDataModel::RealDataModel));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        SeraphLog("[S] main: DataModel resolved, Name()=\"" + dataModel.Name() + "\"");

        if (!IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            Memory->closeProcess();
            continue;
        }

        Globals::Roblox::DataModel = dataModel;

        OutputDebugStringA("[S] main: reading VisualEngine...\n");
        auto visualEngine = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
        OutputDebugStringA("[S] main: waiting for VisualEngine\n");
        int veWait = 0;
        while (visualEngine == 0 && IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            if (veWait % 5 == 0) {
                OutputDebugStringA("[S] main: still waiting for VisualEngine...\n");
                SeraphLog("[S] main: still waiting for VisualEngine, iter " + std::to_string(veWait));
            }
            veWait++;
            visualEngine = Memory->read<uintptr_t>(base + Offsets::VisualEngine::Pointer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        SeraphLog("[S] main: VisualEngine = 0x" + std::to_string(visualEngine));

        if (!IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            Memory->closeProcess();
            continue;
        }

        Globals::Roblox::VisualEngine = visualEngine;
        OutputDebugStringA("[S] main: got VisualEngine\n");

        Globals::Roblox::Workspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
        OutputDebugStringA("[S] main: got Workspace\n");
        Globals::Roblox::Players = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
        OutputDebugStringA("[S] main: got Players\n");
        Globals::Roblox::Camera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");
        OutputDebugStringA("[S] main: got Camera\n");

        Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
        OutputDebugStringA("[S] main: got LocalPlayer\n");

        Globals::Roblox::lastPlaceID = Memory->read<int>(Globals::Roblox::DataModel.address + Offsets::DataModel::PlaceId);
        Globals::Roblox::isPhantomForces = (Globals::Roblox::lastPlaceID == Globals::Roblox::PHANTOM_FORCES_ID);
        Globals::Roblox::isRivals = (Globals::Roblox::lastPlaceID == Globals::Roblox::RIVALS_ID);
        Globals::Roblox::isOverkill = (Globals::Roblox::lastPlaceID == Globals::Roblox::OVERKILL_ID);
        Globals::Roblox::isMM2 = (Globals::Roblox::lastPlaceID == Globals::Roblox::MM2_ID);
        Globals::Roblox::isBladeBall = (Globals::Roblox::lastPlaceID == Globals::Roblox::BLADEBALL_ID);

        // Resolve a human-readable game name for the detected place.
        if (Globals::Roblox::isPhantomForces) Globals::Roblox::gameName = "Phantom Forces";
        else if (Globals::Roblox::isRivals) Globals::Roblox::gameName = "Rivals";
        else if (Globals::Roblox::isOverkill) Globals::Roblox::gameName = "Overkill";
        else if (Globals::Roblox::isMM2) Globals::Roblox::gameName = "Murder Mystery 2";
        else if (Globals::Roblox::isBladeBall) Globals::Roblox::gameName = "Blade Ball";
        else Globals::Roblox::gameName = "Game #" + std::to_string(Globals::Roblox::lastPlaceID);

        OutputDebugStringA("[S] main: about to launch threads\n");
        // Enable global hack state and launch all threads
        Globals::running = true;
        OutputDebugStringA("[S] main: launching threads\n");
        SeraphLog("[S] main: running=true, launching threads");

        // Scan hitsounds folder (resolve relative to the original exe directory
        // so it works no matter where the process is running from)
        {
            Globals::HitSounds::FolderPath = Globals::ResolveExeDir() + "\\hitsounds";
            Globals::HitSounds::ScanFolder();
        }

        std::thread(InitTray).detach();
        OutputDebugStringA("[S] main: InitTray launched\n");
        std::thread(ShowImgui).detach();
        OutputDebugStringA("[S] main: ShowImgui launched\n");
        SeraphLog("[S] main: ShowImgui launched");
        std::thread(CachePlayers).detach();
        std::thread(CachePlayerObjects).detach();
        std::thread(TPHandler).detach();
        std::thread(MiscLoop).detach();
        std::thread(AnimationChangerLoop).detach();
        std::thread(RunHitboxExpander).detach();
        std::thread(FlyLoop).detach();
        std::thread(AutoClickerLoop).detach();
        std::thread(SpeedLoop).detach();
        std::thread(WorldLoop).detach();
        std::thread(AntiAimLoop).detach();
        std::thread(TickRateLoop).detach();
        std::thread(Spin360Loop).detach();

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
		std::thread(ThirdPersonLoop).detach();
		std::thread(Rewind::Tick).detach();
		std::thread(StretchResLoop).detach();
		std::thread(RageKillLoop).detach();
        std::thread(RivalsSkinChangerLoop).detach();
        std::thread(BladeBall::RunService).detach();
		Visibility::StartOccluderThread();
		std::thread(MapParser::WorkerLoop).detach();

        // Monitor process exit without holding any handle into Roblox.
        // Poll via snapshot so no process handle is ever retained.
        while (IsGameRunning(L"RobloxPlayerBeta.exe"))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        // Turn off features so threads exit cleanly
        Globals::running = false;
        Globals::overlayShouldShutdown = true;
        ShutdownTray();
        SeraphLog("[S] main: Roblox exited, running=false, overlayShouldShutdown=true");

        // Force close overlay window if still open
        if (Globals::Viewport::RobloxHWND)
        {
            DestroyWindow(Globals::Viewport::RobloxHWND);
            Globals::Viewport::RobloxHWND = nullptr;
        }

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
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Close the Roblox process handle and reset state
        Memory->closeProcess();
        SeraphLog("[S] main: cleanup complete, looping for next Roblox session");

        // Clear all caches and reset global state
        Globals::Caches::CachedPlayers.clear();
        Globals::Caches::CachedPlayerObjects.clear();
        Globals::Caches::CharacterFallbackCache.clear();
        Globals::DynamicOffsets::PlayerTeam = 0;
        Globals::Roblox::isPhantomForces = false;
        Globals::Roblox::isRivals = false;
        Globals::Roblox::isOverkill = false;
        Globals::Roblox::gameName = "Unknown";
        Globals::Roblox::DataModel = RobloxInstance(0);
        Globals::Roblox::VisualEngine = 0;
        Globals::Roblox::Workspace = RobloxInstance(0);
        Globals::Roblox::Players = RobloxInstance(0);
        Globals::Roblox::Camera = RobloxInstance(0);
        Globals::Roblox::LocalPlayer = RobloxInstance(0);
        Globals::Roblox::LocalPlayerTeam = RobloxInstance(0);
        Globals::Roblox::LocalPlayerTeamColor = 0;
        Globals::Roblox::LocalPlayerTeamName = "";
        Globals::Viewport::Valid = false;
        Globals::Viewport::Dimensions = {0, 0};
        Globals::Viewport::ScreenPos = {0, 0};
        Globals::Viewport::RobloxHWND = nullptr;
        Globals::Viewport::ViewMatrix = Matrixes::Matrix4{};
        Globals::Caches::CachedPlayers.clear();
        Globals::Caches::CachedPlayerObjects.clear();
        Globals::Caches::CharacterFallbackCache.clear();
        Globals::DynamicOffsets::PlayerTeam = 0;
        Globals::Roblox::isPhantomForces = false;
        Globals::Roblox::isRivals = false;
        Globals::Roblox::isOverkill = false;
        Globals::Roblox::gameName = "Unknown";
        Chams::ClearHullCache();
        WorldVisuals::savedOriginals = false;
        WorldVisuals::cachedLighting = RobloxInstance(0);
        WorldVisuals::cachedSky = RobloxInstance(0);
        WorldVisuals::lastSkyPreset = -1;
        CombatFeedback::hitFlashTimer.clear();
        CombatFeedback::notifications.clear();
        CombatFeedback::effects.clear();
        CombatFeedback::bulletTracers.clear();
        CombatFeedback::footsteps.clear();
        CombatFeedback::lastHealth.clear();
        CombatFeedback::lastPlayerPos.clear();
    }

    return 0;
}
