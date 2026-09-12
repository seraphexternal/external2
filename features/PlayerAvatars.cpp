#include "PlayerAvatars.h"

#include "../Memory/MemoryManager.h"
#include "../rbx/globals/globals.h"
#include "../rbx/globals/options.h"
#include "../../overlay/renderer.h"

extern ID3D11Device* g_pd3dDevice;
#include "preview3d/stb_image.h"
#include "../../overlay/imgui/imgui.h"
#include <Windows.h>
#include <winhttp.h>
#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "winhttp.lib")

namespace Cheat {
namespace Features {
namespace PlayerAvatars {
namespace {

	struct Tex {
		ID3D11ShaderResourceView* srv = nullptr;
	};

	struct Pending {
		std::int64_t uid = 0;
		std::vector<std::uint8_t> png;
	};

	std::mutex g_mu;
	std::unordered_map<std::int64_t, Tex> g_tex;
	std::unordered_map<std::int64_t, float> g_fail_at;
	std::unordered_set<std::int64_t> g_queued;
	std::unordered_set<std::int64_t> g_loading;
	std::vector<std::int64_t> g_queue;
	std::vector<Pending> g_ready;

	std::unordered_map<std::string, std::int64_t> g_name_uid;
	std::unordered_set<std::string> g_name_queued;
	std::unordered_set<std::string> g_name_loading;
	std::vector<std::string> g_name_queue;
	std::unordered_map<std::string, float> g_name_fail_at;

	std::atomic<int> g_alive{ 0 };
	const int g_max_workers = 12;

	std::string lower_copy(std::string s)
	{
		for (char& c : s)
			c = (char)std::tolower((unsigned char)c);
		return s;
	}

