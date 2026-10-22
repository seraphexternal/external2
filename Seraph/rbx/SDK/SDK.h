#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

#include "../offsets.h"
#include "../../rbx/math/math.h"
#include "../../Memory/MemoryManager.h"

namespace Globals {
    namespace DynamicOffsets {
        extern uintptr_t PlayerTeam;
    }
}

class RobloxInstance;

// Character fallback: when Player::ModelInstance offset is wrong (e.g. new Roblox versions),
// this function pointer resolves the character by searching Workspace for matching Models.
using CharacterFallbackFn = RobloxInstance(*)(uintptr_t playerAddress);
inline CharacterFallbackFn g_ResolveCharacterFallback = nullptr;

class RobloxInstance
{
public:
	uintptr_t address;

	// ── per-instance accessor caches ────────────────────────────────────
	// Every Memory->read is a raw syscall (Luck_ReadVirtualMemory), and
	// Class() costs three of them plus a heap allocation for the string.
	// CFrame() called Class() on every invocation purely to test for "Camera",
	// which made a BasePart CFrame read cost four syscalls instead of one.
	// These caches are keyed on the instance address and reset per frame by
	// Globals::FrameCache (an instance's class and primitive never change).
	//
	// The class name is held in a fixed inline buffer, NOT a std::string.
	// RobloxInstance is copied in bulk (every RobloxPlayer holds 21 of them and
	// the whole player vector is copied whenever the cache refreshes), so a
	// std::string here turned each cache refresh into hundreds of heap
	// allocations. Every Roblox class name is far shorter than this buffer.
	mutable uintptr_t cachedPrimitive = 0;
	mutable bool      primitiveResolved = false;
	static constexpr size_t kMaxClassName = 40;
	mutable char      cachedClass[kMaxClassName] = {};
	mutable bool      classResolved = false;

	RobloxInstance(uintptr_t addy)
	{
		address = addy;
	}

	// Clears the accessor caches. Called once per frame.
	inline void ResetAccessorCache() const
	{
		primitiveResolved = false;
		classResolved = false;
		cachedClass[0] = '\0';
	}

	RobloxInstance() : address(0) {}

	operator bool() const
	{
		return address != 0;
	}

	inline uintptr_t NameAddress() const
	{
		if (address == 0)
			return 0;

		const uintptr_t nameContainer = Memory->read<uintptr_t>(address + Offsets::Instance::NameContainer);
		if (nameContainer == 0)
			return 0;

		return nameContainer + Offsets::Instance::Name;
	}

	inline std::string Name() const
	{
		if (address == 0) return "";
		return Memory->readString(NameAddress());
	}

	inline std::string Class() const
	{
		if (address == 0) return "";
		if (classResolved) return std::string(cachedClass);
		// Read the class name straight into the inline buffer. A ClassDescriptor
		// pointer chain plus one bounded string read: three syscalls, once per
		// instance per frame.
		const uintptr_t descriptor = Memory->read<uintptr_t>(address + Offsets::Instance::ClassDescriptor);
		const uintptr_t namePtr = descriptor
			? Memory->read<uintptr_t>(descriptor + Offsets::Instance::ClassName)
			: 0;
		if (namePtr == 0)
		{
			cachedClass[0] = '\0';
			classResolved = true;
			return "";
		}
		// Roblox strings use a small-string optimisation: a length field at
		// +0x18, with the characters inline when the length is under 16 and
		// behind a pointer at the string address otherwise. Names longer than
		// 15 chars (CornerWedgePart, SkinnedMeshPart) would otherwise be read
		// as pointer bytes.
		const int32_t len = Memory->read<int32_t>(namePtr + 0x18);
		uintptr_t charsAddr = namePtr;
		if (len >= 16)
		{
			charsAddr = Memory->read<uintptr_t>(namePtr);
			if (charsAddr == 0)
			{
				cachedClass[0] = '\0';
				classResolved = true;
				return "";
			}
		}
		// One bounded raw read of the whole name, then NUL-terminate. A single
		// syscall beats the character-at-a-time loop this replaced.
		Memory->readRaw(charsAddr, cachedClass, kMaxClassName - 1);
		cachedClass[kMaxClassName - 1] = '\0';
		classResolved = true;
		return std::string(cachedClass);
	}

	// Primitive pointer, read once per frame. Every BasePart accessor needs it,
	// and it was previously fetched with a fresh syscall on each call.
	inline uintptr_t Primitive() const
	{
		if (address == 0) return 0;
		if (primitiveResolved) return cachedPrimitive;
		cachedPrimitive = Memory->read<uintptr_t>(address + Offsets::BasePart::Primitive);
		primitiveResolved = true;
		return cachedPrimitive;
	}

	inline bool IsA(std::string className) const
	{
		if (address == 0) return false;
		if (Class() == className)
		{
			return true;
		}
		return false;
	}

