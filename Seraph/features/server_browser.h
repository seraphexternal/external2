#pragma once

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "../rbx/configs/json.hpp"
#include "../overlay/imgui/imgui.h"
#include "stb_image.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

extern ID3D11Device* g_pd3dDevice;

namespace Cheat {
namespace Features {
namespace ServerBrowser {
namespace {

	inline std::wstring to_w(const std::string& s)
	{
		if (s.empty())
			return std::wstring();
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
		if (n <= 0)
			return std::wstring();
		std::wstring out((size_t)n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
		return out;
	}

	inline std::vector<std::uint8_t> base64_decode(const std::string& in)
	{
		static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::vector<std::uint8_t> out;
		int val = 0, bits = -8;
		for (unsigned char c : in)
		{
			if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
				continue;
			const char* p = strchr(tbl, (char)c);
			if (!p)
				continue;
			val = (val << 6) | (int)(p - tbl);
			bits += 6;
			if (bits >= 0)
			{
				out.push_back((std::uint8_t)((val >> bits) & 0xFF));
				bits -= 8;
			}
		}
		return out;
	}

	inline bool dpapi_unprotect(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out)
	{
		if (in.empty())
			return false;
		DATA_BLOB din{};
		din.pbData = (BYTE*)in.data();
		din.cbData = (DWORD)in.size();
		DATA_BLOB dout{};
		if (!CryptUnprotectData(&din, nullptr, nullptr, nullptr, nullptr, 0, &dout))
			return false;
		out.assign(dout.pbData, dout.pbData + dout.cbData);
		LocalFree(dout.pbData);
		return true;
	}

	inline bool parse_url(const std::string& url, std::wstring& host, std::wstring& path, bool& https)
	{
		std::string u = url;
		https = true;
		if (u.rfind("https://", 0) == 0)
		{
			u = u.substr(8);
			https = true;
		}
		else if (u.rfind("http://", 0) == 0)
		{
			u = u.substr(7);
			https = false;
		}
		if (u.empty())
			return false;
		std::string h, p;
		size_t slash = u.find('/');
		if (slash == std::string::npos)
		{
			h = u;
			p = "/";
		}
		else
		{
			h = u.substr(0, slash);
			p = u.substr(slash);
		}
		size_t colon = h.find(':');
		if (colon != std::string::npos)
			h = h.substr(0, colon);
		if (h.empty())
			return false;
		host = to_w(h);
		path = to_w(p);
		return true;
	}

	inline bool do_http(const std::wstring& host, INTERNET_PORT port, const std::wstring& path, bool https,
	                    const std::wstring& method, const std::wstring& headers,
	                    const std::string* body, std::string& out, DWORD timeout_ms = 8000)
	{
		out.clear();
		HINTERNET ses = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!ses)
			return false;
		WinHttpSetTimeouts(ses, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms);

		HINTERNET con = WinHttpConnect(ses, host.c_str(), port, 0);
		if (!con)
		{
			WinHttpCloseHandle(ses);
			return false;
		}

		DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET req = WinHttpOpenRequest(con, method.c_str(), path.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
		if (!req)
		{
			WinHttpCloseHandle(con);
			WinHttpCloseHandle(ses);
			return false;
		}

		DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
		WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
		DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
		WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));

		const wchar_t* hdr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
		DWORD hdr_len = headers.empty() ? 0 : (DWORD)-1L;
		LPVOID body_ptr = body ? (LPVOID)body->data() : WINHTTP_NO_REQUEST_DATA;
		DWORD body_len = body ? (DWORD)body->size() : 0;

		BOOL ok = WinHttpSendRequest(req, hdr, hdr_len, body_ptr, body_len, body_len, 0);
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
			size_t old = out.size();
			out.resize(old + avail);
			DWORD read = 0;
			if (!WinHttpReadData(req, &out[old], avail, &read))
			{
				out.resize(old);
				break;
			}
			out.resize(old + read);
			if (out.size() > (8u << 20))
				break;
		}

		WinHttpCloseHandle(req);
		WinHttpCloseHandle(con);
		WinHttpCloseHandle(ses);
		return !out.empty();
	}

