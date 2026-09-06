#pragma once
// Ported from jew-dick-hack: src/features/explorer/ExplorerTree.h
// Adapted to Seraph's RobloxInstance reflection API.
#include <cstdint>
#include <string>
#include <vector>

#include "../../rbx/SDK/sdk.h"
#include "../../rbx/globals/globals.h"

struct Node {
	std::uint64_t address = 0;
	std::string name;
	std::string cls;
	std::vector<Node> children;
	bool loaded = false;
	bool open = false;
	bool has_kids = false;
};

inline Node g_root;
inline std::uint64_t g_root_addr = 0;

inline RobloxInstance ExplorerInstance(uint64_t addr) { return RobloxInstance(addr); }

inline uint64_t ExplorerParentAddr(uint64_t address)
{
	if (address == 0 || Memory == nullptr)
		return 0;
	return Memory->read<uint64_t>(address + Offsets::Instance::Parent);
}

inline bool ExplorerHasChildren(const RobloxInstance& inst)
{
	return !inst.GetChildren().empty();
}

inline int ServiceRank(const std::string& cls)
{
	static const char* order[] = {
		"Workspace",
		"Players",
		"Lighting",
		"MaterialService",
		"ReplicatedFirst",
		"ReplicatedStorage",
		"ServerStorage",
		"ServerScriptService",
		"StarterGui",
		"StarterPack",
		"StarterPlayer",
		"SoundService",
		"Chat",
		"TextChatService",
		"Teams",
		"TeleportService",
		"TweenService",
		"RunService",
		"UserInputService",
		"GuiService",
		"HttpService",
		"MarketplaceService",
		"InsertService",
		"Debris",
	};
	for (int i = 0; i < (int)(sizeof(order) / sizeof(order[0])); ++i)
	{
		if (cls == order[i])
			return i;
	}
	return 1000;
}

inline bool ServiceLess(const Node& a, const Node& b)
{
	int ra = ServiceRank(a.cls);
	int rb = ServiceRank(b.cls);
	if (ra != rb)
		return ra < rb;
	return a.name < b.name;
}

inline void SortChildren(Node& n)
{
	if (n.cls != "DataModel")
		return;

	auto& v = n.children;
	for (size_t i = 1; i < v.size(); ++i)
	{
		Node key = std::move(v[i]);
		size_t j = i;
		while (j > 0 && ServiceLess(key, v[j - 1]))
		{
			v[j] = std::move(v[j - 1]);
			--j;
		}
		v[j] = std::move(key);
	}
}

inline void LoadChildren(Node& n)
{
	if (n.loaded)
		return;

	n.loaded = true;
	n.children.clear();

	RobloxInstance inst(n.address);
	for (const auto& c : inst.GetChildren())
	{
		Node cn;
		cn.address = c.address;
		cn.name = c.Name();
		cn.cls = c.Class();
		cn.has_kids = ExplorerHasChildren(c);
		n.children.push_back(std::move(cn));
	}
	n.has_kids = !n.children.empty();

	SortChildren(n);

	if (n.cls == "DataModel")
	{
		for (auto& c : n.children)
		{
			if (c.cls == "Workspace")
			{
				c.open = true;
				LoadChildren(c);
				break;
			}
		}
	}
}

inline void EnsureRoot()
{
	std::uint64_t dm = Globals::Roblox::DataModel.address;
	if (dm && dm != g_root_addr)
	{
		g_root_addr = dm;
		g_root = Node{};
		g_root.address = dm;
		g_root.cls = "DataModel";
		g_root.name = "game";
		g_root.open = true;
	}
	else if (!dm)
	{
		g_root_addr = 0;
		g_root = Node{};
	}
}

inline std::string BuildPath(std::uint64_t address)
{
	std::vector<std::string> parts;
	std::uint64_t cur = address;
	int guard = 64;

	while (cur && guard-- > 0)
	{
		RobloxInstance node(cur);
		std::string nm = node.Name();
		if (nm.empty())
			nm = "?";
		parts.push_back(nm);

		if (cur == g_root_addr)
			break;

		uint64_t parent = ExplorerParentAddr(cur);
		if (!parent)
			break;

		cur = parent;
	}

	std::string out;
	for (auto it = parts.rbegin(); it != parts.rend(); ++it)
	{
		if (!out.empty())
			out += ".";
		out += *it;
	}

	return out;
}
