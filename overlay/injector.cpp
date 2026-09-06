// Remote process manual DLL injection
// Adapts stealth_core's ManualMapDll for cross-process injection

#include "injector.h"
#include <winternl.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <fstream>
#include <algorithm>
#include <iostream>

// Forward declaration - will be set by executor
namespace Injector {
    typedef void(*LogCallback)(const std::string&);
    static LogCallback g_logCallback = nullptr;
    
    void SetLogCallback(LogCallback cb) { g_logCallback = cb; }
    
    static void Log(const std::string& msg) {
        char dbg[256];
        sprintf_s(dbg, "[Injector] %s\n", msg.c_str());
        OutputDebugStringA(dbg);
        if (g_logCallback) g_logCallback(msg);
    }
}

namespace Injector {

using NtCreateThreadEx_t = NTSTATUS (NTAPI*)(
    PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID
);

static NtCreateThreadEx_t g_NtCreateThreadEx = nullptr;

void InitNtdll() {
    if (!g_NtCreateThreadEx) {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        g_NtCreateThreadEx = (NtCreateThreadEx_t)GetProcAddress(ntdll, "NtCreateThreadEx");
    }
}

int FindRobloxPID() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);
    
    if (Process32FirstW(snap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, L"RobloxPlayerBeta.exe") == 0 ||
                _wcsicmp(pe32.szExeFile, L"RobloxPlayer.exe") == 0) {
                CloseHandle(snap);
                return pe32.th32ProcessID;
            }
        } while (Process32NextW(snap, &pe32));
    }
    CloseHandle(snap);
    return 0;
}

std::string GetLastErrorStr() {
    DWORD err = GetLastError();
    if (err == 0) return "Success";
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), nullptr);
    return std::string(buf) + " (0x" + std::to_string(err) + ")";
}

bool IsRobloxRunning() {
    return FindRobloxPID() != 0;
}

HANDLE OpenProcessForInjection(DWORD pid) {
    HANDLE h = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | 
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE, pid);
    if (!h) {
        OutputDebugStringA(("[Injector] OpenProcess failed: " + GetLastErrorStr() + "\n").c_str());
    }
    return h;
}

uintptr_t AllocRemoteMemory(HANDLE hProc, SIZE_T size) {
    uintptr_t addr = (uintptr_t)VirtualAllocEx(hProc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr) {
        OutputDebugStringA(("[Injector] VirtualAllocEx failed: " + GetLastErrorStr() + "\n").c_str());
    }
    return addr;
}

bool WriteRemoteMemory(HANDLE hProc, uintptr_t addr, const void* data, SIZE_T size) {
    SIZE_T written = 0;
    bool ok = WriteProcessMemory(hProc, (LPVOID)addr, data, size, &written) && written == size;
    if (!ok) {
        OutputDebugStringA(("[Injector] WriteProcessMemory failed: " + GetLastErrorStr() + "\n").c_str());
    }
    return ok;
}

bool ReadRemoteMemory(HANDLE hProc, uintptr_t addr, void* data, SIZE_T size) {
    SIZE_T read = 0;
    bool ok = ReadProcessMemory(hProc, (LPCVOID)addr, data, size, &read) && read == size;
    if (!ok) {
        OutputDebugStringA(("[Injector] ReadProcessMemory failed: " + GetLastErrorStr() + "\n").c_str());
    }
    return ok;
}

bool SetRemoteMemoryProtection(HANDLE hProc, uintptr_t addr, SIZE_T size, DWORD protect, DWORD* oldProtect) {
    bool ok = VirtualProtectEx(hProc, (LPVOID)addr, size, protect, oldProtect);
    if (!ok) {
        OutputDebugStringA(("[Injector] VirtualProtectEx failed: " + GetLastErrorStr() + "\n").c_str());
    }
    return ok;
}

DWORD WINAPI RemoteLoadLibrary(LPVOID lpParameter) {
    return (DWORD)(ULONG_PTR)LoadLibraryW((LPCWSTR)lpParameter);
}

