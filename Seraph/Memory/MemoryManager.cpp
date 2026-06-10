#include "MemoryManager.h"
#include <vector>

int32_t MemoryManager::getProcessId(const std::string& processName) {
	uint32_t processId = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (snapshot == INVALID_HANDLE_VALUE) {
		return processId;
	}

	PROCESSENTRY32 processEntry{};
	processEntry.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(snapshot, &processEntry)) {
		do {
			if (!_stricmp(processName.c_str(), processEntry.szExeFile)) {
				processId = processEntry.th32ProcessID;
				break;
			}
		} while (Process32Next(snapshot, &processEntry));
	}

	CloseHandle(snapshot);
	return processId;
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