	// GetChildren() and GetChildrenBulk() are the same query. This used to be
	// the one-read-per-child version, kept "just in case" while GetChildrenBulk
	// was used only in a few places. Every read here is a syscall, so fetching a
	// 30-child Model cost 30 of them, and the whole Workspace is walked on a
	// 500ms cadence by the player cache and the map parser. The bulk read
	// returns the identical list in a single call, so GetChildren now uses it.
	inline std::vector<RobloxInstance> GetChildren() const
	{
		return GetChildrenBulk();
	}

	inline std::vector<RobloxInstance> GetChildrenSlow() const
	{
		std::vector<RobloxInstance> returnVector;

		if (address == 0)
			return returnVector;

		uintptr_t childrenStart = Memory->read<uintptr_t>(address + Offsets::Instance::ChildrenStart);
		if (childrenStart == 0)
			return returnVector;

		uintptr_t childrenEnd = Memory->read<uintptr_t>(childrenStart + Offsets::Instance::ChildrenEnd);
		if (childrenEnd == 0)
			return returnVector;

		uintptr_t arrayStart = Memory->read<uintptr_t>(childrenStart);
		if (arrayStart == 0 || arrayStart > childrenEnd)
			return returnVector;

		uintptr_t diff = childrenEnd - arrayStart;
		if (diff % 0x10 != 0)
			return returnVector;

		size_t numChildren = diff / 0x10;
		if (numChildren > 20000) // Safety limit to prevent massive loops/memory depletion on corrupted heap memory
			return returnVector;

		returnVector.reserve(numChildren);

		for (uintptr_t child = arrayStart; child < childrenEnd; child += 0x10)
		{
			uintptr_t childAddress = Memory->read<uintptr_t>(child);
			if (childAddress != 0)
			{
				returnVector.emplace_back(RobloxInstance(childAddress));
			}
		}

		return returnVector;
	}

	// Bulk variant of GetChildren().
//
// GetChildren() issues one driver read PER CHILD, and every read opens and
// closes a process handle. Scanning a Player instance that way costs dozens of
// handle round-trips, which is brutal when it happens for every player on the
// server. This reads the whole child array in a single call instead - the same
// trick the reference ESP uses with its buffer read.
//
// Entries are 16 bytes: { address, metadata }.
inline std::vector<RobloxInstance> GetChildrenBulk() const
{
    std::vector<RobloxInstance> result;
    if (address == 0)
        return result;

    const uintptr_t childrenStart = Memory->read<uintptr_t>(address + Offsets::Instance::ChildrenStart);
    if (childrenStart == 0)
        return result;

    const uintptr_t arrayStart = Memory->read<uintptr_t>(childrenStart);
    const uintptr_t childrenEnd = Memory->read<uintptr_t>(childrenStart + Offsets::Instance::ChildrenEnd);

    if (arrayStart == 0 || childrenEnd <= arrayStart)
        return result;

    const uintptr_t bytes = childrenEnd - arrayStart;
    if (bytes % 0x10 != 0 || bytes > (0x10 * 20000))
        return result;

    struct Entry
    {
        uintptr_t address;
        uintptr_t metadata;
    };

    std::vector<Entry> entries(bytes / 0x10);
    Memory->readRaw(arrayStart, entries.data(), bytes);

    result.reserve(entries.size());
    for (const auto& entry : entries)
        if (entry.address)
            result.emplace_back(entry.address);

    return result;
}

inline RobloxInstance FindFirstChild(std::string name = "") const
	{
		for (auto& child : this->GetChildren())
		{
			if (name == "")
				return child;

			if (child.Name() == name)
				return child;
		}
		return 0;
	}

	// Same search, against a child list the caller already fetched. Callers that
	// walk a tree (the Workspace character scan, the map parser) fetch children
	// once per node; re-reading the same array inside the search tripled the
	// read cost of every Model they visited.
	inline RobloxInstance FindFirstChildIn(const std::vector<RobloxInstance>& kids,
		std::string name) const
	{
		for (const auto& child : kids)
		{
			if (name.empty())
				return child;
			if (child.Name() == name)
				return child;
		}
		return RobloxInstance(0);
	}

	inline RobloxInstance FindFirstChildWhichIsAIn(const std::vector<RobloxInstance>& kids,
		std::string className) const
	{
		for (const auto& child : kids)
			if (child.Class() == className)
				return child;
		return RobloxInstance(0);
	}

	inline RobloxInstance FindFirstChildWhichIsA(std::string className = "") const
	{
		for (auto& child : this->GetChildren())
		{
			if (child.Class() == className)
				return child;
		}
		return 0;
	}