	inline bool http_image(const std::string& url, std::vector<std::uint8_t>& png)
	{
		std::wstring host, path;
		bool https = true;
		if (!parse_url(url, host, path, https))
			return false;
		INTERNET_PORT port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
		std::string out;
		if (!do_http(host, port, path, https, L"GET", L"", nullptr, out, 6000))
			return false;
		png.assign(out.begin(), out.end());
		return !png.empty();
	}

	inline std::string read_file_text(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return std::string();
		std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		return s;
	}

	inline std::string extract_cookie(const std::string& text)
	{
		const char* name = ".ROBLOSECURITY";
		size_t p = text.find(name);
		if (p == std::string::npos)
			return std::string();
		p += strlen(name);
		while (p < text.size())
		{
			char c = text[p];
			if (c == ' ' || c == '\t' || c == ':' || c == '=' || c == '"' || c == '\r' || c == '\n')
				p++;
			else
				break;
		}
		size_t e = p;
		while (e < text.size())
		{
			char c = text[e];
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';' || c == '"')
				break;
			e++;
		}
		return text.substr(p, e - p);
	}

	inline std::string load_cookie_file(std::string& status)
	{
		char lad[512]{};
		DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", lad, (DWORD)sizeof(lad));
		if (n == 0 || n >= sizeof(lad))
		{
			status = "no LOCALAPPDATA";
			return std::string();
		}
		std::string path = std::string(lad, n) + "\\Roblox\\LocalStorage\\robloxcookies.dat";
		std::string raw = read_file_text(path);
		if (raw.empty())
		{
			status = "file not found";
			return std::string();
		}
		size_t k = raw.find("\"CookiesData\"");
		if (k == std::string::npos)
		{
			status = "no CookiesData";
			return std::string();
		}
		size_t q1 = raw.find('"', raw.find(':', k));
		if (q1 == std::string::npos)
		{
			status = "malformed";
			return std::string();
		}
		size_t q2 = raw.find('"', q1 + 1);
		if (q2 == std::string::npos)
		{
			status = "malformed";
			return std::string();
		}
		std::string b64 = raw.substr(q1 + 1, q2 - q1 - 1);
		std::vector<std::uint8_t> blob = base64_decode(b64);
		std::vector<std::uint8_t> plain;
		if (!dpapi_unprotect(blob, plain))
		{
			status = "decrypt failed";
			return std::string();
		}
		std::string text(plain.begin(), plain.end());
		std::string ck = extract_cookie(text);
		if (ck.empty())
		{
			status = "no cookie";
			return std::string();
		}
		status = "ok";
		return ck;
	}

	inline std::string extract_ip(const std::string& body)
	{
		try
		{
			nlohmann::json j = nlohmann::json::parse(body);
			if (j.contains("joinScript"))
			{
				auto& js = j["joinScript"];
				if (js.contains("MachineAddress") && js["MachineAddress"].is_string())
					return js["MachineAddress"].get<std::string>();
				if (js.contains("UdmuxEndpoints") && js["UdmuxEndpoints"].is_array() && !js["UdmuxEndpoints"].empty()
					&& js["UdmuxEndpoints"][0].contains("Address"))
					return js["UdmuxEndpoints"][0]["Address"].get<std::string>();
				if (js.contains("Address") && js["Address"].is_string())
					return js["Address"].get<std::string>();
			}
			if (j.contains("MachineAddress") && j["MachineAddress"].is_string())
				return j["MachineAddress"].get<std::string>();
		}
		catch (...) {}
		return std::string();
	}

	inline std::string geolocate(const std::string& ip, const std::string& cookie)
	{
		std::wstring path = L"/json/" + to_w(ip) + L"?fields=status,country,regionName,city";
		std::string resp;
		if (!do_http(L"ip-api.com", INTERNET_DEFAULT_HTTP_PORT, path, false, L"GET", L"", nullptr, resp, 6000))
			return std::string();
		try
		{
			nlohmann::json j = nlohmann::json::parse(resp);
			if (j.value("status", std::string()) != "success")
				return std::string();
			std::string city = j.value("city", std::string());
			std::string country = j.value("country", std::string());
			if (!city.empty() && !country.empty())
				return city + ", " + country;
			if (!country.empty())
				return country;
			return j.value("regionName", std::string());
		}
		catch (...)
		{
			return std::string();
		}
	}

