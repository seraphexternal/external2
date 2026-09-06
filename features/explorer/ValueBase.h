#pragma once
// Ported from jew-dick-hack: src/features/explorer/ValueBase.h
// Adapted to Seraph's MemoryManager (void writeRaw) + RobloxInstance API.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../Memory/MemoryManager.h"
#include "../../rbx/offsets.h"
#include "../../rbx/SDK/sdk.h"
#include "../../rbx/math/math.h"

namespace ValueBase
{
	bool IsClass(const std::string& cls);

	std::uint64_t Field(std::uint64_t addr);

	bool GetBool(std::uint64_t addr);
	void SetBool(std::uint64_t addr, bool v);

	std::int32_t GetInt(std::uint64_t addr);
	void SetInt(std::uint64_t addr, std::int32_t v);

	double GetNumber(std::uint64_t addr);
	void SetNumber(std::uint64_t addr, double v);

	std::string GetString(std::uint64_t addr);
	bool SetString(std::uint64_t addr, const std::string& s);

	std::uint64_t GetObject(std::uint64_t addr);
	void SetObject(std::uint64_t addr, std::uint64_t obj);

	Vectors::Vector3 GetVector3(std::uint64_t addr);
	void SetVector3(std::uint64_t addr, const Vectors::Vector3& v);

	std::string Format(std::uint64_t addr, const std::string& cls);
}
