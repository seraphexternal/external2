#pragma once
// Ported from jew-dick-hack: src/features/explorer/ExplorerSearch.h
// Adapted to Seraph's RobloxInstance reflection API. Uses g_root_addr from
// ExplorerTree.h.
#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../rbx/SDK/sdk.h"

struct SearchResult {
	std::uint64_t address = 0;
	std::string name;
	std::string cls;
	std::string path;
};

inline std::mutex g_search_mtx;
inline std::vector<SearchResult> g_search_results;
inline std::atomic<std::uint32_t> g_search_gen{ 0 };
inline std::atomic<bool> g_searching{ false };
inline bool g_search_on = false;

inline std::string ExplorerToLower(std::string s)
{
	for (char& ch : s)
		ch = (char)std::tolower((unsigned char)ch);
	return s;
}

void ExplorerRunSearch(std::string query, std::uint64_t root_addr, std::uint32_t gen)
{
	std::string q = ExplorerToLower(query);
	std::vector<SearchResult> local;

	struct Frame
	{
		std::uint64_t addr;
		std::string path;
	};
	std::vector<Frame> stack;
	stack.push_back({ root_addr, "game" });

	int budget = 400000;
	while (!stack.empty())
	{
		if (g_search_gen.load() != gen)
			return;

		if (budget-- <= 0)
			break;

		Frame f = std::move(stack.back());
		stack.pop_back();

		RobloxInstance inst(f.addr);
		for (const auto& child : inst.GetChildren())
		{
			if (g_search_gen.load() != gen)
				return;

			std::string name = child.Name();
			std::string cls = child.Class();

			if (ExplorerToLower(name).find(q) != std::string::npos ||
				ExplorerToLower(cls).find(q) != std::string::npos)
			{
				if ((int)local.size() < 1000)
				{
					SearchResult r;
					r.address = child.address;
					r.name = name;
					r.cls = cls;
					r.path = f.path + "." + name;
					local.push_back(std::move(r));
				}
			}

			if ((int)stack.size() < 400000)
				stack.push_back({ child.address, f.path + "." + name });
		}
	}

	if (g_search_gen.load() != gen)
		return;

	{
		std::lock_guard<std::mutex> lk(g_search_mtx);
		g_search_results = std::move(local);
	}
	g_searching.store(false);
}

inline void StartSearch(const char* query)
{
	std::uint32_t gen = ++g_search_gen;
	g_searching.store(true);
	g_search_on = true;
	{
		std::lock_guard<std::mutex> lk(g_search_mtx);
		g_search_results.clear();
	}

	if (!g_root_addr)
	{
		g_searching.store(false);
		return;
	}

	std::thread(ExplorerRunSearch, std::string(query), g_root_addr, gen).detach();
}

inline void StopSearch()
{
	++g_search_gen;
	g_searching.store(false);
	g_search_on = false;
	std::lock_guard<std::mutex> lk(g_search_mtx);
	g_search_results.clear();
}
