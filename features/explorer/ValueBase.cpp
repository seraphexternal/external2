#include "ValueBase.h"
#include "ExplorerTree.h"

namespace ValueBase
{
	bool IsClass(const std::string& cls)
	{
		return cls == "BoolValue" || cls == "IntValue" || cls == "NumberValue"
			|| cls == "StringValue" || cls == "ObjectValue" || cls == "Vector3Value";
	}

	std::uint64_t Field(std::uint64_t addr)
	{
		return addr + Offsets::Misc::Value;
	}

	bool GetBool(std::uint64_t addr)
	{
		return Memory->read<std::uint8_t>(Field(addr)) != 0;
	}

	void SetBool(std::uint64_t addr, bool v)
	{
		Memory->write<std::uint8_t>(Field(addr), v ? 1 : 0);
	}

	std::int32_t GetInt(std::uint64_t addr)
	{
		return Memory->read<std::int32_t>(Field(addr));
	}

	void SetInt(std::uint64_t addr, std::int32_t v)
	{
		Memory->write<std::int32_t>(Field(addr), v);
	}

	double GetNumber(std::uint64_t addr)
	{
		return Memory->read<double>(Field(addr));
	}

	void SetNumber(std::uint64_t addr, double v)
	{
		Memory->write<double>(Field(addr), v);
	}

	std::string GetString(std::uint64_t addr)
	{
		return Memory->readString(Field(addr));
	}

	bool SetString(std::uint64_t addr, const std::string& s)
	{
		if (s.size() >= 16)
			return false;
		char buf[24]{};
		std::memcpy(buf, s.data(), s.size());
		*reinterpret_cast<std::int32_t*>(buf + 0x10) = static_cast<std::int32_t>(s.size());
		Memory->writeRaw(static_cast<uintptr_t>(Field(addr)), buf, 0x18);
		return true;
	}

	std::uint64_t GetObject(std::uint64_t addr)
	{
		return Memory->read<std::uint64_t>(Field(addr));
	}

	void SetObject(std::uint64_t addr, std::uint64_t obj)
	{
		Memory->write<std::uint64_t>(Field(addr), obj);
	}

	Vectors::Vector3 GetVector3(std::uint64_t addr)
	{
		return Memory->read<Vectors::Vector3>(Field(addr));
	}

	void SetVector3(std::uint64_t addr, const Vectors::Vector3& v)
	{
		Memory->write<Vectors::Vector3>(Field(addr), v);
	}

	std::string Format(std::uint64_t addr, const std::string& cls)
	{
		if (!addr || !IsClass(cls))
			return {};

		char buf[96]{};
		if (cls == "BoolValue")
			return GetBool(addr) ? "true" : "false";
		if (cls == "IntValue")
		{
			std::snprintf(buf, sizeof(buf), "%d", GetInt(addr));
			return buf;
		}
		if (cls == "NumberValue")
		{
			std::snprintf(buf, sizeof(buf), "%.6g", GetNumber(addr));
			return buf;
		}
		if (cls == "StringValue")
		{
			std::string s = GetString(addr);
			if (s.size() > 40)
				s = s.substr(0, 40) + "...";
			return "\"" + s + "\"";
		}
		if (cls == "ObjectValue")
		{
			const std::uint64_t obj = GetObject(addr);
			if (!obj)
				return "nil";
			RobloxInstance inst(obj);
			std::string nm = inst.Name();
			std::string c = inst.Class();
			if (nm.empty())
				nm = "?";
			return nm + " (" + c + ")";
		}
		if (cls == "Vector3Value")
		{
			Vectors::Vector3 v = GetVector3(addr);
			std::snprintf(buf, sizeof(buf), "%.3g, %.3g, %.3g", v.x, v.y, v.z);
			return buf;
		}
		return {};
	}
}
