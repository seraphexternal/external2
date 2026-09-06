#pragma once

// ── Weapon Icon Manager: Roblox weapon icons for the AIM tab ────────────────
// Detects the local player's equipped weapon, reads its Tool::TextureId
// (the icon asset id), then fetches the icon PNG from Roblox thumbnails via
// WinInet, decodes it with stb_image and caches the D3D11 SRV per weapon.
// Modelled on AvatarManager (proven to reach roblox.com in this environment).
// Header-only; call Update() once per frame and Shutdown() before the device
// is destroyed.

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <future>
#include <mutex>
#include <chrono>
#include <cstring>
#include <functional>
#include <cctype>

#include <d3d11.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

#include "../overlay/imgui/imgui.h"
#include "stb_image.h"

// Declared in overlay/renderer.cpp
extern ID3D11Device* g_pd3dDevice;

namespace WeaponIcons
{
    namespace Detail
    {
        enum class state_t : uint8_t { none, pending, ready, failed };

        struct entry_t
        {
            ID3D11ShaderResourceView* srv = nullptr;
            state_t state = state_t::none;
            std::chrono::steady_clock::time_point last_use;
            std::chrono::steady_clock::time_point last_attempt;
        };

        // Keyed by asset id (string) so the same weapon reuses its icon.
        inline std::mutex s_mtx;
        inline std::unordered_map<std::string, entry_t> s_cache;
        inline std::unordered_map<std::string, std::future<std::vector<uint8_t>>> s_pending;
        inline constexpr size_t k_max_cache = 32;

        inline std::string http_get(const std::string& url)
        {
            std::string body;
            HINTERNET hIn = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hIn) return body;
            HINTERNET hUrl = InternetOpenUrlA(hIn, url.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
            if (!hUrl) { InternetCloseHandle(hIn); return body; }
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
            HINTERNET hIn = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hIn) return data;
            HINTERNET hUrl = InternetOpenUrlA(hIn, url.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
            if (!hUrl) { InternetCloseHandle(hIn); return data; }
            std::vector<char> buf(8192);
            DWORD read = 0;
            while (InternetReadFile(hUrl, buf.data(), (DWORD)buf.size(), &read) && read > 0)
                data.insert(data.end(), buf.begin(), buf.begin() + read);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hIn);
            return data;
        }

        // Resolve a Roblox asset id to a thumbnail image URL, then download it.
        inline std::vector<uint8_t> download_asset(const std::string& assetIdStr)
        {
            // Asset IDs like "17160682738" resolve directly from assetdelivery
            // (the thumbnails v1 API 400s on these). URL-encode the numeric id.
            std::string url = "https://assetdelivery.roblox.com/v1/asset/?id=" + assetIdStr;
            return http_get_binary(url);
        }