	struct ServerEntry {
		std::string id;
		int playing = 0;
		int maxPlayers = 0;
		int ping = -1;
		double fps = 0.0;
		std::vector<std::string> playerTokens;
	};

	struct AvatarPx {
		std::string token;
		std::vector<std::uint8_t> rgba;
		int w = 0;
		int h = 0;
	};

	struct State {
		std::mutex mu;

		std::int64_t placeId = 0;
		bool fetching = false;
		int gen = 0;
		std::string status = "not in a game";
		std::vector<ServerEntry> servers;

		std::unordered_map<std::string, ID3D11ShaderResourceView*> tex;
		std::unordered_map<std::string, int> texState;
		std::vector<std::string> avatarQueue;
		std::vector<AvatarPx> uploads;
		bool avatarRunning = false;

		std::unordered_map<std::string, std::string> loc;
		std::unordered_map<std::string, std::string> locByIp;
		std::unordered_set<std::string> locPending;
		std::vector<std::string> locQueue;
		bool locRunning = false;

		std::string cookie;
		std::string cookieStatus = "unread";
		bool cookieRunning = false;

		int sortMode = 0;
		int filtMinPlayers = 0;
		int filtMaxPing = 0;
		bool filtPingCap = false;
		bool filtHideFull = false;
		bool filtHideEmpty = false;

		bool posInit = false;
		ImVec2 pos = ImVec2(0, 0);
	};

	inline State& S()
	{
		static State s;
		return s;
	}