	inline Vectors::Vector3 Position() const
	{
		if (address == 0) return Vectors::Vector3{ 0.f, 0.f, 0.f };
		uintptr_t prim = Primitive();
		if (prim == 0) return Vectors::Vector3{ 0.f, 0.f, 0.f };
		return Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Position);
	}

	inline Vectors::Vector3 Size() const
	{
		if (address == 0) return Vectors::Vector3{ 0.f, 0.f, 0.f };
		uintptr_t prim = Primitive();
		if (prim == 0) return Vectors::Vector3{ 0.f, 0.f, 0.f };
		return Memory->read<Vectors::Vector3>(prim + Offsets::Primitive::Size);
	}

	inline sCFrame CFrame() const
	{
		if (address == 0) return sCFrame{};
		// Only a Camera stores its CFrame on the instance itself; everything
		// else keeps it on the primitive. The check is cached, so this no
		// longer costs three syscalls per call.
		if (Class() == "Camera")
		{
			auto rotation = Memory->read<Matrixes::Matrix3x3>(address + Offsets::Camera::Rotation);
			auto position = Memory->read<Vectors::Vector3>(address + Offsets::Camera::Position);

			sCFrame newCFrame
			{
				rotation.r00, rotation.r01, rotation.r02,
				rotation.r10, rotation.r11, rotation.r12,
				rotation.r20, rotation.r21, rotation.r22,
				position.x, position.y, position.z
			};
			return newCFrame;
		}
		else
		{
			uintptr_t primitiveAddr = Primitive();
			if (primitiveAddr == 0) return sCFrame{};
			return Memory->read<sCFrame>(primitiveAddr + Offsets::Primitive::Rotation);
		}
	}

	inline RobloxInstance Character() const
	{
		uintptr_t addr = Memory->read<uintptr_t>(address + Offsets::Player::ModelInstance);
		if (addr) return RobloxInstance(addr);
		if (g_ResolveCharacterFallback) return g_ResolveCharacterFallback(address);
		return RobloxInstance(0);
	}

	inline float Health() const
	{
		auto character = Character();
		auto humanoid = character.FindFirstChildWhichIsA("Humanoid");

		return Memory->read<float>(humanoid.address + Offsets::Humanoid::Health);
	}

	inline float MaxHealth() const
	{
		auto character = Character();
		auto humanoid = character.FindFirstChildWhichIsA("Humanoid");

		return Memory->read<float>(humanoid.address + Offsets::Humanoid::MaxHealth);
	}

	inline RobloxInstance Team() const
	{
		uintptr_t teamOffset = Globals::DynamicOffsets::PlayerTeam != 0 ? Globals::DynamicOffsets::PlayerTeam : Offsets::Player::Team;
		return RobloxInstance(Memory->read<uintptr_t>(address + teamOffset));
	}

	inline int RigType() const
	{
		return Memory->read<int>(address + Offsets::Humanoid::RigType);
	}

	// Rivals hides progression in CustomLeaderstats (NOT the usual "leaderstats")
	// under "Level" and "Current ELO". Both are Int64 payloads living at
	// Offsets::Misc::Value. Returns false rather than a number when the read does
	// not look like progression, so a transient miss leaves the ESP blank instead
	// of showing a garbage number.
	inline bool ProgressionValue(std::int64_t& out) const
	{
		if (address == 0)
			return false;

		const std::int64_t v = Memory->read<std::int64_t>(address + Offsets::Misc::Value);

		// ELO's floor is -1 for "unranked"; Level is always positive.
		if (v < -1 || v > 1000000)
			return false;

		out = v;
		return true;
	}

	// Case-insensitive child lookup by name, over a child list the caller already
	// has. Taking the list as a parameter matters: calling GetChildren() per
	// lookup re-scans the instance every time. Static because it does not touch
	// the instance it was called on.
	static RobloxInstance FindInChildren(const std::vector<RobloxInstance>& children, const std::string& name)
	{
		for (auto& child : children)
		{
			const std::string n = child.Name();
			if (n.size() == name.size() && ::_stricmp(n.c_str(), name.c_str()) == 0)
				return child;
		}
		return RobloxInstance(0);
	}

	inline RobloxInstance FindChildCI(const std::string& name) const
	{
		return FindInChildren(GetChildren(), name);
	}

	inline void SetWalkspeed(float value)
	{
		value = std::round(value);
		Memory->write(address + Offsets::Humanoid::WalkspeedCheck, value);
		Memory->write(address + Offsets::Humanoid::Walkspeed, value);
	}

	inline void SetJumpPower(float value)
	{
		value = std::round(value);
		Memory->write(address + Offsets::Humanoid::JumpPower, value);
	}

	inline float GetWalkspeed()
	{
		return std::round(Memory->read<float>(address + Offsets::Humanoid::Walkspeed));
	}

	inline float GetJumpPower()
	{
		return std::round(Memory->read<float>(address + Offsets::Humanoid::JumpPower));
	}

	inline float GetFOV()
	{
		auto radiantsFOV = Memory->read<float>(address + Offsets::Camera::FieldOfView);

		auto degreesFOV = radiantsFOV * 180 / 3.1415926535;

		return std::round(degreesFOV);

	}

	inline void SetFOV(float value)
	{
		value = std::round(value);

		auto radiantsValue = value * 3.1415926535 / 180;

		Memory->write<float>(address + Offsets::Camera::FieldOfView, radiantsValue);
	}

};