// Manual map in remote process - simplified: write DLL bytes, create remote thread at entry point
InjectResult ManualMapRemote(HANDLE hProc, const std::vector<BYTE>& dllBytes) {
    InjectResult result;
    
    Injector::Log("ManualMapRemote entered, DLL size: " + std::to_string(dllBytes.size()) + " bytes");
    OutputDebugStringA(("[Injector] ManualMapRemote entered, DLL size: " + std::to_string(dllBytes.size()) + "\n").c_str());
    
    if (dllBytes.size() < sizeof(IMAGE_DOS_HEADER)) {
        result.error = "DLL too small";
        return result;
    }
    
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)dllBytes.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        result.error = "Invalid DOS signature";
        return result;
    }
    
    if (dllBytes.size() < dos->e_lfanew + sizeof(IMAGE_NT_HEADERS)) {
        result.error = "DLL too small for NT headers";
        return result;
    }
    
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(dllBytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        result.error = "Invalid NT signature";
        return result;
    }
    
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        result.error = "Not a 64-bit DLL";
        return result;
    }
    
    // Log to executor console too
    Injector::Log("[injector] Validating DLL headers... OK");
    
    // Allocate memory in target process
    Injector::Log("[injector] Step 5: Allocating remote memory (" + std::to_string(nt->OptionalHeader.SizeOfImage) + " bytes)...");
    OutputDebugStringA("[Injector] Allocating remote memory...\n");
    uintptr_t remoteBase = AllocRemoteMemory(hProc, nt->OptionalHeader.SizeOfImage);
    if (!remoteBase) {
        result.error = "VirtualAllocEx failed: " + GetLastErrorStr();
        Injector::Log("[injector] VirtualAllocEx failed: " + GetLastErrorStr());
        return result;
    }
    char dbg[128];
    sprintf_s(dbg, "[Injector] Remote base: 0x%p\n", (void*)remoteBase);
    OutputDebugStringA(dbg);
    Injector::Log("[injector] Remote base: 0x" + std::to_string(remoteBase));
    
    // Write headers
    Injector::Log("[injector] Step 6: Writing headers...");
    OutputDebugStringA("[Injector] Writing headers...\n");
    if (!WriteRemoteMemory(hProc, remoteBase, dllBytes.data(), nt->OptionalHeader.SizeOfHeaders)) {
        result.error = "Write headers failed: " + GetLastErrorStr();
        Injector::Log("[injector] Write headers failed: " + GetLastErrorStr());
        VirtualFreeEx(hProc, (LPVOID)remoteBase, 0, MEM_RELEASE);
        return result;
    }
    Injector::Log("[injector] Headers written OK");
    
    // Write sections
    Injector::Log("[injector] Step 7: Writing " + std::to_string(nt->FileHeader.NumberOfSections) + " sections...");
    OutputDebugStringA("[Injector] Writing sections...\n");
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (sec->SizeOfRawData) {
            uintptr_t secAddr = remoteBase + sec->VirtualAddress;
            if (!WriteRemoteMemory(hProc, secAddr, 
                dllBytes.data() + sec->PointerToRawData, sec->SizeOfRawData)) {
                result.error = "Write section failed: " + GetLastErrorStr();
                Injector::Log("[injector] Write section failed: " + GetLastErrorStr());
                VirtualFreeEx(hProc, (LPVOID)remoteBase, 0, MEM_RELEASE);
                return result;
            }
        }
    }
    Injector::Log("[injector] Sections written OK");
    OutputDebugStringA("[Injector] Sections written, processing relocations...\n");
    
    // Process relocations
    Injector::Log("[injector] Step 8: Processing relocations...");
    OutputDebugStringA("[Injector] Processing relocations...\n");
    if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)
            (dllBytes.data() + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        
        std::vector<BYTE> relocBuffer;
        SIZE_T relocSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        relocBuffer.resize(relocSize);
        if (!ReadRemoteMemory(hProc, remoteBase + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress,
            relocBuffer.data(), relocSize)) {
            result.error = "Read relocations failed: " + GetLastErrorStr();
            Injector::Log("[injector] Read relocations failed: " + GetLastErrorStr());
            VirtualFreeEx(hProc, (LPVOID)remoteBase, 0, MEM_RELEASE);
            return result;
        }
        
        PIMAGE_BASE_RELOCATION localReloc = (PIMAGE_BASE_RELOCATION)relocBuffer.data();
        DWORD_PTR delta = (DWORD_PTR)remoteBase - nt->OptionalHeader.ImageBase;
        
        while (localReloc->VirtualAddress) {
            DWORD count = (localReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* entries = (WORD*)(localReloc + 1);
            
            for (DWORD j = 0; j < count; j++) {
                if ((entries[j] >> 12) == IMAGE_REL_BASED_DIR64) {
                    DWORD_PTR remotePatchAddr = remoteBase + localReloc->VirtualAddress + (entries[j] & 0xFFF);
                    DWORD_PTR value;
                    if (ReadRemoteMemory(hProc, remotePatchAddr, &value, sizeof(value))) {
                        value += delta;
                        WriteRemoteMemory(hProc, remotePatchAddr, &value, sizeof(value));
                    }
                }
            }
            localReloc = (PIMAGE_BASE_RELOCATION)((BYTE*)localReloc + localReloc->SizeOfBlock);
        }
    }
    Injector::Log("[injector] Relocations processed OK");
    OutputDebugStringA("[Injector] Relocations done, resolving imports...\n");
    
    // Resolve imports - we need to do this locally then write IAT
    // For simplicity, assume DLL uses only kernel32/user32 which are at same base in all processes
    // (ASLR for system DLLs is consistent per boot)
    Injector::Log("[injector] Step 9: Resolving imports...");
    if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        OutputDebugStringA("[Injector] Resolving imports...\n");
        PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)
            (dllBytes.data() + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        
        int importCount = 0;
        while (imp->Name) {
            char* modName = (char*)dllBytes.data() + imp->Name;
            HMODULE localMod = GetModuleHandleA(modName);
            if (!localMod) localMod = LoadLibraryA(modName);
            
            if (localMod) {
                PIMAGE_THUNK_DATA64 orig = (PIMAGE_THUNK_DATA64)(dllBytes.data() + imp->OriginalFirstThunk);
                PIMAGE_THUNK_DATA64 iat = (PIMAGE_THUNK_DATA64)(dllBytes.data() + imp->FirstThunk);
                
                while (orig->u1.AddressOfData) {
                    DWORD_PTR funcAddr = 0;
                    if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                        funcAddr = (DWORD_PTR)GetProcAddress(localMod, (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
                    } else {
                        PIMAGE_IMPORT_BY_NAME ibn = (PIMAGE_IMPORT_BY_NAME)(dllBytes.data() + orig->u1.AddressOfData);
                        funcAddr = (DWORD_PTR)GetProcAddress(localMod, (LPCSTR)ibn->Name);
                    }
                    
                    if (funcAddr) {
                        // Write to remote IAT
                        uintptr_t remoteIATAddr = remoteBase + imp->FirstThunk + (orig - (PIMAGE_THUNK_DATA64)(dllBytes.data() + imp->FirstThunk)) * sizeof(DWORD_PTR);
                        WriteRemoteMemory(hProc, remoteIATAddr, &funcAddr, sizeof(funcAddr));
                        importCount++;
                    }
                    orig++; iat++;
                }
            }
            imp++;
        }
        Injector::Log("[injector] Imports resolved (" + std::to_string(importCount) + " functions)");
    }
    OutputDebugStringA("[Injector] Imports resolved, setting section protections...\n");
    
    // Set section protections
    Injector::Log("[injector] Step 10: Setting section protections...");
    sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        DWORD protect = PAGE_READONLY;
        if (sec->Characteristics & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) 
            protect = (protect == PAGE_READWRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        
        DWORD old;
        SetRemoteMemoryProtection(hProc, remoteBase + sec->VirtualAddress, sec->Misc.VirtualSize, protect, &old);
    }
    Injector::Log("[injector] Protections set OK");
    OutputDebugStringA("[Injector] Protections set, calling entry point...\n");
    
    // Call entry point
    Injector::Log("[injector] Step 11: Calling entry point...");
    if (nt->OptionalHeader.AddressOfEntryPoint) {
        uintptr_t entryPoint = remoteBase + nt->OptionalHeader.AddressOfEntryPoint;
        sprintf_s(dbg, "[Injector] Entry point: 0x%p\n", (void*)entryPoint);
        OutputDebugStringA(dbg);
        Injector::Log("[injector] Entry point: 0x" + std::to_string(entryPoint));
        
        InitNtdll();
        Injector::Log("[injector] Step 12: Creating remote thread...");
        OutputDebugStringA("[Injector] Creating remote thread...\n");
        HANDLE hThread = nullptr;
        NTSTATUS status = 0;
        
        if (g_NtCreateThreadEx) {
            status = g_NtCreateThreadEx(&hThread, THREAD_ALL_ACCESS, nullptr, hProc,
                (PVOID)entryPoint, (PVOID)DLL_PROCESS_ATTACH, 0, 0, 0, 0, nullptr);
            sprintf_s(dbg, "[Injector] NtCreateThreadEx status: 0x%08X, thread: 0x%p\n", status, hThread);
            OutputDebugStringA(dbg);
        }
        
        if (!hThread || status != 0) {
            Injector::Log("[injector] Falling back to CreateRemoteThread...");
            OutputDebugStringA("[Injector] Falling back to CreateRemoteThread...\n");
            hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)entryPoint, 
                (LPVOID)DLL_PROCESS_ATTACH, 0, nullptr);
            sprintf_s(dbg, "[Injector] CreateRemoteThread: 0x%p (error: %lu)\n", hThread, GetLastError());
            OutputDebugStringA(dbg);
            Injector::Log("[injector] CreateRemoteThread handle: 0x" + std::to_string((uintptr_t)hThread) + " error: " + std::to_string(GetLastError()));
        }
        
        if (hThread) {
            Injector::Log("[injector] Step 13: Waiting for remote thread (5s timeout)...");
            OutputDebugStringA("[Injector] Waiting for remote thread (5s timeout)...\n");
            DWORD waitResult = WaitForSingleObject(hThread, 5000);
            sprintf_s(dbg, "[Injector] Wait result: %lu\n", waitResult);
            OutputDebugStringA(dbg);
            Injector::Log("[injector] Wait result: " + std::to_string(waitResult));
            
            DWORD exitCode = 0;
            GetExitCodeThread(hThread, &exitCode);
            sprintf_s(dbg, "[Injector] Thread exit code: %lu\n", exitCode);
            OutputDebugStringA(dbg);
            Injector::Log("[injector] Thread exit code: " + std::to_string(exitCode));
            CloseHandle(hThread);
            
            if (exitCode == 0) {
                result.error = "DLL entry point returned FALSE";
                Injector::Log("[injector] Failed: DLL entry point returned FALSE");
                VirtualFreeEx(hProc, (LPVOID)remoteBase, 0, MEM_RELEASE);
                return result;
            }
        } else {
            result.error = "CreateRemoteThread failed: " + GetLastErrorStr();
            Injector::Log("[injector] Failed: " + result.error);
            VirtualFreeEx(hProc, (LPVOID)remoteBase, 0, MEM_RELEASE);
            return result;
        }
    }
    
    Injector::Log("[injector] Injection successful!");
    OutputDebugStringA("[Injector] Injection successful!\n");
    result.success = true;
    result.pid = GetProcessId(hProc);
    result.dllBase = remoteBase;
    return result;
}

InjectResult InjectDLL(int targetPid, const std::vector<BYTE>& dllBytes) {
    HANDLE hProc = OpenProcessForInjection(targetPid);
    if (!hProc) {
        return {false, 0, 0, "OpenProcess failed"};
    }
    
    InjectResult result = ManualMapRemote(hProc, dllBytes);
    CloseHandle(hProc);
    return result;
}

InjectResult InjectDLLByName(const wchar_t* processName, const std::vector<BYTE>& dllBytes) {
    int pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);
        if (Process32FirstW(snap, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName) == 0) {
                    pid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe32));
        }
        CloseHandle(snap);
    }
    
    if (pid == 0) {
        return {false, 0, 0, "Process not found"};
    }
    
    return InjectDLL(pid, dllBytes);
}

std::vector<BYTE> LoadDllFromDisk(const wchar_t* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    size_t size = f.tellg();
    f.seekg(0);
    std::vector<BYTE> buf(size);
    f.read((char*)buf.data(), size);
    return buf;
}

} // namespace Injector
