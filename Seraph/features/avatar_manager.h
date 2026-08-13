#pragma once

// ── Avatar Manager: async Roblox avatar-headshot thumbnails for the Target HUD ──
// Ported from the reference avatar_manager. Downloads the headshot PNG via
// WinInet (thumbnails.roblox.com + the rbxcdn image URL), decodes with
// stb_image and caches the D3D11 SRV per user id. WinInet is used instead of
// raw WinHTTP because it negotiates TLS/SChannel correctly for roblox.com and
// is the same proven path the ESP preview avatar relies on.
// Header-only: the cache/futures are shared across TUs via C++17 inline statics.
// Call AvatarManager::Update() once per frame on the render thread and
// AvatarManager::Shutdown() before the D3D device is destroyed.

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <future>
#include <mutex>
#include <chrono>
#include <cstring>

#include <d3d11.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

#include "../overlay/imgui/imgui.h"
#include "stb_image.h"

// Declared in overlay/renderer.cpp
extern ID3D11Device* g_pd3dDevice;

namespace AvatarManager
{
    namespace Detail
    {
        enum class state_t : uint8_t { none, pending, ready, failed };

        struct entry_t
        {
            ID3D11ShaderResourceView* srv = nullptr;
            state_t state = state_t::none;
            std::chrono::steady_clock::time_point last_use;
            std::chrono::steady_clock::time_point last_attempt; // for retry-after-failure
        };

        inline std::mutex s_mtx;
        inline std::unordered_map<uint64_t, entry_t> s_cache;
        inline std::unordered_map<uint64_t, std::future<std::vector<uint8_t>>> s_pending;
        inline constexpr size_t k_max_cache = 64;

        // WinInet GET (the same path ESPPreviewAvatar::FetchUrl uses, which is
        // verified to reach roblox.com in this environment). Binary-safe: the
        // body is built with length-aware append, so it can hold a PNG too.
        inline std::string http_get(const std::string& url)
        {
            std::string body;
            HINTERNET hIn = InternetOpenA("Seraph",
                INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hIn) return body;

            HINTERNET hUrl = InternetOpenUrlA(hIn, url.c_str(),
                NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
            if (!hUrl)
            {
                InternetCloseHandle(hIn);
                return body;
            }

            char buf[8192];
            DWORD read = 0;
            while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
                body.append(buf, read);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hIn);
            return body;
        }

        inline std::vector<uint8_t> http_get_binary(const std::string& url)
        {
            std::vector<uint8_t> data;
            HINTERNET hIn = InternetOpenA("Seraph",
                INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hIn) return data;

            HINTERNET hUrl = InternetOpenUrlA(hIn, url.c_str(),
                NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
            if (!hUrl)
            {
                InternetCloseHandle(hIn);
                return data;
            }

            std::vector<char> buf(8192);
            DWORD read = 0;
            while (InternetReadFile(hUrl, buf.data(), (DWORD)buf.size(), &read) && read > 0)
                data.insert(data.end(), buf.begin(), buf.begin() + read);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hIn);
            return data;
        }

        inline std::vector<uint8_t> download_thumbnail(uint64_t user_id)
        {
            // Official Roblox headshot endpoint (thumbnails.roblox.com).
            std::string apiUrl = "https://thumbnails.roblox.com/v1/users/avatar-headshot?userIds="
                + std::to_string(user_id)
                + "&size=100x100&format=Png&isCircular=false";

            std::string json = http_get(apiUrl);
            if (json.empty()) return {};

            // imageUrl may be null when the thumbnail hasn't been generated yet.
            const char* key = "\"imageUrl\":";
            size_t kpos = json.find(key);
            if (kpos == std::string::npos) return {};
            kpos += strlen(key);
            // skip optional whitespace
            while (kpos < json.size() && json[kpos] == ' ') kpos++;
            if (json.compare(kpos, 4, "null") == 0)
                return {}; // thumbnail not ready yet; Request() will retry later
            if (kpos >= json.size() || json[kpos] != '"') return {}; // not a string
            kpos++; // opening quote
            size_t epos = json.find('"', kpos);
            if (epos == std::string::npos) return {};

            std::string img_url = json.substr(kpos, epos - kpos);
            if (img_url.empty()) return {};

            return http_get_binary(img_url);
        }