	inline ID3D11ShaderResourceView* make_tex(const std::vector<std::uint8_t>& rgba, int w, int h)
	{
		if (!g_pd3dDevice || rgba.empty() || w <= 0 || h <= 0)
			return nullptr;
		if (rgba.size() < (size_t)w * (size_t)h * 4u)
			return nullptr;

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
		sd.pSysMem = rgba.data();
		sd.SysMemPitch = (UINT)(w * 4);

		ID3D11Texture2D* tex = nullptr;
		if (FAILED(g_pd3dDevice->CreateTexture2D(&td, &sd, &tex)) || !tex)
			return nullptr;

		ID3D11ShaderResourceView* srv = nullptr;
		HRESULT hr = g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv);
		tex->Release();
		if (FAILED(hr))
			return nullptr;
		return srv;
	}

	inline void cookie_worker()
	{
		std::string status;
		std::string ck = load_cookie_file(status);
		State& s = S();
		std::lock_guard<std::mutex> lk(s.mu);
		s.cookie = ck;
		s.cookieStatus = status;
		s.cookieRunning = false;
	}

	inline void fetch_worker(std::int64_t place_id, int gen)
	{
		std::wstring path = L"/v1/games/" + std::to_wstring(place_id) + L"/servers/Public?limit=100&sortOrder=Asc";
		std::string body;
		bool ok = do_http(L"games.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, path, true,
			L"GET", L"Accept: application/json\r\n", nullptr, body, 10000);

		std::vector<ServerEntry> out;
		std::string status;
		if (!ok)
		{
			status = "request failed";
		}
		else
		{
			try
			{
				nlohmann::json j = nlohmann::json::parse(body);
				if (j.contains("data") && j["data"].is_array())
				{
					for (auto& sv : j["data"])
					{
						ServerEntry e;
						e.id = sv.value("id", std::string());
						e.playing = sv.value("playing", 0);
						e.maxPlayers = sv.value("maxPlayers", 0);
						e.ping = sv.value("ping", -1);
						e.fps = sv.value("fps", 0.0);
						if (sv.contains("playerTokens") && sv["playerTokens"].is_array())
						{
							for (auto& t : sv["playerTokens"])
								if (t.is_string())
									e.playerTokens.push_back(t.get<std::string>());
						}
						if (!e.id.empty())
							out.push_back(std::move(e));
					}
					status = std::to_string(out.size()) + " servers";
				}
				else
				{
					status = "bad response";
				}
			}
			catch (...)
			{
				status = "parse failed";
			}
		}

		State& s = S();
		std::lock_guard<std::mutex> lk(s.mu);
		if (s.gen != gen)
			return;
		s.fetching = false;
		s.status = status;
		if (!out.empty() || ok)
			s.servers = std::move(out);
	}

	inline void avatar_worker()
	{
		State& s = S();
		for (;;)
		{
			std::vector<std::string> batch;
			std::string cookie;
			{
				std::lock_guard<std::mutex> lk(s.mu);
				if (s.avatarQueue.empty())
				{
					s.avatarRunning = false;
					return;
				}
				while (!s.avatarQueue.empty() && batch.size() < 20)
				{
					batch.push_back(s.avatarQueue.back());
					s.avatarQueue.pop_back();
				}
				cookie = s.cookie;
			}

			std::string req = "[";
			for (size_t i = 0; i < batch.size(); ++i)
			{
				if (i)
					req += ",";
				std::string t = batch[i];
				req += "{\"requestId\":\"" + t + "\",\"token\":\"" + t +
					"\",\"type\":\"AvatarHeadShot\",\"size\":\"150x150\",\"format\":\"Png\",\"isCircular\":true}";
			}
			req += "]";

			std::wstring hdrs = L"Content-Type: application/json\r\n";
			if (!cookie.empty())
				hdrs += L"Cookie: .ROBLOSECURITY=" + to_w(cookie) + L"\r\n";

			std::string resp;
			bool ok = do_http(L"thumbnails.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, L"/v1/batch", true,
				L"POST", hdrs, &req, resp, 10000);

			std::unordered_map<std::string, std::string> urlByReq;
			if (ok)
			{
				try
				{
					nlohmann::json j = nlohmann::json::parse(resp);
					if (j.contains("data") && j["data"].is_array())
					{
						for (auto& d : j["data"])
						{
							std::string rid = d.value("requestId", std::string());
							std::string img = d.value("imageUrl", std::string());
							std::string st = d.value("state", std::string());
							if (!rid.empty() && st == "Completed" && !img.empty())
								urlByReq[rid] = img;
						}
					}
				}
				catch (...) {}
			}

			for (auto& t : batch)
			{
				ID3D11ShaderResourceView* dummy = nullptr;
				(void)dummy;
				if (urlByReq.find(t) == urlByReq.end())
				{
					std::lock_guard<std::mutex> lk(s.mu);
					auto it = s.texState.find(t);
					if (it != s.texState.end() && it->second == 1)
						it->second = 3;
					continue;
				}
				std::vector<std::uint8_t> png;
				if (!http_image(urlByReq[t], png))
				{
					std::lock_guard<std::mutex> lk(s.mu);
					auto it = s.texState.find(t);
					if (it != s.texState.end() && it->second == 1)
						it->second = 3;
					continue;
				}
				int w = 0, h = 0, n = 0;
				unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 4);
				if (!px)
				{
					std::lock_guard<std::mutex> lk(s.mu);
					auto it = s.texState.find(t);
					if (it != s.texState.end() && it->second == 1)
						it->second = 3;
					continue;
				}
				AvatarPx up;
				up.token = t;
				up.w = w;
				up.h = h;
				up.rgba.assign(px, px + (size_t)w * (size_t)h * 4u);
				stbi_image_free(px);
				std::lock_guard<std::mutex> lk(s.mu);
				s.uploads.push_back(std::move(up));
			}
		}
	}

	inline void location_worker(std::int64_t place_id)
	{
		State& s = S();
		for (;;)
		{
			std::string id;
			std::string cookie;
			{
				std::lock_guard<std::mutex> lk(s.mu);
				if (s.locQueue.empty())
				{
					s.locRunning = false;
					return;
				}
				id = s.locQueue.back();
				s.locQueue.pop_back();
				cookie = s.cookie;
			}
			if (cookie.empty())
			{
				std::lock_guard<std::mutex> lk(s.mu);
				s.locPending.erase(id);
				s.loc[id] = "no cookie";
				continue;
			}

			std::string body = "{\"placeId\":" + std::to_string(place_id) +
				",\"isTeleport\":false,\"gameId\":\"" + id + "\",\"gameJoinAttemptId\":\"" + id + "\"}";
			std::wstring hdrs = L"Content-Type: application/json\r\n";
			hdrs += L"Cookie: .ROBLOSECURITY=" + to_w(cookie) + L"\r\n";
			hdrs += L"User-Agent: Roblox/WinInet\r\n";

			std::string resp;
			bool ok = do_http(L"gamejoin.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, L"/v1/join-game-instance",
				true, L"POST", hdrs, &body, resp, 8000);

			std::string result;
			if (!ok)
			{
				result = "request failed";
			}
			else
			{
				std::string ip = extract_ip(resp);
				if (ip.empty())
				{
					result = "unknown";
				}
				else
				{
					{
						std::lock_guard<std::mutex> lk(s.mu);
						auto it = s.locByIp.find(ip);
						if (it != s.locByIp.end())
							result = it->second;
					}
					if (result.empty())
					{
						std::string geo = geolocate(ip, cookie);
						result = geo.empty() ? ip : geo;
						std::lock_guard<std::mutex> lk(s.mu);
						s.locByIp[ip] = result;
					}
				}
			}

			{
				std::lock_guard<std::mutex> lk(s.mu);
				s.loc[id] = result;
				s.locPending.erase(id);
			}
			Sleep(650);
		}
	}

	inline void kick_avatar()
	{
		State& s = S();
		{
			std::lock_guard<std::mutex> lk(s.mu);
			if (s.avatarRunning || s.avatarQueue.empty())
				return;
			s.avatarRunning = true;
		}
		std::thread(avatar_worker).detach();
	}

	inline void kick_location()
	{
		State& s = S();
		std::int64_t pid;
		{
			std::lock_guard<std::mutex> lk(s.mu);
			if (s.locRunning || s.locQueue.empty())
				return;
			s.locRunning = true;
			pid = s.placeId;
		}
		std::thread(location_worker, pid).detach();
	}

	inline void kick_cookie()
	{
		State& s = S();
		{
			std::lock_guard<std::mutex> lk(s.mu);
			if (s.cookieRunning)
				return;
			s.cookieRunning = true;
			if (s.cookieStatus == "unread")
				s.cookieStatus = "reading";
		}
		std::thread(cookie_worker).detach();
	}

	inline void release_textures()
	{
		State& s = S();
		std::lock_guard<std::mutex> lk(s.mu);
		for (auto& kv : s.tex)
			if (kv.second)
				kv.second->Release();
		s.tex.clear();
		s.texState.clear();
		s.avatarQueue.clear();
		s.uploads.clear();
	}

	inline ImU32 ping_color(int ms)
	{
		if (ms < 0)
			return IM_COL32(150, 150, 150, 255);
		if (ms < 120)
			return IM_COL32(90, 210, 90, 255);
		if (ms < 200)
			return IM_COL32(235, 210, 80, 255);
		if (ms < 300)
			return IM_COL32(235, 140, 60, 255);
		return IM_COL32(235, 70, 70, 255);
	}

	constexpr float kRowH = 30.f;
	constexpr float kTitleH = 26.f;
	constexpr float kMargin = 3.f;

} // namespace