        inline ID3D11ShaderResourceView* make_srv(const std::vector<uint8_t>& data)
        {
            if (data.empty()) return nullptr;
            ID3D11Device* dev = g_pd3dDevice;
            if (!dev) return nullptr;

            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &ch, 4);
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
            std::string oldest_key;
            auto oldest = std::chrono::steady_clock::now();
            for (auto& [k, e] : s_cache)
            {
                if (k.empty() || e.state != state_t::ready) continue;
                if (e.last_use < oldest) { oldest = e.last_use; oldest_key = k; }
            }
            if (!oldest_key.empty())
            {
                auto& e = s_cache[oldest_key];
                if (e.srv) e.srv->Release();
                s_cache.erase(oldest_key);
            }
        }
    }

    // Start (or reuse) an async download for the given tool texture id string,
    // e.g. "123456789". Returns true once a fetch is underway or cached.
    inline bool Request(const std::string& assetIdStr)
    {
        if (assetIdStr.empty()) return false;

        std::lock_guard<std::mutex> lk(Detail::s_mtx);

        auto& e = Detail::s_cache[assetIdStr];
        if (e.state == Detail::state_t::ready && e.srv) { e.last_use = std::chrono::steady_clock::now(); return true; }

        // If a download is already pending for this id, check in-flight future.
        auto pit = Detail::s_pending.find(assetIdStr);
        if (pit != Detail::s_pending.end())
        {
            auto st = pit->second.wait_for(std::chrono::seconds(0));
            if (st == std::future_status::ready)
            {
                auto data = pit->second.get();
                Detail::s_pending.erase(pit);
                e.srv = Detail::make_srv(data);
                e.state = e.srv ? Detail::state_t::ready : Detail::state_t::failed;
                e.last_use = std::chrono::steady_clock::now();
                Detail::evict_if_needed();
                return e.state == Detail::state_t::ready;
            }
            e.state = Detail::state_t::pending;
            return false;
        }

        // Start a fresh download if this id failed recently, throttle retries.
        auto now = std::chrono::steady_clock::now();
        if (e.state == Detail::state_t::failed && now - e.last_attempt < std::chrono::seconds(5))
            return false;

        e.state = Detail::state_t::pending;
        e.last_attempt = now;
        Detail::s_pending[assetIdStr] = std::async(std::launch::async,
            [assetIdStr]() { return Detail::download_asset(assetIdStr); });
        return false;
    }

    // Returns the ready SRV for the given asset id, or nullptr if not yet loaded.
    inline ID3D11ShaderResourceView* Get(const std::string& assetIdStr)
    {
        std::lock_guard<std::mutex> lk(Detail::s_mtx);
        auto it = Detail::s_cache.find(assetIdStr);
        if (it == Detail::s_cache.end()) return nullptr;
        if (it->second.state != Detail::state_t::ready || !it->second.srv) return nullptr;
        it->second.last_use = std::chrono::steady_clock::now();
        return it->second.srv;
    }

    inline void Update()
    {
        // No-op frame pump: pending futures are consumed on Request() calls.
    }

    inline void Shutdown()
    {
        std::lock_guard<std::mutex> lk(Detail::s_mtx);
        for (auto& [k, e] : Detail::s_cache)
        {
            if (e.srv) e.srv->Release();
        }
        Detail::s_cache.clear();
    }

    // ── Hardcoded weapon-name -> asset id map ───────────────────────────────
    // Populated from exported icon PNGs (filename embeds the asset id).
    // Key is the normalized weapon name (lowercase, spaces -> underscores).
    inline std::string AssetIdForWeapon(const std::string& weaponName)
    {
        if (weaponName.empty()) return "";

        std::string key = weaponName;
        for (auto& c : key) c = (char)tolower((unsigned char)c);
        for (auto& c : key) if (c == ' ') c = '_';

        static const std::unordered_map<std::string, std::string> s_ids = {
            { "assault_rifle",       "17160682738" },
            { "burst_rifle",         "17160801983" },
            { "crossbow",            "140211832612284" },
            { "energy_pistols",      "79471670126710" },
            { "pixel_handgun",       "72665687846028" },
            { "sniper",              "17160799574" },
        };

        auto it = s_ids.find(key);
        return it == s_ids.end() ? "" : it->second;
    }

    // ── Diagnostic dump ─────────────────────────────────────────────────────
    // Writes the weapon ViewModel structure + icon asset ids to
    // %LOCALAPPDATA%\Seraph\weapon_debug.txt so we can map each weapon name to
    // its icon. Walks StarterPlayerScripts > Assets > ViewModels > Weapons.
    namespace Diag
    {
        inline std::string Path()
        {
            char* env = nullptr; size_t sz = 0;
            std::string base = ".";
            if (_dupenv_s(&env, &sz, "LOCALAPPDATA") == 0 && env) { base = env; free(env); }
            std::string dir = base + "\\Seraph";
            CreateDirectoryA(dir.c_str(), nullptr);
            return dir + "\\weapon_debug.txt";
        }
        inline void Write(const std::string& line, bool reset = false)
        {
            FILE* f = nullptr;
            fopen_s(&f, Path().c_str(), reset ? "w" : "a");
            if (f) { fprintf(f, "%s\n", line.c_str()); fclose(f); }
        }

        // Read a string property at (inst, off) if it looks like an asset ref.
        inline std::string ReadAssetStr(uintptr_t inst, uintptr_t off)
        {
            if (!inst || !off) return "";
            uintptr_t p = Memory->read<uintptr_t>(inst + off);
            if (!p) return "";
            std::string s = Memory->readString(p);
            if (s.find("asset") != std::string::npos || s.find("http") != std::string::npos)
                return s;
            return "";
        }

        // Recursively scan a subtree (max depth) for icon-ish properties.
        inline void Scan(uintptr_t inst, const std::string& prefix, int depth, bool dumpTree)
        {
            if (!inst || depth > 8) return;
            RobloxInstance ri(inst);
            std::string cls = ri.Class();

            if (dumpTree)
            {
                Write(prefix + "[" + cls + "] " + ri.Name());
            }

            // ImageLabel / ImageButton: Image property (GuiObject::Image)
            if (cls == "ImageLabel" || cls == "ImageButton")
            {
                auto im = ReadAssetStr(inst, Offsets::GuiObject::Image);
                if (!im.empty()) Write(prefix + "  [Icon] Image=" + im);
            }
            // Tool: TextureId
            if (cls == "Tool")
            {
                auto t = ReadAssetStr(inst, Offsets::Tool::TextureId);
                if (!t.empty()) Write(prefix + "  [Icon] TextureId=" + t);
            }
            // Decal / Texture: Texture (Textures::Decal_Texture)
            if (cls == "Decal" || cls == "Texture" || cls == "MeshPart" || cls == "Part")
            {
                auto d = ReadAssetStr(inst, Offsets::Textures::Decal_Texture);
                if (!d.empty()) Write(prefix + "  [Icon] Texture=" + d);
            }
            // Value containers (StringValue etc.) can hold an asset id string
            if (cls == "StringValue" || cls == "BinaryValue")
            {
                auto v = ReadAssetStr(inst, Offsets::Textures::Decal_Texture);
                if (!v.empty()) Write(prefix + "  [Value] " + v);
            }

            for (auto& ch : ri.GetChildren())
                Scan(ch.address, prefix + "  ", depth + 1, dumpTree);
        }

        inline void ScanCurrentWeaponState();

        // Find the Weapons folder under StarterPlayerScripts by name stepping.
        inline void Dump()
        {
            Write("==== weapon dump " + std::to_string(GetTickCount()) + " ====", true);

            RobloxInstance root = Globals::Roblox::DataModel;
            if (!root.address)
            {
                Write("No DataModel");
                return;
            }

            // StarterPlayer -> StarterPlayerScripts -> Assets -> ViewModels -> Weapons
            RobloxInstance sp = root.FindFirstChild("StarterPlayer");
            if (!sp.address) { Write("StarterPlayer not found"); }
            RobloxInstance sps = sp.FindFirstChild("StarterPlayerScripts");
            if (!sps.address) { Write("StarterPlayerScripts not found"); }
            RobloxInstance assets = sps.FindFirstChild("Assets");
            RobloxInstance vms = assets.FindFirstChild("ViewModels");
            RobloxInstance weapons = vms.FindFirstChild("Weapons");

            if (!weapons.address)
            {
                Write("Weapons folder not found under StarterPlayer path (sp="
                    + std::to_string(sp.address) + ", sps=" + std::to_string(sps.address)
                    + ", assets=" + std::to_string(assets.address)
                    + ", vms=" + std::to_string(vms.address) + ", weapons=" + std::to_string(weapons.address) + ")");
                return;
            }

            Write("Weapons folder ok. Children:");
            int count = 0;
            int treeDump = 0;
            for (auto& ch : weapons.GetChildren())
            {
                std::string name = ch.Name();
                std::string cls = ch.Class();
                if (cls == "Folder") continue;
                Write("  * " + name + " (class=" + cls + ", addr=0x"
                    + std::to_string(ch.address) + ")");
                count++;
            }
            Write("total weapons: " + std::to_string(count));

            // ── GUI icon scan ──
            // Walk StarterPlayerScripts > UserInterface and dump every
            // ImageLabel/ImageButton that holds an rbxassetid, with its full
            // ancestor path, so we can locate per-weapon icon images.
            Write("");
            Write("== GUI icon scan (UserInterface) ==");
            RobloxInstance ui = sps.FindFirstChild("UserInterface");
            if (!ui.address) { Write("UserInterface not found (sps=" + std::to_string(sps.address) + ")"); }
            else
            {
                Write("UI found at 0x" + std::to_string(ui.address) + ". Children of UI:");
                for (auto& c : ui.GetChildren())
                    Write("   / " + c.Name() + " (" + c.Class() + ")");
                static char pathBuf[4096];
                long imgCount = 0;
                std::function<void(uintptr_t, const std::string&, int)> walk =
                    [&](uintptr_t inst, const std::string& path, int depth) {
                        if (!inst || depth > 14) return;
                        RobloxInstance ri(inst);
                        std::string cls = ri.Class();
                        std::string nm = ri.Name();
                        if (nm.empty()) nm = "(unnamed)";
                        std::string full = path + "/" + nm;
                        if (cls == "ImageLabel" || cls == "ImageButton")
                        {
                            uintptr_t p = Memory->read<uintptr_t>(inst + Offsets::GuiObject::Image);
                            if (p)
                            {
                                std::string img = Memory->readString(p);
                                if (img.find("asset") != std::string::npos)
                                {
                                    Write("[IMG] " + full + "  =>  " + img);
                                    imgCount++;
                                }
                            }
                        }
                        for (auto& c : ri.GetChildren())
                            walk(c.address, full, depth + 1);
                    };
                walk(ui.address, "UserInterface", 0);
                Write("[IMG] total image labels with assets: " + std::to_string(imgCount));
            }

            // ── Current-weapon state scan ──
            // Rivals weapons are Model ViewModels (not Tools on the character/
            // backpack), so GetLocalPlayerWeapon() returns empty. Dump the local
            // Player + Character subtrees to find where the CURRENT weapon is
            // stored (a Value/Configuration or a held Tool/Weapon instance).
            ScanCurrentWeaponState();
            Write("== end scan ==");
        }

        // Lightweight, auto-run once at attach: dump the LocalPlayer/Character
        // subtree + any Tool-ish/weapon-named instances under the player, so we
        // can locate where the currently-held weapon is stored.
        inline void ScanCurrentWeaponState()
        {
            Write("");
            Write("== current weapon state scan ==");
            RobloxInstance lp = Globals::Roblox::LocalPlayer;
            Write("LocalPlayer addr=0x" + std::to_string(lp.address));
            Write("LocalPlayer children:");
            for (auto& c : lp.GetChildren())
                Write("   / " + c.Name() + " (" + c.Class() + ")");

            // Dump the Backpack subtree (where Rivals keeps the player's
            // weapons) to find where the currently-held weapon lives.
            Write("Backpack subtree (depth 6):");
            RobloxInstance bk = lp.FindFirstChild("Backpack");
            Write("Backpack addr=0x" + std::to_string(bk.address));
            std::function<void(uintptr_t, int, const std::string&)> bwalk =
                [&](uintptr_t inst, int depth, const std::string& path) {
                    if (!inst || depth > 6) return;
                    RobloxInstance ri(inst);
                    std::string nm = ri.Name();
                    if (nm.empty()) nm = "(unnamed)";
                    std::string full = path + "/" + nm;
                    std::string pad((size_t)depth * 3, ' ');
                    Write(pad + "- " + nm + " (" + ri.Class() + ")");
                    for (auto& c : ri.GetChildren())
                        bwalk(c.address, depth + 1, full);
                };
            bwalk(bk.address, 1, "Backpack");

            // Search for weapon Model instances with their FULL parent path so
            // we can distinguish the equipped weapon (on character) from the
            // inventory (in backpack).
            Write("Weapon Model instances (with parent path):");
            std::function<void(uintptr_t, int, const std::string&)> twalk =
                [&](uintptr_t inst, int depth, const std::string& path) {
                    if (!inst || depth > 8) return;
                    RobloxInstance ri(inst);
                    std::string cls = ri.Class();
                    std::string nm = ri.Name();
                    std::string full = path + "/" + nm;
                    auto isWeaponName = [&]() {
                        return nm == "Assault Rifle" || nm == "Sniper" || nm == "Bow" ||
                               nm == "Uzi" || nm == "Handgun" || nm == "Shotgun" ||
                               nm == "Katana" || nm == "Minigun" || nm == "RPG" ||
                               nm == "Knife" || nm == "Daggers" || nm == "Revolver";
                    };
                    if (cls == "Model" && isWeaponName())
                        Write("   [WEAPON] " + full);
                    for (auto& c : ri.GetChildren())
                        twalk(c.address, depth + 1, full);
                };
            twalk(lp.address, 0, "LP");
        }
    }
}