	bool HttpGet(const wchar_t* host, INTERNET_PORT port, const wchar_t* path,
	             bool https, std::vector<std::uint8_t>& out)
	{
		out.clear();
		HINTERNET ses = WinHttpOpen(
			L"Seraph/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!ses)
		{
			return false;
		}

		HINTERNET con = WinHttpConnect(ses, host, port, 0);
		if (!con)
		{
			WinHttpCloseHandle(ses);
			return false;
		}

		DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET req = WinHttpOpenRequest(
			con, L"GET", path, nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
		if (!req)
		{
			WinHttpCloseHandle(con);
			WinHttpCloseHandle(ses);
			return false;
		}

		DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
		WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

		BOOL ok = WinHttpSendRequest(
			req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
		if (!ok || !WinHttpReceiveResponse(req, nullptr))
		{
			WinHttpCloseHandle(req);
			WinHttpCloseHandle(con);
			WinHttpCloseHandle(ses);
			return false;
		}

		for (;;)
		{
			DWORD avail = 0;
			if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
			{
				break;
			}

			std::size_t old = out.size();
			out.resize(old + avail);
			DWORD read = 0;
			if (!WinHttpReadData(req, out.data() + old, avail, &read))
			{
				out.resize(old);
				break;
			}

			out.resize(old + read);
			if (out.size() > (4u << 20))
			{
				break;
			}
		}

		WinHttpCloseHandle(req);
		WinHttpCloseHandle(con);
		WinHttpCloseHandle(ses);
		return !out.empty();
	}

	bool HttpPostJson(const wchar_t* host, const wchar_t* path, const std::string& body,
	                  std::vector<std::uint8_t>& out)
	{
		out.clear();
		HINTERNET ses = WinHttpOpen(
			L"Seraph/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!ses)
			return false;

		HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
		if (!con)
		{
			WinHttpCloseHandle(ses);
			return false;
		}

		HINTERNET req = WinHttpOpenRequest(
			con, L"POST", path, nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
		if (!req)
		{
			WinHttpCloseHandle(con);
			WinHttpCloseHandle(ses);
			return false;
		}

		wchar_t hdrs[] = L"Content-Type: application/json\r\n";
		BOOL ok = WinHttpSendRequest(
			req, hdrs, (DWORD)-1L,
			(LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
		if (!ok || !WinHttpReceiveResponse(req, nullptr))
		{
			WinHttpCloseHandle(req);
			WinHttpCloseHandle(con);
			WinHttpCloseHandle(ses);
			return false;
		}

		for (;;)
		{
			DWORD avail = 0;
			if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
				break;

			std::size_t old = out.size();
			out.resize(old + avail);
			DWORD read = 0;
			if (!WinHttpReadData(req, out.data() + old, avail, &read))
			{
				out.resize(old);
				break;
			}
			out.resize(old + read);
			if (out.size() > (1u << 20))
				break;
		}

		WinHttpCloseHandle(req);
		WinHttpCloseHandle(con);
		WinHttpCloseHandle(ses);
		return !out.empty();
	}

	std::int64_t ParseUserIdFromJson(const std::string& json, const std::string& name)
	{
		std::string key = "\"name\":\"" + name + "\"";
		std::size_t pos = json.find(key);
		if (pos == std::string::npos)
		{
			std::string low = lower_copy(name);
			std::string jlow = lower_copy(json);
			std::string k2 = "\"name\":\"" + low + "\"";
			pos = jlow.find(k2);
			if (pos == std::string::npos)
				pos = 0;
		}

		std::size_t id = json.find("\"id\":", pos == std::string::npos ? 0 : pos);
		if (id == std::string::npos)
			id = json.find("\"id\":");
		if (id == std::string::npos)
			return 0;

		id += 5;
		while (id < json.size() && (json[id] == ' ' || json[id] == '\t'))
			++id;

		char* end = nullptr;
		long long v = std::strtoll(json.c_str() + id, &end, 10);
		if (v <= 0)
			return 0;
		return (std::int64_t)v;
	}

	bool ResolveNameToUid(const std::string& name, std::int64_t& out_uid)
	{
		out_uid = 0;
		if (name.empty() || name == "unknown")
			return false;

		std::string body = "{\"usernames\":[\"";
		for (char c : name)
		{
			if (c == '"' || c == '\\')
				body.push_back('\\');
			body.push_back(c);
		}
		body += "\"],\"excludeBannedUsers\":false}";

		std::vector<std::uint8_t> raw;
		if (!HttpPostJson(L"users.roblox.com", L"/v1/usernames/users", body, raw))
			return false;

		std::string json(raw.begin(), raw.end());
		out_uid = ParseUserIdFromJson(json, name);
		return out_uid > 0;
	}

	bool ParseUrl(const std::string& url, std::wstring& host, std::wstring& path, bool& https)
	{
		https = true;
		const char* p = url.c_str();
		if (std::strncmp(p, "https://", 8) == 0)
		{
			p += 8;
			https = true;
		}

		else if (std::strncmp(p, "http://", 7) == 0)
		{
			p += 7;
			https = false;
		}

		else
		{
			return false;
		}

		const char* slash = std::strchr(p, '/');
		std::string h;
		std::string path_a;
		if (!slash)
		{
			h = p;
			path_a = "/";
		}

		else
		{
			h.assign(p, slash);
			path_a = slash;
		}

		if (h.empty())
		{
			return false;
		}

		host.assign(h.begin(), h.end());
		path.assign(path_a.begin(), path_a.end());
		return true;
	}

	std::string FindImageUrl(const std::string& json, std::int64_t uid)
	{
		char key[64]{};
		std::snprintf(key, sizeof(key), "\"targetId\":%lld", (long long)uid);

		std::size_t pos = json.find(key);
		if (pos == std::string::npos)
		{
			std::snprintf(key, sizeof(key), "\"targetId\": %lld", (long long)uid);
			pos = json.find(key);
		}

		std::size_t from = (pos == std::string::npos) ? 0 : pos;
		std::size_t u = json.find("\"imageUrl\"", from);
		if (u == std::string::npos)
		{
			return {};
		}

		u = json.find(':', u);
		if (u == std::string::npos)
		{
			return {};
		}

		u = json.find('"', u);
		if (u == std::string::npos)
		{
			return {};
		}

		std::size_t a = u + 1;
		std::size_t b = json.find('"', a);
		if (b == std::string::npos || b <= a)
		{
			return {};
		}

		std::string url = json.substr(a, b - a);
		for (std::size_t i = 0; i + 1 < url.size();)
		{
			if (url[i] == '\\' && url[i + 1] == '/')
			{
				url.erase(i, 1);
			}

			else
			{
				++i;
			}
		}

		return url;
	}

	bool FetchPng(std::int64_t uid, std::vector<std::uint8_t>& png)
	{
		png.clear();
		if (uid <= 0)
		{
			return false;
		}

		wchar_t path[160]{};
		_snwprintf_s(
			path, _TRUNCATE,
			L"/v1/users/avatar-bust?userIds=%lld&size=420x420&format=Png&isCircular=false",
			(long long)uid);

		std::vector<std::uint8_t> raw;
		if (!HttpGet(L"thumbnails.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, path, true, raw))
		{
			return false;
		}

		std::string json(raw.begin(), raw.end());
		if (json.find("\"Pending\"") != std::string::npos &&
			json.find("\"imageUrl\"") == std::string::npos)
		{
			return false;
		}

		std::string img = FindImageUrl(json, uid);
		if (img.empty())
		{
			return false;
		}

		std::wstring host, pth;
		bool https = true;
		if (!ParseUrl(img, host, pth, https))
		{
			return false;
		}

		INTERNET_PORT port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
		return HttpGet(host.c_str(), port, pth.c_str(), https, png);
	}

	void WorkerLoop()
	{
		for (;;)
		{
			std::string name_job;
			std::int64_t uid = 0;
			{
				std::lock_guard<std::mutex> lk(g_mu);
				if (!g_name_queue.empty())
				{
					name_job = g_name_queue.back();
					g_name_queue.pop_back();
					g_name_queued.erase(name_job);
					g_name_loading.insert(name_job);
				}
				else if (!g_queue.empty())
				{
					uid = g_queue.back();
					g_queue.pop_back();
					g_queued.erase(uid);
					g_loading.insert(uid);
				}
				else
				{
					break;
				}
			}

			if (!name_job.empty())
			{
				std::int64_t resolved = 0;
				bool ok = ResolveNameToUid(name_job, resolved);
				std::lock_guard<std::mutex> lk(g_mu);
				g_name_loading.erase(name_job);
				if (ok && resolved > 0)
				{
					g_name_uid[lower_copy(name_job)] = resolved;
					g_name_fail_at.erase(name_job);
					if (!g_tex.count(resolved) && !g_queued.count(resolved) && !g_loading.count(resolved))
					{
						g_queue.push_back(resolved);
						g_queued.insert(resolved);
					}
				}
				else
				{
					g_name_fail_at[name_job] = -1.f;
				}
				continue;
			}

			std::vector<std::uint8_t> png;
			bool ok = FetchPng(uid, png);

			{
				std::lock_guard<std::mutex> lk(g_mu);
				g_loading.erase(uid);
				if (ok && !png.empty())
				{
					Pending p{};
					p.uid = uid;
					p.png = std::move(png);
					g_ready.push_back(std::move(p));
				}
				else
				{
					g_fail_at[uid] = -1.f;
				}
			}
		}

		g_alive.fetch_sub(1);
	}

	void KickWorkers()
	{
		for (;;)
		{
			int cur = g_alive.load();
			if (cur >= g_max_workers)
			{
				return;
			}

			{
				std::lock_guard<std::mutex> lk(g_mu);
				if (g_queue.empty() && g_name_queue.empty())
					return;
			}

			if (!g_alive.compare_exchange_weak(cur, cur + 1))
			{
				continue;
			}

			std::thread(WorkerLoop).detach();
		}
	}

	ID3D11ShaderResourceView* MakeSrv(const std::vector<std::uint8_t>& png)
	{
		if (!Memory || png.empty())
		{
			return nullptr;
		}

		int w = 0, h = 0, n = 0;
		unsigned char* px = stbi_load_from_memory(
			png.data(), (int)png.size(), &w, &h, &n, 4);
		if (!px)
		{
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC td{};
		td.Width = (UINT)w;
		td.Height = (UINT)h;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA sd{};
		sd.pSysMem = px;
		sd.SysMemPitch = (UINT)(w * 4);

		ID3D11Texture2D* tex = nullptr;
		HRESULT hr = g_pd3dDevice->CreateTexture2D(&td, &sd, &tex);
		stbi_image_free(px);
		if (FAILED(hr) || !tex)
		{
			return nullptr;
		}

		ID3D11ShaderResourceView* srv = nullptr;
		hr = g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv);
		tex->Release();
		if (FAILED(hr))
		{
			return nullptr;
		}

		return srv;
	}

	void UploadReady()
	{
		std::vector<Pending> ready;
		{
			std::lock_guard<std::mutex> lk(g_mu);
			ready.swap(g_ready);
		}

		for (auto& p : ready)
		{
			ID3D11ShaderResourceView* srv = MakeSrv(p.png);
			if (!srv)
			{
				std::lock_guard<std::mutex> lk(g_mu);
				g_fail_at[p.uid] = -1.f;
				continue;
			}

			std::lock_guard<std::mutex> lk(g_mu);
			auto it = g_tex.find(p.uid);
			if (it != g_tex.end() && it->second.srv)
			{
				it->second.srv->Release();
			}

			g_tex[p.uid].srv = srv;
			g_fail_at.erase(p.uid);
		}
	}

	void EnqueueWanted()
	{
		struct Want {
			std::int64_t uid = 0;
			std::string name;
		};
		std::vector<Want> want;
		
		// Get local player name for avatar
		if (Globals::Roblox::LocalPlayer.address != 0)
		{
			std::string name = Globals::Roblox::LocalPlayer.Name();
			if (!name.empty())
			{
				want.push_back({0, name});
			}
		}

		float now = (float)ImGui::GetTime();
		bool kick = false;
		{
			std::lock_guard<std::mutex> lk(g_mu);

			for (auto& w : want)
			{
				std::int64_t uid = w.uid;
				if (uid <= 0 && !w.name.empty())
				{
					auto it = g_name_uid.find(lower_copy(w.name));
					if (it != g_name_uid.end())
						uid = it->second;
					else
					{
						if (g_name_queued.count(w.name) || g_name_loading.count(w.name))
							continue;

						auto fit = g_name_fail_at.find(w.name);
						if (fit != g_name_fail_at.end())
						{
							float t = fit->second;
							if (t < 0.f)
							{
								fit->second = now + 2.5f;
								continue;
							}
							if (now < t)
								continue;
							g_name_fail_at.erase(fit);
						}

						g_name_queue.push_back(w.name);
						g_name_queued.insert(w.name);
						kick = true;
						continue;
					}
				}

				if (uid <= 0)
					continue;
				if (g_tex.count(uid))
					continue;
				if (g_queued.count(uid) || g_loading.count(uid))
					continue;

				auto fit = g_fail_at.find(uid);
				if (fit != g_fail_at.end())
				{
					float t = fit->second;
					if (t < 0.f)
					{
						fit->second = now + 2.5f;
						continue;
					}
					if (now < t)
						continue;
					g_fail_at.erase(fit);
				}

				g_queue.push_back(uid);
				g_queued.insert(uid);
				kick = true;
			}

			while (g_queue.size() > 96)
			{
				std::int64_t drop = g_queue.front();
				g_queue.erase(g_queue.begin());
				g_queued.erase(drop);
			}
			while (g_name_queue.size() > 64)
			{
				std::string drop = g_name_queue.front();
				g_name_queue.erase(g_name_queue.begin());
				g_name_queued.erase(drop);
			}
		}

		if (!kick)
		{
			std::lock_guard<std::mutex> lk(g_mu);
			if ((!g_queue.empty() || !g_name_queue.empty()) && g_alive.load() < g_max_workers)
				kick = true;
		}

		if (kick)
			KickWorkers();
	}

} // namespace

void Tick()
{
	UploadReady();
	EnqueueWanted();
}

void Clear()
{
	{
		std::lock_guard<std::mutex> lk(g_mu);
		g_queue.clear();
		g_queued.clear();
		g_loading.clear();
		g_ready.clear();
		g_fail_at.clear();
		g_name_queue.clear();
		g_name_queued.clear();
		g_name_loading.clear();
		g_name_uid.clear();
		g_name_fail_at.clear();
		for (auto& kv : g_tex)
		{
			if (kv.second.srv)
				kv.second.srv->Release();
		}
		g_tex.clear();
	}
}

ID3D11ShaderResourceView* Get(std::int64_t user_id)
{
	if (user_id <= 0)
		return nullptr;

	std::lock_guard<std::mutex> lk(g_mu);
	auto it = g_tex.find(user_id);
	if (it == g_tex.end())
		return nullptr;

	return it->second.srv;
}

std::int64_t LookupUserId(const std::string& username)
{
	if (username.empty())
		return 0;
	std::lock_guard<std::mutex> lk(g_mu);
	auto it = g_name_uid.find(lower_copy(username));
	if (it == g_name_uid.end())
		return 0;
	return it->second;
}

}
}
}