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

	RobloxInstance(uintptr_t addy)
	{
		address = addy;
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
		return Memory->readString(NameAddress());
	}

	inline std::string Class() const
	{
		return Memory->readString(Memory->read<uintptr_t>(Memory->read<uintptr_t>(address + Offsets::Instance::ClassDescriptor) + Offsets::Instance::ClassName));
	}

	inline bool IsA(std::string className) const
	{
		if (Class() == className)
		{
			return true;
		}
		return false;
	}

	inline std::vector<RobloxInstance> GetChildren() const
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
		return Memory->read<Vectors::Vector3>(Memory->read<uintptr_t>(address + Offsets::BasePart::Primitive) + Offsets::Primitive::Position); // offsets::Primitive) + offsets::Position
	}

	inline Vectors::Vector3 Size() const
	{
		return Memory->read<Vectors::Vector3>(Memory->read<uintptr_t>(address + Offsets::BasePart::Primitive) + Offsets::Primitive::Size); // offsets::Primitive) + offsets::PartSize
	}

	inline sCFrame CFrame() const
	{
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
			uintptr_t primitiveAddr = Memory->read<uintptr_t>(address + Offsets::BasePart::Primitive);
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