inline void RequestRefresh();

inline void Tick()
{
	State& s = S();

	if (s.cookieStatus == "unread")
		kick_cookie();

	std::vector<AvatarPx> up;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		up.swap(s.uploads);
	}
	for (auto& p : up)
	{
		ID3D11ShaderResourceView* srv = make_tex(p.rgba, p.w, p.h);
		std::lock_guard<std::mutex> lk(s.mu);
		if (srv)
		{
			auto it = s.tex.find(p.token);
			if (it != s.tex.end() && it->second)
				it->second->Release();
			s.tex[p.token] = srv;
			s.texState[p.token] = 2;
		}
		else
		{
			s.texState[p.token] = 3;
		}
	}
}

inline void SetPlaceId(std::int64_t place_id)
{
	State& s = S();
	bool changed = false;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		if (place_id != s.placeId)
		{
			s.placeId = place_id;
			s.gen++;
			s.servers.clear();
			s.fetching = false;
			s.loc.clear();
			s.locPending.clear();
			s.locQueue.clear();
			s.status = place_id ? "loading" : "not in a game";
			changed = true;
		}
	}
	if (changed)
	{
		release_textures();
		if (place_id)
			RequestRefresh();
	}
}

inline void RequestRefresh()
{
	State& s = S();
	std::int64_t pid;
	int gen;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		if (s.placeId == 0 || s.fetching)
			return;
		s.fetching = true;
		s.gen++;
		gen = s.gen;
		pid = s.placeId;
		s.status = "fetching";
	}
	std::thread(fetch_worker, pid, gen).detach();
}

