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
