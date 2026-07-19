#include "MemoryManager.h"
#include <vector>

int32_t MemoryManager::getProcessId(const std::string& processName) {
	// Collect every matching process. Multiple RobloxPlayerBeta.exe instances can
	// exist (launcher, crash handler, etc.); we must attach to the one that owns
	// the actual game window, not a helper process.
	std::vector<uint32_t> pids;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (snapshot == INVALID_HANDLE_VALUE) return 0;

	PROCESSENTRY32 processEntry{};
	processEntry.dwSize = sizeof(PROCESSENTRY32);
	if (Process32First(snapshot, &processEntry)) {
		do {
			if (!_stricmp(processName.c_str(), processEntry.szExeFile))
				pids.push_back(processEntry.th32ProcessID);
		} while (Process32Next(snapshot, &processEntry));
	}
	CloseHandle(snapshot);

	if (pids.empty()) return 0;

	// Find which of those PIDs owns a visible top-level window with a title
	// (the game client). Launcher/helper processes usually have no window or an
	// empty title. Prefer the foreground window's PID as a tie-breaker.
	DWORD fgPid = 0;
	{
		HWND fg = GetForegroundWindow();
		if (fg) GetWindowThreadProcessId(fg, &fgPid);
	}

	uint32_t windowedPid = 0;
	struct WinCheck { uint32_t pid; bool found; };
	for (uint32_t p : pids) {
		WinCheck wc{ p, false };
		EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
			auto* wc = reinterpret_cast<WinCheck*>(lParam);
			if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
				return TRUE;
			wchar_t title[256] = { 0 };
			if (GetWindowTextW(hwnd, title, 256) == 0)
				return TRUE; // no title -> not the game client
			DWORD wpid = 0;
			GetWindowThreadProcessId(hwnd, &wpid);
			if (wpid == wc->pid) { wc->found = true; return FALSE; }
			return TRUE;
		}, reinterpret_cast<LPARAM>(&wc));
		if (wc.found) {
			windowedPid = p;
			break;
		}
	}

	if (windowedPid != 0) {
		// Prefer the foreground game if it's among the windowed matches.
		if (fgPid != 0) {
			for (uint32_t p : pids) {
				if (p == fgPid) { windowedPid = p; break; }
			}
		}
		return (int32_t)windowedPid;
	}

	// Fallback: highest PID (most recently started).
	uint32_t best = 0;
	for (uint32_t p : pids) if (p > best) best = p;
	return (int32_t)best;
}

uintptr_t MemoryManager::getModuleAddress(const std::string& moduleName) {
	uintptr_t moduleAddress = 0;

	if (!processHandle) {
		return moduleAddress;
	}

	DWORD processId = GetProcessId(processHandle);
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);

	if (snapshot == INVALID_HANDLE_VALUE) {
		return moduleAddress;
	}

	MODULEENTRY32 moduleEntry{};
	moduleEntry.dwSize = sizeof(MODULEENTRY32);

	if (Module32First(snapshot, &moduleEntry)) {
		do {
			if (!_stricmp(moduleName.c_str(), moduleEntry.szModule)) {
				moduleAddress = reinterpret_cast<uintptr_t>(moduleEntry.modBaseAddr);
				break;
			}
		} while (Module32Next(snapshot, &moduleEntry));
	}

	CloseHandle(snapshot);
	return moduleAddress;
}

bool MemoryManager::attachToProcess(const std::string& processName)
{
	closeProcess();
	auto pid = getProcessId(processName);
	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, false, pid);

	if (process == INVALID_HANDLE_VALUE || !process) {
		return false;
	}

	processHandle = process;
	processId = pid;

	baseAddress = getModuleAddress(processName);

	return true;
}


void MemoryManager::readRaw(uintptr_t address, void* buffer, uintptr_t size) {
	Luck_ReadVirtualMemory(processHandle, reinterpret_cast<void*>(address), &buffer, size, nullptr);
}

bool MemoryManager::writeString(uintptr_t address, const std::string& value)
{
	if (address == 0)
		return false;

	const int32_t length = static_cast<int32_t>(value.size());
	const int32_t capacity = read<int32_t>(address + 0x18);
	if (capacity <= 0 || capacity > 50000)
		return false;

	uintptr_t dataAddress = address;
	if (capacity >= 16)
	{
		dataAddress = read<uintptr_t>(address);
		if (dataAddress == 0)
			return false;
	}

	if (length > capacity)
		return false;

	for (int32_t i = 0; i < length; ++i)
		write<char>(dataAddress + static_cast<uintptr_t>(i), value[static_cast<size_t>(i)]);

	write<char>(dataAddress + static_cast<uintptr_t>(length), '\0');
	write<int32_t>(address + 0x10, length);
	return true;
}

std::string MemoryManager::readString(uintptr_t address) {
	std::string result;
	if (address == 0)
		return result;

	char character;
	int offset = 0;

	int32_t StrLength = read<int32_t>(address + 0x18);

	// Basic safety check for StrLength to prevent massive jumps or issues
	if (StrLength < 0 || StrLength > 50000) {
		return result;
	}

	if (StrLength >= 16) {
		address = read<uintptr_t>(address);
		if (address == 0)
			return result;
	}

	while ((character = read<char>(address + offset)) != 0)
	{
		result.push_back(character);
		offset += sizeof(character);
		if (offset > 10000) // Upper limit for string length safety
			break;
	}

	return result;
}

int32_t MemoryManager::getProcessId() {
	return processId;
}

void MemoryManager::setProcessId(int32_t newProcessId) {
	processId = newProcessId;
}

uintptr_t MemoryManager::getBaseAddress() {
	return baseAddress;
}

void MemoryManager::setBaseAddress(uintptr_t newBaseAddress) {
	baseAddress = newBaseAddress;
}

void MemoryManager::closeProcess() {
	if (processHandle && processHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(processHandle);
		processHandle = nullptr;
	}
	processId = 0;
	baseAddress = 0;
}