inline bool IsFetching()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mu);
	return s.fetching;
}

inline std::string Status()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mu);
	return s.status;
}

inline std::vector<ServerEntry> Servers()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mu);
	return s.servers;
}

inline ID3D11ShaderResourceView* AvatarTexture(const std::string& token)
{
	if (token.empty())
		return nullptr;
	State& s = S();
	bool kick = false;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		auto it = s.tex.find(token);
		if (it != s.tex.end())
			return it->second;
		auto st = s.texState.find(token);
		if (st != s.texState.end() && (st->second == 1 || st->second == 3))
			return nullptr;
		s.texState[token] = 1;
		s.avatarQueue.push_back(token);
		kick = true;
	}
	if (kick)
		kick_avatar();
	return nullptr;
}

inline std::string Location(const std::string& serverId)
{
	if (serverId.empty())
		return std::string();
	State& s = S();
	bool kick = false;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		auto it = s.loc.find(serverId);
		if (it != s.loc.end())
			return it->second;
		if (!s.locPending.count(serverId))
		{
			s.locPending.insert(serverId);
			s.locQueue.push_back(serverId);
			kick = true;
		}
	}
	if (kick)
		kick_location();
	return std::string();
}

inline bool LocationPending(const std::string& serverId)
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mu);
	return s.locPending.count(serverId) > 0;
}

