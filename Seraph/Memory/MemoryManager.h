#pragma once
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <memory>
#include <atomic>
#include <cstring>
#include <cstdint>

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
	// Cached read handle for the attached process.
	//
	// Every read<T>() used to call OpenProcess + CloseHandle around a single
	// NtReadVirtualMemory. RenderESP performs hundreds of small reads per
	// frame, so that was hundreds of handle creations per frame - each of them
	// a kernel object create/destroy plus a security check. It cost roughly
	// two thirds of the game's frame rate whenever the overlay was loaded.
	//
	// Keep one handle open for the lifetime of the attachment instead. It is
	// closed in closeProcess() and re-created if the pid changes or the handle
	// becomes invalid (the target exiting invalidates it).
	HANDLE cachedReadHandle = nullptr;
	int32_t cachedReadHandlePid = 0;
	HANDLE readHandle();
	void closeReadHandle();
public:
	// ── frame-scoped read cache ─────────────────────────────────────────
	// Every read is a raw syscall. The same address is read many times in a
	// single frame (a player's Head position is fetched by ESP, wallcheck,
	// aimbot and the triggerbot independently; HRP.CFrame() is read far more
	// often than once). A small direct-mapped cache keyed on address turns
	// those repeats into array lookups.
	//
	// Invalidated wholesale by InvalidateReadCache(), which the overlay calls
	// once per frame, and on every write so a cached value can never outlive
	// the data it mirrors.
	static constexpr size_t kReadCacheSlots = 512;
	struct ReadCacheSlot
	{
		uintptr_t address = 0;
		uint8_t   bytes[16] = {};
		uint8_t   size = 0;
		uint64_t  generation = 0;
	};
	ReadCacheSlot readCache[kReadCacheSlots];
	uint64_t readCacheGeneration = 1;

	// Diagnostics: total fallback reads performed, so the cost of a frame can be
	// attributed rather than guessed.
	inline static std::atomic<uint64_t> readCount{ 0 };
	// Reads served from the frame cache rather than the syscall path.
	inline static std::atomic<uint64_t> readCacheHits{ 0 };

	MemoryManager() = default;
	~MemoryManager() = default;

	// Drops every cached read. Call once per frame before the render pass.
	void InvalidateReadCache()
	{
		++readCacheGeneration;
	}

	int32_t getProcessId(const std::string& processName);
	uintptr_t getModuleAddress(const std::string& moduleName);

	bool attachToProcess(const std::string& processName);

	void readRaw(uintptr_t address, void* buffer, uintptr_t size);
	void writeRaw(uintptr_t address, const void* buffer, uintptr_t size);
	std::string readString(uintptr_t address);
	bool writeString(uintptr_t address, const std::string& value);

	// Opens a short-lived handle to the target process (caller must CloseHandle).
	// Handles are never retained across calls so no persistent handle exists.
	HANDLE openTransientHandle(bool forWrite = false);

	// Kernel-driver IO path (ring 0 MmCopyVirtualMemory). Returns true only when
	// the BYOVD driver is loaded and the copy succeeded; callers fall back to
	// the direct-syscall path if this returns false.
	bool kernelReadRaw(uintptr_t address, void* buffer, uintptr_t size);
	bool kernelWriteRaw(uintptr_t address, const void* buffer, uintptr_t size);

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

	if (address == 0) return buffer;

	// Small types only: the cache stores 16 bytes and is keyed on a single
	// address, so anything larger (matrices, CFrames) goes straight through.
	if constexpr (sizeof(T) <= 16)
	{
		ReadCacheSlot& slot = readCache[(address >> 2) & (kReadCacheSlots - 1)];
		if (slot.generation == readCacheGeneration &&
			slot.address == address && slot.size == sizeof(T))
		{
			++readCacheHits;
			T out{};
			memcpy(&out, slot.bytes, sizeof(T));
			return out;
		}

		if (kernelReadRaw(address, &buffer, sizeof(T)))
		{
			slot.address = address;
			slot.size = (uint8_t)sizeof(T);
			memcpy(slot.bytes, &buffer, sizeof(T));
			slot.generation = readCacheGeneration;
			return buffer;
		}

		HANDLE h = readHandle();
		if (!h) return buffer;

		++readCount;
		Luck_ReadVirtualMemory(h, reinterpret_cast<void*>(address), &buffer, sizeof(T), nullptr);

		slot.address = address;
		slot.size = (uint8_t)sizeof(T);
		memcpy(slot.bytes, &buffer, sizeof(T));
		slot.generation = readCacheGeneration;
		return buffer;
	}

	if (kernelReadRaw(address, &buffer, sizeof(T)))
		return buffer;

	HANDLE h = readHandle();
	if (!h) return buffer;

	++readCount;
	Luck_ReadVirtualMemory(h, reinterpret_cast<void*>(address), &buffer, sizeof(T), nullptr);

	return buffer;
}

template <typename T>
void MemoryManager::write(uintptr_t address, T value) {
	// A write can change data a cached read mirrors; drop the cache.
	InvalidateReadCache();

	if (kernelWriteRaw(address, &value, sizeof(T)))
		return;

	HANDLE h = openTransientHandle(true);
	if (!h) return;

	Luck_WriteVirtualMemory(h, reinterpret_cast<void*>(address), &value, sizeof(T), nullptr);
	CloseHandle(h);
}

inline void MemoryManager::writeRaw(uintptr_t address, const void* buffer, uintptr_t size) {
	if (kernelWriteRaw(address, buffer, size))
		return;

	HANDLE h = openTransientHandle(true);
	if (!h) return;

	Luck_WriteVirtualMemory(h, reinterpret_cast<void*>(address), const_cast<void*>(buffer), (ULONG)size, nullptr);
	CloseHandle(h);
}

inline std::unique_ptr<MemoryManager> Memory = std::make_unique<MemoryManager>();