#pragma once
#include <string>
#include <windows.h>

template<size_t N>
struct ObStr {
    char data[N];
    constexpr ObStr(const char(&s)[N]) : data{} {
        for (size_t i = 0; i < N; ++i)
            data[i] = s[i] ^ 0x4F;
    }
};

template<size_t N>
struct ObStrW {
    wchar_t data[N];
    constexpr ObStrW(const wchar_t(&s)[N]) : data{} {
        for (size_t i = 0; i < N; ++i)
            data[i] = s[i] ^ 0x4F;
    }
};

inline std::string Decode(const char* data, size_t len) {
    std::string r(len, '\0');
    for (size_t i = 0; i < len; ++i)
        r[i] = data[i] ^ 0x4F;
    r.pop_back();
    return r;
}

inline std::wstring DecodeW(const wchar_t* data, size_t len) {
    std::wstring r(len, L'\0');
    for (size_t i = 0; i < len; ++i)
        r[i] = data[i] ^ 0x4F;
    r.pop_back();
    return r;
}

// SX("literal") -> std::string decoded at runtime from a compile-time XOR'd
// buffer, so the plaintext never appears contiguously in the binary's data
// section (defeats binary string scans). The literal is only ever the source
// of a constexpr construction and is not emitted.
#define SX(str) ([]() { \
    constexpr ::ObStr _osx(str); \
    ::std::string _sx(sizeof(_osx.data), '\0'); \
    for (::std::size_t _i = 0; _i < sizeof(_osx.data); ++_i) _sx[_i] = _osx.data[_i] ^ 0x4F; \
    _sx.pop_back(); \
    return _sx; }())

// Wide variant of SX. Returns a temporary std::wstring.
#define SXW(str) ([]() { \
    constexpr ::ObStrW _osxw(str); \
    ::std::wstring _sxw(::std::size_t(sizeof(_osxw.data)), L'\0'); \
    for (::std::size_t _i = 0; _i < sizeof(_osxw.data); ++_i) _sxw[_i] = _osxw.data[_i] ^ 0x4F; \
    _sxw.pop_back(); \
    return _sxw; }())