inline void JoinServer(const std::string& serverId)
{
	State& s = S();
	std::int64_t pid;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		pid = s.placeId;
	}
	if (pid == 0 || serverId.empty())
		return;
	std::wstring url = L"roblox://experiences/start?placeId=" + std::to_wstring(pid) +
		L"&gameInstanceId=" + to_w(serverId);
	ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline void RenderWindow(bool* open)
{
	if (!open || !*open)
		return;

	State& s = S();

	if (!s.posInit)
	{
		ImVec2 c = ImGui::GetMainViewport()->GetCenter();
		s.pos = ImVec2(c.x - 270.f, c.y - 150.f);
		s.posInit = true;
	}

	ImGui::SetNextWindowPos(s.pos, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(540.f, 460.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(420.f, 260.f), ImVec2(FLT_MAX, FLT_MAX));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.13f, 0.13f, 0.13f, 1.f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));

	bool visible = ImGui::Begin("##serverbrowser_window", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar();

	if (!visible)
	{
		ImGui::End();
		return;
	}

	s.pos = ImGui::GetWindowPos();
	ImVec2 wp = s.pos;
	ImVec2 ws = ImGui::GetWindowSize();
	ImDrawList* draw = ImGui::GetWindowDrawList();

	draw->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + kTitleH), IM_COL32(20, 20, 20, 255));

	std::vector<ServerEntry> servers;
	{
		std::lock_guard<std::mutex> lk(s.mu);
		servers = s.servers;
	}
	if (s.filtHideFull || s.filtHideEmpty || s.filtMinPlayers > 0 || s.filtPingCap)
	{
		std::vector<ServerEntry> view;
		view.reserve(servers.size());
		for (auto& e : servers)
		{
			if (s.filtHideFull && e.maxPlayers > 0 && e.playing >= e.maxPlayers) continue;
			if (s.filtHideEmpty && e.playing <= 0) continue;
			if (e.playing < s.filtMinPlayers) continue;
			if (s.filtPingCap && e.ping >= 0 && e.ping > s.filtMaxPing) continue;
			view.push_back(e);
		}
		servers.swap(view);
	}
	if (s.sortMode == 1)
		std::stable_sort(servers.begin(), servers.end(), [](const ServerEntry& a, const ServerEntry& b) { return a.playing > b.playing; });
	else if (s.sortMode == 2)
		std::stable_sort(servers.begin(), servers.end(), [](const ServerEntry& a, const ServerEntry& b) { return a.playing < b.playing; });
	else if (s.sortMode == 3)
		std::stable_sort(servers.begin(), servers.end(), [](const ServerEntry& a, const ServerEntry& b) {
			int ap = a.ping < 0 ? 1 << 30 : a.ping;
			int bp = b.ping < 0 ? 1 << 30 : b.ping;
			return ap < bp;
		});
	else if (s.sortMode == 4)
		std::stable_sort(servers.begin(), servers.end(), [](const ServerEntry& a, const ServerEntry& b) { return a.ping > b.ping; });

	int count = (int)servers.size();

	char title[64];
	std::snprintf(title, sizeof(title), "server browser");
	char badge[32];
	std::snprintf(badge, sizeof(badge), "%d", count);
	ImVec2 title_ts = ImGui::CalcTextSize(title);
	draw->AddText(ImVec2(wp.x + (ws.x - title_ts.x) * 0.5f, wp.y + (kTitleH - title_ts.y) * 0.5f),
		IM_COL32(230, 230, 230, 255), title);

	ImVec2 xsz = ImGui::CalcTextSize("X");
	float x_w = xsz.x + 14.f;
	float btn_h = kTitleH - 8.f;

	ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - x_w, wp.y + 4.f));
	if (ImGui::InvisibleButton("##sb_close", ImVec2(x_w, btn_h)))
		*open = false;
	draw->AddText(ImVec2(wp.x + ws.x - x_w * 0.5f - xsz.x * 0.5f, wp.y + (kTitleH - xsz.y) * 0.5f),
		IM_COL32(210, 210, 210, 255), "X");

	float refresh_w = 62.f;
	ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - x_w - 6.f - refresh_w, wp.y + 4.f));
	if (ImGui::Button("refresh", ImVec2(refresh_w, btn_h)))
		RequestRefresh();

	{
		ImVec2 bsz = ImGui::CalcTextSize(badge);
		float pill_w = bsz.x + 12.f;
		float pill_right = wp.x + ws.x - x_w - 6.f - refresh_w - 8.f;
		ImVec2 p0(pill_right - pill_w, wp.y + (kTitleH - 14.f) * 0.5f);
		draw->AddRectFilled(p0, ImVec2(p0.x + pill_w, p0.y + 14.f), IM_COL32(45, 45, 45, 255), 7.f);
		draw->AddText(ImVec2(p0.x + 6.f, p0.y + (14.f - bsz.y) * 0.5f), IM_COL32(200, 200, 200, 255), badge);
	}

	ImGui::SetCursorPos(ImVec2(kMargin, kTitleH + kMargin));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 2.f));
	ImGui::BeginChild("##sb_head", ImVec2(0.f, 24.f), false);
	{
		std::string status;
		{
			std::lock_guard<std::mutex> lk(s.mu);
			status = s.status;
		}
		ImGui::TextUnformatted(status.c_str());
		float fw = 58.f;
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - fw - 8.f);
		if (ImGui::Button("filter", ImVec2(fw, 20.f)))
			ImGui::OpenPopup("##sb_filter");
		if (ImGui::BeginPopup("##sb_filter"))
		{
			ImGui::TextUnformatted("filter & sort");
			ImGui::Separator();
			ImGui::Combo("sort", &s.sortMode, "server order\0players high\0players low\0ping low\0ping high\0");
			ImGui::Checkbox("hide full", &s.filtHideFull);
			ImGui::Checkbox("hide empty", &s.filtHideEmpty);
			ImGui::SliderInt("min players", &s.filtMinPlayers, 0, 50);
			ImGui::Checkbox("ping cap", &s.filtPingCap);
			if (s.filtPingCap)
				ImGui::SliderInt("max ping", &s.filtMaxPing, 20, 500);
			ImGui::Separator();
			if (ImGui::Button("reset", ImVec2(60.f, 20.f)))
			{
				s.sortMode = 0;
				s.filtHideFull = false;
				s.filtHideEmpty = false;
				s.filtMinPlayers = 0;
				s.filtPingCap = false;
				s.filtMaxPing = 0;
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	ImGui::SetCursorPos(ImVec2(kMargin, kTitleH + kMargin + 26.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::BeginChild("##sb_list", ImVec2(0.f, 0.f), false);
	{
		ImDrawList* ld = ImGui::GetWindowDrawList();
		float list_x = ImGui::GetWindowPos().x;
		float right_edge = list_x + ImGui::GetWindowSize().x - ImGui::GetStyle().ScrollbarSize - 8.f;
		float avail = right_edge - list_x;

		ImGuiListClipper clipper;
		clipper.Begin((int)servers.size(), kRowH);
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
			{
				ServerEntry& e = servers[i];
				ImGui::PushID(i);
				ImVec2 p = ImGui::GetCursorScreenPos();
				if (i & 1)
					ld->AddRectFilled(p, ImVec2(p.x + avail, p.y + kRowH), IM_COL32(255, 255, 255, 8));

				float y = p.y + (kRowH - 22.f) * 0.5f;
				float ax = p.x + 6.f;
				int shown = (int)std::min<size_t>(e.playerTokens.size(), 5);
				for (int k = 0; k < shown; ++k)
				{
					ID3D11ShaderResourceView* srv = AvatarTexture(e.playerTokens[k]);
					if (srv)
						ld->AddImage((ImTextureID)srv, ImVec2(ax, y), ImVec2(ax + 22.f, y + 22.f));
					else
						ld->AddRectFilled(ImVec2(ax, y), ImVec2(ax + 22.f, y + 22.f),
							IM_COL32(60, 60, 60, 255), 11.f);
					ax += 24.f;
				}

				float tx = ax + 8.f;
				char cnt[32];
				std::snprintf(cnt, sizeof(cnt), "%d/%d", e.playing, e.maxPlayers);
				ImVec2 cnt_sz = ImGui::CalcTextSize(cnt);
				ld->AddText(ImVec2(tx, p.y + (kRowH - cnt_sz.y) * 0.5f), IM_COL32(220, 220, 220, 255), cnt);
				tx += cnt_sz.x + 16.f;

				char pin[32];
				if (e.ping >= 0)
					std::snprintf(pin, sizeof(pin), "%dms", e.ping);
				else
					std::snprintf(pin, sizeof(pin), "—");
				ImVec2 pin_sz = ImGui::CalcTextSize(pin);
				ld->AddText(ImVec2(tx, p.y + (kRowH - pin_sz.y) * 0.5f), ping_color(e.ping), pin);
				tx += pin_sz.x + 16.f;

				std::string loc = Location(e.id);
				std::string loc_txt;
				if (!loc.empty())
					loc_txt = loc;
				else if (LocationPending(e.id))
					loc_txt = "...";
				else
					loc_txt = "—";
				float join_w = 56.f;
				float join_x = right_edge - join_w;
				float loc_max = join_x - 10.f - tx;
				ImVec2 loc_sz = ImGui::CalcTextSize(loc_txt.c_str());
				if (loc_sz.x > loc_max && loc_max > 0.f)
				{
					int fit = (int)loc_txt.size();
					while (fit > 1 && ImGui::CalcTextSize(loc_txt.substr(0, fit).c_str()).x > loc_max)
						fit--;
					loc_txt = loc_txt.substr(0, fit) + "..";
				}
				ld->AddText(ImVec2(tx, p.y + (kRowH - loc_sz.y) * 0.5f), IM_COL32(180, 180, 180, 255), loc_txt.c_str());

				ImGui::SetCursorScreenPos(ImVec2(join_x, p.y + 3.f));
				if (ImGui::Button("Join", ImVec2(join_w, kRowH - 6.f)))
					JoinServer(e.id);

				ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kRowH));
				ImGui::PopID();
			}
		}
		clipper.End();

		if (servers.empty())
		{
			const char* msg = "no servers — press refresh";
			ImVec2 ms = ImGui::CalcTextSize(msg);
			ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - ms.x) * 0.5f, 40.f));
			ImGui::TextUnformatted(msg);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	ImGui::End();
}

} // namespace ServerBrowser
} // namespace Features
} // namespace Cheat
