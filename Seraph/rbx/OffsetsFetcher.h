#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "offsets.h"  // pulls in Offsets::SetOffset and Offsets::ClientVersion

namespace OffsetsFetcher {

    // Public state after the most recent FetchAndApply call.
    inline std::string lastUrl;
    inline std::string lastError;
    inline std::string fetchedClientVersion;
    inline std::size_t overridesApplied = 0;
    inline std::size_t overridesSkipped = 0;
    inline bool loaded = false;

    namespace detail {

        inline std::string Trim(const std::string& s) {
            std::size_t start = 0;
            while (start < s.size() && (static_cast<unsigned char>(s[start]) <= ' ')) ++start;
            std::size_t end = s.size();
            while (end > start && (static_cast<unsigned char>(s[end - 1]) <= ' ')) --end;
            return s.substr(start, end - start);
        }

        inline bool StartsWith(const std::string& s, const char* prefix) {
            const std::size_t n = std::strlen(prefix);
            return s.size() >= n && std::memcmp(s.c_str(), prefix, n) == 0;
        }

        inline std::string HttpGet(const std::wstring& url) {
            std::string result;

            HINTERNET session = WinHttpOpen(
                L"Seraph/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);

            if (!session) {
                lastError = "WinHttpOpen failed";
                return {};
            }

            URL_COMPONENTSW parts = {};
            wchar_t host[256] = {};
            wchar_t path[4096] = {};
            parts.dwStructSize = sizeof(parts);
            parts.lpszHostName = host;
            parts.dwHostNameLength = 256;
            parts.lpszUrlPath = path;
            parts.dwUrlPathLength = 4096;

            if (!WinHttpCrackUrl(url.c_str(), 0, 0, (LPURL_COMPONENTS)&parts)) {
                lastError = "WinHttpCrackUrl failed";
                WinHttpCloseHandle(session);
                return {};
            }

            HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
            if (!connect) {
                lastError = "WinHttpConnect failed";
                WinHttpCloseHandle(session);
                return {};
            }

            const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET request = WinHttpOpenRequest(
                connect,
                L"GET",
                path,
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                flags);

            if (!request) {
                lastError = "WinHttpOpenRequest failed";
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return {};
            }

            // 10 second timeouts.
            WinHttpSetTimeouts(request, 10000, 10000, 10000, 10000);

            if (!WinHttpSendRequest(
                    request,
                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                lastError = "WinHttpSendRequest failed";
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return {};
            }

            if (!WinHttpReceiveResponse(request, nullptr)) {
                lastError = "WinHttpReceiveResponse failed";
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return {};
            }

            DWORD bytesAvailable = 0;
            while (WinHttpQueryDataAvailable(request, &bytesAvailable) && bytesAvailable > 0) {
                std::vector<char> buf(static_cast<std::size_t>(bytesAvailable) + 1, 0);
                DWORD bytesRead = 0;
                if (WinHttpReadData(request, buf.data(), bytesAvailable, &bytesRead)) {
                    result.append(buf.data(), bytesRead);
                } else {
                    bytesRead = 0; // break to avoid infinite loop on persistent failure
                }
                if (bytesRead == 0) break;
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return result;
        }
    } // namespace detail

    // Fetch the offsets dump from `url` and apply any matching offsets
    // to the runtime-mutable variables in Seraph/rbx/offsets.h.
    //
    // Returns true iff:
    //   - HTTP GET succeeded
    //   - ClientVersion line was parsed out of the response
    //   - Fetched ClientVersion equals the bundled Offsets::ClientVersion
    //   - At least one offset was applied
    //
    // On any failure the bundled values are preserved untouched.
    inline bool FetchAndApply(const std::string& url) {
        using namespace detail;

        lastUrl = url;
        lastError.clear();
        fetchedClientVersion.clear();
        overridesApplied = 0;
        overridesSkipped = 0;
        loaded = false;

        if (url.empty()) {
            lastError = "Empty URL";
            Offsets::lastFetchStatus = "skip: empty url";
            return false;
        }

        const int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
        if (wlen <= 0) {
            lastError = "Could not convert URL to wide string";
            Offsets::lastFetchStatus = "skip: utf-16 conversion failed";
            return false;
        }
        std::wstring wurl(static_cast<std::size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], wlen);

        const std::string source = HttpGet(wurl);
        if (source.empty()) {
            if (lastError.empty()) lastError = "Empty response from server";
            Offsets::lastFetchStatus = "skip: " + lastError;
            return false;
        }

        // -- line-based parse -------------------------------------------------
        int depth = 0;
        std::string currentNs;
        bool foundVersion = false;
        std::string fetchedVersion;
        std::size_t applied = 0;
        std::size_t skipped = 0;

        constexpr const char* kNamespacePrefix = "namespace ";
        constexpr const char* kClientVerPrefix = "inline std::string ClientVersion";
        constexpr const char* kOffsetPrefix    = "inline constexpr uintptr_t ";

        std::size_t lineStart = 0;
        while (lineStart < source.size()) {
            const std::size_t lineEnd = source.find('\n', lineStart);
            const std::size_t stop = (lineEnd == std::string::npos) ? source.size() : lineEnd;
            const std::string line = source.substr(lineStart, stop - lineStart);
            lineStart = (lineEnd == std::string::npos) ? source.size() : lineEnd + 1;

            const std::string trimmed = Trim(line);

            // `namespace Foo {` open
            if (StartsWith(trimmed, kNamespacePrefix) && trimmed.find('{') != std::string::npos) {
                const std::size_t nameStart = std::strlen(kNamespacePrefix);
                const std::size_t bracePos = trimmed.find('{', nameStart);
                if (bracePos != std::string::npos) {
                    const std::string ns = Trim(trimmed.substr(nameStart, bracePos - nameStart));
                    ++depth;
                    if (depth == 2) currentNs = ns; // first inner namespace under `Offsets`
                }
                continue;
            }

            // Stand-alone `}` close
            if (trimmed == "}") {
                if (depth == 2) currentNs.clear();
                if (depth > 0) --depth;
                continue;
            }

            // `inline std::string ClientVersion = "...";` (top-level only)
            if (depth == 1 && StartsWith(trimmed, kClientVerPrefix)) {
                const std::size_t eq = trimmed.find('=', std::strlen(kClientVerPrefix));
                if (eq != std::string::npos) {
                    const std::size_t firstQuote = trimmed.find('"', eq);
                    const std::size_t lastQuote  = trimmed.rfind('"');
                    if (firstQuote != std::string::npos &&
                        lastQuote  != std::string::npos &&
                        lastQuote > firstQuote) {
                        fetchedVersion = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                        foundVersion = true;
                    }
                }
                continue;
            }

            // `inline constexpr uintptr_t Name = 0xVALUE;` (inside a sub-namespace)
            if (!currentNs.empty() &&
                StartsWith(trimmed, kOffsetPrefix) &&
                !trimmed.empty() && trimmed.back() == ';') {
                const std::size_t nameStart = std::strlen(kOffsetPrefix);
                const std::size_t eq = trimmed.find(" = ", nameStart);
                if (eq != std::string::npos) {
                    const std::string name  = Trim(trimmed.substr(nameStart, eq - nameStart));
                    const std::string value = Trim(trimmed.substr(eq + 3,
                                              trimmed.size() - eq - 3 - 1));
                    char* endp = nullptr;
                    // std::strtoull (C library) takes const char* + char** natively; this
                    // avoids C3688 from MSVC's overloaded std::stoull(string&, size_t*, int)
                    // not matching our C-string + char** argument shape cleanly under C++20.
                    const uintptr_t v = std::strtoull(value.c_str(), &endp, 16);
                    if (endp != value.c_str() && *endp == '\0') {
                        if (Offsets::SetOffset(currentNs, name, v)) {
                            ++applied;
                        } else {
                            ++skipped;
                        }
                    }
                }
            }
        }

        if (!foundVersion) {
            lastError = "Could not find ClientVersion in fetched source";
            Offsets::lastFetchStatus = "skip: " + lastError;
            return false;
        }

        if (fetchedVersion != Offsets::ClientVersion) {
            lastError = "ClientVersion mismatch: bundled=" + Offsets::ClientVersion +
                        " fetched=" + fetchedVersion;
            Offsets::lastFetchStatus = "skip: " + lastError;
            return false;
        }

        if (applied == 0) {
            lastError = "No offsets applied (matched 0/" +
                        std::to_string(applied + skipped) + ")";
            Offsets::lastFetchStatus = "skip: " + lastError;
            return false;
        }

        fetchedClientVersion = fetchedVersion;
        overridesApplied = applied;
        overridesSkipped = skipped;
        loaded = true;
        Offsets::lastFetchStatus = "ok: applied " + std::to_string(applied) +
                                   " skipped " + std::to_string(skipped) +
                                   " version=" + fetchedVersion;
        return true;
    }

    inline void Reset() {
        loaded = false;
        lastError.clear();
        lastUrl.clear();
        fetchedClientVersion.clear();
        overridesApplied = 0;
        overridesSkipped = 0;
        Offsets::lastFetchStatus = "(reset)";
    }
}