        inline ID3D11ShaderResourceView* make_srv(const std::vector<uint8_t>& data)
        {
            if (data.empty()) return nullptr;
            ID3D11Device* dev = g_pd3dDevice;
            if (!dev) return nullptr;

            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(
                data.data(), (int)data.size(), &w, &h, &ch, 4);
            if (!px) return nullptr;

            D3D11_TEXTURE2D_DESC td = {};
            td.Width = (UINT)w;
            td.Height = (UINT)h;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA sd = {};
            sd.pSysMem = px;
            sd.SysMemPitch = (UINT)(w * 4);

            ID3D11Texture2D* tex = nullptr;
            HRESULT hr = dev->CreateTexture2D(&td, &sd, &tex);
            stbi_image_free(px);
            if (FAILED(hr)) return nullptr;

            D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
            svd.Format = td.Format;
            svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            svd.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* srv = nullptr;
            hr = dev->CreateShaderResourceView(tex, &svd, &srv);
            tex->Release();
            return FAILED(hr) ? nullptr : srv;
        }

        inline void evict_if_needed()
        {
            if (s_cache.size() <= k_max_cache) return;

            uint64_t oldest_id = 0;
            bool found = false;
            std::chrono::steady_clock::time_point oldest_time;

            for (auto& kv : s_cache)
            {
                if (kv.second.state != state_t::ready) continue;
                if (!found || kv.second.last_use < oldest_time)
                {
                    oldest_time = kv.second.last_use;
                    oldest_id = kv.first;
                    found = true;
                }
            }

            if (!found) return;

            auto it = s_cache.find(oldest_id);
            if (it != s_cache.end())
            {
                if (it->second.srv) it->second.srv->Release();
                s_cache.erase(it);
            }
        }
    }

    // Promote completed downloads to SRVs. Call once per frame on render thread.
    inline void Update()
    {
        std::lock_guard<std::mutex> lk{ Detail::s_mtx };

        for (auto it = Detail::s_pending.begin(); it != Detail::s_pending.end(); )
        {
            if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            uint64_t id = it->first;
            auto data = it->second.get();
            it = Detail::s_pending.erase(it);

            auto& entry = Detail::s_cache[id];
            if (!data.empty())
            {
                entry.srv = Detail::make_srv(data);
                entry.state = entry.srv ? Detail::state_t::ready : Detail::state_t::failed;
            }
            else
            {
                entry.state = Detail::state_t::failed;
            }
        }
    }

    // Fire-and-forget. Safe to call every frame for the same user_id.
    inline void Request(uint64_t user_id)
    {
        if (!user_id) return;
        std::lock_guard<std::mutex> lk{ Detail::s_mtx };

        auto& entry = Detail::s_cache[user_id];
        if (entry.state == Detail::state_t::ready ||
            entry.state == Detail::state_t::pending)
            return;

        // Previously failed: retry on a 10s cooldown so transient network
        // errors (e.g. Roblox avatar not ready yet) eventually recover instead
        // of being cached as "failed" forever.
        if (entry.state == Detail::state_t::failed)
        {
            if (entry.last_attempt.time_since_epoch().count() == 0)
                return;
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - entry.last_attempt).count() < 10)
                return;
        }

        entry.state = Detail::state_t::pending;
        entry.last_attempt = std::chrono::steady_clock::now();
        Detail::evict_if_needed();

        Detail::s_pending[user_id] = std::async(std::launch::async,
            Detail::download_thumbnail, user_id);
    }

    // Returns nullptr if not ready yet (still loading or failed).
    inline ImTextureID Get(uint64_t user_id)
    {
        if (!user_id) return nullptr;
        std::lock_guard<std::mutex> lk{ Detail::s_mtx };

        auto it = Detail::s_cache.find(user_id);
        if (it == Detail::s_cache.end() || it->second.state != Detail::state_t::ready)
            return nullptr;

        it->second.last_use = std::chrono::steady_clock::now();
        return reinterpret_cast<ImTextureID>(it->second.srv);
    }

    // Call before the D3D device is destroyed.
    inline void Shutdown()
    {
        std::lock_guard<std::mutex> lk{ Detail::s_mtx };
        for (auto& kv : Detail::s_pending)
            kv.second.wait_for(std::chrono::milliseconds(100));
        Detail::s_pending.clear();

        for (auto& kv : Detail::s_cache)
            if (kv.second.srv) kv.second.srv->Release();
        Detail::s_cache.clear();
    }
}
