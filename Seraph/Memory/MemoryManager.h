#pragma once
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <memory>

extern "C" int64_t
Luck_ReadVirtualMemory
(
	HANDLE ProcessHandle,
	PVOID BaseAddress,
	PVOID Buffer,
	ULONG NumberOfBytesToRead,
	PULONG NumberOfBytesRead
);

extern "C" int64_t
Luck_WriteVirtualMemory
(
	HANDLE Processhandle,
	PVOID BaseAddress,
	PVOID Buffer,
	ULONG NumberOfBytesToWrite,
	PULONG NumberOfBytesWritten
);

class MemoryManager final {
private:
	int32_t processId;
	uintptr_t baseAddress;
public:
	MemoryManager() = default;
	~MemoryManager() = default;

	int32_t getProcessId(const std::string& processName);
	uintptr_t getModuleAddress(const std::string& moduleName);

	bool attachToProcess(const std::string& processName);

	void readRaw(uintptr_t address, void* buffer, uintptr_t size);
	std::string readString(uintptr_t address);
	bool writeString(uintptr_t address, const std::string& value);

	// Opens a short-lived handle to the target process (caller must CloseHandle).
	// Handles are never retained across calls so no persistent handle exists.
	HANDLE openTransientHandle(bool forWrite = false);

	template <typename T>
	T read(uintptr_t address);

	template <typename T>
	void write(uintptr_t address, T value);

	int32_t getProcessId();
	void setProcessId(int32_t newProcessId);

	uintptr_t getBaseAddress();
	void setBaseAddress(uintptr_t newBaseAddress);

	void closeProcess();
};

template <typename T>
T MemoryManager::read(uintptr_t address) {
	T buffer{};

	HANDLE h = openTransientHandle(false);
	if (!h) return buffer;

	Luck_ReadVirtualMemory(h, reinterpret_cast<void*>(address), &buffer, sizeof(T), nullptr);
	CloseHandle(h);

	return buffer;
}

template <typename T>
void MemoryManager::write(uintptr_t address, T value) {
	HANDLE h = openTransientHandle(true);
	if (!h) return;

	Luck_WriteVirtualMemory(h, reinterpret_cast<void*>(address), &value, sizeof(T), nullptr);
	CloseHandle(h);
}

inline std::unique_ptr<MemoryManager> Memory = std::make_unique<MemoryManager>();